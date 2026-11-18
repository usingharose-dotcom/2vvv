#pragma once

#include <JuceHeader.h>
#include "YoloDSP.h"

class YoloAudioProcessor : public juce::AudioProcessor
{
public:
    YoloAudioProcessor();
    ~YoloAudioProcessor() override = default;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    // ---- shared with the editor ----
    juce::AudioProcessorValueTreeState apvts;
    YoloDSP dsp;

    // Parameter IDs, in the units the UI uses (dials 0..1, sliders 0..100, cents, bools).
    static const juce::StringArray& paramIds();

    juce::String getUiState() const              { const juce::ScopedLock sl (uiLock); return uiState; }
    void setUiState (const juce::String& s)       { const juce::ScopedLock sl (uiLock); uiState = s; }

    // Bumped whenever the host loads a saved state, so an open editor can refresh.
    std::atomic<int> stateVersion { 0 };

    double getCurrentSampleRate() const { return currentSampleRate; }

    // Converts the dial position (0..1) into retune speed in ms (same curve as the web UI).
    static float speedDialToMs (float v) { return -3.0f + (1.0f - v) * (1.0f - v) * 203.0f; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioParameterBool* bypassParam = nullptr;
    std::array<std::atomic<float>*, 12> noteParams {};
    std::atomic<float> *speedP = nullptr, *sustainP = nullptr, *amountP = nullptr, *stabP = nullptr,
                       *lowlatP = nullptr, *instP = nullptr, *refP = nullptr, *inGainP = nullptr,
                       *outGainP = nullptr, *doublerP = nullptr, *flexP = nullptr, *humP = nullptr, *toneP = nullptr;

    std::vector<float> mono;
    double currentSampleRate = 48000.0;
    int reportedLatency = -1;

    juce::CriticalSection uiLock;
    juce::String uiState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (YoloAudioProcessor)
};
