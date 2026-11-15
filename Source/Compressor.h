#pragma once
#include "NativeEffects.h"

namespace auralis {
struct CompressorSettings {
    float threshold = -18, ratio = 3, attackMs = 10, releaseMs = 120, knee = 6, makeup = 0, mix = 100;
    bool bypass = false;
};
class Compressor final : public NativeEffect {
public:
    Compressor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void reset() override;
    void processBlock(AudioBuffer<float>&, MidiBuffer&) override;
    AudioProcessorEditor* createEditor() override;
    CompressorSettings settings() const;
    bool setSettings(const CompressorSettings&);
    double transferDb(double input) const;
    float reductionDb() const { return reduction.load(); }
    float inputDb() const { return inputMeter.load(); }
    float outputDb() const { return outputMeter.load(); }
private:
    AudioParameterFloat *threshold, *ratio, *attack, *release, *knee, *makeup, *mix;
    AudioParameterBool* bypass;
    double sampleRate = 48000, envelope = 0;
    SmoothedValue<float> thresholdSmooth, ratioSmooth, kneeSmooth, makeupSmooth, wetSmooth;
    std::atomic<float> reduction {0}, inputMeter {-120}, outputMeter {-120};
    static double gainReduction(double inputDb, double threshold, double ratio, double knee);
};
}
