#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

//==============================================================================
// Hosts the BreakoutMIDI WebView UI (the existing index.html) and bridges it to
// the processor's MIDI I/O.
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakoutMidiEditor)
};
