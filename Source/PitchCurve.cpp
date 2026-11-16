#include "PitchRoll.h"
#include <cmath>

namespace auralis {
double PitchRollPanel::curvePitchAt(int index, double time) const {
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
    if (!isPositiveAndBelow(index, int(notes.size()))) return jlimit(24.0, 96.0, measured + (snapshot.pitchOriginal && !snapshot.pitchSettings.manual ? automaticShift : 0));
    const auto& n = notes[size_t(index)];
    if (!frame) measured = n.original;
    const double shift = n.edited ? pitchEditShift(measured, time, n)
        : snapshot.pitchOriginal && !snapshot.pitchSettings.manual ? automaticShift : 0;
    return jlimit(24.0, 96.0, measured + shift);
}

bool PitchRollPanel::drawCurve(int index, double fromTime, double fromNote, double toTime, double toNote) {
    if (!owner || owner->busy || owner->engine.recording || stale || !resolve() || !isPositiveAndBelow(index, int(notes.size()))
        || !std::isfinite(fromTime) || !std::isfinite(toTime) || !std::isfinite(fromNote) || !std::isfinite(toNote)) return false;
    auto& n = notes[size_t(index)]; const auto before = n;
    fromTime = jlimit(n.start, n.end, fromTime); toTime = jlimit(n.start, n.end, toTime);
    if (fromTime > toTime) { std::swap(fromTime, toTime); std::swap(fromNote, toNote); }
    if (n.curve.empty()) {
        n.curve.push_back({n.start, curvePitchAt(index, n.start)});
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
    // Include the gesture endpoints while bounding point growth for long strokes.
    for (double time : {fromTime, toTime}) {
        auto next = std::lower_bound(n.curve.begin(), n.curve.end(), time, [](const auto& point, double t) { return point.seconds < t; });
        if (n.curve.size() >= 200000 || (next != n.curve.end() && next->seconds - time < 0.001) || (next != n.curve.begin() && time - (next - 1)->seconds < 0.001)) continue;
        const double note = curvePitchAt(index, time); n.curve.insert(next, {time, note});
    }
    constexpr double shoulder = 0.03;
    for (auto& point : n.curve) {
        if (point.seconds < n.start || point.seconds > n.end) continue;
        const double distance = point.seconds < fromTime ? fromTime - point.seconds : point.seconds > toTime ? point.seconds - toTime : 0;
        if (distance >= shoulder) continue;
        const double u = toTime - fromTime > 1e-7 ? jlimit(0.0, 1.0, (point.seconds - fromTime) / (toTime - fromTime)) : 1;
        const double target = jlimit(24.0, 96.0, fromNote + (toNote - fromNote) * u);
        const double mix = 1 - distance / shoulder, weight = mix * mix * (3 - 2 * mix);
        point.note = jlimit(24.0, 96.0, point.note + (target - point.note) * weight);
    }
    n.edited = true; return n != before;
}
}
