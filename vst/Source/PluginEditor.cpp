#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
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
                if (a.size() >= 1) proc.setStateFromVar (a[0]);
                complete (juce::var());
            })
            .withNativeFunction ("getState", [&proc] (const juce::Array<juce::var>&, WBC::NativeFunctionCompletion complete)
            {
                complete (proc.getStateVar());
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
