#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

//==============================================================================
// Hosts the BreakoutMIDI WebView UI. The UI is a control surface + renderer:
// it sends config/play/reset to the processor and draws the render snapshots
// the processor pushes back ("simState").
//==============================================================================
class BreakoutMidiEditor : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit BreakoutMidiEditor (BreakoutMidiProcessor&);
    ~BreakoutMidiEditor() override;

    void resized() override;

private:
    void timerCallback() override;
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    BreakoutMidiProcessor& proc;
    juce::WebBrowserComponent webView;
    int frameCounter = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakoutMidiEditor)
};
