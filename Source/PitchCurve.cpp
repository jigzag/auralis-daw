#include "PitchRoll.h"
#include <cmath>

namespace auralis {
double PitchRollPanel::curvePitchAt(int index, double time) const {
    return curvePitchAt(isPositiveAndBelow(index, int(notes.size())) ? &notes[size_t(index)] : nullptr, time);
}
double PitchRollPanel::curvePitchAt(const PitchSegment* segment, double time) const {
    const auto next = std::lower_bound(frames.begin(), frames.end(), time - snapshot.offset, [](const auto& frame, double t) { return frame.seconds < t; });
    const PitchFrame* frame = nullptr;
    if (next != frames.end() && next->voiced) frame = &*next;
    if (next != frames.begin() && (next - 1)->voiced && (!frame || std::abs(snapshot.offset + (next - 1)->seconds - time) < std::abs(snapshot.offset + frame->seconds - time))) frame = &*(next - 1);
    double measured = frame ? frame->note : 60, automaticShift = frame ? frame->shift : 0;
    if (next != frames.begin() && next != frames.end() && next->voiced && (next - 1)->voiced) {
        const auto& before = *(next - 1);
        const double mix = jlimit(0.0, 1.0, (time - snapshot.offset - before.seconds) / (next->seconds - before.seconds));
        measured = before.note + (next->note - before.note) * mix;
        automaticShift = before.shift + (next->shift - before.shift) * mix;
    }
    if (!segment) return jlimit(24.0, 96.0, measured + (snapshot.pitchOriginal && !snapshot.pitchSettings.manual ? automaticShift : 0));
    const auto& n = *segment;
    if (!frame) measured = n.original;
    const double shift = n.edited ? pitchEditShift(measured, time, n)
        : snapshot.pitchOriginal && !snapshot.pitchSettings.manual ? automaticShift : 0;
    return jlimit(24.0, 96.0, measured + shift);
}

namespace {
double curveValue(const std::vector<PitchCurvePoint>& curve, double time) {
    auto next = std::lower_bound(curve.begin(), curve.end(), time, [](const auto& p, double t) { return p.seconds < t; });
    if (next == curve.end()) return curve.back().note;
    if (next == curve.begin()) return next->note;
    const auto& before = *(next - 1);
    return before.note + (next->note - before.note) * (time - before.seconds) / (next->seconds - before.seconds);
}
double smoothValue(const std::vector<PitchCurvePoint>& curve, double time) {
    double value = 0, weights = 0;
    for (int i = -12; i <= 12; ++i) {
        const double weight = std::exp(-0.5 * std::pow(i * 0.0025 / 0.015, 2));
        value += weight * curveValue(curve, time + i * 0.0025); weights += weight;
    }
    return value / weights;
}
std::vector<PitchCurvePoint> compactCurve(const std::vector<PitchCurvePoint>& curve) {
    if (curve.size() < 3) return curve;
    std::vector<bool> keep(curve.size(), false); keep.front() = keep.back() = true;
    std::vector<std::pair<size_t, size_t>> pending {{0, curve.size() - 1}};
    while (!pending.empty()) {
        const auto [first, last] = pending.back(); pending.pop_back();
        double error = 0.01; size_t worst = first;
        for (size_t i = first + 1; i < last; ++i) {
            const double mix = (curve[i].seconds - curve[first].seconds) / (curve[last].seconds - curve[first].seconds);
            const double difference = std::abs(curve[i].note - curve[first].note - (curve[last].note - curve[first].note) * mix);
            if (difference > error) { error = difference; worst = i; }
        }
        if (worst != first) { keep[worst] = true; pending.push_back({first, worst}); pending.push_back({worst, last}); }
    }
    std::vector<PitchCurvePoint> result;
    for (size_t i = 0; i < curve.size(); ++i) if (keep[i]) result.push_back(curve[i]);
    return result;
}
}

bool PitchRollPanel::drawCurve(int index, const PitchSegment& base, const std::vector<PitchCurvePoint>& stroke) {
    if (!owner || owner->busy || owner->engine.recording || stale || !resolve() || !isPositiveAndBelow(index, int(notes.size()))
        || stroke.empty() || stroke.size() > 200000) return false;
    double previous = -1;
    for (const auto& point : stroke) {
        if (!std::isfinite(point.seconds) || !std::isfinite(point.note) || point.seconds <= previous
            || point.seconds < base.start - 1e-7 || point.seconds > base.end + 1e-7 || point.note < 24 || point.note > 96) return false;
        previous = point.seconds;
    }
    auto n = base;
    if (n.curve.empty()) {
        n.curve.push_back({n.start, curvePitchAt(&base, n.start)});
        // Keep all untouched measured pitch, including vibrato and previously set scoops.
        const PitchNoteEdit edit {n.start, n.end, n.original, n.target, n.scoop};
        for (const auto& frame : frames) {
            const double time = snapshot.offset + frame.seconds;
            if (!frame.voiced || time <= n.start + 1e-7 || time >= n.end - 1e-7) continue;
            const double shift = n.edited ? pitchEditShift(frame.note, time, edit) : snapshot.pitchOriginal && !snapshot.pitchSettings.manual ? frame.shift : 0;
            n.curve.push_back({time, jlimit(24.0, 96.0, frame.note + shift)});
        }
        const double endNote = n.curve.back().note; n.curve.push_back({n.end, endNote});
    }
    const auto original = n.curve;
    const double from = stroke.front().seconds, to = stroke.back().seconds;
    constexpr double shoulder = 0.06;
    const double left = jmax(n.start, from - shoulder), right = jmin(n.end, to + shoulder);
    std::vector<double> times {left, from, to, right};
    for (const auto& point : original) if (point.seconds > left && point.seconds < right && (point.seconds < from || point.seconds > to)) times.push_back(point.seconds);
    for (double t = left + 0.01; t < from - 1e-7; t += 0.01) times.push_back(t);
    for (double t = from + 0.02; t < to - 1e-7 && times.size() < 200000; t += 0.02) times.push_back(t);
    for (double t = to + 0.01; t < right - 1e-7; t += 0.01) times.push_back(t);
    std::sort(times.begin(), times.end()); times.erase(std::unique(times.begin(), times.end(), [](double a, double b) { return b - a < 1e-7; }), times.end());
    std::vector<PitchCurvePoint> edited, core;
    for (double time : times) {
        const double distance = time < from ? from - time : time > to ? time - to : 0;
        const double mix = jlimit(0.0, 1.0, 1 - distance / shoulder), weight = mix * mix * (3 - 2 * mix);
        const double dry = curveValue(original, time), target = smoothValue(stroke, jlimit(from, to, time));
        const PitchCurvePoint point {time, jlimit(24.0, 96.0, dry + (target - dry) * weight)};
        if (time >= from - 1e-7 && time <= to + 1e-7) core.push_back(point); else edited.push_back(point);
    }
    core = compactCurve(core); edited.insert(edited.end(), core.begin(), core.end());
    n.curve.clear();
    for (const auto& point : original) if (point.seconds < left - 1e-7 || point.seconds > right + 1e-7) n.curve.push_back(point);
    n.curve.insert(n.curve.end(), edited.begin(), edited.end());
    std::sort(n.curve.begin(), n.curve.end(), [](const auto& a, const auto& b) { return a.seconds < b.seconds; });
    if (n.curve.size() > 200000) return false;
    n.edited = true;
    if (n == notes[size_t(index)]) return false;
    notes[size_t(index)] = std::move(n); return true;
}

void PitchRollPanel::smoothSelectedCurves() {
    if (!owner || owner->busy || owner->engine.recording || stale || !resolve() || grid.interacting()) return;
    auto result = notes;
    for (int i : selected) if (isPositiveAndBelow(i, int(result.size())) && result[size_t(i)].curve.size() > 2) {
        auto& n = result[size_t(i)]; const auto before = n.curve;
        const double from = jmax(n.start, before.front().seconds), to = jmin(n.end, before.back().seconds);
        if (to <= from) continue;
        std::vector<PitchCurvePoint> smoothed {{from, curveValue(before, from)}};
        for (double time = from + 0.02; time < to - 1e-7 && smoothed.size() < 200000; time += 0.02) smoothed.push_back({time, smoothValue(before, time)});
        smoothed.push_back({to, curveValue(before, to)});
        smoothed = compactCurve(smoothed);
        n.curve.clear();
        for (const auto& point : before) if (point.seconds < from || point.seconds > to) n.curve.push_back(point);
        n.curve.insert(n.curve.end(), smoothed.begin(), smoothed.end());
        std::sort(n.curve.begin(), n.curve.end(), [](const auto& a, const auto& b) { return a.seconds < b.seconds; });
        n.edited = true;
    }
    if (result != notes) { endAudition(true); remember(); notes = std::move(result); changed(); }
}
}
