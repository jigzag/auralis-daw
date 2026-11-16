#include "PitchCorrection.h"
#include <cmath>

namespace auralis {
std::optional<PitchSegment> uncoveredPitchRegion(const Clip& clip, const std::vector<PitchFrame>& frames, const std::vector<PitchSegment>& notes, double time) {
    if (frames.empty() || notes.size() >= 20000 || !std::isfinite(time) || time < clip.offset || time >= clip.offset + clip.length) return {};
    const double step = frames.size() > 1 ? frames[1].seconds - frames[0].seconds : 0.01;
    const auto next = std::lower_bound(frames.begin(), frames.end(), time - clip.offset, [](const auto& f, double t) { return f.seconds < t; });
    int index = int(next - frames.begin());
    if (index == int(frames.size()) || (index > 0 && time - clip.offset - frames[size_t(index - 1)].seconds < frames[size_t(index)].seconds - time + clip.offset)) --index;
    if (index < 0 || !frames[size_t(index)].voiced || std::abs(clip.offset + frames[size_t(index)].seconds - time) > step * 0.5 + 1e-7) return {};
    int first = index, last = index;
    while (first > 0 && frames[size_t(first - 1)].voiced) --first;
    while (last + 1 < int(frames.size()) && frames[size_t(last + 1)].voiced) ++last;
    PitchSegment region;
    region.start = jmax(clip.offset, clip.offset + frames[size_t(first)].seconds - step * 0.5);
    region.end = jmin(clip.offset + clip.length, clip.offset + frames[size_t(last)].seconds + step * 0.5);
    for (const auto& n : notes) {
        if (n.start <= time && time < n.end) return {};
        if (n.end <= time) region.start = jmax(region.start, n.end);
        if (n.start > time) region.end = jmin(region.end, n.start);
    }
    if (region.end <= region.start) return {};
    std::vector<double> measured, shifts;
    for (int i = first; i <= last; ++i) if (clip.offset + frames[size_t(i)].seconds >= region.start && clip.offset + frames[size_t(i)].seconds < region.end) {
        measured.push_back(frames[size_t(i)].note); shifts.push_back(frames[size_t(i)].shift);
    }
    std::sort(measured.begin(), measured.end()); std::sort(shifts.begin(), shifts.end());
    region.original = measured.empty() ? frames[size_t(index)].note : measured[measured.size() / 2];
    region.target = region.original + (clip.pitchOriginal && !clip.pitchSettings.manual ? (shifts.empty() ? frames[size_t(index)].shift : shifts[shifts.size() / 2]) : 0);
    return region;
}

std::vector<PitchSegment> pitchSegments(const Clip& clip, const std::vector<PitchFrame>& frames) {
    std::vector<PitchSegment> detected;
    if (frames.empty()) return detected;
    const double step = frames.size() > 1 ? frames[1].seconds - frames[0].seconds : 0.01;
    auto add = [&](int first, int last) {
        if (first < 0 || last <= first) return;
        const double start = jmax(clip.offset, clip.offset + frames[size_t(first)].seconds - step * 0.5);
        const double end = jmin(clip.offset + clip.length, clip.offset + frames[size_t(last - 1)].seconds + step * 0.5);
        if (end - start >= 0.055) detected.push_back({start, end, 60, 60, false});
    };
    int first = -1, lastVoiced = -1, anchor = -1; double peakLevel = 0;
    for (int i = 0; i < int(frames.size()); ++i) {
        const auto& frame = frames[size_t(i)];
        if (!frame.voiced) {
            if (first >= 0 && i - lastVoiced >= 3) { add(first, lastVoiced + 1); first = -1; peakLevel = 0; }
            continue;
        }
        const int note = int(std::round(frame.note));
        if (first < 0) { first = i; anchor = note; peakLevel = frame.level; }
        else {
            bool changed = note != anchor;
            for (int j = i + 1; changed && j <= i + 2; ++j) changed = j < int(frames.size()) && frames[size_t(j)].voiced && int(std::round(frames[size_t(j)].note)) == note;
            const bool attack = i - first >= 9 && i >= 2 && frames[size_t(i - 2)].level < peakLevel * 0.35
                && frame.level > jmax(0.003, frames[size_t(i - 2)].level * 2);
            if (changed || attack) { add(first, lastVoiced + 1); first = i; anchor = note; peakLevel = 0; }
        }
        lastVoiced = i; peakLevel = jmax(peakLevel, frame.level);
    }
    if (first >= 0) add(first, lastVoiced + 1);
    // A saved scoop may span several detected semitones at the start of a note.
    // Keep its manually joined note intact on re-analysis.
    std::vector<PitchSegment> joined;
    for (const auto& region : detected) {
        bool merge = false;
        if (!joined.empty() && region.start - joined.back().end <= 0.04) for (const auto& edit : clip.pitchSettings.notes)
            if (edit.start < joined.back().end && edit.end > region.start) { merge = true; break; }
        if (merge) joined.back().end = region.end; else joined.push_back(region);
    }
    detected = std::move(joined);
    // Manual curves may occupy short voiced fragments omitted by note detection.
    // Restore those source-relative regions without expanding neighbouring notes.
    for (const auto& edit : clip.pitchSettings.notes) if (!edit.curve.empty()) {
        double start = jmax(clip.offset, edit.start), end = jmin(clip.offset + clip.length, edit.end);
        if (end <= start) continue;
        std::vector<PitchSegment> missing;
        for (const auto& region : detected) {
            if (region.end <= start) continue;
            if (region.start >= end) break;
            if (region.start > start) missing.push_back({start, region.start, edit.original, edit.target, false});
            start = jmax(start, region.end); if (start >= end) break;
        }
        if (start < end) missing.push_back({start, end, edit.original, edit.target, false});
        detected.insert(detected.end(), missing.begin(), missing.end());
        std::sort(detected.begin(), detected.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    }
    // Persisted manual splits are source-relative, so they survive trims, moves
    // and re-opening a project without relying on unstable detection indices.
    std::vector<PitchSegment> result;
    for (const auto& region : detected) {
        std::vector<double> boundaries {region.start, region.end};
        for (const auto& edit : clip.pitchSettings.notes) {
            if (edit.start > region.start + 1e-5 && edit.start < region.end - 1e-5) boundaries.push_back(edit.start);
            if (edit.end > region.start + 1e-5 && edit.end < region.end - 1e-5) boundaries.push_back(edit.end);
        }
        std::sort(boundaries.begin(), boundaries.end()); boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
        for (int i = 0; i + 1 < int(boundaries.size()); ++i) {
            PitchSegment segment; segment.start = boundaries[size_t(i)]; segment.end = boundaries[size_t(i + 1)];
            std::vector<double> notes, shifts;
            const auto first = std::lower_bound(frames.begin(), frames.end(), segment.start - clip.offset, [](const auto& frame, double seconds) { return frame.seconds < seconds; });
            for (auto it = first; it != frames.end() && clip.offset + it->seconds < segment.end; ++it) if (it->voiced) { notes.push_back(it->note); shifts.push_back(it->shift); }
            if (notes.empty()) {
                for (const auto& edit : clip.pitchSettings.notes) if (!edit.curve.empty() && segment.start >= edit.start - 1e-7 && segment.end <= edit.end + 1e-7) {
                    notes.push_back(edit.original); shifts.push_back(0); break;
                }
                if (notes.empty()) continue;
            }
            std::sort(notes.begin(), notes.end()); std::sort(shifts.begin(), shifts.end());
            segment.original = notes[notes.size() / 2]; segment.target = segment.original + (clip.pitchSettings.manual || !clip.pitchOriginal ? 0 : shifts[shifts.size() / 2]);
            for (const auto& edit : clip.pitchSettings.notes) if ((segment.start + segment.end) / 2 >= edit.start && (segment.start + segment.end) / 2 < edit.end) {
                segment.target = segment.original + edit.target - edit.original; segment.edited = true; segment.scoop = edit.scoop; segment.curve = edit.curve;
                if (edit.scoop.enabled || !edit.curve.empty()) { segment.original = edit.original; segment.target = edit.target; }
                break;
            }
            result.push_back(segment);
        }
    }
    return result;
}
}
