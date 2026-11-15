#include "PitchRoll.h"
#include <cmath>
#include <stdexcept>

namespace auralis {
void runPitchZoomTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, const String& name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Workstation w(false); w.stopTimer(); auto& e = w.engine; e.importAudio(directory.getChildFile("pitch-scoop-original.wav"), 0, 12.5);
    auto& clip = e.tracks[0]->clips[0]; clip.offset = 0.25; clip.length = 1.5; const auto source = clip.source;
    PitchCorrectionSettings settings; settings.manual = true;
    PitchRollWindow window(w, e.tracks[0]->id, clip.id, clip, analysePitch(clip, settings)); window.setAlpha(0);
    auto& p = window.panel(); p.stopTimer(); auto& grid = p.grid; grid.pixels = 900; p.updateSize();
    int body = 0; for (int i = 1; i < int(p.notes.size()); ++i) if (p.notes[size_t(i)].end - p.notes[size_t(i)].start > p.notes[size_t(body)].end - p.notes[size_t(body)].start) body = i;
    p.viewport.setViewPosition(90, int(grid.noteBounds(body).getCentreY()) - 230); w.dirty = false;
    const auto notes = p.notes; const double cursor = e.cursor; const int startX = p.viewport.getViewPositionX();
    auto event = [&](Component& component, Point<float> at, Point<float> origin, int mods) {
        const auto now = Time::getCurrentTime(); return MouseEvent(Desktop::getInstance().getMainMouseSource(), at, ModifierKeys(mods),
            1, 0, 0, 0, 0, &component, &component, now, origin, now, 1, at != origin);
    };
    MouseWheelDetails wheel; wheel.deltaY = 0.3f;
    const double mouseY = 153.4, anchor = (p.viewport.getViewPositionY() + mouseY) / grid.row;
    const auto wheelAt = Point<float>(400, float(p.viewport.getViewPositionY() + mouseY));
    check(grid.row == 22 && p.verticalZoom.getValue() == 1, "Pitch editor starts at its familiar one-times vertical scale");
    grid.mouseWheelMove(event(grid, wheelAt, wheelAt, ModifierKeys::ctrlModifier | ModifierKeys::shiftModifier), wheel);
    check(grid.row > 22 && grid.pixels == 900 && p.viewport.getViewPositionX() == startX
          && std::abs(p.viewport.getViewPositionY() + mouseY - anchor * grid.row) <= 0.51,
          "Ctrl+Shift+wheel vertically zooms around the pointer without changing source time or horizontal view");
    check(p.notes == notes && !p.hasPendingEdits() && !w.dirty && clip.source == source && e.cursor == cursor,
          "Vertical zoom changes only the view and preserves pitch edits, audio, saved state and transport");
    const double centre = p.viewport.getViewHeight() * 0.5, centreRow = (p.viewport.getViewPositionY() + centre) / grid.row;
    p.verticalZoom.setValue(8, sendNotificationSync);
    check(grid.row == 176 && std::abs(p.viewport.getViewPositionY() + centre - centreRow * grid.row) <= 0.51,
          "The vertical-zoom slider enlarges pitches eight times while retaining the centre of the view");
    p.verticalZoom.setValue(32, sendNotificationSync); check(grid.row == 704 && grid.row / 100.0 > 7, "Maximum vertical zoom gives more than seven pixels per cent for detailed vibrato editing");
    const double keyboardAnchor = (p.viewport.getViewPositionY() + mouseY) / grid.row;
    const auto keyAt = Point<float>(25, float(PitchRollPanel::bodyTop + mouseY)); MouseWheelDetails down; down.deltaY = -0.2f;
    p.mouseWheelMove(event(p, keyAt, keyAt, ModifierKeys::ctrlModifier), down);
    check(grid.row < 704 && std::abs(p.viewport.getViewPositionY() + mouseY - keyboardAnchor * grid.row) <= 0.51,
          "Ctrl+wheel over the fixed piano keys also anchors vertical zoom to the hovered pitch");
    p.zoomVertical(16); p.dragAudition.setToggleState(false, dontSendNotification); p.curveEdit.setToggleState(true, dontSendNotification); p.curveEdit.onClick();
    const double beforePitch = p.curvePitchAt(body, 0.9);
    const auto a = Point<float>(float((0.9 - clip.offset) * grid.pixels), float((PitchRollGrid::highest - beforePitch + 0.5) * grid.row));
    const auto b = a.translated(0, -float(grid.row / 100.0));
    grid.mouseDown(event(grid, a, a, ModifierKeys::leftButtonModifier)); grid.mouseDrag(event(grid, b, a, ModifierKeys::leftButtonModifier));
    check(std::abs(p.curvePitchAt(body, 0.9) - beforePitch - 0.01) < 0.002, "A one-cent mouse movement at sixteen-times zoom edits the curve by one cent");
    const int heldRow = grid.row, heldY = p.viewport.getViewPositionY();
    grid.mouseWheelMove(event(grid, b, a, ModifierKeys::ctrlModifier | ModifierKeys::shiftModifier), wheel); p.verticalZoom.setValue(4, sendNotificationSync);
    check(grid.row == heldRow && p.viewport.getViewPositionY() == heldY, "Wheel and slider zoom cannot displace an active curve-edit gesture");
    grid.mouseUp(event(grid, b, a, 0)); p.undoLocal(); check(p.notes == notes, "Undo restores a fine curve edit independently of vertical zoom");
    p.curveEdit.setToggleState(false, dontSendNotification); p.selected = {body};
    const double beforeTarget = p.notes[size_t(body)].target; const auto noteAt = grid.noteBounds(body).getCentre(); const auto moved = noteAt.translated(0, -float(grid.row));
    grid.mouseDown(event(grid, noteAt, noteAt, ModifierKeys::leftButtonModifier)); grid.mouseDrag(event(grid, moved, noteAt, ModifierKeys::leftButtonModifier)); grid.mouseUp(event(grid, moved, noteAt, 0));
    check(p.notes[size_t(body)].target == std::round(beforeTarget + 1), "Whole-note dragging still moves one semitone per enlarged piano row"); p.undoLocal();
    p.seeking = true; p.zoomVertical(2); check(grid.row == heldRow, "Ruler dragging also protects the editor from a mid-gesture vertical scale change"); p.seeking = false;
    const double oldPixels = grid.pixels; const int oldY = p.viewport.getViewPositionY();
    grid.mouseWheelMove(event(grid, noteAt, noteAt, ModifierKeys::ctrlModifier), wheel);
    check(grid.pixels > oldPixels && grid.row == heldRow && p.viewport.getViewPositionY() == oldY, "Existing Ctrl+wheel horizontal zoom stays independent of vertical pitch zoom");
    p.zoomVertical(1000); check(grid.row == 704, "Vertical enlargement clamps at thirty-two times");
    p.zoomVertical(-1); check(grid.row == 11 && p.viewport.getViewPositionY() >= 0 && p.viewport.getViewPositionY() <= jmax(0, grid.getHeight() - p.viewport.getViewHeight()),
          "Vertical zoom-out clamps to half scale and stays within the piano scroll range");
    p.fit(); check(grid.row == 22 && p.verticalZoom.getValue() == 1 && p.notes == notes && !w.dirty, "Fit restores the normal vertical scale and fits the audio without editing the project");
    p.setSize(880, 480); check(p.getLocalBounds().contains(p.verticalZoom.getBounds()), "Vertical-zoom controls remain visible at the smallest editor size");
    p.setSize(1100, 720); p.zoomVertical(16); grid.pixels = 650; p.updateSize();
    p.viewport.setViewPosition(0, int((PitchRollGrid::highest - 69 + 0.5) * grid.row - p.viewport.getViewHeight() * 0.5));
    p.curveEdit.setToggleState(true, dontSendNotification); p.curveEdit.onClick();
    if (auto stream = directory.getChildFile("pitch-vertical-zoom-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(p.createComponentSnapshot(p.getLocalBounds()), *stream); }
    check(!e.devices.getCurrentAudioDevice(), "Vertical pitch-zoom verification never opens audio hardware");
    directory.getChildFile("pitch-vertical-zoom-verification.txt").replaceWithText("Offline/native editor. Verified 0.5x-32x scaling, pointer-anchored Ctrl+Shift+wheel, keyboard Ctrl+wheel, centre-anchored numeric slider, independent horizontal zoom, enlarged cent editing and semitone dragging, gesture guards, undo independence, fit/reset, scroll limits, smallest window layout, and preservation of pitch/audio/saved/transport state.\n");
}
}
