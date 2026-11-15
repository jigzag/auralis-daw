#include "PitchRoll.h"
#include <cmath>

namespace auralis {
bool Engine::setPitchLoop(const String& ownerId, double start, double end) {
    if (ownerId.isEmpty() || !std::isfinite(start) || !std::isfinite(end) || start < 0 || end - start < 0.02 - 1e-9) return false;
    const ScopedLock guard(lock); if (recording || exporting) return false;
    if (pitchLoopActive && pitchLoopOwner == ownerId && pitchLoopStart == start && pitchLoopEnd == end) return true;
    pitchLoopOwner = ownerId; pitchLoopStart = start; pitchLoopEnd = end; pitchLoopActive = true; midiDiscontinuity = true; return true;
}
void Engine::clearPitchLoop(const String& ownerId) {
    const ScopedLock guard(lock);
    if (!pitchLoopActive || (ownerId.isNotEmpty() && pitchLoopOwner != ownerId)) return;
    pitchLoopActive = false; midiDiscontinuity = true;
}
bool Engine::ownsPitchLoop(const String& ownerId) { const ScopedLock guard(lock); return pitchLoopActive && pitchLoopOwner == ownerId; }

void PitchRollPanel::updateLoop() {
    if (!owner) return;
    if (!rangeLoop.getToggleState()) { owner->engine.clearPitchLoop(auditionId); return; }
    loopRange = loopRange.getIntersectionWith({0, snapshot.length});
    if (owner->engine.recording || stale || !resolve() || loopRange.getLength() < 0.02 - 1e-9
        || !owner->engine.setPitchLoop(auditionId, snapshot.start + loopRange.getStart(), snapshot.start + loopRange.getEnd())) {
        rangeLoop.setToggleState(false, dontSendNotification); owner->engine.clearPitchLoop(auditionId);
    }
}
void PitchRollPanel::dragLoop(float x) {
    if (!owner || owner->busy || owner->engine.recording) return;
    const double time = jlimit(0.0, snapshot.length, (x - keyWidth + viewport.getViewPositionX()) / grid.pixels);
    loopRange = {jmin(loopAnchor, time), jmax(loopAnchor, time)}; repaint();
}
void PitchRollPanel::finishLoop(bool cancel) {
    if (!selectingLoop) return;
    cancel |= !owner || owner->busy || owner->engine.recording;
    selectingLoop = false;
    if (cancel || loopRange.getLength() < 0.02 - 1e-9) loopRange = loopBefore;
    else { rangeLoop.setToggleState(true, dontSendNotification); updateLoop(); }
    repaint();
}
void PitchRollPanel::mouseDoubleClick(const MouseEvent& e) {
    if (e.y < rangeTop || e.y >= bodyTop || e.x < keyWidth || !owner || owner->busy || owner->engine.recording) return;
    selectingLoop = false; loopRange = {}; rangeLoop.setToggleState(false, dontSendNotification); updateLoop(); repaint();
}
}
