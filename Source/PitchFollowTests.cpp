#include "PitchRoll.h"
#include <cmath>
#include <stdexcept>

namespace auralis {
void runPitchFollowTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, const String& name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Workstation w(false); w.stopTimer(); auto& e = w.engine;
    e.importAudio(directory.getChildFile("pitch-melody.wav"), 0, 0);
    auto& clip = e.tracks[0]->clips[0]; clip.start = 12.5; clip.offset = 0.25; clip.length = 1.5;
    auto frames = analysePitch(clip, clip.pitchSettings);
    PitchRollWindow window(w, e.tracks[0]->id, clip.id, clip, std::move(frames)); window.setAlpha(0);
    auto& p = window.panel(); p.stopTimer(); p.grid.pixels = 1200; p.updateSize();
    p.viewport.setViewPosition(0, 520); p.followViewX = 0; p.followResumeAt = 0; w.dirty = false;
    const int width = p.viewport.getViewWidth(), y = p.viewport.getViewPositionY();
    const auto notes = p.notes; const auto source = clip.source; p.selected = {0}; const auto selected = p.selected;
    e.seek(clip.start + 0.2); e.play(); p.timerCallback();
    check(p.viewport.getViewPositionX() == 0, "Pitch editor follows by default without moving while the playhead is comfortably visible");
    e.cursor = clip.start + width * 0.92 / p.grid.pixels; p.timerCallback();
    check(p.viewport.getViewPositionX() > 0 && std::abs(p.playheadX() - p.viewport.getViewPositionX() - width * 0.2) < 1.01,
          "Pitch editor scrolls near the right edge and places the playing position back inside the view");
    check(p.viewport.getViewPositionY() == y && p.grid.pixels == 1200 && p.notes == notes && p.selected == selected && !w.dirty && clip.source == source,
          "Following preserves pitch edits, selection, zoom, vertical position and saved audio state");
    e.cursor = clip.start + 0.1; p.timerCallback();
    check(p.viewport.getViewPositionX() == 0, "Pitch editor follows a loop wrap or backward seek using part-local time");
    e.cursor = clip.start + clip.length; p.timerCallback();
    check(p.playheadX() >= p.viewport.getViewPositionX() && p.playheadX() < p.viewport.getViewPositionX() + width,
          "Following clamps to the trimmed part end and keeps its final playhead visible");
    const int endView = p.viewport.getViewPositionX(); e.cursor = clip.start - 1; p.timerCallback();
    e.cursor = clip.start + clip.length + 1; p.timerCallback();
    check(p.viewport.getViewPositionX() == endView, "Playback outside the edited part cannot push the editor beyond its audio range");
    p.viewport.setViewPosition(100, y); e.cursor = clip.start + 1.4; p.timerCallback();
    check(p.viewport.getViewPositionX() == 100 && p.followResumeAt > Time::getMillisecondCounterHiRes(),
          "Manual horizontal scrolling suspends pitch-editor follow for two seconds");
    p.followResumeAt = 0; p.timerCallback();
    check(p.viewport.getViewPositionX() > 100, "Pitch-editor follow resumes after manual navigation without changing transport");
    auto reset = [&] { p.viewport.setViewPosition(0, y); p.followViewX = 0; p.followResumeAt = 0; e.cursor = clip.start + 1.4; };
    reset(); e.stop(); p.timerCallback(); check(p.viewport.getViewPositionX() == 0, "Stopped pitch-editor transport does not scroll");
    e.play(); w.followPlayhead = false; p.timerCallback(); check(p.viewport.getViewPositionX() == 0, "Tools Follow playhead also controls the pitch editor"); w.followPlayhead = true;
    p.seeking = true; p.timerCallback(); check(p.viewport.getViewPositionX() == 0, "Pitch ruler dragging suspends automatic scrolling"); p.seeking = false;
    const auto now = Time::getCurrentTime(); const Point<float> at(30, 30);
    const MouseEvent down(Desktop::getInstance().getMainMouseSource(), at, ModifierKeys(ModifierKeys::leftButtonModifier),
        1, 0, 0, 0, 0, &p.grid, &p.grid, now, at, now, 1, false);
    p.grid.mouseDown(down); p.timerCallback();
    check(p.viewport.getViewPositionX() == 0, "Selecting notes with a drag cannot be displaced by follow"); p.grid.mouseUp(down);
    reset(); check(e.startPitchPreview("follow-test", e.tracks[0]->id, source, 0.25, 0.9, 0), "Follow regression can start an offline note audition");
    p.timerCallback(); check(p.viewport.getViewPositionX() == 0, "Drag audition keeps the pitch-editor view fixed even when transport is playing"); e.stopPitchPreview({}, true);
    e.recording = true; e.countInRemaining = 128; p.timerCallback(); check(p.viewport.getViewPositionX() == 0, "Pitch-editor follow waits for recording count-in");
    e.countInRemaining = 0; p.timerCallback(); check(p.viewport.getViewPositionX() > 0, "Pitch editor follows recording transport after count-in"); e.recording = false;
    reset(); e.exporting = true; p.timerCallback(); check(p.viewport.getViewPositionX() == 0, "Offline export does not scroll the pitch editor"); e.exporting = false;
    window.setVisible(false); p.timerCallback(); check(p.viewport.getViewPositionX() == 0, "Hidden pitch editors do not follow transport");
    window.setVisible(true); p.timerCallback(); check(p.viewport.getViewPositionX() > 0, "Reopening a pitch editor resumes following the current playback position");
    e.stop();
    if (auto stream = directory.getChildFile("pitch-follow-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(p.createComponentSnapshot(p.getLocalBounds()), *stream);
    }
    check(!e.devices.getCurrentAudioDevice(), "Pitch-editor follow is verified without opening audio hardware");
    directory.getChildFile("pitch-follow-verification.txt").replaceWithText("Offline/native editor only. Verified near-edge advance, backward/loop return, moved/trimmed coordinates, part-end clamping, outside-part guards, manual navigation grace/resume, stopped/disabled/hidden/export/count-in guards, recording follow, ruler/selection/audition drag protection, and preservation of edits, audio, zoom, selection, vertical view and saved state.\n");
}
}
