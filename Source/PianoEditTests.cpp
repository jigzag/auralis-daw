#include "PianoRoll.h"
#include <stdexcept>
#include <cmath>

namespace auralis {
void runPianoEditTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-7; };
    Workstation workstation(false); workstation.stopTimer();
    auto& engine = workstation.engine; auto& track = engine.addTrack("Group edit test", true);
    auto& initial = engine.addMidiClip(1, 0, 8); String clipId = initial.id;
    PianoRollPanel panel(workstation, track.id, clipId); panel.setLookAndFeel(&workstation.getLookAndFeel());
    auto& roll = panel.roll; roll.stopTimer();
    auto clip = [&]() -> Clip& { auto* result = roll.resolve(); if (!result) throw std::runtime_error("Piano test clip lost."); return *result; };
    auto point = [&](double beat, int pitch) { return Point<float>(float(62 + (beat - clip().offset) * roll.pixels), float(28 + (127 - pitch) * 16 + 8)); };
    auto event = [&](Point<float> position, Point<float> origin, int modifiers, bool dragged = false) {
        auto now = Time::getCurrentTime();
        return MouseEvent(Desktop::getInstance().getMainMouseSource(), position, ModifierKeys(modifiers), 1, 0, 0, 0, 0, &roll, &roll, now, origin, now, 1, dragged);
    };
    auto click = [&](Point<float> p, int extra = 0) { auto e = event(p, p, ModifierKeys::leftButtonModifier | extra); roll.mouseDown(e); roll.mouseUp(e); };
    auto drag = [&](Point<float> a, Point<float> b, int extra = 0) {
        auto down = event(a, a, ModifierKeys::leftButtonModifier | extra); roll.mouseDown(down);
        auto move = event(b, a, ModifierKeys::leftButtonModifier | extra, true); roll.mouseDrag(move); roll.mouseUp(move);
    };
    auto command = [&](int key) { return roll.keyPressed(KeyPress(key, ModifierKeys(ModifierKeys::ctrlModifier), 0)); };
    auto emptyClick = [&] { click(point(7.8, 40)); };

    clip().notes = {{1, 0.5, 60, 90, 1}, {2, 0.5, 64, 100, 2}, {5, 1, 67, 110, 3}};
    workstation.dirty = false;
    drag(point(0.5, 66), point(3, 58));
    check(roll.getNumSelectedNotes() == 2 && roll.isNoteSelected(0) && roll.isNoteSelected(1) && !roll.isNoteSelected(2), "Piano box selection selects intersecting notes in both time and pitch");
    check(!workstation.dirty, "Changing a piano selection does not modify the project");
    if (auto stream = directory.getChildFile("piano-selection-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(panel.createComponentSnapshot(panel.getLocalBounds()), *stream);
    }
    drag(point(1.1, 60), point(2.1, 62));
    check(near(clip().notes[0].beat, 2) && near(clip().notes[1].beat, 3) && clip().notes[0].pitch == 62 && clip().notes[1].pitch == 66, "Dragging selected notes moves the complete group without changing intervals");
    check(near(clip().notes[2].beat, 5) && clip().notes[2].pitch == 67 && clip().notes[0].velocity == 90 && clip().notes[1].channel == 2, "Group movement preserves note properties and leaves unselected notes unchanged");
    roll.keyPressed(KeyPress(KeyPress::deleteKey));
    check(clip().notes.size() == 1 && clip().notes[0].pitch == 67 && roll.getNumSelectedNotes() == 0, "Delete removes every selected piano note and preserves other notes");
    command('Z');
    check(clip().notes.size() == 3 && near(clip().notes[0].beat, 2) && roll.getNumSelectedNotes() == 0, "One undo restores a batch deletion");
    command('Z');
    check(near(clip().notes[0].beat, 1) && near(clip().notes[1].beat, 2) && clip().notes[1].pitch == 64, "One undo restores a complete group drag");

    clip().notes = {{0, 0.5, 60, 90, 1}, {1, 0.5, 64, 100, 2}, {2, 0.5, 72, 110, 3}}; clip().length = 4;
    emptyClick(); drag(point(0, 66), point(2, 58));
    command('D');
    check(clip().notes.size() == 5 && roll.getNumSelectedNotes() == 2 && roll.isNoteSelected(3) && roll.isNoteSelected(4), "Ctrl+D selects the newly duplicated group instead of the original notes");
    check(near(clip().notes[3].beat, 2) && near(clip().notes[4].beat, 3) && clip().notes[3].velocity == 90 && clip().notes[4].channel == 2,
          "Group duplication preserves timing and note properties including the selected trailing rest");
    command('D'); command('D');
    check(clip().notes.size() == 9 && near(clip().notes[5].beat, 4) && near(clip().notes[6].beat, 5) && near(clip().notes[7].beat, 6) && near(clip().notes[8].beat, 7),
          "Repeated Ctrl+D appends consecutive copies at a constant musical interval");
    check(roll.isNoteSelected(7) && roll.isNoteSelected(8) && clip().length >= 8, "Continuous duplication selects its latest copy and extends the clip");
    roll.keyPressed(KeyPress(KeyPress::deleteKey));
    check(clip().notes.size() == 7, "Delete after continuous duplication removes only the latest selected group");
    command('Z'); check(clip().notes.size() == 9, "Undo restores the latest duplicated group after deletion");
    command('A'); check(roll.getNumSelectedNotes() == 9, "Ctrl+A selects all piano notes");
    click(point(0.1, 60), ModifierKeys::ctrlModifier);
    check(roll.getNumSelectedNotes() == 8 && !roll.isNoteSelected(0), "Ctrl-click removes a note from a multi-note selection");
    click(point(0.1, 60), ModifierKeys::shiftModifier);
    check(roll.getNumSelectedNotes() == 9 && roll.isNoteSelected(0), "Shift-click adds a note to the existing selection");
    roll.keyPressed(KeyPress(KeyPress::deleteKey)); check(clip().notes.empty(), "Ctrl+A followed by Delete clears the entire clip");
    command('Z'); check(clip().notes.size() == 9, "Undo restores all notes after clearing the clip");

    emptyClick(); clip().notes = {{1, 0.5, 0, 90, 1}, {2, 0.5, 127, 100, 2}};
    command('A'); drag(point(1.1, 0), point(-8.9, 12));
    check(near(clip().notes[0].beat, 0) && near(clip().notes[1].beat, 1) && clip().notes[0].pitch == 0 && clip().notes[1].pitch == 127,
          "Group movement clamps time and pitch together instead of flattening intervals at MIDI limits");
    auto edge = point(0.5, 0); edge.x -= 2; auto shorter = edge; shorter.x -= float(roll.pixels);
    drag(edge, shorter);
    check(near(clip().notes[0].length, 0.25) && near(clip().notes[1].length, 0.25), "Dragging a selected note edge resizes the group with positive snapped lengths");
    roll.setVelocity(77); roll.setChannel(9);
    check(clip().notes[0].velocity == 77 && clip().notes[1].velocity == 77 && clip().notes[0].channel == 9 && clip().notes[1].channel == 9,
          "Velocity and channel controls apply to every selected note");
    emptyClick(); command('D');
    check(clip().notes.size() == 2 && roll.getNumSelectedNotes() == 0, "Ctrl+D without a selection does not create accidental notes");
    clip().notes = {{0, 0.5, 60, 90, 1}, {1, 0.5, 64, 100, 2}}; clip().length = 4;
    drag(point(0, 66), point(2, 58));
    for (int i = 0; i < 12; ++i) command('D');
    auto* view = roll.findParentComponentOfClass<Viewport>();
    check(clip().notes.size() == 26 && near(clip().notes[24].beat, 24) && near(clip().notes[25].beat, 25)
          && roll.getNumSelectedNotes() == 2 && view && view->getViewPositionX() > 0,
          "Continuous Ctrl+D scrolls to the latest group beyond the visible piano-roll area");
    workstation.dirty = false;
    view->setViewPosition(300, 600);
    const double anchorBeat = (view->getViewPositionX() + 400 - 62) / roll.pixels;
    MouseWheelDetails wheel; wheel.deltaY = 0.1f;
    auto zoomEvent = event({float(view->getViewPositionX() + 400), 800}, {}, ModifierKeys::ctrlModifier);
    roll.mouseWheelMove(zoomEvent, wheel);
    check(roll.pixels > 78 && std::abs((view->getViewPositionX() + 400 - 62) / roll.pixels - anchorBeat) * roll.pixels <= 1
          && view->getViewPositionY() == 600, "Ctrl+wheel zoom preserves the beat under the pointer and vertical scroll");
    const double zoomed = roll.pixels;
    roll.mouseWheelMove(event({500, 800}, {}, 0), wheel);
    check(near(roll.pixels, zoomed), "Ordinary wheel scrolling does not change piano zoom");
    wheel.deltaY = 1;
    for (int i = 0; i < 8; ++i) roll.mouseWheelMove(zoomEvent, wheel);
    check(near(roll.pixels, 624), "Piano zoom has a stable maximum");
    wheel.deltaY = -1;
    for (int i = 0; i < 18; ++i) roll.mouseWheelMove(zoomEvent, wheel);
    check(near(roll.pixels, 0.125), "Piano zoom can show thousands of beats with a stable minimum");
    const double originalLength = clip().length; clip().length = 4096;
    roll.keyPressed(KeyPress('F'));
    check(62 + clip().length * roll.pixels <= view->getViewWidth() - 20 && view->getViewPositionX() == 0
          && roll.pixels < 1 && roll.getNumSelectedNotes() == 2 && !workstation.dirty,
          "F fits a 1024-bar piano clip without changing notes or selection");
    if (auto stream = directory.getChildFile("piano-wide-zoom-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(panel.createComponentSnapshot(panel.getLocalBounds()), *stream);
    }
    clip().length = originalLength;
    roll.pixels = 78; roll.updateSize(); view->setViewPosition(0, 600);
    engine.setTempo(120); clip().start = 12; clip().offset = 2;
    auto rulerEvent = [&](float x, int extra = 0) {
        auto now = Time::getCurrentTime();
        return MouseEvent(Desktop::getInstance().getMainMouseSource(), {x, 66}, ModifierKeys(ModifierKeys::leftButtonModifier | extra),
                          1, 0, 0, 0, 0, &panel, &panel, now, {x, 66}, now, 1, false);
    };
    auto seek = rulerEvent(62 + 2 * 78); panel.mouseDown(seek); panel.mouseUp(seek);
    check(near(engine.cursor, 7) && !engine.playing && near(roll.getPlayheadX(), 62 + 2 * 78),
          "Piano ruler seeks stopped transport in project time including clip start and trim offset");
    engine.play(); panel.mouseDown(seek);
    auto move = rulerEvent(62 + 5 * 78); panel.mouseDrag(move); panel.mouseUp(move);
    check(near(engine.cursor, 8.5) && engine.playing, "Dragging piano ruler moves transport without stopping playback");
    engine.stop();
    auto snappedSeek = rulerEvent(float(62 + 2.13 * 78), ModifierKeys::shiftModifier); panel.mouseDown(snappedSeek); panel.mouseUp(snappedSeek);
    check(std::abs(double(engine.cursor) - 7.125) < 1e-5, "Shift optionally snaps piano ruler to selected grid");
    auto freeSeek = rulerEvent(float(62 + 2.13 * 78)); panel.mouseDown(freeSeek); panel.mouseUp(freeSeek);
    check(std::abs(double(engine.cursor) - (12 + (freeSeek.position.x - 62) / 78.0) * 0.5) < 1e-7, "Piano ruler follows fractional mouse position without snapping by default");
    engine.play(); panel.mouseDown(freeSeek);
    auto preciseDrag = rulerEvent(421.75f); panel.mouseDrag(preciseDrag);
    double heldPosition = engine.cursor; engine.cursor = heldPosition + 0.1;
    check(std::abs(roll.getPlayheadX() - preciseDrag.position.x) < 1e-5,
          "Piano playhead remains attached to pointer while audio transport advances during drag");
    panel.mouseUp(preciseDrag);
    check(std::abs(double(engine.cursor) - heldPosition) < 1e-7 && engine.playing,
          "Releasing piano playhead resumes playback from the exact dragged position");
    engine.stop();
    view->setViewPosition(156, 600);
    auto scrolledSeek = rulerEvent(62 + 2 * 78); panel.mouseDown(scrolledSeek); panel.mouseUp(scrolledSeek);
    check(near(engine.cursor, 8), "Piano ruler seek accounts for horizontal scrolling");
    engine.recording = true;
    auto blocked = rulerEvent(62 + 6 * 78); panel.mouseDown(blocked); panel.mouseDrag(blocked); panel.mouseUp(blocked);
    engine.recording = false;
    check(near(engine.cursor, 8), "Piano ruler cannot interrupt an active recording");
    drag({62 + 3 * 78.0f, 10}, {62 + 4 * 78.0f, 10});
    check(near(engine.cursor, 8) && roll.getNumSelectedNotes() == 2 && clip().notes.size() == 26 && !workstation.dirty,
          "Piano navigation preserves notes, selection and project saved state");
    if (auto stream = directory.getChildFile("piano-navigation-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(panel.createComponentSnapshot(panel.getLocalBounds()), *stream);
    }
    directory.getChildFile("piano-edit-verification.txt").replaceWithText("Verified group editing, Ctrl+wheel pointer anchored zoom, zoom limits, ruler click/drag seek, grid bypass, scrolled/trimmed clips, playing/stopped transport and recording guard through GUI event handlers.\n");
}
}
