#include "Interface.h"
#include <cmath>
#include <stdexcept>
namespace auralis {
void runClipTrimTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto approx = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    File fixture = directory.getChildFile("trim-source.wav");
    { AudioBuffer<float> samples(2, 192000);
      for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 192000; ++i) samples.setSample(ch, i, float(std::sin(MathConstants<double>::twoPi * (ch ? 880 : 440) * i / 48000.0)) * 0.2f);
      auto stream = fixture.createOutputStream(); stream->setPosition(0); stream->truncate(); WavAudioFormat wav;
      std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 48000, 2, 24, {}, 0)); stream.release(); writer->writeFromAudioSampleBuffer(samples, 0, 192000); }
    Workstation workstation(false); workstation.stopTimer(); auto& engine = workstation.engine; auto& timeline = workstation.timeline;
    engine.importAudio(fixture, 0, 1); engine.tracks[0]->gain = 1; engine.master = 1; engine.tracks[0]->clips[0].gainDb = -6.5f;
    auto& clips = engine.tracks[0]->clips; auto source = clips[0].source; String id = clips[0].id;
    workstation.setTimelineZoom(100); workstation.dirty = false;
    auto event = [&](Point<float> point, Point<float> origin, int modifiers = 0) {
        auto now = Time::getCurrentTime(); return MouseEvent(Desktop::getInstance().getMainMouseSource(), point, ModifierKeys(ModifierKeys::leftButtonModifier | modifiers),
                                                           1, 0, 0, 0, 0, &timeline, &timeline, now, origin, now, 1, point != origin);
    };
    auto point = [&](double time, int track = 0) { return Point<float>(timeline.xAt(time), float(Timeline::ruler + track * Timeline::row + 65)); };
    auto drag = [&](double edge, double deltaSeconds, int side, int modifiers = 0, int track = 0) {
        auto down = point(edge, track).translated(side == 1 ? -4.0f : 4.0f, 0);
        auto move = down.translated(float(deltaSeconds * timeline.zoom), 0);
        timeline.mouseMove(event(down, down, modifiers)); timeline.mouseDown(event(down, down, modifiers));
        timeline.mouseDrag(event(down + (move - down) * 0.5f, down, modifiers)); timeline.mouseDrag(event(move, down, modifiers));
        timeline.mouseUp(event(move, down, modifiers));
    };
    auto left = point(1).translated(-4, 0); timeline.mouseMove(event(left, left));
    check(timeline.getMouseCursor() == MouseCursor::LeftRightResizeCursor && !workstation.dirty && workstation.undo.empty(), "Hovering just outside a part edge shows a resize cursor without editing");
    drag(1, 1, 1);
    check(approx(clips[0].start, 2) && approx(clips[0].offset, 1) && approx(clips[0].length, 3) && approx(clips[0].endTime(120), 5)
          && approx(clips[0].gainDb, -6.5) && clips[0].id == id, "Dragging the left edge hides leading audio and preserves end, identity and part volume");
    workstation.undoEdit(); check(approx(clips[0].start, 1) && approx(clips[0].length, 4) && workstation.undo.empty(), "One undo restores a complete multi-event edge drag");
    drag(5, -1, 2);
    check(approx(clips[0].start, 1) && approx(clips[0].offset, 0) && approx(clips[0].length, 3), "Dragging the right edge hides trailing audio without moving its start");
    drag(4, 20, 2); check(approx(clips[0].length, 4) && approx(clips[0].endTime(120), 5), "Extending the right edge restores hidden audio and stops at source end");
    workstation.undo.clear(); workstation.dirty = false;
    drag(1, -5, 1); drag(5, 5, 2);
    check(approx(clips[0].start, 1) && approx(clips[0].length, 4) && !workstation.dirty && workstation.undo.empty(), "Dragging beyond source boundaries is a safe no-op without empty undo steps");
    drag(1, 0.123, 1, ModifierKeys::shiftModifier); drag(5, -0.234, 2, ModifierKeys::shiftModifier);
    check(approx(clips[0].start, 1.123) && approx(clips[0].offset, 0.123) && approx(clips[0].endTime(120), 4.766)
          && !workstation.hasTimeSelection, "Shift edge dragging trims freely instead of creating a time selection");
    check(clips[0].source == source && source->samples.getNumSamples() == 192000 && approx(clips[0].gainDb, -6.5), "Trimming preserves the full original recording and clip volume");
    AudioBuffer<float> output(2, 512); engine.seek(1); engine.play(); engine.render(output.getArrayOfWritePointers(), 2, 512); engine.stop();
    check(output.getMagnitude(0, 512) == 0, "Playback excludes trimmed leading audio");
    engine.seek(1.623); engine.play(); engine.render(output.getArrayOfWritePointers(), 2, 512); engine.stop();
    check(std::abs(output.getSample(0, 100) - source->samples.getSample(0, 30004) * Decibels::decibelsToGain(-6.5f)) < 1e-5f, "Trimmed playback reads the original source at the retained offset");
    engine.seek(4.8); engine.play(); engine.render(output.getArrayOfWritePointers(), 2, 512); engine.stop();
    check(output.getMagnitude(0, 512) == 0, "Playback excludes trimmed trailing audio");
    File project = directory.getChildFile("trimmed.auralis"); engine.saveProject(project); Engine loaded; loaded.loadProject(project);
    check(approx(loaded.tracks[0]->clips[0].start, 1.123) && approx(loaded.tracks[0]->clips[0].offset, 0.123)
          && approx(loaded.tracks[0]->clips[0].endTime(120), 4.766), "Project save and reload preserve both trimmed edges");
    drag(clips[0].start, -5, 1); drag(clips[0].endTime(120), 5, 2);
    check(approx(clips[0].start, 1) && approx(clips[0].offset, 0) && approx(clips[0].length, 4), "Dragging both edges back restores the complete recorded part");
    engine.importAudio(fixture, 0, 5); workstation.refresh();
    auto shared = point(5).translated(-3, 0); auto moved = shared.translated(-50, 0);
    timeline.mouseMove(event(shared, shared)); timeline.mouseDown(event(shared, shared)); timeline.mouseDrag(event(moved, shared)); timeline.mouseUp(event(moved, shared));
    check(approx(clips[0].endTime(120), 4.5) && approx(clips[1].start, 5) && approx(clips[1].length, 4), "Shared-boundary dragging trims the side under the pointer and preserves its neighbor");
    workstation.undoEdit();
    shared = point(5).translated(-3, 0); moved = shared.translated(-2000, 0);
    timeline.mouseDown(event(shared, shared)); timeline.mouseDrag(event(moved, shared)); timeline.mouseUp(event(moved, shared));
    check(clips[0].length > 0 && clips[0].length <= 1.1 / 48000, "Edges cannot cross or produce a zero-length audio part"); workstation.undoEdit();
    engine.addTrack("Trim MIDI", true); auto& midi = engine.addMidiClip(1, 10, 8); midi.notes = {{0, 4, 60, 100, 1}, {6, 2, 64, 90, 2}};
    timeline.scroll = 8; workstation.refresh();
    drag(10, 0.5, 1, ModifierKeys::shiftModifier, 1); drag(14, -0.75, 2, ModifierKeys::shiftModifier, 1);
    check(approx(midi.startTime(120), 10.5) && approx(midi.offset, 1) && approx(midi.endTime(120), 13.25) && midi.notes.size() == 2,
          "MIDI part edges trim in beats while retaining the original notes");
    drag(10.5, -0.5, 1, ModifierKeys::shiftModifier, 1); drag(13.25, 0.75, 2, ModifierKeys::shiftModifier, 1);
    check(approx(midi.startTime(120), 10) && approx(midi.offset, 0) && approx(midi.length, 8), "Extending MIDI edges reveals its original notes again");
    timeline.scroll = 0; workstation.refresh();
    engine.recording = true; auto before = clips[0].length; drag(5, -1, 2); engine.recording = false;
    check(approx(clips[0].length, before), "Recording blocks edge trimming");
    workstation.setTimelineZoom(10); auto narrow = point(1).translated(3, 0); timeline.mouseMove(event(narrow, narrow));
    check(timeline.getMouseCursor() == MouseCursor::LeftRightResizeCursor, "Trim handles remain reachable when zoomed out");
    workstation.setTimelineZoom(100); workstation.selectTrack(0); timeline.selectedClip = 0;
    auto hover = point(1).translated(2, 0); timeline.mouseMove(event(hover, hover));
    if (auto stream = directory.getChildFile("clip-trim-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(workstation.createComponentSnapshot(workstation.getLocalBounds()), *stream);
    }
    directory.getChildFile("clip-trim-verification.txt").replaceWithText("Verified resize cursor, inside/outside edge hit testing, drag coalescing and undo, source-boundary clamps, Shift free trim, source preservation, audible offset/crop, persistence, adjacent clips, positive lengths, MIDI trim/restore and recording guards through GUI handlers.\n");
}
}
