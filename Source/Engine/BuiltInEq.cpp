#include "BuiltInEq.h"
#include "Core/GraphicEq.h"

namespace el {
namespace {

constexpr const char* kEqId = "builtin:eq";
constexpr float kRangeDb = 12.0f;

class EqProcessor : public juce::AudioPluginInstance {
public:
    EqProcessor()
        : juce::AudioPluginInstance (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                                      .withOutput ("Out", juce::AudioChannelSet::stereo()))
    {
        for (int i = 0; i < kEqBands; ++i) {
            const float f = GraphicEq::kFrequencies[i];
            const juce::String name = f >= 1000.0f ? juce::String (f / 1000.0f, f < 10000.0f ? 0 : 0) + "k" : juce::String ((int) std::round (f));
            auto* p = new juce::AudioParameterFloat (juce::ParameterID ("band" + juce::String (i), 1), name + " Hz",
                                                     juce::NormalisableRange<float> (-kRangeDb, kRangeDb, 0.1f), 0.0f,
                                                     juce::AudioParameterFloatAttributes().withLabel ("dB"));
            bands[(size_t) i] = p;
            addHostedParameter (std::unique_ptr<juce::HostedAudioProcessorParameter> (p));
        }
    }

    void fillInPluginDescription (juce::PluginDescription& d) const override { d = builtin::eqDescription(); }
    const juce::String getName() const override { return "ELooper EQ"; }

    void prepareToPlay (double sr, int) override { eq.prepare (sr); eq.reset(); pushGains(); }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        return l.getMainInputChannelSet() == l.getMainOutputChannelSet() && ! l.getMainOutputChannelSet().isDisabled()
            && l.getMainOutputChannelSet().size() <= 2;
    }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        pushGains();
        if (eq.isFlat() || buffer.getNumChannels() == 0) return;
        float* l = buffer.getWritePointer (0);
        float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
        if (r != nullptr) { eq.process (l, r, buffer.getNumSamples()); return; }
        // Mono: run a scratch copy as the right channel.
        scratch.setSize (1, buffer.getNumSamples(), false, false, true);
        scratch.copyFrom (0, 0, buffer, 0, 0, buffer.getNumSamples());
        eq.process (l, scratch.getWritePointer (0), buffer.getNumSamples());
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& out) override
    {
        juce::XmlElement x ("ELooperEQ");
        for (int i = 0; i < kEqBands; ++i) x.setAttribute ("b" + juce::String (i), (double) bands[(size_t) i]->get());
        copyXmlToBinary (x, out);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size))
            for (int i = 0; i < kEqBands; ++i)
                *bands[(size_t) i] = (float) x->getDoubleAttribute ("b" + juce::String (i), 0.0);
    }

    std::array<juce::AudioParameterFloat*, kEqBands> bands {};

private:
    void pushGains() noexcept
    {
        float g[kEqBands];
        for (int i = 0; i < kEqBands; ++i) g[i] = bands[(size_t) i]->get();
        eq.setGains (g);
    }
    GraphicEq eq;
    juce::AudioBuffer<float> scratch;
};

// Ten vertical sliders, one per band; double-click a slider to reset it to 0 dB.
class EqEditor : public juce::AudioProcessorEditor {
public:
    explicit EqEditor (EqProcessor& p) : juce::AudioProcessorEditor (p)
    {
        for (int i = 0; i < kEqBands; ++i) {
            auto& s = sliders[(size_t) i];
            s.setSliderStyle (juce::Slider::LinearVertical);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 44, 18);
            s.setDoubleClickReturnValue (true, 0.0);
            s.setColour (juce::Slider::trackColourId, juce::Colour (0xff4fc3f7));
            s.setColour (juce::Slider::thumbColourId, juce::Colours::white);
            s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            addAndMakeVisible (s);
            attachments[(size_t) i] = std::make_unique<juce::SliderParameterAttachment> (*p.bands[(size_t) i], s);
            auto& l = labels[(size_t) i];
            l.setText (p.bands[(size_t) i]->getName (16).upToFirstOccurrenceOf (" ", false, false), juce::dontSendNotification);
            l.setJustificationType (juce::Justification::centred);
            l.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
            addAndMakeVisible (l);
        }
        flat.setButtonText ("Flat");
        flat.onClick = [&p] { for (auto* b : p.bands) *b = 0.0f; };
        addAndMakeVisible (flat);
        setSize (560, 280);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff15171c));
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText ("ELooper EQ", 12, 8, 200, 22, juce::Justification::centredLeft);
        g.setColour (juce::Colours::grey);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("+12 dB / -12 dB, double-click a band to reset", 12, 8, getWidth() - 100, 22, juce::Justification::centredRight);
    }
    void resized() override
    {
        flat.setBounds (getWidth() - 70, 8, 58, 22);
        const int top = 40, w = (getWidth() - 24) / kEqBands;
        for (int i = 0; i < kEqBands; ++i) {
            labels[(size_t) i].setBounds (12 + i * w, top, w, 18);
            sliders[(size_t) i].setBounds (12 + i * w, top + 20, w, getHeight() - top - 30);
        }
    }
private:
    std::array<juce::Slider, kEqBands> sliders;
    std::array<juce::Label, kEqBands> labels;
    std::array<std::unique_ptr<juce::SliderParameterAttachment>, kEqBands> attachments;
    juce::TextButton flat;
};

juce::AudioProcessorEditor* EqProcessor::createEditor() { return new EqEditor (*this); }

} // namespace

namespace builtin {

juce::PluginDescription eqDescription()
{
    juce::PluginDescription d;
    d.name = "ELooper EQ";
    d.descriptiveName = "10-band graphic EQ";
    d.manufacturerName = "ELooper";
    d.pluginFormatName = "Built-in";
    d.category = "EQ";
    d.fileOrIdentifier = kEqId;
    d.uniqueId = d.deprecatedUid = 0x454c4551;   // 'ELEQ'
    d.version = "1.0";
    d.isInstrument = false;
    d.numInputChannels = d.numOutputChannels = 2;
    return d;
}

juce::MemoryBlock eqState (const float* gainsDb)
{
    juce::XmlElement x ("ELooperEQ");
    for (int i = 0; i < kEqBands; ++i) x.setAttribute ("b" + juce::String (i), (double) gainsDb[i]);
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary (x, out);
    return out;
}

bool isBuiltIn (const juce::PluginDescription& d) { return d.fileOrIdentifier == kEqId; }

std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d)
{
    if (d.fileOrIdentifier == kEqId) return std::make_unique<EqProcessor>();
    return nullptr;
}

} // namespace builtin
} // namespace el
