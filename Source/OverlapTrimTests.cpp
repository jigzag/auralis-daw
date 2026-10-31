#include "Interface.h"
#include <cmath>
#include <stdexcept>
namespace auralis {
void runOverlapTrimTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto approx = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    Workstation w(false); w.stopTimer(); auto& e = w.engine; auto& t = w.timeline;
    e.importAudio(directory.getChildFile("overlap-old.wav"), 0, 0);
    e.importAudio(directory.getChildFile("overlap-new.wav"), 0, 1);
    auto& clips = e.tracks[0]->clips; clips[0].length = 3; clips[1].length = 3;
    clips[0].fadeIn = clips[0].fadeOut = clips[1].fadeIn = clips[1].fadeOut = 0;
    clips[0].gainDb = -6; e.tracks[0]->gain = 1; e.master = 1;
    const auto oldId = clips[0].id, newId = clips[1].id, trackId = e.tracks[0]->id;
    const auto oldOrder = clips[0].order, newOrder = clips[1].order;
    w.setTimelineZoom(180); w.dirty = false;
    auto point = [&](double seconds) { return Point<float>(t.xAt(seconds), float(Timeline::ruler + 65)); };
    auto event = [&](Point<float> p, Point<float> origin) {
        auto now = Time::getCurrentTime(); return MouseEvent(Desktop::getInstance().getMainMouseSource(), p, ModifierKeys(ModifierKeys::leftButtonModifier),
            1, 0, 0, 0, 0, &t, &t, now, origin, now, 1, p != origin);
    };
    auto click = [&](double seconds) { auto p = point(seconds); t.mouseDown(event(p, p)); t.mouseUp(event(p, p)); };
    auto drag = [&](double seconds, double delta) {
        auto p = point(seconds), q = point(seconds + delta);
        t.mouseMove(event(p, p)); t.mouseDown(event(p, p)); t.mouseDrag(event(q, p)); t.mouseUp(event(q, p));
    };
    click(0.5); auto covered = point(3); t.mouseMove(event(covered, covered));
    check(t.selectedClip == 0 && t.getMouseCursor() == MouseCursor::LeftRightResizeCursor, "Selected older part exposes its covered right trim edge");
    drag(3, -0.5);
    check(approx(clips[0].length, 2.5) && approx(clips[1].start, 1) && approx(clips[1].length, 3), "Dragging a covered right edge trims only the selected older recording");
    w.undoEdit(); check(approx(clips[0].length, 3), "Undo restores a covered-edge trim");
    clips[0].start = 1; clips[0].length = 3; clips[1].start = 0; clips[1].length = 2;
    click(3); drag(1, 0.5);
    check(approx(clips[0].start, 1.5) && approx(clips[0].offset, 0.5) && approx(clips[0].endTime(120), 4)
        && approx(clips[1].start, 0) && approx(clips[1].length, 2), "Covered left-edge trim retains the old end and leaves the front part unchanged");
    w.undoEdit(); clips[0].start = clips[1].start = 0; clips[0].length = clips[1].length = 3; clips[0].offset = 0;
    w.dirty = false;
    check(w.selectClipById(trackId, oldId) && t.selectedClip == 0 && !w.dirty, "A fully hidden part can be selected by stable identity without editing the project");
    drag(3, -0.5);
    check(approx(clips[0].length, 2.5) && approx(clips[1].length, 3) && approx(clips[0].gainDb, -6)
        && clips[0].order == oldOrder && clips[1].order == newOrder, "Coincident-edge trim targets the selected part without changing volume or playback priority");
    e.seek(1); e.play(); AudioBuffer<float> out(2, 64); e.render(out.getArrayOfWritePointers(), 2, 64); e.stop();
    check(std::abs(out.getSample(0, 0) - 0.2f) < 1e-6, "Selecting and trimming an older layer preserves newest-only playback");
    w.selectClipById(trackId, newId); drag(3, -0.5);
    check(approx(clips[1].length, 2.5) && approx(clips[0].length, 2.5), "The front layer also remains resizable at coincident edges");
    check(!w.selectClipById(trackId, "missing") && t.selectedClip == 1, "Stale layer selection leaves the current selection unchanged");
    e.recording = true; check(!w.selectClipById(trackId, oldId) && t.selectedClip == 1, "Recording blocks overlap-layer selection"); e.recording = false;
    clips[0].length = 2; clips[1].start = 1; clips[1].length = 2;
    t.selectedClip = -1; click(1.5);
    check(t.selectedClip == 1, "Normal overlapping-body clicks still select the newest part");
    w.selectClipById(trackId, oldId); covered = point(2); t.mouseMove(event(covered, covered));
    if (auto stream = directory.getChildFile("overlap-trim-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(w.createComponentSnapshot(w.getLocalBounds()), *stream);
    }
    directory.getChildFile("overlap-trim-verification.txt").replaceWithText("Covered left/right and coincident edges verified through native mouse handlers; selection by stable ID, undo, gain/order preservation, newest-only audio playback, stale IDs and recording guards verified.\n");
}
}
