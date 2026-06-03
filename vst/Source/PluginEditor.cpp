#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    juce::WebBrowserComponent::Options makeOptions (BreakoutMidiEditor& owner,
                                                    BreakoutMidiProcessor& proc,
                                                    std::function<std::optional<juce::WebBrowserComponent::Resource> (const juce::String&)> resourceProvider)
    {
        juce::ignoreUnused (owner);

        return juce::WebBrowserComponent::Options{}
            .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
            .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
                                         .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)))
            .withNativeIntegrationEnabled()
            .withResourceProvider (std::move (resourceProvider))
            .withNativeFunction ("sendMidi",
                                 [&proc] (const juce::Array<juce::var>& args,
                                          juce::WebBrowserComponent::NativeFunctionCompletion complete)
                                 {
                                     if (args.size() >= 4)
                                         proc.enqueueNote ((int) args[0], (int) args[1],
                                                           (int) args[2], (int) args[3]);

                                     complete (juce::var());
                                 });
    }
}

//==============================================================================
BreakoutMidiEditor::BreakoutMidiEditor (BreakoutMidiProcessor& p)
    : juce::AudioProcessorEditor (&p),
      proc (p),
      webView (makeOptions (*this, p, [this] (const juce::String& url) { return getResource (url); }))
{
    addAndMakeVisible (webView);
    setResizable (true, true);
    setResizeLimits (640, 420, 4096, 2400);
    setSize (1040, 680);

    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    startTimerHz (60);
}

BreakoutMidiEditor::~BreakoutMidiEditor()
{
    stopTimer();
}

//==============================================================================
void BreakoutMidiEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

void BreakoutMidiEditor::timerCallback()
{
    BreakoutMidiProcessor::MidiInEvent e;
    while (proc.popMidiIn (e))
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("note", e.note);
        obj->setProperty ("vel",  e.vel);
        obj->setProperty ("on",   e.on);
        webView.emitEventIfBrowserIsVisible ("midiIn", juce::var (obj));
    }
}

//==============================================================================
std::optional<juce::WebBrowserComponent::Resource>
BreakoutMidiEditor::getResource (const juce::String& url)
{
    const auto path = url == "/" ? juce::String ("index.html")
                                 : url.fromFirstOccurrenceOf ("/", false, false);

   #if BREAKOUT_MIDI_DEBUG_LOG
    juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getChildFile ("breakoutmidi_midi.log")
        .appendText ("resource requested: '" + url + "'\n");
   #endif

    if (path == "index.html")
    {
        const auto* raw = reinterpret_cast<const std::byte*> (BinaryData::index_html);
        std::vector<std::byte> data (raw, raw + BinaryData::index_htmlSize);
        return juce::WebBrowserComponent::Resource { std::move (data),
                                                     juce::String ("text/html") };
    }

    return std::nullopt;
}
