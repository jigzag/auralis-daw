#include "Compressor.h"

namespace auralis {
Compressor::Compressor() : NativeEffect(NativeEffectKind::compressor) {
    auto parameter = [this](const char* id, const char* name, float low, float high, float value) { return addNative(new AudioParameterFloat(ParameterID(id, 1), name, low, high, value)); };
    threshold = parameter("threshold", "Threshold", -60, 0, -18); ratio = parameter("ratio", "Ratio", 1, 20, 3);
    attack = parameter("attack", "Attack", 0.1f, 200, 10); release = parameter("release", "Release", 10, 2000, 120);
    knee = parameter("knee", "Knee", 0, 24, 6); makeup = parameter("makeup", "Makeup", -12, 24, 0); mix = parameter("mix", "Mix", 0, 100, 100);
    bypass = addNative(new AudioParameterBool(ParameterID("bypass", 1), "Bypass", false)); prepareToPlay(48000, 128);
}
CompressorSettings Compressor::settings() const { return {threshold->get(), ratio->get(), attack->get(), release->get(), knee->get(), makeup->get(), mix->get(), bypass->get()}; }
bool Compressor::setSettings(const CompressorSettings& s) {
    if (!std::isfinite(s.threshold) || s.threshold < -60 || s.threshold > 0 || !std::isfinite(s.ratio) || s.ratio < 1 || s.ratio > 20
        || !std::isfinite(s.attackMs) || s.attackMs < 0.1f || s.attackMs > 200 || !std::isfinite(s.releaseMs) || s.releaseMs < 10 || s.releaseMs > 2000
        || !std::isfinite(s.knee) || s.knee < 0 || s.knee > 24 || !std::isfinite(s.makeup) || s.makeup < -12 || s.makeup > 24
        || !std::isfinite(s.mix) || s.mix < 0 || s.mix > 100) return false;
    change(threshold, s.threshold); change(ratio, s.ratio); change(attack, s.attackMs); change(release, s.releaseMs); change(knee, s.knee);
    change(makeup, s.makeup); change(mix, s.mix); change(bypass, s.bypass ? 1.0f : 0.0f); return true;
}
double Compressor::gainReduction(double input, double limit, double amount, double width) {
    const double x = input - limit, slope = 1 / amount - 1;
    if (width <= 0) return x > 0 ? slope * x : 0;
    if (x <= -width * 0.5) return 0;
    if (x >= width * 0.5) return slope * x;
    return slope * std::pow(x + width * 0.5, 2) / (2 * width);
}
double Compressor::transferDb(double input) const {
    const auto s = settings(); if (s.bypass) return input;
    const double compressed = std::pow(10.0, (gainReduction(input, s.threshold, s.ratio, s.knee) + s.makeup) / 20);
    return input + 20 * std::log10(jmax(1e-12, 1 - s.mix * 0.01 + s.mix * 0.01 * compressed));
}
void Compressor::prepareToPlay(double rate, int) {
    sampleRate = std::isfinite(rate) && rate >= 8000 ? rate : 48000;
    for (auto* smoother : {&thresholdSmooth, &ratioSmooth, &kneeSmooth, &makeupSmooth, &wetSmooth}) smoother->reset(sampleRate, 0.005);
    setLatencySamples(0); reset();
}
void Compressor::reset() {
    envelope = 0; reduction = 0; inputMeter = outputMeter = -120; const auto s = settings();
    thresholdSmooth.setCurrentAndTargetValue(s.threshold); ratioSmooth.setCurrentAndTargetValue(s.ratio); kneeSmooth.setCurrentAndTargetValue(s.knee);
    makeupSmooth.setCurrentAndTargetValue(Decibels::decibelsToGain(s.makeup)); wetSmooth.setCurrentAndTargetValue(s.bypass ? 0 : s.mix * 0.01f);
}
void Compressor::processBlock(AudioBuffer<float>& audio, MidiBuffer&) {
    const ScopedNoDenormals noDenormals; const auto s = settings();
    const double attackAlpha = std::exp(-1 / (s.attackMs * 0.001 * sampleRate)), releaseAlpha = std::exp(-1 / (s.releaseMs * 0.001 * sampleRate));
    thresholdSmooth.setTargetValue(s.threshold); ratioSmooth.setTargetValue(s.ratio); kneeSmooth.setTargetValue(s.knee);
    makeupSmooth.setTargetValue(Decibels::decibelsToGain(s.makeup)); wetSmooth.setTargetValue(s.bypass ? 0 : s.mix * 0.01f);
    float peakIn = 0, peakOut = 0, peakReduction = 0;
    for (int i = 0; i < audio.getNumSamples(); ++i) {
        std::array<float, 2> dry {}; double level = 0;
        for (int ch = 0; ch < jmin(2, audio.getNumChannels()); ++ch) { const float v = audio.getSample(ch, i); dry[size_t(ch)] = std::isfinite(v) ? v : 0; level = jmax(level, std::abs(double(dry[size_t(ch)]))); }
        envelope = level + (level > envelope ? attackAlpha : releaseAlpha) * (envelope - level);
        const double db = 20 * std::log10(jmax(1e-6, envelope));
        const double attenuation = gainReduction(db, thresholdSmooth.getNextValue(), ratioSmooth.getNextValue(), kneeSmooth.getNextValue());
        const float gain = float(std::pow(10.0, attenuation / 20)) * makeupSmooth.getNextValue(), wet = wetSmooth.getNextValue();
        for (int ch = 0; ch < jmin(2, audio.getNumChannels()); ++ch) {
            const float result = dry[size_t(ch)] + wet * (dry[size_t(ch)] * gain - dry[size_t(ch)]);
            audio.setSample(ch, i, std::isfinite(result) ? result : 0); peakOut = jmax(peakOut, std::abs(audio.getSample(ch, i)));
        }
        peakIn = jmax(peakIn, float(level)); if (!s.bypass) peakReduction = jmax(peakReduction, float(-attenuation));
    }
    reduction = peakReduction; inputMeter = Decibels::gainToDecibels(peakIn, -120.0f); outputMeter = Decibels::gainToDecibels(peakOut, -120.0f);
}
}
