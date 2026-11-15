#pragma once
#include "NativeEffects.h"

namespace auralis {
struct TunerReading { double frequency = 0; float confidence = 0, inputDb = -120; };
class Tuner final : public NativeEffect, private Thread {
public:
    Tuner();
    ~Tuner() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    void reset() override;
    void processBlock(AudioBuffer<float>&, MidiBuffer&) override;
    AudioProcessorEditor* createEditor() override;
    float referenceHz() const { return reference->get(); }
    int inputChannel() const { return channel->getIndex(); }
    bool muted() const { return mute->get(); }
    void setReferenceHz(float hz) { if (std::isfinite(hz) && hz >= 400 && hz <= 480) change(reference, hz); }
    void setInputChannel(int index) { if (index >= 0 && index <= 2) change(channel, float(index)); }
    void setMuted(bool value) { change(mute, value ? 1.0f : 0.0f); }
    TunerReading reading() const;
private:
    void run() override;
    void analyse(uint32_t);
    struct Sample { float value = 0; uint32_t epoch = 0; };
    static constexpr size_t capacity = 32768, windowSize = 2048;
    std::array<Sample, capacity> queue;
    std::atomic<uint64_t> written {0}, consumed {0};
    std::atomic<uint32_t> epoch {1};
    std::atomic<uint32_t> publishedEpoch {0};
    std::atomic<double> frequency {0}, lastAudio {0};
    std::atomic<float> confidence {0}, inputLevel {-120};
    AudioParameterFloat* reference = nullptr;
    AudioParameterChoice* channel = nullptr;
    AudioParameterBool* mute = nullptr;
    double analysisRate = 12000;
    int decimation = 4, phase = 0, selectedChannel = 0;
    std::array<double, 5> filter {1, 0, 0, 0, 0};
    std::array<std::array<double, 2>, 2> filterState {};
    SmoothedValue<float> audible;
    std::array<float, windowSize> history {}, ordered {}, difference {};
    size_t position = 0, available = 0;
    int sinceAnalysis = 0;
    uint32_t workerEpoch = 0;
    double stableFrequency = 0;
};
}
