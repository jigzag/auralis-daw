#include "PitchCorrection.h"
#include <cmath>
#include <stdexcept>

namespace auralis {
bool PitchCorrectionSettings::valid() const {
    if (notes.size() > 20000) return false;
    double previousEnd = 0;
    size_t curvePoints = 0;
    for (const auto& note : notes) {
        if (!std::isfinite(note.start) || !std::isfinite(note.end) || !std::isfinite(note.original) || !std::isfinite(note.target)
            || note.start < previousEnd - 1e-7 || note.end <= note.start || note.original < 24 || note.original > 96
            || note.target < 24 || note.target > 96 || std::abs(note.target - note.original) > 12.00001) return false;
        const auto& scoop = note.scoop;
        if (!std::isfinite(scoop.start) || scoop.start < 0 || !std::isfinite(scoop.depthCents) || scoop.depthCents < 0 || scoop.depthCents > 1200
            || !std::isfinite(scoop.durationMs) || scoop.durationMs < 20 || scoop.durationMs > 2000
            || !std::isfinite(scoop.curve) || scoop.curve < 0.25 || scoop.curve > 4
            || (scoop.enabled && (scoop.start >= note.end || note.target - scoop.depthCents / 100 < 24))) return false;
        previousEnd = note.end;
        curvePoints += note.curve.size(); if (note.curve.size() > 200000 || curvePoints > 1000000) return false;
        double previousTime = -1;
        for (const auto& point : note.curve) {
            if (!std::isfinite(point.seconds) || point.seconds < 0 || point.seconds <= previousTime || !std::isfinite(point.note) || point.note < 24 || point.note > 96) return false;
            previousTime = point.seconds;
        }
    }
    return root >= 0 && root < 12 && scale >= 0 && scale <= 2 && (targetNote == -1 || (targetNote >= 24 && targetNote <= 96))
        && std::isfinite(referenceHz) && referenceHz >= 400 && referenceHz <= 480
        && std::isfinite(amount) && amount >= 0 && amount <= 1 && std::isfinite(speedMs) && speedMs >= 0 && speedMs <= 1000
        && std::isfinite(maxCents) && maxCents >= 10 && maxCents <= 1200;
}
template<class Edit> static double editShift(double measuredNote, double time, const Edit& edit) {
    if (!edit.curve.empty()) {
        const auto& curve = edit.curve;
        const auto next = std::lower_bound(curve.begin(), curve.end(), time, [](const auto& point, double t) { return point.seconds < t; });
        double target = next == curve.end() ? curve.back().note : next->note;
        if (next != curve.begin() && next != curve.end()) { const auto& before = *(next - 1); target = before.note + (target - before.note) * (time - before.seconds) / (next->seconds - before.seconds); }
        const double shift = jlimit(-24.0, 24.0, target - measuredNote); return std::abs(shift) < 1e-7 ? 0 : shift;
    }
    const double base = edit.target - edit.original;
    const auto& scoop = edit.scoop;
    const double age = time - scoop.start, duration = scoop.durationMs * 0.001;
    if (!scoop.enabled || age < 0 || age >= duration) return base;
    const double target = edit.target - scoop.depthCents / 100 * std::pow(1 - age / duration, scoop.curve);
    // Rejoin the recorded vibrato over the last 20 ms rather than creating a pitch step.
    const double edge = jlimit(0.0, 1.0, (duration - age) / jmin(0.02, duration * 0.25));
    const double mix = edge * edge * (3 - 2 * edge);
    return jlimit(-24.0, 24.0, base + (target - measuredNote - base) * mix);
}
double pitchEditShift(double measuredNote, double time, const PitchNoteEdit& edit) { return editShift(measuredNote, time, edit); }
double pitchEditShift(double measuredNote, double time, const PitchSegment& edit) { return editShift(measuredNote, time, edit); }
namespace {
std::shared_ptr<SourceAudio> baseSource(const Clip& clip) { return clip.pitchOriginal ? clip.pitchOriginal : clip.source; }
std::pair<int, int> sampleRange(const Clip& clip, const SourceAudio& audio) {
    if (clip.midi || !std::isfinite(clip.offset) || !std::isfinite(clip.length) || clip.offset < 0 || clip.length < 0.12
        || !std::isfinite(audio.rate) || audio.rate < 8000 || audio.rate > 192000 || audio.samples.getNumChannels() < 1
        || clip.offset + clip.length > audio.samples.getNumSamples() / audio.rate + 0.001)
        throw std::runtime_error("Pitch correction requires an audio part at least 120 ms long.");
    const int first = jlimit(0, audio.samples.getNumSamples(), int(std::llround(clip.offset * audio.rate)));
    const int last = jlimit(first, audio.samples.getNumSamples(), int(std::llround((clip.offset + clip.length) * audio.rate)));
    return {first, last};
}
double nearestNote(double note, const PitchCorrectionSettings& settings) {
    if (settings.targetNote >= 0) return settings.targetNote;
    if (settings.scale == 0) return std::round(note);
    constexpr bool major[12] {true, false, true, false, true, true, false, true, false, true, false, true};
    constexpr bool minor[12] {true, false, true, true, false, true, false, true, true, false, true, false};
    double best = std::round(note), distance = 100;
    for (int n = int(std::floor(note)) - 2; n <= int(std::ceil(note)) + 2; ++n) {
        const int degree = ((n - settings.root) % 12 + 12) % 12;
        if ((settings.scale == 1 ? major[degree] : minor[degree]) && std::abs(note - n) < distance) { best = n; distance = std::abs(note - n); }
    }
    return best;
}
}

std::vector<PitchFrame> analysePitch(const Clip& clip, const PitchCorrectionSettings& settings) {
    const ScopedNoDenormals noDenormals;
    const auto source = baseSource(clip);
    if (!source || !settings.valid()) throw std::runtime_error("Invalid pitch correction settings or audio source.");
    const auto [first, last] = sampleRange(clip, *source);
    const auto& audio = source->samples; int channel = 0;
    for (int ch = 1; ch < audio.getNumChannels(); ++ch)
        if (audio.getRMSLevel(ch, first, last - first) > audio.getRMSLevel(channel, first, last - first)) channel = ch;
    const int decimation = jmax(1, int(std::round(source->rate / 12000)));
    const double rate = source->rate / decimation;
    dsp::IIR::Filter<float> low1, low2;
    low1.coefficients = low2.coefficients = dsp::IIR::Coefficients<float>::makeLowPass(source->rate, jmin(3000.0, rate * 0.4));
    std::vector<float> mono; mono.reserve(size_t((last - first) / decimation + 1));
    const auto* input = audio.getReadPointer(channel);
    for (int i = first; i < last; ++i) {
        if (!std::isfinite(input[i])) throw std::runtime_error("Audio contains invalid samples.");
        const float value = low2.processSample(low1.processSample(input[i]));
        if ((i - first) % decimation == 0) mono.push_back(value);
    }
    constexpr int window = 1024, fftSize = 2048;
    const int hop = jmax(1, int(std::round(rate * 0.01))), minLag = jmax(2, int(rate / 1200)), maxLag = jmin(400, int(std::ceil(rate / 55)));
    dsp::FFT fft(11); std::vector<float> transform(fftSize * 2), difference(size_t(maxLag + 2));
    std::array<double, window + 1> energy {};
    std::vector<PitchFrame> frames; frames.reserve(mono.size() / hop + 1);
    for (int centre = 0; centre < int(mono.size()); centre += hop) {
        std::fill(transform.begin(), transform.end(), 0); double mean = 0;
        for (int i = 0; i < window; ++i) {
            const int index = centre + i - window / 2;
            if (isPositiveAndBelow(index, int(mono.size()))) transform[size_t(i)] = mono[size_t(index)];
            mean += transform[size_t(i)];
        }
        mean /= window; energy[0] = 0;
        for (int i = 0; i < window; ++i) { transform[size_t(i)] -= float(mean); energy[size_t(i + 1)] = energy[size_t(i)] + double(transform[size_t(i)]) * transform[size_t(i)]; }
        PitchFrame frame; frame.seconds = centre / rate;
        double shortEnergy = 0; int shortCount = 0;
        for (int i = jmax(0, centre - hop / 2); i < jmin(int(mono.size()), centre + hop / 2); ++i) { shortEnergy += double(mono[size_t(i)]) * mono[size_t(i)]; ++shortCount; }
        frame.level = std::sqrt(shortEnergy / jmax(1, shortCount));
        if (energy.back() / window < 1e-6) { frames.push_back(frame); continue; }
        fft.performRealOnlyForwardTransform(transform.data(), true);
        for (int i = 0; i <= fftSize / 2; ++i) {
            const auto re = transform[size_t(2 * i)], im = transform[size_t(2 * i + 1)];
            transform[size_t(2 * i)] = re * re + im * im; transform[size_t(2 * i + 1)] = 0;
        }
        fft.performRealOnlyInverseTransform(transform.data());
        double accumulated = 0; difference[0] = 1;
        for (int lag = 1; lag <= maxLag + 1; ++lag) {
            const double value = jmax(0.0, energy[size_t(window - lag)] + energy.back() - energy[size_t(lag)] - 2 * transform[size_t(lag)]);
            accumulated += value; difference[size_t(lag)] = accumulated > 1e-12 ? float(value * lag / accumulated) : 1.0f;
        }
        int lag = minLag;
        for (; lag <= maxLag; ++lag) if (difference[size_t(lag)] < 0.18f) {
            while (lag < maxLag && difference[size_t(lag + 1)] < difference[size_t(lag)]) ++lag;
            break;
        }
        if (lag <= maxLag) {
            const double a = difference[size_t(lag - 1)], b = difference[size_t(lag)], c = difference[size_t(lag + 1)], denominator = a - 2 * b + c;
            const double fraction = std::abs(denominator) > 1e-12 ? jlimit(-0.5, 0.5, 0.5 * (a - c) / denominator) : 0;
            frame.note = 69 + 12 * std::log2(rate / (lag + fraction) / settings.referenceHz);
            frame.voiced = std::isfinite(frame.note) && frame.note >= 24 && frame.note <= 96;
        }
        frames.push_back(frame);
    }
    const auto measured = frames; double shift = 0; bool previousOverride = false; size_t editIndex = 0;
    const double alpha = settings.speedMs == 0 ? 1 : 1 - std::exp(-hop / rate / (settings.speedMs * 0.001));
    for (int i = 0; i < int(frames.size()); ++i) {
        auto& frame = frames[size_t(i)]; double desired = 0; bool overridden = false;
        const double sourceTime = clip.offset + frame.seconds;
        while (editIndex < settings.notes.size() && settings.notes[editIndex].end <= sourceTime) ++editIndex;
        if (frame.voiced) {
            std::array<double, 5> neighbours {}; int count = 0;
            for (int k = jmax(0, i - 2); k <= jmin(int(measured.size()) - 1, i + 2); ++k)
                if (measured[size_t(k)].voiced) neighbours[size_t(count++)] = measured[size_t(k)].note;
            if (count < 3) frame.voiced = false;
            else {
                std::sort(neighbours.begin(), neighbours.begin() + count); frame.note = neighbours[size_t(count / 2)];
                if (editIndex < settings.notes.size() && settings.notes[editIndex].start <= sourceTime && sourceTime < settings.notes[editIndex].end) {
                    const auto& edit = settings.notes[editIndex]; desired = pitchEditShift(frame.note, sourceTime, edit); overridden = true;
                } else if (!settings.manual) {
                    const double delta = nearestNote(frame.note, settings) - frame.note;
                    if (std::abs(delta) * 100 <= settings.maxCents) desired = delta * settings.amount;
                }
            }
        }
        if (overridden || previousOverride || settings.manual) shift = desired; else shift += alpha * (desired - shift);
        frame.shift = shift; previousOverride = overridden;
    }
    return frames;
}

PitchCorrectionResult renderPitchCorrection(const Clip& clip, const PitchCorrectionSettings& settings, const File& output) {
    const ScopedNoDenormals noDenormals;
    auto frames = analysePitch(clip, settings); PitchCorrectionResult result;
    for (const auto& frame : frames) { if (frame.voiced) ++result.voicedFrames; if (std::abs(frame.shift) >= 0.005) ++result.correctedFrames; }
    if (result.voicedFrames < 3) throw std::runtime_error("No stable single-note pitch was detected. Use a clean solo voice or melody.");
    const auto original = baseSource(clip);
    if (result.correctedFrames == 0 || settings.amount == 0) { result.source = original; return result; }
    if (output == original->file || output == clip.source->file) throw std::runtime_error("Corrected audio must use a separate file.");
    const auto [first, last] = sampleRange(clip, *original); const int count = last - first, channels = original->samples.getNumChannels();
    auto corrected = std::make_shared<SourceAudio>(); corrected->rate = original->rate; corrected->samples.makeCopyOf(original->samples);
    for (int ch = 0; ch < channels; ++ch) corrected->samples.clear(ch, first, count);
    std::vector<float> weights(size_t(count), 0), wetMask(size_t(count), 0);
    int referenceChannel = 0;
    for (int ch = 1; ch < channels; ++ch) if (original->samples.getRMSLevel(ch, first, count) > original->samples.getRMSLevel(referenceChannel, first, count)) referenceChannel = ch;
    const auto* reference = original->samples.getReadPointer(referenceChannel) + first;
    const double frameTime = frames.size() > 1 ? frames[1].seconds : 0.01;
    auto editBoundary = [&](double left, double right) {
        left += clip.offset; right += clip.offset;
        for (const auto& edit : settings.notes) {
            if (edit.start > right) break;
            if ((edit.start > left && edit.start <= right) || (edit.end > left && edit.end <= right)) return true;
        }
        return false;
    };
    auto frameAt = [&](double sample) {
        const double position = jlimit(0.0, double(frames.size() - 1), sample / original->rate / frameTime);
        const int index = int(position), next = jmin(index + 1, int(frames.size()) - 1); auto frame = frames[size_t(index)];
        if (frame.voiced && frames[size_t(next)].voiced) {
            frame.note += (frames[size_t(next)].note - frame.note) * (position - index);
            frame.shift += (frames[size_t(next)].shift - frame.shift) * (position - index);
        } else if (!frame.voiced && frames[size_t(next)].voiced) frame = frames[size_t(next)];
        return frame;
    };
    auto periodAt = [&](double sample) {
        return original->rate / (settings.referenceHz * std::pow(2.0, (frameAt(sample).note - 69) / 12));
    };
    auto peakAt = [&](int left, int right) {
        left = jlimit(1, count - 2, left); right = jlimit(left + 1, count - 1, right); int best = left;
        for (int i = left + 1; i < right; ++i) if (reference[i] > reference[best]) best = i;
        const double a = reference[best - 1], b = reference[best], c = reference[best + 1], denominator = a - 2 * b + c;
        return best + (std::abs(denominator) > 1e-12 ? jlimit(-0.5, 0.5, 0.5 * (a - c) / denominator) : 0);
    };
    bool rendered = false;
    // Time-domain pitch-synchronous overlap-add. Analysis marks follow complete
    // input periods; synthesis marks follow the requested pitch, at unchanged time.
    for (int beginFrame = 0; beginFrame < int(frames.size());) {
        if (!frames[size_t(beginFrame)].voiced) { ++beginFrame; continue; }
        int endFrame = beginFrame + 1;
        while (endFrame < int(frames.size()) && frames[size_t(endFrame)].voiced
               && std::abs(frames[size_t(endFrame)].note - frames[size_t(endFrame - 1)].note) < 1.5
               && !editBoundary(frames[size_t(endFrame - 1)].seconds, frames[size_t(endFrame)].seconds)) ++endFrame;
        const int runStart = jlimit(0, count, int(std::round((frames[size_t(beginFrame)].seconds - frameTime * 0.5) * original->rate)));
        const int runEnd = jlimit(runStart, count, int(std::round((frames[size_t(endFrame - 1)].seconds + frameTime * 0.5) * original->rate)));
        bool needsCorrection = false;
        for (int i = beginFrame; i < endFrame; ++i) needsCorrection |= std::abs(frames[size_t(i)].shift) >= 0.005;
        beginFrame = endFrame;
        if (!needsCorrection || runEnd <= runStart) continue;
        std::vector<double> marks;
        const double firstPeriod = periodAt(runStart);
        // A very short edited tail still needs complete input periods for PSOLA.
        // Borrow analysis context while keeping synthesis and its wet mask local.
        const int context = runEnd - runStart < original->rate * 0.025 ? int(std::ceil(firstPeriod * 2)) : 0;
        const int analysisStart = jmax(0, runStart - context), analysisEnd = jmin(count, runEnd + context);
        marks.push_back(peakAt(analysisStart, jmin(analysisEnd, analysisStart + int(firstPeriod * 1.5))));
        while (marks.back() + periodAt(marks.back()) < analysisEnd) {
            const double period = periodAt(marks.back());
            const double mark = peakAt(int(marks.back() + period * 0.85), jmin(analysisEnd, int(marks.back() + period * 1.15)));
            if (mark <= marks.back() + period * 0.5) break;
            marks.push_back(mark);
        }
        if (marks.size() < 2) continue;
        rendered = true; size_t nearest = 0; double outputMark = marks.front();
        const int blend = jmax(1, jmin((runEnd - runStart) / 2, int(original->rate * 0.01)));
        for (int i = runStart; i < runEnd; ++i) wetMask[size_t(i)] = float(jmin(1.0, double(i - runStart) / blend, double(runEnd - 1 - i) / blend));
        while (outputMark < runEnd) {
            while (nearest + 1 < marks.size() && std::abs(marks[nearest + 1] - outputMark) < std::abs(marks[nearest] - outputMark)) ++nearest;
            const auto frame = frameAt(outputMark);
            const double factor = std::pow(2.0, frame.shift / 12), sampleScale = settings.preserveFormants ? 1.0 : factor;
            const double period = periodAt(marks[nearest]);
            const double left = (nearest > 0 ? marks[nearest] - marks[nearest - 1] : period) / sampleScale;
            const double right = (nearest + 1 < marks.size() ? marks[nearest + 1] - marks[nearest] : period) / sampleScale;
            const int leftIndex = jmax(runStart, int(std::ceil(outputMark - left))), rightIndex = jmin(runEnd, int(std::floor(outputMark + right)) + 1);
            for (int i = leftIndex; i < rightIndex; ++i) {
                const double delta = i - outputMark, position = marks[nearest] + delta * sampleScale;
                if (position < 0 || position >= count - 1) continue;
                const float weight = float(0.5 + 0.5 * std::cos(MathConstants<double>::pi * delta / (delta < 0 ? left : right)));
                const int a = int(position); const float fraction = float(position - a); weights[size_t(i)] += weight;
                for (int ch = 0; ch < channels; ++ch) {
                    const auto* data = original->samples.getReadPointer(ch) + first;
                    corrected->samples.addSample(ch, first + i, (data[a] + (data[a + 1] - data[a]) * fraction) * weight);
                }
            }
            outputMark += original->rate / (settings.referenceHz * std::pow(2.0, (frame.note + frame.shift - 69) / 12));
        }
    }
    if (!rendered) { result.correctedFrames = 0; result.source = original; return result; }
    for (int i = 0; i < count; ++i) {
        const auto frame = frameAt(i);
        const float wet = frame.voiced && weights[size_t(i)] > 1e-6f ? wetMask[size_t(i)] * float(jmin(1.0, std::abs(frame.shift) * 100)) : 0;
        for (int ch = 0; ch < channels; ++ch) {
            const float dry = original->samples.getSample(ch, first + i);
            const float processed = wet > 0 ? corrected->samples.getSample(ch, first + i) / weights[size_t(i)] : dry;
            if (!std::isfinite(processed)) throw std::runtime_error("Pitch processing produced an invalid sample.");
            corrected->samples.setSample(ch, first + i, dry + (processed - dry) * wet);
        }
    }
    corrected->buildWaveform();
    if (!output.getParentDirectory().createDirectory()) throw std::runtime_error("Cannot create the pitch audio folder.");
    TemporaryFile temp(output); auto stream = temp.getFile().createOutputStream();
    if (!stream) throw std::runtime_error("Cannot create corrected audio.");
    WavAudioFormat wav; std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(stream.get(), corrected->rate, unsigned(channels), 32, {}, 0));
    if (!writer) throw std::runtime_error("Cannot create the pitch WAV writer."); stream.release();
    if (!writer->writeFromAudioSampleBuffer(corrected->samples, 0, corrected->samples.getNumSamples())) throw std::runtime_error("Cannot write corrected audio.");
    writer.reset(); if (!temp.overwriteTargetFileWithTemporary()) throw std::runtime_error("Cannot save corrected audio.");
    corrected->file = output; result.source = std::move(corrected); return result;
}
}
