#include "Equalizer.h"
#include <cmath>
#include <complex>

namespace auralis {
PluginDescription equalizerDescription() {
    PluginDescription p; p.name = p.descriptiveName = "Auralis EQ"; p.pluginFormatName = "Auralis";
    p.category = "EQ"; p.manufacturerName = "Auralis"; p.version = "1.0"; p.fileOrIdentifier = "auralis:equalizer:v1";
    p.uniqueId = 0x41514531; p.isInstrument = false; p.numInputChannels = p.numOutputChannels = 2; return p;
}
bool isEqualizer(const PluginDescription& p) { return p.pluginFormatName == "Auralis" && p.fileOrIdentifier == "auralis:equalizer:v1"; }
namespace {
template<class Parameter> void change(Parameter* p, float actual) {
    const float value = p->convertTo0to1(actual);
    auto& parameter = static_cast<AudioProcessorParameter&>(*p);
    if (std::abs(parameter.getValue() - value) > 1e-7f) parameter.setValueNotifyingHost(value);
}
}
Equalizer::Equalizer() : AudioPluginInstance(BusesProperties().withInput("Input", AudioChannelSet::stereo(), true).withOutput("Output", AudioChannelSet::stereo(), true)) {
    auto add = [this](auto* p) { addHostedParameter(std::unique_ptr<HostedParameter>(p)); return p; };
    constexpr float frequencies[] {100, 400, 2000, 8000}; constexpr int types[] {1, 0, 0, 2};
    for (int i = 0; i < 4; ++i) {
        auto& p = parameters[size_t(i)]; const String id = "band" + String(i + 1), name = "Band " + String(i + 1) + " ";
        NormalisableRange<float> range(20, 20000, 0.01f); range.setSkewForCentre(1000);
        p.frequency = add(new AudioParameterFloat(ParameterID(id + ".frequency", 1), name + "Frequency", range, frequencies[i]));
        p.gain = add(new AudioParameterFloat(ParameterID(id + ".gain", 1), name + "Gain", -24, 24, 0));
        p.q = add(new AudioParameterFloat(ParameterID(id + ".q", 1), name + "Q", 0.1f, 12.0f, i == 0 || i == 3 ? 0.7071f : 1.0f));
        p.type = add(new AudioParameterChoice(ParameterID(id + ".type", 1), name + "Type", {"Bell", "Low shelf", "High shelf", "Low cut", "High cut"}, types[i]));
        p.enabled = add(new AudioParameterBool(ParameterID(id + ".enabled", 1), name + "Enabled", true));
    }
    output = add(new AudioParameterFloat(ParameterID("output", 1), "Output", -24, 12, 0));
    bypass = add(new AudioParameterBool(ParameterID("bypass", 1), "Bypass", false));
    prepareToPlay(48000, 128);
}
bool Equalizer::isBusesLayoutSupported(const BusesLayout& layout) const {
    return layout.getMainInputChannelSet() == AudioChannelSet::stereo() && layout.getMainOutputChannelSet() == AudioChannelSet::stereo();
}
EqBand Equalizer::band(int i) const {
    if (!isPositiveAndBelow(i, 4)) return {};
    const auto& p = parameters[size_t(i)]; return {p.frequency->get(), p.gain->get(), p.q->get(), p.type->getIndex(), p.enabled->get()};
}
bool Equalizer::setBand(int i, const EqBand& b) {
    if (!isPositiveAndBelow(i, 4) || !std::isfinite(b.frequency) || b.frequency < 20 || b.frequency > 20000
        || !std::isfinite(b.gain) || b.gain < -24 || b.gain > 24 || !std::isfinite(b.q) || b.q < 0.1f || b.q > 12 || b.type < 0 || b.type > 4) return false;
    auto& p = parameters[size_t(i)]; change(p.frequency, b.frequency); change(p.gain, b.gain); change(p.q, b.q); change(p.type, float(b.type)); change(p.enabled, b.enabled ? 1.0f : 0.0f); return true;
}
float Equalizer::outputDb() const { return output->get(); }
void Equalizer::setOutputDb(float db) { if (std::isfinite(db)) change(output, jlimit(-24.0f, 12.0f, db)); }
bool Equalizer::bypassed() const { return bypass->get(); }
void Equalizer::setBypassed(bool value) { change(bypass, value ? 1.0f : 0.0f); }
void Equalizer::restoreDefaults() { for (auto* p : getParameters()) p->setValueNotifyingHost(p->getDefaultValue()); }
Equalizer::Coefficients Equalizer::makeCoefficients(const EqBand& band, double rate) {
    if (!band.enabled) return {1, 0, 0, 0, 0};
    const double frequency = jlimit(20.0, jmin(20000.0, rate * 0.49), double(band.frequency));
    const double gain = std::pow(10.0, band.gain / 20.0), q = band.q;
    using Factory = dsp::IIR::ArrayCoefficients<double>; std::array<double, 6> c;
    switch (band.type) {
        case 1: c = Factory::makeLowShelf(rate, frequency, q, gain); break;
        case 2: c = Factory::makeHighShelf(rate, frequency, q, gain); break;
        case 3: c = Factory::makeHighPass(rate, frequency, q); break;
        case 4: c = Factory::makeLowPass(rate, frequency, q); break;
        default: c = Factory::makePeakFilter(rate, frequency, q, gain); break;
    }
    return {c[0] / c[3], c[1] / c[3], c[2] / c[3], c[4] / c[3], c[5] / c[3]};
}
void Equalizer::prepareToPlay(double rate, int) {
    displayRate = std::isfinite(rate) && rate >= 8000 ? rate : 48000;
    slew = 1 - std::exp(-1 / (0.005 * displayRate.load())); wet.reset(displayRate, 0.005); trim.reset(displayRate, 0.005); setLatencySamples(0); reset();
}
void Equalizer::reset() {
    for (int i = 0; i < 4; ++i) { auto& f = filters[size_t(i)]; f.z1.fill(0); f.z2.fill(0); f.coefficients = makeCoefficients(band(i), displayRate); }
    wet.setCurrentAndTargetValue(bypassed() ? 0.0f : 1.0f); trim.setCurrentAndTargetValue(Decibels::decibelsToGain(outputDb()));
}
void Equalizer::processBlock(AudioBuffer<float>& audio, MidiBuffer&) {
    const ScopedNoDenormals noDenormals;
    std::array<Coefficients, 4> target; for (int i = 0; i < 4; ++i) target[size_t(i)] = makeCoefficients(band(i), displayRate);
    wet.setTargetValue(bypassed() ? 0.0f : 1.0f); trim.setTargetValue(Decibels::decibelsToGain(outputDb()));
    for (int sample = 0; sample < audio.getNumSamples(); ++sample) {
        for (int i = 0; i < 4; ++i) for (int k = 0; k < 5; ++k) filters[size_t(i)].coefficients[size_t(k)] += slew * (target[size_t(i)][size_t(k)] - filters[size_t(i)].coefficients[size_t(k)]);
        const float mix = wet.getNextValue(), level = trim.getNextValue();
        for (int ch = 0; ch < jmin(2, audio.getNumChannels()); ++ch) {
            const float dry = std::isfinite(audio.getSample(ch, sample)) ? audio.getSample(ch, sample) : 0; double value = dry;
            for (auto& f : filters) {
                const auto& c = f.coefficients; const double y = c[0] * value + f.z1[size_t(ch)];
                f.z1[size_t(ch)] = c[1] * value - c[3] * y + f.z2[size_t(ch)]; f.z2[size_t(ch)] = c[2] * value - c[4] * y;
                if (!std::isfinite(y) || !std::isfinite(f.z1[size_t(ch)]) || !std::isfinite(f.z2[size_t(ch)])) { f.z1[size_t(ch)] = f.z2[size_t(ch)] = 0; value = 0; } else value = y;
            }
            const float result = float(dry + mix * (value * level - dry)); audio.setSample(ch, sample, std::isfinite(result) ? result : 0);
        }
    }
}
double Equalizer::responseDb(double frequency, int onlyBand) const {
    const double rate = displayRate.load(); frequency = jlimit(1.0, rate * 0.499, frequency);
    const auto z = std::exp(std::complex<double>(0, -MathConstants<double>::twoPi * frequency / rate)); double magnitude = 1;
    for (int i = 0; i < 4; ++i) if (onlyBand < 0 || onlyBand == i) { const auto c = makeCoefficients(band(i), rate); magnitude *= std::abs((c[0] + c[1] * z + c[2] * z * z) / (1.0 + c[3] * z + c[4] * z * z)); }
    return 20 * std::log10(jmax(1e-12, magnitude)) + (onlyBand < 0 ? outputDb() : 0);
}
void Equalizer::getStateInformation(MemoryBlock& result) {
    auto* state = new DynamicObject(); state->setProperty("kind", "auralis-eq-v1"); Array<var> values;
    for (auto* p : getParameters()) values.add(p->getValue()); state->setProperty("values", values);
    const auto text = JSON::toString(var(state), true); result.replaceWith(text.toRawUTF8(), size_t(text.getNumBytesAsUTF8()));
}
void Equalizer::setStateInformation(const void* data, int size) {
    if (!data || size <= 0 || size > 16384) return;
    const auto state = JSON::parse(String::fromUTF8(static_cast<const char*>(data), size)); const auto values = state["values"];
    if (state["kind"].toString() != "auralis-eq-v1" || !values.isArray() || values.size() != getParameters().size()) return;
    for (const auto& value : *values.getArray()) if ((!value.isDouble() && !value.isInt()) || !std::isfinite(double(value)) || double(value) < 0 || double(value) > 1) return;
    for (int i = 0; i < values.size(); ++i) getParameters()[i]->setValue(float(values[i]));
}
}
