#include "PitchRoll.h"
#include <cmath>

namespace auralis {
PitchRollGrid::PitchRollGrid(PitchRollPanel& p) : panel(p) { setWantsKeyboardFocus(true); setMouseCursor(MouseCursor::UpDownResizeCursor); }
Rectangle<float> PitchRollGrid::noteBounds(int i) const {
    if (!isPositiveAndBelow(i, int(panel.notes.size()))) return {};
    const auto& note = panel.notes[size_t(i)];
    return {float((note.start - panel.snapshot.offset) * pixels), float((highest - note.target) * row + 2),
            float(jmax(3.0, (note.end - note.start) * pixels - 1)), float(row - 4)};
}
int PitchRollGrid::noteAt(Point<float> p) const {
    for (int i = int(panel.notes.size()) - 1; i >= 0; --i) if (noteBounds(i).contains(p)) return i;
    return -1;
}
int PitchRollGrid::curveNoteAt(float x) const {
    const double time = panel.snapshot.offset + x / pixels;
    for (int i = 0; i < int(panel.notes.size()); ++i) if (panel.notes[size_t(i)].start <= time && time < panel.notes[size_t(i)].end) return i;
    return -1;
}
void PitchRollGrid::paint(Graphics& g) {
    g.fillAll(Colour(0xff11151b)); const auto visible = g.getClipBounds();
    for (int n = lowest; n <= highest; ++n) {
        if ((highest - n + 1) * row < visible.getY() || (highest - n) * row > visible.getBottom()) continue;
        g.setColour(MidiMessage::isMidiNoteBlack(n) ? Colour(0xff141a22) : Colour(0xff1c242d));
        g.fillRect(visible.getX(), (highest - n) * row, visible.getWidth(), row - 1);
        if (row >= 55) {
            const int step = row >= 440 ? 5 : row >= 110 ? 10 : 20;
            g.setColour(Colour(0xff304039).withAlpha(0.55f));
            for (int cents = -40; cents <= 40; cents += step) {
                const int y = int(std::round((highest - n + 0.5 - cents / 100.0) * row));
                g.drawHorizontalLine(y, float(visible.getX()), float(visible.getRight()));
            }
        }
    }
    double step = 0.1; while (step * pixels < 45) step *= 2;
    for (double s = std::floor(visible.getX() / pixels / step) * step; s * pixels < visible.getRight(); s += step) {
        g.setColour(Colour(0xff2c3844)); g.drawVerticalLine(int(s * pixels), float(visible.getY()), float(visible.getBottom()));
    }
    for (int i = 0; i < int(panel.notes.size()); ++i) {
        auto r = noteBounds(i); if (!r.intersects(visible.toFloat())) continue;
        const auto& n = panel.notes[size_t(i)]; const bool selected = panel.selected.contains(i);
        const Colour colour = selected ? Colour(0xffffbd79) : n.edited ? Colour(0xff80e3c0) : Colour(0xff8297ba);
        g.setColour(colour.withAlpha(0.25f)); g.fillRoundedRectangle(r, 3);
        g.setColour(colour); g.drawRoundedRectangle(r, 3, selected ? 2.0f : 1.0f);
        if (r.getWidth() > 30) {
            const int midi = int(std::round(n.target)); const int cents = int(std::round((n.target - midi) * 100));
            String name = MidiMessage::getMidiNoteName(midi, true, true, 4);
            if (cents != 0 && r.getWidth() > 95) name += " " + String(cents > 0 ? "+" : "") + String(cents) + "c";
            if (!n.curve.empty() && r.getWidth() > 140) name += String::fromUTF8("  カーブ");
            else if (n.scoop.enabled && r.getWidth() > 140) name += String::fromUTF8("  しゃくり");
            g.setFont(11); g.drawText(name, r.reduced(5, 0), Justification::centredLeft);
        }
    }
    Point<float> previous, previousDry; bool havePrevious = false; size_t index = 0;
    for (const auto& frame : panel.frames) {
        const float x = float(frame.seconds * pixels);
        if (x < visible.getX() - 4 || x > visible.getRight() + 4) { havePrevious = false; continue; }
        if (!frame.voiced) { havePrevious = false; continue; }
        const double time = panel.snapshot.offset + frame.seconds;
        while (index < panel.notes.size() && panel.notes[index].end <= time) ++index;
        double shift = panel.snapshot.pitchSettings.manual || !panel.snapshot.pitchOriginal ? 0 : frame.shift;
        if (index < panel.notes.size() && panel.notes[index].start <= time && panel.notes[index].edited) {
            const auto& n = panel.notes[index]; shift = pitchEditShift(frame.note, time, n);
        }
        const Point<float> dry(x, float((highest - frame.note) * row + row * 0.5));
        const Point<float> wet(x, float((highest - frame.note - shift) * row + row * 0.5));
        if (havePrevious) {
            g.setColour(Colour(0xff8895a6).withAlpha(0.6f)); g.drawLine({previousDry, dry}, 1);
            g.setColour(Colour(0xff80e3c0)); g.drawLine({previous, wet}, 1.5f);
        }
        previousDry = dry; previous = wet; havePrevious = true;
    }
    if (panel.curveEdit.getToggleState()) {
        g.setColour(Colour(0xffb7ffe3));
        for (const auto& n : panel.notes) { double previousX = -1e10; for (const auto& point : n.curve) {
            if (point.seconds < n.start || point.seconds > n.end) continue;
            const float x = float((point.seconds - panel.snapshot.offset) * pixels), y = float((highest - point.note) * row + row * 0.5);
            if (x < visible.getX() || x > visible.getRight() || x - previousX < 18) continue;
            previousX = x; g.fillEllipse(x - 2, y - 2, 4, 4);
        } }
    }
    if (panel.owner) {
        g.setColour(Colour(0xffffe1a3)); g.drawVerticalLine(int(panel.playheadX()), float(visible.getY()), float(visible.getBottom()));
    }
    if (selecting) { auto r = Rectangle<float>(origin, getMouseXYRelative().toFloat()); g.setColour(Colour(0xff80e3c0).withAlpha(0.15f)); g.fillRect(r); g.setColour(Colour(0xff80e3c0)); g.drawRect(r); }
}
void PitchRollGrid::mouseDown(const MouseEvent& e) {
    if (isShowing()) grabKeyboardFocus(); dragging = selecting = toggleOnUp = false;
    if (!panel.owner || panel.owner->busy || panel.owner->engine.recording || panel.stale || !panel.resolve()) return;
    const int index = panel.curveEdit.getToggleState() ? curveNoteAt(e.position.x) : noteAt(e.position); clickedNote = index;
    if (e.mods.isPopupMenu()) { if (index >= 0) { if (!panel.selected.contains(index)) panel.selected = {index}; panel.contextMenu(index, panel.snapshot.offset + e.x / pixels); } return; }
    if (!e.mods.isLeftButtonDown()) return;
    origin = e.position; selectionBefore = panel.selected;
    if (panel.curveEdit.getToggleState()) {
        if (index < 0) return;
        panel.selected = {index}; drawingCurve = true; dragBefore = panel.notes; curveLast = e.position;
        const double time = panel.snapshot.offset + e.position.x / pixels;
        const double lineY = (highest - panel.curvePitchAt(index, time)) * row + row * 0.5;
        curveGrabOffset = std::abs(lineY - e.position.y) <= 12 ? lineY - e.position.y : 0;
        panel.beginAudition(index, time); panel.showSelectedMessage(); repaint(); return;
    }
    if (index < 0) { if (!e.mods.isCtrlDown()) panel.selected.clear(); selecting = true; repaint(); return; }
    if (e.mods.isCtrlDown()) { if (panel.selected.contains(index)) toggleOnUp = true; else panel.selected.insert(index); }
    else if (!panel.selected.contains(index)) panel.selected = {index};
    if (panel.selected.contains(index)) { dragging = true; dragBefore = panel.notes; panel.beginAudition(index, panel.snapshot.offset + e.x / pixels); }
    panel.showSelectedMessage(); repaint();
}
void PitchRollGrid::mouseDrag(const MouseEvent& e) {
    if (!panel.owner || panel.owner->busy || panel.owner->engine.recording || panel.stale) return;
    if (drawingCurve) {
        const float y = origin.y + (e.position.y - origin.y) * (e.mods.isCtrlDown() ? 0.1f : 1.0f);
        const auto point = Point<float>(e.position.x, y);
        const auto pitch = [&](float value) { return highest + 0.5 - (value + curveGrabOffset) / row; };
        if (panel.drawCurve(clickedNote, panel.snapshot.offset + curveLast.x / pixels, pitch(curveLast.y), panel.snapshot.offset + point.x / pixels, pitch(point.y))) { panel.changed(); panel.updateAudition(); }
        curveLast = point; return;
    }
    if (selecting) {
        const Rectangle<float> range(origin, e.position); panel.selected = e.mods.isCtrlDown() ? selectionBefore : std::set<int>();
        for (int i = 0; i < int(panel.notes.size()); ++i) if (range.intersects(noteBounds(i))) panel.selected.insert(i);
        panel.showSelectedMessage(); repaint(); return;
    }
    if (!dragging || dragBefore.size() != panel.notes.size() || std::abs(e.position.y - origin.y) < 2) return;
    toggleOnUp = false;
    const double delta = (origin.y - e.position.y) / row * (e.mods.isCtrlDown() ? 0.1 : 1.0);
    for (int i : panel.selected) if (isPositiveAndBelow(i, int(dragBefore.size()))) {
        auto& note = panel.notes[size_t(i)]; const auto& before = dragBefore[size_t(i)];
        const double target = e.mods.isCtrlDown() ? std::round((before.target + delta) * 100) / 100 : std::round(before.target + delta);
        note.target = jlimit(jmax(24.0, note.original - 12), jmin(96.0, note.original + 12), target); note.edited = true;
        note.curve = before.curve; for (auto& point : note.curve) point.note = jlimit(24.0, 96.0, point.note + note.target - before.target);
        if (note.scoop.enabled) note.scoop.depthCents = jmin(note.scoop.depthCents, (note.target - 24) * 100);
    }
    panel.changed(); panel.updateAudition();
}
void PitchRollGrid::mouseUp(const MouseEvent&) {
    if (drawingCurve) { finishCurve(false); return; }
    panel.endAudition();
    if (toggleOnUp) panel.selected.erase(clickedNote);
    if (dragging && dragBefore != panel.notes) { panel.undo.push_back(dragBefore); if (panel.undo.size() > 80) panel.undo.erase(panel.undo.begin()); panel.redo.clear(); }
    dragging = selecting = false; dragBefore.clear(); panel.changed();
}
void PitchRollGrid::mouseDoubleClick(const MouseEvent& e) { if (panel.curveEdit.getToggleState()) return; const int i = noteAt(e.position); if (i >= 0 && !panel.owner->busy) { panel.selected = {i}; panel.snapSelected(); } }
void PitchRollGrid::finishCurve(bool cancel) {
    panel.endAudition(cancel);
    if (cancel) panel.notes = dragBefore;
    else if (dragBefore != panel.notes) { panel.undo.push_back(std::move(dragBefore)); if (panel.undo.size() > 80) panel.undo.erase(panel.undo.begin()); panel.redo.clear(); }
    drawingCurve = false; dragBefore.clear(); panel.changed();
}
void PitchRollGrid::focusLost(FocusChangeType) { if (drawingCurve) finishCurve(true); panel.endAudition(true); panel.finishSeek(); panel.finishLoop(true); }
void PitchRollGrid::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) {
    if (!e.mods.isCommandDown()) { Component::mouseWheelMove(e, wheel); return; }
    if (interacting() || panel.seeking || panel.selectingLoop) return;
    if (e.mods.isShiftDown()) {
        panel.zoomVertical(double(row) / defaultRow * std::exp(jlimit(-0.7, 0.7, double(wheel.deltaY) * 2.5)), e.position.y - panel.viewport.getViewPositionY()); return;
    }
    const double time = e.position.x / pixels, screenX = e.position.x - panel.viewport.getViewPositionX();
    const int y = panel.viewport.getViewPositionY();
    pixels = jlimit(0.25, 1200.0, pixels * std::exp(jlimit(-0.7, 0.7, double(wheel.deltaY) * 2.5)));
    panel.updateSize(); panel.viewport.setViewPosition(jmax(0, int(time * pixels - screenX)), y); panel.repaint();
}
bool PitchRollGrid::keyPressed(const KeyPress& key) {
    if (!panel.owner || panel.owner->busy) return true;
    if (panel.selectingLoop) { if (key == KeyPress::escapeKey) panel.finishLoop(true); return true; }
    if (drawingCurve) { if (key == KeyPress::escapeKey) finishCurve(true); return true; }
    if (key == KeyPress::escapeKey) { panel.endAudition(true); return true; }
    const bool command = key.getModifiers().isCommandDown();
    if (command && key.getKeyCode() == 'Z') { if (key.getModifiers().isShiftDown()) panel.redoLocal(); else panel.undoLocal(); return true; }
    if (command && key.getKeyCode() == 'Y') { panel.redoLocal(); return true; }
    if (command && key.getKeyCode() == 'A') { panel.selected.clear(); for (int i = 0; i < int(panel.notes.size()); ++i) panel.selected.insert(i); repaint(); return true; }
    if (command && key.getKeyCode() == 'S') return panel.owner->keyPressed(key);
    if (command && key.getKeyCode() == KeyPress::returnKey) { panel.apply(); return true; }
    if (key == KeyPress::spaceKey) { if (panel.owner->engine.playing) panel.owner->engine.stop(); else panel.apply(true); return true; }
    if (key.getKeyCode() == KeyPress::upKey || key.getKeyCode() == KeyPress::downKey) { panel.moveSelected((key.getKeyCode() == KeyPress::upKey ? 1 : -1) * (command ? 0.01 : 1.0)); return true; }
    if (key == KeyPress::deleteKey || key == KeyPress::backspaceKey) { panel.restoreSelected(); return true; }
    if (key.getKeyCode() == 'F' && !command) { panel.fit(); return true; }
    return false;
}
}
