#include "Engine.h"
#include <cmath>

namespace auralis {
namespace {
bool validCurve(const std::shared_ptr<const PitchPreviewCurve>& curve) {
    if (!curve) return true;
    if (curve->empty() || curve->size() > 200000) return false;
    double previous = -1;
    for (const auto& point : *curve) {
        if (!std::isfinite(point[0]) || point[0] < 0 || point[0] <= previous || !std::isfinite(point[1]) || std::abs(point[1]) > 24.00001) return false;
        previous = point[0];
    }
    return true;
}
}
bool Engine::startPitchPreview(const String& ownerId, const String& trackId, std::shared_ptr<SourceAudio> source,
                              double start, double end, double semitones, float gainDb, double position, std::shared_ptr<const PitchPreviewCurve> curve) {
    if (ownerId.isEmpty() || !source || source->samples.getNumChannels() < 1 || source->samples.getNumSamples() < 2
        || !std::isfinite(source->rate) || source->rate <= 0 || !std::isfinite(start) || !std::isfinite(end)
        || start < 0 || end <= start || end > source->samples.getNumSamples() / source->rate + 1e-7
        || !std::isfinite(semitones) || std::abs(semitones) > 12.00001 || !std::isfinite(gainDb) || gainDb < -60 || gainDb > 60
        || !std::isfinite(position) || !validCurve(curve)) return false;
    std::shared_ptr<SourceAudio> retired;
    std::shared_ptr<const PitchPreviewCurve> retiredCurve;
    const ScopedLock guard(lock);
    if (recording || exporting) return false;
    bool found = false; for (const auto& track : tracks) if (track->id == trackId && !track->midi) found = true;
    if (!found) return false;
    retired.swap(pitchPreview.source);
    retiredCurve.swap(pitchPreview.curve); pitchPreview.curve = std::move(curve);
    pitchPreview.ownerId = ownerId; pitchPreview.trackId = trackId; pitchPreview.source = std::move(source);
    pitchPreview.first = start * pitchPreview.source->rate; pitchPreview.last = end * pitchPreview.source->rate;
    pitchPreview.position = (position >= start && position < end ? position : start) * pitchPreview.source->rate;
    pitchPreview.ratio = pitchPreview.targetRatio = std::pow(2.0, semitones / 12);
    pitchPreview.gain = Decibels::decibelsToGain(gainDb); pitchPreview.envelope = 0; pitchPreview.releasing = false;
    pitchPreviewResumeFade = 0; midiDiscontinuity = true; pitchPreviewActive = true; return true;
}
void Engine::updatePitchPreview(const String& ownerId, double semitones) {
    if (!std::isfinite(semitones) || std::abs(semitones) > 12.00001) return;
    std::shared_ptr<const PitchPreviewCurve> retired;
    const ScopedLock guard(lock);
    if (pitchPreviewActive && pitchPreview.ownerId == ownerId && !pitchPreview.releasing) { retired.swap(pitchPreview.curve); pitchPreview.targetRatio = std::pow(2.0, semitones / 12); }
}
void Engine::updatePitchPreviewCurve(const String& ownerId, std::shared_ptr<const PitchPreviewCurve> curve) {
    if (!curve || !validCurve(curve)) return;
    std::shared_ptr<const PitchPreviewCurve> retired;
    const ScopedLock guard(lock);
    if (pitchPreviewActive && pitchPreview.ownerId == ownerId && !pitchPreview.releasing) { retired.swap(pitchPreview.curve); pitchPreview.curve = std::move(curve); }
}
void Engine::stopPitchPreview(const String& ownerId, bool immediate) {
    const ScopedLock guard(lock);
    if (!pitchPreviewActive || (ownerId.isNotEmpty() && pitchPreview.ownerId != ownerId)) return;
    pitchPreview.releasing = true;
    if (immediate) { pitchPreviewActive = false; pitchPreview.envelope = 0; pitchPreviewResumeFade = 0; }
    midiDiscontinuity = true;
}
bool Engine::renderPitchPreview(float* const* output, int outputs, int n) {
    if (!pitchPreviewActive) return false;
    Track* track = nullptr;
    for (auto& t : tracks) { t->peak = 0; if (t->id == pitchPreview.trackId) track = t.get(); }
    if (recording || exporting || !track || !pitchPreview.source) { pitchPreviewActive = false; return false; }
    const auto& source = *pitchPreview.source; auto& buffer = track->work; buffer.clear();
    const double length = pitchPreview.last - pitchPreview.first;
    const double crossfade = jmin(source.rate * 0.005, length * 0.1), loopLength = length - crossfade;
    const double slew = 1 - std::exp(-1 / (0.004 * rate.load()));
    auto sample = [&](int ch, double position) {
        position = jlimit(0.0, double(source.samples.getNumSamples() - 1), position);
        const int a = int(position), b = jmin(a + 1, source.samples.getNumSamples() - 1); const auto* data = source.samples.getReadPointer(jmin(ch, source.samples.getNumChannels() - 1));
        return data[a] + (data[b] - data[a]) * float(position - a);
    };
    for (int i = 0; i < n; ++i) {
        if (pitchPreview.position >= pitchPreview.last) pitchPreview.position = pitchPreview.first + crossfade + std::fmod(pitchPreview.position - pitchPreview.last, loopLength);
        if (pitchPreview.curve) {
            const auto& curve = *pitchPreview.curve; const double time = pitchPreview.position / source.rate;
            auto next = std::lower_bound(curve.begin(), curve.end(), time, [](const auto& point, double t) { return point[0] < t; });
            double shift = next == curve.end() ? curve.back()[1] : (*next)[1];
            if (next != curve.begin() && next != curve.end()) { const auto& before = *(next - 1); shift = before[1] + (shift - before[1]) * (time - before[0]) / ((*next)[0] - before[0]); }
            pitchPreview.targetRatio = std::pow(2.0, shift / 12);
        }
        for (int ch = 0; ch < 2; ++ch) {
            float value = sample(ch, pitchPreview.position);
            if (crossfade > 0 && pitchPreview.position >= pitchPreview.last - crossfade) {
                const double phase = pitchPreview.position - (pitchPreview.last - crossfade);
                value += (sample(ch, pitchPreview.first + phase) - value) * float(phase / crossfade);
            }
            buffer.setSample(ch, i, value * pitchPreview.gain);
        }
        pitchPreview.ratio += (pitchPreview.targetRatio - pitchPreview.ratio) * slew;
        pitchPreview.position += source.rate / rate * pitchPreview.ratio;
    }
    track->midiBuffer.clear(); AudioBuffer<float> view(buffer.getArrayOfWritePointers(), 2, n);
    for (auto& insert : track->inserts) if (!insert->bypass) insert->instance->processBlock(view, track->midiBuffer);
    const float left = track->gain * (track->pan > 0 ? 1 - track->pan : 1), right = track->gain * (track->pan < 0 ? 1 + track->pan : 1);
    buffer.applyGain(0, 0, n, left); buffer.applyGain(1, 0, n, right); track->peak = buffer.getMagnitude(0, n);
    masterMidi.clear(); for (auto& insert : masterInserts) if (!insert->bypass) insert->instance->processBlock(view, masterMidi);
    const int fadeSamples = jmax(1, int(rate * 0.005)); const float ramp = 1.0f / fadeSamples; float peak = 0;
    for (int i = 0; i < n; ++i) {
        pitchPreview.envelope = jlimit(0.0f, 1.0f, pitchPreview.envelope + (pitchPreview.releasing ? -ramp : ramp));
        for (int ch = 0; ch < outputs; ++ch) if (output[ch]) {
            const float value = ch < 2 ? buffer.getSample(ch, i) * master.load() * pitchPreview.envelope : 0;
            output[ch][i] = std::isfinite(value) ? value : 0; peak = jmax(peak, std::abs(output[ch][i]));
        }
    }
    if (pitchPreview.releasing && pitchPreview.envelope <= 0) { pitchPreviewActive = false; pitchPreviewResumeFade = fadeSamples; midiDiscontinuity = true; }
    outputPeak = peak; return true;
}
}
