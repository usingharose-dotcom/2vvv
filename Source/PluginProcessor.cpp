#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }
}

const juce::StringArray& YoloAudioProcessor::paramIds()
{
    static const juce::StringArray ids = []
    {
        juce::StringArray a { "speed", "sustain", "amount", "stab", "bypass", "lowlat", "inst", "ref",
                              "inGain", "outGain", "doubler", "flex", "humanize", "tone" };
        for (int i = 0; i < 12; ++i) a.add ("n" + juce::String (i));
        return a;
    }();
    return ids;
}

juce::AudioProcessorValueTreeState::ParameterLayout YoloAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto dial = [&] (const char* id, const char* name, float def, std::function<String (float, int)> fmt)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 1.0f), def,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (std::move (fmt))));
    };
    auto slider = [&] (const char* id, const char* name, float def, std::function<String (float, int)> fmt = pct)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (0.0f, 100.0f, 1.0f), def,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (std::move (fmt))));
    };

    dial ("speed", "Speed", 0.489f, [] (float v, int) { return String (speedDialToMs (v), 1) + " ms"; });
    dial ("sustain", "Sustain", 0.0f, [] (float v, int) { return String (roundToInt (v * 100)); });
    dial ("amount", "Amount", 0.5f, [] (float v, int) { return String (roundToInt (v * 100)); });

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "stab", 1 }, "Note Stabilizer",
                                                        StringArray { "None", "Short", "Mid", "Long" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "lowlat", 1 }, "Low Latency", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "inst", 1 }, "Instrument Mode", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "ref", 1 }, "Pitch Reference",
                                                       NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction ([] (float c, int)
                                                       { return String (c, 1) + " cts (" + String (440.0 * std::pow (2.0, c / 1200.0), 1) + " Hz)"; })));

    slider ("inGain", "Input", 50.0f, [] (float v, int) { return String (roundToInt (v * 2)) + "%"; });
    slider ("outGain", "Output", 50.0f, [] (float v, int) { return String (roundToInt (v * 2)) + "%"; });
    slider ("doubler", "Doubler", 0.0f);
    slider ("flex", "Flex-Tune", 0.0f);
    slider ("humanize", "Humanize", 0.0f);
    slider ("tone", "Tone Volume", 0.0f);

    for (int i = 0; i < 12; ++i)
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { "n" + String (i), 1 }, String ("Note ") + noteNames[i], true));

    return layout;
}

YoloAudioProcessor::YoloAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "YOLO", createLayout())
{
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));

    speedP   = apvts.getRawParameterValue ("speed");
    sustainP = apvts.getRawParameterValue ("sustain");
    amountP  = apvts.getRawParameterValue ("amount");
    stabP    = apvts.getRawParameterValue ("stab");
    lowlatP  = apvts.getRawParameterValue ("lowlat");
    instP    = apvts.getRawParameterValue ("inst");
    refP     = apvts.getRawParameterValue ("ref");
    inGainP  = apvts.getRawParameterValue ("inGain");
    outGainP = apvts.getRawParameterValue ("outGain");
    doublerP = apvts.getRawParameterValue ("doubler");
    flexP    = apvts.getRawParameterValue ("flex");
    humP     = apvts.getRawParameterValue ("humanize");
    toneP    = apvts.getRawParameterValue ("tone");
    for (int i = 0; i < 12; ++i)
        noteParams[(size_t) i] = apvts.getRawParameterValue ("n" + juce::String (i));
}

bool YoloAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    const bool inOk  = in  == juce::AudioChannelSet::mono() || in  == juce::AudioChannelSet::stereo();
    return outOk && inOk;
}

void YoloAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    mono.assign ((size_t) juce::jmax (samplesPerBlock, 512), 0.0f);
    dsp.prepare (sampleRate);
    reportedLatency = dsp.getLatencySamples();
    setLatencySamples (reportedLatency);
}

void YoloAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();

    YoloParams p;
    p.speedMs  = speedDialToMs (speedP->load());
    p.sustain  = sustainP->load();
    p.amount   = amountP->load();
    p.stab     = juce::roundToInt (stabP->load());
    p.bypass   = bypassParam->get();
    p.lowLat   = lowlatP->load() > 0.5f;
    p.inst     = instP->load() > 0.5f;
    p.refCents = refP->load();
    p.inGain   = inGainP->load() / 50.0f;
    p.outGain  = outGainP->load() / 50.0f;
    p.doubler  = doublerP->load() / 100.0f;
    p.flex     = flexP->load() / 100.0f;
    p.hum      = humP->load() / 100.0f;
    p.tone     = toneP->load() / 100.0f;
    for (int i = 0; i < 12; ++i)
        p.allowed[i] = noteParams[(size_t) i]->load() > 0.5f;

    // the tuner is mono: fold the input down, process, then write stereo out
    for (int start = 0; start < n; start += (int) mono.size())
    {
        const int len = juce::jmin ((int) mono.size(), n - start);

        if (numIn <= 0)
            std::fill (mono.begin(), mono.begin() + len, 0.0f);
        else
        {
            const float g = 1.0f / (float) juce::jmin (numIn, 2);
            for (int i = 0; i < len; ++i)
            {
                float s = 0;
                for (int c = 0; c < juce::jmin (numIn, 2); ++c) s += buffer.getReadPointer (c)[start + i];
                mono[(size_t) i] = s * g;
            }
        }

        if (numOut > 0)
        {
            float* l = buffer.getWritePointer (0) + start;
            float* r = numOut > 1 ? buffer.getWritePointer (1) + start : nullptr;
            dsp.process (mono.data(), l, r, len, p);
        }
    }

    for (int c = 2; c < numOut; ++c)
        buffer.clear (c, 0, n);

    if (dsp.getLatencySamples() != reportedLatency)
    {
        reportedLatency = dsp.getLatencySamples();
        setLatencySamples (reportedLatency);
    }
}

void YoloAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("ui", getUiState(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void YoloAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            setUiState (tree.getProperty ("ui").toString());
            tree.removeProperty ("ui", nullptr);
            apvts.replaceState (tree);
            ++stateVersion;
        }
    }
}

juce::AudioProcessorEditor* YoloAudioProcessor::createEditor()
{
    return new YoloEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new YoloAudioProcessor();
}
