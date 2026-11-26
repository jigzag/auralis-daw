#pragma once
#include "Engine.h"
#include <optional>

namespace auralis {
// Sub-nanosecond differences between equivalent analysis boundaries are rounding noise.
inline constexpr double pitchBoundaryTolerance = 1e-9;
struct PitchFrame {
    double seconds = 0, note = 0, shift = 0;
    double level = 0;
    bool voiced = false;
};
struct PitchSegment {
    double start = 0, end = 0, original = 60, target = 60;
    bool edited = false;
    PitchScoop scoop;
    std::vector<PitchCurvePoint> curve;
    bool operator==(const PitchSegment&) const = default;
};
struct PitchCorrectionResult {
    std::shared_ptr<SourceAudio> source;
    int voicedFrames = 0, correctedFrames = 0;
};
std::vector<PitchFrame> analysePitch(const Clip&, const PitchCorrectionSettings&);
std::vector<PitchSegment> pitchSegments(const Clip&, const std::vector<PitchFrame>&);
std::optional<PitchSegment> uncoveredPitchRegion(const Clip&, const std::vector<PitchFrame>&, const std::vector<PitchSegment>&, double sourceSeconds);
double pitchEditShift(double measuredNote, double sourceSeconds, const PitchNoteEdit&);
double pitchEditShift(double measuredNote, double sourceSeconds, const PitchSegment&);
PitchCorrectionResult renderPitchCorrection(const Clip&, const PitchCorrectionSettings&, const File& output);
}
