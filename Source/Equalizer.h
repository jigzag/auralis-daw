#pragma once
#include "Engine.h"

namespace auralis {
struct EqBand {
    float frequency = 1000, gain = 0, q = 1;
    int type = 0; // Bell, low shelf, high shelf, low cut, high cut.
    bool enabled = true;
    bool operator==(const EqBand&) const = default;
};
PluginDescription equalizerDescription();
bool isEqualizer(const PluginDescription&);

class Equalizer final : public AudioPluginInstance {
public:
    Equalizer();
    const String getName() const override { return "Auralis EQ"; }
    void fillInPluginDescription(PluginDescription& p) const override { p = equalizerDescription(); }
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void reset() override;
    void processBlock(AudioBuffer<float>&, MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    bool hasEditor() const override { return true; }
    AudioProcessorEditor* createEditor() override;
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
    EqBand band(int) const;
    bool setBand(int, const EqBand&);
    float outputDb() const;
    void setOutputDb(float);
    bool bypassed() const;
    void setBypassed(bool);
    void restoreDefaults();
    double responseDb(double frequency, int onlyBand = -1) const;
    double currentRate() const { return displayRate.load(); }
private:
    struct Parameters {
        AudioParameterFloat *frequency = nullptr, *gain = nullptr, *q = nullptr;
        AudioParameterChoice* type = nullptr;
        AudioParameterBool* enabled = nullptr;
    };
    std::array<Parameters, 4> parameters;
    AudioParameterFloat* output = nullptr;
    AudioParameterBool* bypass = nullptr;
    std::atomic<double> displayRate {48000};
    using Coefficients = std::array<double, 5>;
    struct Filter { Coefficients coefficients {1, 0, 0, 0, 0}; std::array<double, 2> z1 {}, z2 {}; };
    std::array<Filter, 4> filters;
    SmoothedValue<float> wet, trim;
    double slew = 0.001;
    static Coefficients makeCoefficients(const EqBand&, double rate);
};
}
