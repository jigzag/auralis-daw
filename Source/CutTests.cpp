#include "Interface.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace auralis {
void runCutTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    Workstation workstation(false); workstation.stopTimer(); auto& engine = workstation.engine; auto& timeline = workstation.timeline;
    auto source = std::make_shared<SourceAudio>(); source->samples.setSize(2, 192000); source->rate = 48000;
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 192000; ++i) source->samples.setSample(ch, i, float(0.1 + i / 640000.0) * (ch == 0 ? 1 : -1));
    source->buildWaveform(); Clip initial; initial.source = source; initial.start = 2; initial.offset = 0.25; initial.length = 2;
    initial.name = "Cut test audio"; initial.fadeIn = initial.fadeOut = 0; engine.tracks[0]->clips.push_back(initial);
    engine.master = 1; engine.tracks[0]->gain = 1; workstation.refresh();
    auto& clips = engine.tracks[0]->clips;
    AudioBuffer<float> before(2, 512), after(2, 512);
    engine.seek(2.495); engine.play(); engine.render(before.getArrayOfWritePointers(), 2, 512); engine.stop();
    auto event = [&](double seconds, int modifiers = 0) {
        Point<float> point(timeline.xAt(seconds), float(Timeline::ruler + 45)); auto now = Time::getCurrentTime();
        return MouseEvent(Desktop::getInstance().getMainMouseSource(), point, ModifierKeys(ModifierKeys::leftButtonModifier | modifiers),
                          1, 0, 0, 0, 0, &timeline, &timeline, now, point, now, 1, false);
    };
    auto click = [&](double seconds, int modifiers = 0) { auto e = event(seconds, modifiers); timeline.mouseDown(e); timeline.mouseUp(e); };
    workstation.keyPressed(KeyPress('C')); check(timeline.isCutMode(), "C enables the cut tool without adding toolbar buttons");
    timeline.mouseMove(event(2.62));
    click(2.62);
    check(clips.size() == 2 && near(clips[0].length, 0.5) && near(clips[1].start, 2.5) && near(clips[1].offset, 0.75)
          && near(clips[1].length, 1.5) && clips[0].id != clips[1].id && timeline.selectedClip == 1,
          "Cut tool splits audio at the snapped click and selects the right-hand clip");
    engine.seek(2.495); engine.play(); engine.render(after.getArrayOfWritePointers(), 2, 512); engine.stop();
    after.addFrom(0, 0, before, 0, 0, 512, -1); after.addFrom(1, 0, before, 1, 0, 512, -1);
    check(after.getMagnitude(0, 512) < 1e-6f, "Cut audio preserves stereo source samples and trimmed offsets across the boundary");
    engine.seek(3.25); workstation.keyPressed(KeyPress('S'));
    check(clips.size() == 3 && near(clips[1].length, 0.75) && near(clips[2].start, 3.25) && near(clips[2].offset, 1.5)
          && near(clips[2].endTime(120), 4), "S cuts the selected clip at exact playhead position and preserves its end");
    workstation.undoEdit(); check(clips.size() == 2 && near(clips[1].length, 1.5), "One undo reverses a playhead cut");
    workstation.undoEdit(); check(clips.size() == 1 && clips[0].id == initial.id && near(clips[0].length, 2), "One undo reverses a clicked cut and restores the original clip");
    workstation.dirty = false;
    check(!workstation.cutClipAt(0, 0, 2) && !workstation.cutClipAt(0, 0, 4) && !workstation.cutClipAt(0, 0, 9)
          && !workstation.cutClipAt(-1, 0, 3) && !workstation.cutClipAt(0, -1, 3)
          && !workstation.cutClipAt(0, 0, std::numeric_limits<double>::quiet_NaN()), "Cuts outside clips, on clip edges or with invalid selection are safe no-ops");
    workstation.undoEdit(); check(clips.size() == 1 && !workstation.dirty, "Invalid cuts do not dirty the project or add empty undo steps");
    engine.recording = true; click(3); check(clips.size() == 1 && !workstation.cutClipAt(0, 0, 3), "Recording blocks cut tool edits"); engine.recording = false;
    click(2.713, ModifierKeys::shiftModifier);
    check(clips.size() == 2 && near(clips[0].length, 0.713) && near(clips[1].offset, 0.963), "Shift-click cuts freely between grid lines");
    workstation.keyPressed(KeyPress('V')); click(3.6);
    check(!timeline.isCutMode() && clips.size() == 2, "V restores normal clip selection without creating cuts");
    engine.addTrack("Cut MIDI", true); auto& midi = engine.addMidiClip(1, 7, 8); midi.offset = 1;
    midi.notes = {{1, 7, 60, 100, 2}}; midi.events.push_back({1, MidiMessage::controllerEvent(2, 64, 127)});
    String originalMidiId = midi.id;
    check(workstation.cutClipAt(1, 0, 8.25), "The shared cut command supports MIDI clips");
    auto& midiClips = engine.tracks[1]->clips;
    check(midiClips.size() == 2 && near(midiClips[0].length, 2.5) && near(midiClips[1].start, 16.5) && near(midiClips[1].offset, 3.5)
          && near(midiClips[1].length, 5.5) && midiClips[1].notes.size() == 1 && midiClips[1].notes[0].channel == 2 && midiClips[1].events.size() == 1,
          "MIDI cuts preserve beat timing, source offset, held notes, channel and controller events");
    workstation.undoEdit(); check(midiClips.size() == 1 && midiClips[0].id == originalMidiId && near(midiClips[0].length, 8), "Undo restores a complete trimmed MIDI clip after cutting");
    workstation.selectTrack(0); workstation.keyPressed(KeyPress('C')); timeline.mouseMove(event(3.1));
    if (auto stream = directory.getChildFile("cut-tool-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(workstation.createComponentSnapshot(workstation.getLocalBounds()), *stream);
    }
    directory.getChildFile("cut-verification.txt").replaceWithText("Verified C/V tool switching, snapped/free click cuts, S playhead cuts, stereo sample continuity, trimmed audio/MIDI offsets, note/controller preservation, undo, invalid-edge protection and recording guard through GUI handlers.\n");
}
}
