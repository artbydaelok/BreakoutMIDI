#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

//==============================================================================
BreakoutMidiProcessor::BreakoutMidiProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)) // silent; for host compatibility
{
    blockNotes.reserve (512);
    activeNotes.reserve (1024);
    published.balls.reserve (64);
    published.bricks.reserve (512);
}

bool BreakoutMidiProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo()
        || out == juce::AudioChannelSet::mono();
}

//==============================================================================
void BreakoutMidiProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    stepAccumulator = 0.0;
    activeNotes.clear();
}

void BreakoutMidiProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                          juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();

    // ---- Host MIDI in -> UI (held notes) ----
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())       pushMidiIn ({ m.getNoteNumber(), m.getVelocity(), true });
        else if (m.isNoteOff()) pushMidiIn ({ m.getNoteNumber(), 0, false });
    }

    // ---- Apply staged UI commands ----
    {
        const juce::SpinLock::ScopedTryLockType sl (configLock);
        if (sl.isLocked() && configDirty) { sim.applyConfig (pendingConfig); configDirty = false; }
    }
    if (resetRequested.exchange (false)) sim.reset();
    const int play = pendingPlay.exchange (-1);
    if      (play == 0) sim.setPlaying (false);
    else if (play == 1) sim.setPlaying (true);

    // ---- Advance simulation at a fixed 60 Hz, regardless of block size ----
    blockNotes.clear();
    constexpr double fixed = 1.0 / 60.0;
    stepAccumulator += numSamples / sampleRate;
    int steps = 0;
    while (stepAccumulator >= fixed && steps < 8) { sim.step (fixed, blockNotes); stepAccumulator -= fixed; ++steps; }
    if (steps == 8) stepAccumulator = 0.0; // don't spiral if we fell behind

    // ---- Emit generated MIDI (we are a MIDI source; drop incoming) ----
    midi.clear();
    for (const auto& e : blockNotes)
    {
        midi.addEvent (juce::MidiMessage::noteOn (e.channel, e.note, (juce::uint8) e.velocity), 0);
        const int durSamples = juce::jmax (1, (int) std::lround (e.durationMs * sampleRate / 1000.0));
        activeNotes.push_back ({ e.channel, e.note, durSamples });
    }
    for (int i = (int) activeNotes.size() - 1; i >= 0; --i)
    {
        auto& a = activeNotes[(size_t) i];
        if (a.samplesRemaining < numSamples)
        {
            midi.addEvent (juce::MidiMessage::noteOff (a.channel, a.note),
                           juce::jlimit (0, juce::jmax (0, numSamples - 1), a.samplesRemaining));
            activeNotes.erase (activeNotes.begin() + i);
        }
        else
        {
            a.samplesRemaining -= numSamples;
        }
    }

    // ---- Publish render snapshot for the editor ----
    {
        const juce::SpinLock::ScopedTryLockType sl (snapLock);
        if (sl.isLocked()) sim.writeSnapshot (published);
    }
}

//==============================================================================
void BreakoutMidiProcessor::setConfig (const Simulation::Config& cfg)
{
    const juce::SpinLock::ScopedLockType sl (configLock);
    pendingConfig = cfg;
    configDirty = true;
}

void BreakoutMidiProcessor::setPlaying (bool shouldPlay) { pendingPlay.store (shouldPlay ? 1 : 0); }
void BreakoutMidiProcessor::requestReset()               { resetRequested.store (true); }

Simulation::RenderState BreakoutMidiProcessor::getRenderSnapshot()
{
    const juce::SpinLock::ScopedLockType sl (snapLock);
    return published; // copy on the message thread
}

//==============================================================================
void BreakoutMidiProcessor::pushMidiIn (const MidiInEvent& e)
{
    const auto scope = midiInFifo.write (1);
    if (scope.blockSize1 > 0)      midiInBuffer[scope.startIndex1] = e;
    else if (scope.blockSize2 > 0) midiInBuffer[scope.startIndex2] = e;
}

bool BreakoutMidiProcessor::popMidiIn (MidiInEvent& out)
{
    const auto scope = midiInFifo.read (1);
    if (scope.blockSize1 > 0) { out = midiInBuffer[scope.startIndex1]; return true; }
    if (scope.blockSize2 > 0) { out = midiInBuffer[scope.startIndex2]; return true; }
    return false;
}

//==============================================================================
juce::AudioProcessorEditor* BreakoutMidiProcessor::createEditor()
{
    return new BreakoutMidiEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BreakoutMidiProcessor();
}
