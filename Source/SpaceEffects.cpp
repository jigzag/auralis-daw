#include "SpaceEffects.h"

namespace auralis {
namespace {
bool inRange(float value, float low, float high) { return std::isfinite(value) && value >= low && value <= high; }
float clean(float value) { return std::isfinite(value) ? value : 0; }
float lowPass(double hz, double rate) { return float(std::exp(-MathConstants<double>::twoPi * jmin(hz, rate * 0.45) / rate)); }
float tap(const AudioBuffer<float>& ring, int channel, int position, double delay) {
    double at = position - delay; if (at < 0) at += ring.getNumSamples();
    const int first = int(at), second = (first + 1) % ring.getNumSamples(); const float fraction = float(at - first);
    return ring.getSample(channel, first) * (1 - fraction) + ring.getSample(channel, second) * fraction;
}
}
ReverbEffect::ReverbEffect() : NativeEffect(NativeEffectKind::reverb) {
    auto add = [this](const char* id, const char* name, float low, float high, float value) { return addNative(new AudioParameterFloat(ParameterID(id, 1), name, low, high, value)); };
    size = add("size", "Room size", 0, 100, 55); damping = add("damping", "Damping", 0, 100, 50); width = add("width", "Stereo width", 0, 100, 100);
    preDelay = add("predelay", "Pre-delay", 0, 250, 20); lowCut = add("lowcut", "Low cut", 20, 1000, 100); highCut = add("highcut", "High cut", 1000, 20000, 8000);
    mix = add("mix", "Mix", 0, 100, 20); output = add("output", "Output", -24, 12, 0); bypass = addNative(new AudioParameterBool(ParameterID("bypass", 1), "Bypass", false));
}
ReverbSettings ReverbEffect::settings() const { return {size->get(), damping->get(), width->get(), preDelay->get(), lowCut->get(), highCut->get(), mix->get(), output->get(), bypass->get()}; }
bool ReverbEffect::setSettings(const ReverbSettings& s) {
    if (!inRange(s.size, 0, 100) || !inRange(s.damping, 0, 100) || !inRange(s.width, 0, 100) || !inRange(s.preDelayMs, 0, 250) || !inRange(s.lowCut, 20, 1000) || !inRange(s.highCut, 1000, 20000) || !inRange(s.mix, 0, 100) || !inRange(s.output, -24, 12)) return false;
    change(size, s.size); change(damping, s.damping); change(width, s.width); change(preDelay, s.preDelayMs); change(lowCut, s.lowCut); change(highCut, s.highCut); change(mix, s.mix); change(output, s.output); change(bypass, s.bypass ? 1.0f : 0.0f); return true;
}
void ReverbEffect::preset(int index) {
    auto s = settings();
    if (index == 1) { s = {25, 65, 75, 8, 120, 6500, 18, 0, s.bypass}; }
    else if (index == 2) { s = {72, 45, 100, 30, 100, 9000, 25, 0, s.bypass}; }
    else if (index == 3) { s = {90, 70, 100, 65, 180, 5500, 35, 0, s.bypass}; }
    else return;
    setSettings(s);
}
void ReverbEffect::configure() { const auto s = settings(); juce::Reverb::Parameters p; p.roomSize = s.size * 0.01f; p.damping = s.damping * 0.01f; p.width = s.width * 0.01f; p.wetLevel = 1.0f / 3; p.dryLevel = 0; p.freezeMode = 0; reverb.setParameters(p); }
void ReverbEffect::prepareToPlay(double rate, int) {
    sampleRate = std::isfinite(rate) && rate >= 8000 && rate <= 768000 ? rate : 48000;
    preBuffer.setSize(2, int(std::ceil(sampleRate * 0.25)) + 4); wetBuffer.setSize(2, 8192);
    for (auto* smoother : {&preSmooth, &mixSmooth, &outputSmooth, &lowSmooth, &highSmooth}) smoother->reset(sampleRate, 0.01);
    setLatencySamples(0); reset();
}
void ReverbEffect::reset() {
    // Set targets before resetting JUCE's smoothers so its default dry gain cannot
    // leak through a fresh wet-only instance. Same-rate resets reuse all buffers.
    configure(); reverb.setSampleRate(sampleRate); preBuffer.clear(); writePosition = 0; lowState = highState = {}; const auto s = settings();
    preSmooth.setCurrentAndTargetValue(float(s.preDelayMs * sampleRate * 0.001)); mixSmooth.setCurrentAndTargetValue(s.bypass ? 0 : s.mix * 0.01f);
    outputSmooth.setCurrentAndTargetValue(s.bypass ? 1 : Decibels::decibelsToGain(s.output)); lowSmooth.setCurrentAndTargetValue(lowPass(s.lowCut, sampleRate)); highSmooth.setCurrentAndTargetValue(lowPass(s.highCut, sampleRate));
}
double ReverbEffect::getTailLengthSeconds() const { const auto s = settings(); if (s.bypass || s.mix == 0) return 0; return s.preDelayMs * 0.001 + 0.05 * std::log(0.001) / std::log(0.7 + s.size * 0.0028); }
void ReverbEffect::processBlock(AudioBuffer<float>& audio, MidiBuffer&) {
    const ScopedNoDenormals noDenormals; if (audio.getNumChannels() < 2 || preBuffer.getNumSamples() == 0) return; const auto s = settings(); configure();
    preSmooth.setTargetValue(float(s.preDelayMs * sampleRate * 0.001)); mixSmooth.setTargetValue(s.bypass ? 0 : s.mix * 0.01f); outputSmooth.setTargetValue(s.bypass ? 1 : Decibels::decibelsToGain(s.output));
    lowSmooth.setTargetValue(lowPass(s.lowCut, sampleRate)); highSmooth.setTargetValue(lowPass(s.highCut, sampleRate));
    for (int start = 0; start < audio.getNumSamples(); start += wetBuffer.getNumSamples()) {
        const int count = jmin(wetBuffer.getNumSamples(), audio.getNumSamples() - start);
        for (int i = 0; i < count; ++i) {
            const float delay = preSmooth.getNextValue(); for (int ch = 0; ch < 2; ++ch) preBuffer.setSample(ch, writePosition, clean(audio.getSample(ch, start + i)));
            for (int ch = 0; ch < 2; ++ch) wetBuffer.setSample(ch, i, tap(preBuffer, ch, writePosition, delay)); writePosition = (writePosition + 1) % preBuffer.getNumSamples();
        }
        reverb.processStereo(wetBuffer.getWritePointer(0), wetBuffer.getWritePointer(1), count);
        for (int i = 0; i < count; ++i) {
            const float low = lowSmooth.getNextValue(), high = highSmooth.getNextValue(), amount = mixSmooth.getNextValue(), gain = outputSmooth.getNextValue();
            for (int ch = 0; ch < 2; ++ch) { const float wet = clean(wetBuffer.getSample(ch, i)); lowState[size_t(ch)] = clean((1 - low) * wet + low * lowState[size_t(ch)]); const float cut = wet - lowState[size_t(ch)]; highState[size_t(ch)] = clean((1 - high) * cut + high * highState[size_t(ch)]);
                const float dry = clean(audio.getSample(ch, start + i)); audio.setSample(ch, start + i, clean((dry + amount * (highState[size_t(ch)] - dry)) * gain)); }
        }
    }
}

StringArray DelayEffect::divisions() { return {"1/1", "1/2", "1/4", "1/8", "1/16", "1/8 dotted", "1/8 triplet", "1/4 dotted"}; }
DelayEffect::DelayEffect() : NativeEffect(NativeEffectKind::delay) {
    auto add = [this](const char* id, const char* name, float low, float high, float value) { return addNative(new AudioParameterFloat(ParameterID(id, 1), name, low, high, value)); };
    time = add("time", "Time", 1, 4000, 350); feedback = add("feedback", "Feedback", 0, 90, 35); highCut = add("highcut", "Repeat high cut", 200, 20000, 6000); mix = add("mix", "Mix", 0, 100, 25); output = add("output", "Output", -24, 12, 0);
    division = addNative(new AudioParameterChoice(ParameterID("division", 1), "Note division", divisions(), 3));
    sync = addNative(new AudioParameterBool(ParameterID("sync", 1), "Tempo sync", false)); pingPong = addNative(new AudioParameterBool(ParameterID("pingpong", 1), "Ping pong", false)); bypass = addNative(new AudioParameterBool(ParameterID("bypass", 1), "Bypass", false));
}
DelaySettings DelayEffect::settings() const { return {time->get(), feedback->get(), highCut->get(), mix->get(), output->get(), division->getIndex(), sync->get(), pingPong->get(), bypass->get()}; }
bool DelayEffect::setSettings(const DelaySettings& s) {
    if (!inRange(s.timeMs, 1, 4000) || !inRange(s.feedback, 0, 90) || !inRange(s.highCut, 200, 20000) || !inRange(s.mix, 0, 100) || !inRange(s.output, -24, 12) || s.division < 0 || s.division >= 8) return false;
    change(time, s.timeMs); change(feedback, s.feedback); change(highCut, s.highCut); change(mix, s.mix); change(output, s.output); change(division, float(s.division)); change(sync, s.sync ? 1.0f : 0.0f); change(pingPong, s.pingPong ? 1.0f : 0.0f); change(bypass, s.bypass ? 1.0f : 0.0f); return true;
}
void DelayEffect::preset(int index) {
    auto s = settings();
    if (index == 1) s = {95, 12, 7000, 22, 0, 3, false, false, s.bypass};
    else if (index == 2) s = {350, 35, 6000, 25, 0, 2, true, false, s.bypass};
    else if (index == 3) s = {350, 45, 5000, 30, 0, 5, true, true, s.bypass};
    else return;
    setSettings(s);
}
double DelayEffect::delayMs(double bpm) const {
    static constexpr double beats[] {4, 2, 1, 0.5, 0.25, 0.75, 1.0 / 3, 1.5}; const auto s = settings();
    return s.sync ? 60000 * beats[s.division] / (std::isfinite(bpm) ? jlimit(20.0, 300.0, bpm) : 120) : s.timeMs;
}
double DelayEffect::getTailLengthSeconds() const { return tailSeconds(tempo.load()); }
double DelayEffect::tailSeconds(double bpm) const {
    const auto s = settings(); if (s.bypass || s.mix == 0) return 0;
    return delayMs(bpm) * 0.001 * (s.feedback > 0 ? 1 + std::ceil(std::log(0.001) / std::log(s.feedback * 0.01)) : 1);
}
void DelayEffect::prepareToPlay(double rate, int) {
    sampleRate = std::isfinite(rate) && rate >= 8000 && rate <= 768000 ? rate : 48000; delayBuffer.setSize(2, int(std::ceil(sampleRate * 12)) + 4);
    for (auto* smoother : {&feedbackSmooth, &toneSmooth, &mixSmooth, &outputSmooth, &pingSmooth}) smoother->reset(sampleRate, 0.005); timeSmooth.reset(sampleRate, 0.02);
    setLatencySamples(0); reset();
}
void DelayEffect::reset() {
    delayBuffer.clear(); writePosition = 0; toneState = {}; fresh = true; const auto s = settings();
    timeSmooth.setCurrentAndTargetValue(float(effectiveTimeMs() * sampleRate * 0.001)); feedbackSmooth.setCurrentAndTargetValue(s.feedback * 0.01f); toneSmooth.setCurrentAndTargetValue(lowPass(s.highCut, sampleRate));
    mixSmooth.setCurrentAndTargetValue(s.bypass ? 0 : s.mix * 0.01f); outputSmooth.setCurrentAndTargetValue(s.bypass ? 1 : Decibels::decibelsToGain(s.output)); pingSmooth.setCurrentAndTargetValue(s.pingPong ? 1 : 0);
}
void DelayEffect::processBlock(AudioBuffer<float>& audio, MidiBuffer&) {
    const ScopedNoDenormals noDenormals; if (audio.getNumChannels() < 2 || delayBuffer.getNumSamples() == 0) return;
    if (auto* head = getPlayHead()) if (const auto position = head->getPosition()) if (auto bpm = position->getBpm()) if (std::isfinite(*bpm) && *bpm > 0) tempo = jlimit(20.0, 300.0, *bpm);
    const auto s = settings(); const float targetTime = float(effectiveTimeMs() * sampleRate * 0.001);
    // Immediately initialise a fresh ring to the current tempo; later edits slew smoothly.
    if (fresh) { timeSmooth.setCurrentAndTargetValue(targetTime); fresh = false; } else timeSmooth.setTargetValue(targetTime);
    feedbackSmooth.setTargetValue(s.feedback * 0.01f); toneSmooth.setTargetValue(lowPass(s.highCut, sampleRate)); mixSmooth.setTargetValue(s.bypass ? 0 : s.mix * 0.01f); outputSmooth.setTargetValue(s.bypass ? 1 : Decibels::decibelsToGain(s.output)); pingSmooth.setTargetValue(s.pingPong ? 1 : 0);
    for (int i = 0; i < audio.getNumSamples(); ++i) {
        const float delay = jlimit(1.0f, float(delayBuffer.getNumSamples() - 2), timeSmooth.getNextValue()), feedbackGain = feedbackSmooth.getNextValue(), tone = toneSmooth.getNextValue(), amount = mixSmooth.getNextValue(), gain = outputSmooth.getNextValue(), ping = pingSmooth.getNextValue();
        std::array<float, 2> dry {clean(audio.getSample(0, i)), clean(audio.getSample(1, i))};
        for (int ch = 0; ch < 2; ++ch) toneState[size_t(ch)] = clean((1 - tone) * tap(delayBuffer, ch, writePosition, delay) + tone * toneState[size_t(ch)]);
        const float mono = (dry[0] + dry[1]) * 0.5f;
        delayBuffer.setSample(0, writePosition, clean(dry[0] + ping * (mono - dry[0]) + feedbackGain * (toneState[0] + ping * (toneState[1] - toneState[0]))));
        delayBuffer.setSample(1, writePosition, clean(dry[1] * (1 - ping) + feedbackGain * (toneState[1] + ping * (toneState[0] - toneState[1]))));
        for (int ch = 0; ch < 2; ++ch) audio.setSample(ch, i, clean((dry[size_t(ch)] + amount * (toneState[size_t(ch)] - dry[size_t(ch)])) * gain)); writePosition = (writePosition + 1) % delayBuffer.getNumSamples();
    }
}
}
