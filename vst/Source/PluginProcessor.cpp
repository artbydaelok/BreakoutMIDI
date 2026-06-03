#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
BreakoutMidiProcessor::BreakoutMidiProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) // silent; for host compatibility
{
}

bool BreakoutMidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        || out == juce::AudioChannelSet::mono();
}

//==============================================================================
void BreakoutMidiProcessor::prepareToPlay (double sampleRate, int)
{
    midiCollector.reset (sampleRate);
}

void BreakoutMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Host MIDI -> UI (held notes). Forward note on/off onto the FIFO.
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            pushMidiIn ({ m.getNoteNumber(), m.getVelocity(), true });
        else if (m.isNoteOff())
            pushMidiIn ({ m.getNoteNumber(), 0, false });
    }

    // UI -> host: emit notes queued by the WebView since the last block.
    midiCollector.removeNextBlockOfMessages (midi, buffer.getNumSamples());
}

//==============================================================================
void BreakoutMidiProcessor::enqueueNote (int note, int vel, int channel, int durationMs)
{
    note    = juce::jlimit (0, 127, note);
    vel     = juce::jlimit (1, 127, vel);
    channel = juce::jlimit (1, 16,  channel);

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

    auto on = juce::MidiMessage::noteOn (channel, note, (juce::uint8) vel);
    on.setTimeStamp (now);
    midiCollector.addMessageToQueue (on);

    auto off = juce::MidiMessage::noteOff (channel, note);
    off.setTimeStamp (now + juce::jmax (1, durationMs) / 1000.0);
    midiCollector.addMessageToQueue (off);

   #if BREAKOUT_MIDI_DEBUG_LOG
    juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("breakoutmidi_midi.log")
        .appendText ("note " + juce::String (note) + " vel " + juce::String (vel)
                     + " ch " + juce::String (channel) + " dur " + juce::String (durationMs) + "\n");
   #endif
}

void BreakoutMidiProcessor::pushMidiIn (const MidiInEvent& e)
{
    const auto scope = midiInFifo.write (1);
    if (scope.blockSize1 > 0)      midiInBuffer[scope.startIndex1] = e;
    else if (scope.blockSize2 > 0) midiInBuffer[scope.startIndex2] = e;
}

bool BreakoutMidiProcessor::popMidiIn (MidiInEvent& out)
{
    const auto scope = midiInFifo.read (1);
    if (scope.blockSize1 > 0)      { out = midiInBuffer[scope.startIndex1]; return true; }
    if (scope.blockSize2 > 0)      { out = midiInBuffer[scope.startIndex2]; return true; }
    return false;
}

//==============================================================================
juce::AudioProcessorEditor* BreakoutMidiProcessor::createEditor()
{
    return new BreakoutMidiEditor (*this);
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BreakoutMidiProcessor();
}
