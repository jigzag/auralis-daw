#pragma once
#include "NativeEffects.h"

namespace auralis {
struct ReverbSettings {
    float size = 55, damping = 50, width = 100, preDelayMs = 20, lowCut = 100, highCut = 8000, mix = 20, output = 0;
    bool bypass = false;
};
class ReverbEffect final : public NativeEffect {
public:
    ReverbEffect();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void reset() override;
    void processBlock(AudioBuffer<float>&, MidiBuffer&) override;
    AudioProcessorEditor* createEditor() override;
    ReverbSettings settings() const;
    bool setSettings(const ReverbSettings&);
    void preset(int);
    double getTailLengthSeconds() const override;
private:
    void configure();
    AudioParameterFloat *size, *damping, *width, *preDelay, *lowCut, *highCut, *mix, *output;
    AudioParameterBool* bypass;
    juce::Reverb reverb;
    AudioBuffer<float> preBuffer, wetBuffer;
    double sampleRate = 48000;
    int writePosition = 0;
    std::array<float, 2> lowState {}, highState {};
    SmoothedValue<float> preSmooth, mixSmooth, outputSmooth, lowSmooth, highSmooth;
};
struct DelaySettings {
    float timeMs = 350, feedback = 35, highCut = 6000, mix = 25, output = 0;
    int division = 3;
    bool sync = false, pingPong = false, bypass = false;
};
class DelayEffect final : public NativeEffect {
public:
    DelayEffect();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void reset() override;
    void processBlock(AudioBuffer<float>&, MidiBuffer&) override;
    AudioProcessorEditor* createEditor() override;
    DelaySettings settings() const;
    bool setSettings(const DelaySettings&);
    void preset(int);
    double getTailLengthSeconds() const override;
    double delayMs(double bpm) const;
    double effectiveTimeMs() const { return delayMs(tempo.load()); }
    double tailSeconds(double bpm) const;
    double tempoBpm() const { return tempo.load(); }
    static StringArray divisions();
private:
    AudioParameterFloat *time, *feedback, *highCut, *mix, *output;
    AudioParameterChoice* division;
    AudioParameterBool *sync, *pingPong, *bypass;
    AudioBuffer<float> delayBuffer;
    int writePosition = 0;
    double sampleRate = 48000;
    std::atomic<double> tempo {120};
    bool fresh = true;
    std::array<float, 2> toneState {};
    SmoothedValue<float> timeSmooth, feedbackSmooth, toneSmooth, mixSmooth, outputSmooth, pingSmooth;
};
}
