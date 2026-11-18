#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

// The plugin window: shows the YOLO HTML interface in a web view and links it to the audio engine.
class YoloEditor : public juce::AudioProcessorEditor,
                   private juce::Timer
{
public:
    explicit YoloEditor (YoloAudioProcessor&);
    ~YoloEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Size the HTML layout is designed at (the page scales itself to fit the window).
    static constexpr int designW = 1440, designH = 1267;

private:
    void timerCallback() override;

    juce::WebBrowserComponent::Options makeOptions();
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);

    juce::var paramsAsVar (bool onlyHostChanges);
    void setParamsFromUi (const juce::var& obj);

    static juce::File presetsFile();

    YoloAudioProcessor& proc;
    std::vector<juce::RangedAudioParameter*> params;
    std::vector<float> lastKnown;     // last value the UI knows about, per parameter
    int lastStateVersion = 0;

    juce::WebBrowserComponent browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YoloEditor)
};
