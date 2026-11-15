#include "Tuner.h"

namespace auralis {
Tuner::Tuner() : NativeEffect(NativeEffectKind::tuner), Thread("Auralis pitch detector") {
    reference = addNative(new AudioParameterFloat(ParameterID("reference", 1), "A4 reference", 400, 480, 440));
    channel = addNative(new AudioParameterChoice(ParameterID("channel", 1), "Input", StringArray {"Auto", "Left", "Right"}, 0));
    mute = addNative(new AudioParameterBool(ParameterID("mute", 1), "Mute while tuning", false));
}
Tuner::~Tuner() { releaseResources(); }
void Tuner::prepareToPlay(double rate, int) {
    releaseResources(); if (!std::isfinite(rate) || rate < 8000) rate = 48000;
    decimation = jmax(1, int(std::round(rate / 12000))); analysisRate = rate / decimation;
    const auto c = dsp::IIR::ArrayCoefficients<double>::makeLowPass(rate, jmin(3000.0, analysisRate * 0.4), 0.70710678);
    filter = {c[0] / c[3], c[1] / c[3], c[2] / c[3], c[4] / c[3], c[5] / c[3]};
    written = consumed = 0; phase = selectedChannel = 0; filterState = {}; position = available = 0; sinceAnalysis = 0; workerEpoch = 0; stableFrequency = 0;
    audible.reset(rate, 0.005); audible.setCurrentAndTargetValue(muted() ? 0.0f : 1.0f); reset(); startThread(Priority::low);
}
void Tuner::releaseResources() { signalThreadShouldExit(); notify(); stopThread(2000); frequency = 0; confidence = 0; }
void Tuner::reset() { epoch.fetch_add(1, std::memory_order_acq_rel); frequency = 0; confidence = 0; inputLevel = -120; lastAudio = 0; }
TunerReading Tuner::reading() const {
    const auto currentEpoch = epoch.load(std::memory_order_acquire);
    if (Time::getMillisecondCounterHiRes() - lastAudio.load() > 350) return {};
    const auto hz = frequency.load(); const auto quality = confidence.load();
    return {quality >= 0.8f && publishedEpoch.load(std::memory_order_acquire) == currentEpoch ? hz : 0, quality, inputLevel.load()};
}
void Tuner::processBlock(AudioBuffer<float>& audio, MidiBuffer&) {
    const ScopedNoDenormals noDenormals; if (audio.getNumChannels() == 0 || audio.getNumSamples() == 0) return;
    const auto currentEpoch = epoch.load(std::memory_order_acquire); const int mode = inputChannel();
    const int right = jmin(1, audio.getNumChannels() - 1);
    if (mode == 0) {
        const float leftPeak = audio.getMagnitude(0, 0, audio.getNumSamples()), rightPeak = audio.getMagnitude(right, 0, audio.getNumSamples());
        if (leftPeak > rightPeak * 1.5f) selectedChannel = 0; else if (rightPeak > leftPeak * 1.5f) selectedChannel = right;
    } else selectedChannel = mode == 1 ? 0 : right;
    auto write = written.load(std::memory_order_relaxed); const auto read = consumed.load(std::memory_order_acquire); float peak = 0;
    audible.setTargetValue(muted() ? 0.0f : 1.0f);
    for (int i = 0; i < audio.getNumSamples(); ++i) {
        const float raw = audio.getSample(selectedChannel, i); double value = std::isfinite(raw) ? raw : 0; peak = jmax(peak, float(std::abs(value)));
        for (auto& state : filterState) {
            const double out = filter[0] * value + state[0]; state[0] = filter[1] * value - filter[3] * out + state[1]; state[1] = filter[2] * value - filter[4] * out; value = out;
        }
        if (++phase >= decimation) { phase = 0; if (write - read < capacity) { queue[size_t(write % capacity)] = {float(value), currentEpoch}; ++write; } }
        const float gain = audible.getNextValue();
        if (gain != 1) for (int ch = 0; ch < audio.getNumChannels(); ++ch) audio.setSample(ch, i, audio.getSample(ch, i) * gain);
    }
    written.store(write, std::memory_order_release); inputLevel = Decibels::gainToDecibels(peak, -120.0f); lastAudio = Time::getMillisecondCounterHiRes();
}
void Tuner::run() {
    auto read = consumed.load(std::memory_order_relaxed);
    while (!threadShouldExit()) {
        const auto write = written.load(std::memory_order_acquire);
        while (read < write && !threadShouldExit()) {
            const auto item = queue[size_t(read++ % capacity)];
            if (item.epoch != epoch.load(std::memory_order_acquire)) continue;
            if (workerEpoch != item.epoch) { workerEpoch = item.epoch; position = available = 0; sinceAnalysis = 0; stableFrequency = 0; }
            history[position] = item.value; position = (position + 1) % windowSize; available = jmin(windowSize, available + 1); ++sinceAnalysis;
            if (available == windowSize && sinceAnalysis >= int(analysisRate * 0.04)) { sinceAnalysis = 0; analyse(workerEpoch); }
        }
        consumed.store(read, std::memory_order_release); wait(8);
    }
}
void Tuner::analyse(uint32_t currentEpoch) {
    double energy = 0, mean = 0;
    for (size_t i = 0; i < windowSize; ++i) { ordered[i] = history[(position + i) % windowSize]; mean += ordered[i]; }
    mean /= windowSize; for (auto& v : ordered) { v -= float(mean); energy += v * v; }
    double result = 0; float quality = 0;
    if (energy / windowSize > 2.5e-7) {
        const int minimum = jmax(2, int(analysisRate / 1500)), maximum = jmin(int(windowSize / 2) - 2, int(std::ceil(analysisRate / 25)));
        // YIN's fixed comparison window avoids the long-lag bias of a shrinking window.
        const int count = int(windowSize) - maximum; double cumulative = 0; difference[0] = 1;
        for (int lag = 1; lag <= maximum; ++lag) {
            double sum = 0; for (int i = 0; i < count; ++i) { const double delta = ordered[size_t(i)] - ordered[size_t(i + lag)]; sum += delta * delta; }
            cumulative += sum; difference[size_t(lag)] = cumulative > 1e-20 ? float(sum * lag / cumulative) : 1;
        }
        int chosen = 0;
        for (int lag = minimum; lag < maximum; ++lag) if (difference[size_t(lag)] < 0.14f) { while (lag + 1 <= maximum && difference[size_t(lag + 1)] < difference[size_t(lag)]) ++lag; chosen = lag; break; }
        if (chosen > 0 && chosen < maximum) {
            const double left = difference[size_t(chosen - 1)], centre = difference[size_t(chosen)], right = difference[size_t(chosen + 1)], denominator = left - 2 * centre + right;
            const double offset = std::abs(denominator) > 1e-12 ? jlimit(-0.5, 0.5, 0.5 * (left - right) / denominator) : 0;
            result = analysisRate / (chosen + offset); quality = 1 - float(centre);
            if (result < 25 || result > 1500 || quality < 0.8f) result = 0;
        }
    }
    if (result > 0) {
        if (stableFrequency <= 0 || std::abs(12 * std::log2(result / stableFrequency)) > 0.7) stableFrequency = result;
        else stableFrequency = std::sqrt(stableFrequency * result);
    } else stableFrequency = 0;
    if (epoch.load(std::memory_order_acquire) == currentEpoch) { confidence = quality; frequency = stableFrequency; publishedEpoch.store(currentEpoch, std::memory_order_release); }
}
}
