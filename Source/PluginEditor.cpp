#include "PluginEditor.h"

YoloEditor::YoloEditor (YoloAudioProcessor& p)
    : AudioProcessorEditor (p),
      proc (p),
      browser (makeOptions())
{
    for (const auto& id : YoloAudioProcessor::paramIds())
    {
        auto* prm = proc.apvts.getParameter (id);
        jassert (prm != nullptr);
        params.push_back (prm);
        lastKnown.push_back (prm->convertFrom0to1 (prm->getValue()));
    }
    lastStateVersion = proc.stateVersion.load();

    addAndMakeVisible (browser);
    browser.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());

    setResizable (true, true);
    setResizeLimits (720, 645, 2880, 2580);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) designW / (double) designH);
    setSize (1152, 1152 * designH / designW);

    startTimerHz (30);
}

void YoloEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff150e12));
}

void YoloEditor::resized()
{
    browser.setBounds (getLocalBounds());
}

juce::File YoloEditor::presetsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("YOLO Tune")
               .getChildFile ("presets.json");
}

std::optional<juce::WebBrowserComponent::Resource> YoloEditor::getResource (const juce::String& url)
{
    // JUCE passes the path part of the request, e.g. "/" or "/index.html"
    const auto path = url.upToFirstOccurrenceOf ("?", false, false).trimCharactersAtStart ("/");

    if (path.isEmpty() || path == "index.html")
    {
        const auto* bytes = reinterpret_cast<const std::byte*> (BinaryData::index_html);
        return juce::WebBrowserComponent::Resource { std::vector<std::byte> (bytes, bytes + BinaryData::index_htmlSize), "text/html" };
    }

    return std::nullopt;
}

juce::WebBrowserComponent::Options YoloEditor::makeOptions()
{
    using Opts = juce::WebBrowserComponent::Options;

    return Opts{}
        .withBackend (Opts::Backend::webview2)
        .withWinWebView2Options (Opts::WinWebView2{}
                                     .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("YOLO-Tune-WebView"))
                                     .withBackgroundColour (juce::Colour (0xff150e12))
                                     .withStatusBarDisabled())
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider ([this] (const juce::String& url) { return getResource (url); })

        // UI asks for everything it needs when it loads
        .withNativeFunction ("getInit", [this] (const juce::Array<juce::var>&, juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("params", paramsAsVar (false));
            o->setProperty ("ui", proc.getUiState());
            o->setProperty ("presets", presetsFile().existsAsFile() ? presetsFile().loadFileAsString() : juce::String());
            o->setProperty ("sr", proc.getCurrentSampleRate());
            done (juce::var (o));
        })

        // UI changed one or more controls
        .withNativeFunction ("setParams", [this] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            if (! args.isEmpty())
                setParamsFromUi (args[0]);
            done (juce::var());
        })

        // UI-only state (key, scale, groups, A/B...) saved inside the DAW project
        .withNativeFunction ("saveUi", [this] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            if (! args.isEmpty())
                proc.setUiState (args[0].toString());
            done (juce::var());
        })

        // user presets are shared by every instance and every project
        .withNativeFunction ("savePresets", [] (const juce::Array<juce::var>& args, juce::WebBrowserComponent::NativeFunctionCompletion done)
        {
            bool ok = false;
            if (! args.isEmpty())
            {
                auto f = presetsFile();
                f.getParentDirectory().createDirectory();
                ok = f.replaceWithText (args[0].toString());
            }
            done (juce::var (ok));
        });
}

juce::var YoloEditor::paramsAsVar (bool)
{
    auto* o = new juce::DynamicObject();
    const auto& ids = YoloAudioProcessor::paramIds();
    for (size_t i = 0; i < params.size(); ++i)
    {
        const float v = params[i]->convertFrom0to1 (params[i]->getValue());
        if (dynamic_cast<juce::AudioParameterBool*> (params[i]) != nullptr)
            o->setProperty (ids[(int) i], v > 0.5f);
        else
            o->setProperty (ids[(int) i], v);
    }
    return juce::var (o);
}

void YoloEditor::setParamsFromUi (const juce::var& obj)
{
    const auto& ids = YoloAudioProcessor::paramIds();
    for (size_t i = 0; i < params.size(); ++i)
    {
        const juce::Identifier id (ids[(int) i]);
        if (! obj.hasProperty (id))
            continue;

        const auto& raw = obj[id];
        const float v = raw.isBool() ? ((bool) raw ? 1.0f : 0.0f) : (float) raw;
        auto* prm = params[i];
        const float norm = prm->convertTo0to1 (v);

        if (std::abs (norm - prm->getValue()) > 1.0e-6f)
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (norm);
            prm->endChangeGesture();
        }
        lastKnown[i] = prm->convertFrom0to1 (prm->getValue());
    }
}

void YoloEditor::timerCallback()
{
    // live pitch + level readout for the visuals
    auto& d = proc.dsp;
    auto* t = new juce::DynamicObject();
    t->setProperty ("m", d.teleM.load());
    t->setProperty ("out", d.teleOut.load());
    t->setProperty ("voiced", d.teleVoiced.load());
    t->setProperty ("pi", d.peakIn.exchange (0.0f));
    t->setProperty ("po", d.peakOut.exchange (0.0f));
    t->setProperty ("sr", proc.getCurrentSampleRate());
    t->setProperty ("bypass", proc.getBypassParameter()->getValue() > 0.5f);
    browser.emitEventIfBrowserIsVisible ("telemetry", juce::var (t));

    // a saved state was loaded by the host: send everything again
    const int sv = proc.stateVersion.load();
    if (sv != lastStateVersion)
    {
        lastStateVersion = sv;
        for (size_t i = 0; i < params.size(); ++i)
            lastKnown[i] = params[i]->convertFrom0to1 (params[i]->getValue());

        auto* s = new juce::DynamicObject();
        s->setProperty ("params", paramsAsVar (false));
        s->setProperty ("ui", proc.getUiState());
        browser.emitEventIfBrowserIsVisible ("state", juce::var (s));
        return;
    }

    // host automation or generic-editor changes: tell the UI
    bool changed = false;
    for (size_t i = 0; i < params.size(); ++i)
    {
        const float v = params[i]->convertFrom0to1 (params[i]->getValue());
        if (std::abs (v - lastKnown[i]) > 1.0e-5f)
        {
            lastKnown[i] = v;
            changed = true;
        }
    }
    if (changed)
        browser.emitEventIfBrowserIsVisible ("params", paramsAsVar (true));
}
