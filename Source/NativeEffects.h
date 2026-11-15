#pragma once
#include "Engine.h"
#include <cmath>

namespace auralis {
enum class NativeEffectKind { equalizer, tuner, compressor, reverb, delay };
PluginDescription nativeDescription(NativeEffectKind);
bool isNativeEffect(const PluginDescription&);
std::unique_ptr<AudioPluginInstance> createNativeEffect(const PluginDescription&);

class NativeEffect : public AudioPluginInstance {
public:
    explicit NativeEffect(NativeEffectKind);
    const String getName() const override { return description.name; }
    void fillInPluginDescription(PluginDescription& p) const override { p = description; }
    bool isBusesLayoutSupported(const BusesLayout& b) const override { return b.getMainInputChannelSet() == AudioChannelSet::stereo() && b.getMainOutputChannelSet() == AudioChannelSet::stereo(); }
    bool hasEditor() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override {}
    void getStateInformation(MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    void restoreDefaults();
protected:
    template<class Parameter> Parameter* addNative(Parameter* p) { addHostedParameter(std::unique_ptr<HostedParameter>(p)); return p; }
    template<class Parameter> static void change(Parameter* p, float actual) {
        auto& parameter = static_cast<AudioProcessorParameter&>(*p); const float normalized = p->convertTo0to1(actual);
        if (std::abs(parameter.getValue() - normalized) > 1e-7f) parameter.setValueNotifyingHost(normalized);
    }
private:
    PluginDescription description;
};
}
