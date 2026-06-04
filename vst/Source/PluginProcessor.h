#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Simulation.h"

//==============================================================================
// BreakoutMIDI plugin processor.
//
// The physics simulation now lives here in C++ and is stepped on the audio
// thread, so it keeps running and emitting MIDI whether or not the editor
// window is open. The WebView is purely a control surface + renderer:
//   UI  -> setConfig / setPlaying / requestReset   (message thread -> staged)
//   sim -> getRenderSnapshot                        (audio thread -> message)
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

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //== Bridge: UI (message thread) -> simulation =============================
    void setConfig (const Simulation::Config& cfg);
    void setPlaying (bool shouldPlay);
    void requestReset();
    void setMouse (float x, float y, bool active); // cursor for mouse modes
    int  takeActiveLevelChanged() { return sim.takeActiveLevelChanged(); }

    // Full UI/config blob: applies to the sim and is stored for persistence
    // (project save/load) and for restoring the UI when the editor reopens.
    void      setStateFromVar (const juce::var& v); // message thread
    juce::var getStateVar();                         // message thread

    // Bumped whenever the host restores state (undo, preset, project load) so the
    // editor can refresh the WebView UI from the restored blob.
    int       getStateEpoch() const { return stateEpoch.load(); }
    juce::var getRestoreVar();                        // message thread

    //== Bridge: simulation -> UI =============================================
    // Copies the latest published render state (allocates on the calling/
    // message thread, never on the audio thread).
    Simulation::RenderState getRenderSnapshot();

    //== Host MIDI input -> UI (held notes) ===================================
    bool popMidiIn (MidiInEvent& out);

private:
    Simulation sim;

    // Staged config (message -> audio).
    juce::SpinLock      configLock;
    Simulation::Config  pendingConfig;
    bool                configDirty = false;

    std::atomic<int>    pendingPlay { -1 };   // -1 none, 0 stop, 1 play
    std::atomic<bool>   resetRequested { false };

    std::atomic<float>  mouseX { 0.0f }, mouseY { 0.0f };
    std::atomic<bool>   mouseActive { false };

    // Persisted UI/config blob (message-thread access; guarded for safety).
    juce::CriticalSection stateLock;
    juce::String          stateJson;
    juce::String          restoreJson;       // immutable snapshot at restore time
    std::atomic<int>      stateEpoch { 0 };

    // Published render snapshot (audio -> message), preallocated.
    juce::SpinLock           snapLock;
    Simulation::RenderState  published;

    double sampleRate = 44100.0;
    double stepAccumulator = 0.0;

    std::vector<Simulation::NoteEvent> blockNotes;        // scratch, per block
    struct ActiveNote { int channel; int note; int samplesRemaining; };
    std::vector<ActiveNote> activeNotes;                  // pending note-offs

    // Lock-free SPSC ring for host MIDI -> UI.
    static constexpr int midiInCapacity = 1024;
    juce::AbstractFifo midiInFifo { midiInCapacity };
    MidiInEvent midiInBuffer[midiInCapacity];
    void pushMidiIn (const MidiInEvent& e);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakoutMidiProcessor)
};
