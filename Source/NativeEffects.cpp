#include "NativeEffects.h"
#include "Equalizer.h"
#include "Compressor.h"
#include "Tuner.h"
#include "SpaceEffects.h"

namespace auralis {
PluginDescription nativeDescription(NativeEffectKind kind) {
    if (kind == NativeEffectKind::equalizer) return equalizerDescription();
    PluginDescription p;
    switch (kind) {
        case NativeEffectKind::tuner: p.name = "Auralis Tuner"; p.category = "Analyzer/Tuner"; p.fileOrIdentifier = "auralis:tuner:v1"; p.uniqueId = 0x41545531; break;
        case NativeEffectKind::reverb: p.name = "Auralis Reverb"; p.category = "Reverb"; p.fileOrIdentifier = "auralis:reverb:v1"; p.uniqueId = 0x41525631; break;
        case NativeEffectKind::delay: p.name = "Auralis Delay"; p.category = "Delay"; p.fileOrIdentifier = "auralis:delay:v1"; p.uniqueId = 0x41445931; break;
        default: p.name = "Auralis Compressor"; p.category = "Dynamics/Compressor"; p.fileOrIdentifier = "auralis:compressor:v1"; p.uniqueId = 0x41435031; break;
    }
    p.descriptiveName = p.name; p.pluginFormatName = "Auralis"; p.manufacturerName = "Auralis"; p.version = "1.0";
    p.isInstrument = false; p.numInputChannels = p.numOutputChannels = 2; return p;
}
bool isNativeEffect(const PluginDescription& p) {
    if (p.pluginFormatName != "Auralis") return false;
    for (auto kind : {NativeEffectKind::equalizer, NativeEffectKind::tuner, NativeEffectKind::compressor, NativeEffectKind::reverb, NativeEffectKind::delay}) if (p.fileOrIdentifier == nativeDescription(kind).fileOrIdentifier) return true;
    return false;
}
std::unique_ptr<AudioPluginInstance> createNativeEffect(const PluginDescription& p) {
    if (!isNativeEffect(p)) return {};
    if (p.fileOrIdentifier == "auralis:equalizer:v1") return std::make_unique<Equalizer>();
    if (p.fileOrIdentifier == "auralis:tuner:v1") return std::make_unique<Tuner>();
    if (p.fileOrIdentifier == "auralis:reverb:v1") return std::make_unique<ReverbEffect>();
    if (p.fileOrIdentifier == "auralis:delay:v1") return std::make_unique<DelayEffect>();
    return std::make_unique<Compressor>();
}
NativeEffect::NativeEffect(NativeEffectKind kind)
    : AudioPluginInstance(BusesProperties().withInput("Input", AudioChannelSet::stereo(), true).withOutput("Output", AudioChannelSet::stereo(), true)), description(nativeDescription(kind)) { setLatencySamples(0); }
void NativeEffect::getStateInformation(MemoryBlock& result) {
    auto* object = new DynamicObject(); object->setProperty("kind", description.fileOrIdentifier); Array<var> values;
    for (auto* p : getParameters()) values.add(p->getValue()); object->setProperty("values", values);
    const auto text = JSON::toString(var(object), true); result.replaceWith(text.toRawUTF8(), size_t(text.getNumBytesAsUTF8()));
}
void NativeEffect::setStateInformation(const void* bytes, int size) {
    if (!bytes || size <= 0 || size > 16384) return;
    const auto state = JSON::parse(String::fromUTF8(static_cast<const char*>(bytes), size)); const auto values = state["values"];
    if (state["kind"].toString() != description.fileOrIdentifier || !values.isArray() || values.size() != getParameters().size()) return;
    for (const auto& v : *values.getArray()) if ((!v.isInt() && !v.isDouble()) || !std::isfinite(double(v)) || double(v) < 0 || double(v) > 1) return;
    for (int i = 0; i < values.size(); ++i) getParameters()[i]->setValue(float(values[i]));
}
void NativeEffect::restoreDefaults() { for (auto* p : getParameters()) p->setValueNotifyingHost(p->getDefaultValue()); }
}
