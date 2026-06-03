#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    int   asInt   (const juce::var& v, int d)   { return v.isVoid() ? d : (int) v; }
    float asFloat (const juce::var& v, float d) { return v.isVoid() ? d : (float) (double) v; }
    bool  asBool  (const juce::var& v, bool d)  { return v.isVoid() ? d : (bool) v; }

    Simulation::Config parseConfig (const juce::var& v)
    {
        Simulation::Config c;

        const auto p = v.getProperty ("params", juce::var());
        auto& P = c.params;
        P.ballSpeed   = asFloat (p.getProperty ("ballSpeed",   {}), P.ballSpeed);
        P.gravity     = asFloat (p.getProperty ("gravity",     {}), P.gravity);
        P.numBalls    = asInt   (p.getProperty ("numBalls",    {}), P.numBalls);
        P.spawnRate   = asFloat (p.getProperty ("spawnRate",   {}), P.spawnRate);
        P.maxBricks   = asInt   (p.getProperty ("maxBricks",   {}), P.maxBricks);
        P.noteLen     = asInt   (p.getProperty ("noteLen",     {}), P.noteLen);
        P.midiChannel = asInt   (p.getProperty ("midiChannel", {}), P.midiChannel);
        P.mode        = asInt   (p.getProperty ("mode",        {}), P.mode);
        P.speedToVel  = asBool  (p.getProperty ("speedToVel",  {}), P.speedToVel);
        P.mouseMode     = asInt   (p.getProperty ("mouseMode",     {}), P.mouseMode);
        P.forceStrength = asFloat (p.getProperty ("forceStrength", {}), P.forceStrength);
        P.forceAttract  = asBool  (p.getProperty ("forceAttract",  {}), P.forceAttract);
        P.cageRadius    = asFloat (p.getProperty ("cageRadius",    {}), P.cageRadius);
        P.paddleSize    = asFloat (p.getProperty ("paddleSize",    {}), P.paddleSize);

        if (auto* arr = v.getProperty ("slots", juce::var()).getArray())
        {
            c.slots.clear();
            for (const auto& sv : *arr)
            {
                Simulation::Slot s;
                s.id         = asInt   (sv.getProperty ("id",         {}), 0);
                s.note       = asInt   (sv.getProperty ("note",       {}), 60);
                s.prob       = asFloat (sv.getProperty ("prob",       {}), 50.0f);
                s.enabled    = asBool  (sv.getProperty ("enabled",    {}), true);
                s.velLock    = asInt   (sv.getProperty ("velLock",    {}), 0);
                s.durability = asInt   (sv.getProperty ("durability", {}), 1);
                s.shape      = asInt   (sv.getProperty ("shape",      {}), 0);
                s.shapeW     = asFloat (sv.getProperty ("shapeW",     {}), 72.0f);
                s.shapeH     = asFloat (sv.getProperty ("shapeH",     {}), 22.0f);
                s.shapeR     = asFloat (sv.getProperty ("shapeR",     {}), 20.0f);
                s.shapeSides = asInt   (sv.getProperty ("shapeSides", {}), 6);
                s.shapeSize  = asFloat (sv.getProperty ("shapeSize",  {}), 28.0f);
                c.slots.push_back (s);
            }
        }

        if (auto* earr = v.getProperty ("edges", juce::var()).getArray())
        {
            for (int i = 0; i < 4 && i < earr->size(); ++i)
            {
                const auto ev = (*earr)[i];
                c.edges[i].note    = asInt  (ev.getProperty ("note",    {}), 48);
                c.edges[i].enabled = asBool (ev.getProperty ("enabled", {}), false);
                c.edges[i].velLock = asInt  (ev.getProperty ("velLock", {}), 0);
            }
        }

        c.width  = asFloat (v.getProperty ("width",  {}), 1000.0f);
        c.height = asFloat (v.getProperty ("height", {}), 600.0f);
        return c;
    }
}

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

    // ---- Feed cursor + advance simulation at a fixed 60 Hz ----
    sim.setMouse (mouseX.load(), mouseY.load(), mouseActive.load());
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

void BreakoutMidiProcessor::setMouse (float x, float y, bool active)
{
    mouseX.store (x); mouseY.store (y); mouseActive.store (active);
}

void BreakoutMidiProcessor::setStateFromVar (const juce::var& v)
{
    { const juce::ScopedLock sl (stateLock); stateJson = juce::JSON::toString (v); }
    setConfig (parseConfig (v));
    if (v.hasProperty ("playing")) setPlaying ((bool) v.getProperty ("playing", false));
}

juce::var BreakoutMidiProcessor::getStateVar()
{
    juce::String json;
    { const juce::ScopedLock sl (stateLock); json = stateJson; }
    return json.isEmpty() ? juce::var() : juce::JSON::parse (json);
}

void BreakoutMidiProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::String json;
    { const juce::ScopedLock sl (stateLock); json = stateJson; }
    juce::MemoryOutputStream (dest, false).writeText (json, false, false, nullptr);
}

void BreakoutMidiProcessor::setStateInformation (const void* data, int size)
{
    const juce::String json = juce::String::createStringFromData (data, size);
    if (json.isEmpty()) return;
    const auto v = juce::JSON::parse (json);
    { const juce::ScopedLock sl (stateLock); stateJson = json; }
    setConfig (parseConfig (v));
    if (v.hasProperty ("playing")) setPlaying ((bool) v.getProperty ("playing", false));
}

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
