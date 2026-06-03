#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

//==============================================================================
// BreakoutMIDI plugin processor.
//
// The physics/UI lives in a WebView (the existing index.html). When a brick is
// hit, JS calls the native `sendMidi` function which lands in enqueueNote() on
// the message thread. Notes are buffered in a MidiMessageCollector and drained
// into the host's MIDI stream inside processBlock() on the audio thread.
//
// Incoming MIDI from the host is pushed onto a lock-free FIFO from the audio
// thread; the editor polls it on a timer and forwards held notes to the UI.
//==============================================================================
class BreakoutMidiProcessor : public juce::AudioProcessor
{
public:
    struct MidiInEvent { int note = 0; int vel = 0; bool on = false; };

    BreakoutMidiProcessor();
    ~BreakoutMidiProcessor() override = default;

    //== AudioProcessor =========================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BreakoutMIDI"; }
    bool acceptsMidi()  const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    //== Bridge to the WebView UI ==============================================
    // Called on the message thread when JS fires a brick/edge note.
    void enqueueNote (int note, int vel, int channel, int durationMs);

    // Called on the message thread (editor timer) to drain host MIDI input.
    bool popMidiIn (MidiInEvent& out);

private:
    juce::MidiMessageCollector midiCollector;

    // Lock-free SPSC ring for host MIDI -> UI.
    static constexpr int midiInCapacity = 1024;
    juce::AbstractFifo midiInFifo { midiInCapacity };
    MidiInEvent midiInBuffer[midiInCapacity];
    void pushMidiIn (const MidiInEvent& e);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakoutMidiProcessor)
};
