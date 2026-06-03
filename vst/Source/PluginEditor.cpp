#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    int    asInt    (const juce::var& v, int d)    { return v.isVoid() ? d : (int) v; }
    float  asFloat  (const juce::var& v, float d)  { return v.isVoid() ? d : (float) (double) v; }
    bool   asBool   (const juce::var& v, bool d)   { return v.isVoid() ? d : (bool) v; }

    Simulation::Config parseConfig (const juce::var& v)
    {
        Simulation::Config c;

        const auto p = v.getProperty ("params", juce::var());
        auto& P = c.params;
        P.ballSpeed    = asFloat (p.getProperty ("ballSpeed",   {}), P.ballSpeed);
        P.gravity      = asFloat (p.getProperty ("gravity",     {}), P.gravity);
        P.numBalls     = asInt   (p.getProperty ("numBalls",    {}), P.numBalls);
        P.spawnRate    = asFloat (p.getProperty ("spawnRate",   {}), P.spawnRate);
        P.maxBricks    = asInt   (p.getProperty ("maxBricks",   {}), P.maxBricks);
        P.noteLen      = asInt   (p.getProperty ("noteLen",     {}), P.noteLen);
        P.midiChannel  = asInt   (p.getProperty ("midiChannel", {}), P.midiChannel);
        P.mode         = asInt   (p.getProperty ("mode",        {}), P.mode);
        P.speedToVel   = asBool  (p.getProperty ("speedToVel",  {}), P.speedToVel);

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

    juce::WebBrowserComponent::Options makeOptions (
        BreakoutMidiProcessor& proc,
        std::function<std::optional<juce::WebBrowserComponent::Resource> (const juce::String&)> resourceProvider)
    {
        using WBC = juce::WebBrowserComponent;
        return WBC::Options{}
            .withBackend (WBC::Options::Backend::webview2)
            .withWinWebView2Options (WBC::Options::WinWebView2{}
                                         .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)))
            .withNativeIntegrationEnabled()
            .withResourceProvider (std::move (resourceProvider))
            .withNativeFunction ("setConfig", [&proc] (const juce::Array<juce::var>& a, WBC::NativeFunctionCompletion complete)
            {
                if (a.size() >= 1) proc.setConfig (parseConfig (a[0]));
                complete (juce::var());
            })
            .withNativeFunction ("setPlaying", [&proc] (const juce::Array<juce::var>& a, WBC::NativeFunctionCompletion complete)
            {
                if (a.size() >= 1) proc.setPlaying ((bool) a[0]);
                complete (juce::var());
            })
            .withNativeFunction ("resetSim", [&proc] (const juce::Array<juce::var>&, WBC::NativeFunctionCompletion complete)
            {
                proc.requestReset();
                complete (juce::var());
            });
    }
}

//==============================================================================
BreakoutMidiEditor::BreakoutMidiEditor (BreakoutMidiProcessor& p)
    : juce::AudioProcessorEditor (&p),
      proc (p),
      webView (makeOptions (p, [this] (const juce::String& url) { return getResource (url); }))
{
    addAndMakeVisible (webView);
    setResizable (true, true);
    setResizeLimits (640, 420, 4096, 2400);
    setSize (1040, 680);

    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    startTimerHz (60);
}

BreakoutMidiEditor::~BreakoutMidiEditor() { stopTimer(); }

void BreakoutMidiEditor::resized() { webView.setBounds (getLocalBounds()); }

//==============================================================================
void BreakoutMidiEditor::timerCallback()
{
    // Host MIDI in -> UI held notes (every tick)
    BreakoutMidiProcessor::MidiInEvent e;
    while (proc.popMidiIn (e))
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("note", e.note);
        obj->setProperty ("vel",  e.vel);
        obj->setProperty ("on",   e.on);
        webView.emitEventIfBrowserIsVisible ("midiIn", juce::var (obj));
    }

    // Render snapshot -> UI at ~30 fps
    if ((++frameCounter & 1) == 0)
    {
        const auto snap = proc.getRenderSnapshot();

        juce::Array<juce::var> balls;
        balls.ensureStorageAllocated ((int) snap.balls.size() * 2);
        for (const auto& b : snap.balls) { balls.add (b.x); balls.add (b.y); }

        juce::Array<juce::var> bricks;
        bricks.ensureStorageAllocated ((int) snap.bricks.size() * 6);
        for (const auto& b : snap.bricks)
        {
            bricks.add (b.slotId); bricks.add (b.cx); bricks.add (b.cy);
            bricks.add (b.alive ? 1 : 0); bricks.add (b.hitsLeft); bricks.add (b.flash);
        }

        auto* o = new juce::DynamicObject();
        o->setProperty ("playing", snap.playing);
        o->setProperty ("balls",  juce::var (balls));
        o->setProperty ("bricks", juce::var (bricks));
        webView.emitEventIfBrowserIsVisible ("simState", juce::var (o));
    }
}

//==============================================================================
std::optional<juce::WebBrowserComponent::Resource>
BreakoutMidiEditor::getResource (const juce::String& url)
{
    const auto path = url == "/" ? juce::String ("index.html")
                                 : url.fromFirstOccurrenceOf ("/", false, false);

    if (path == "index.html")
    {
        const auto* raw = reinterpret_cast<const std::byte*> (BinaryData::index_html);
        std::vector<std::byte> data (raw, raw + BinaryData::index_htmlSize);
        return juce::WebBrowserComponent::Resource { std::move (data), juce::String ("text/html") };
    }
    return std::nullopt;
}
