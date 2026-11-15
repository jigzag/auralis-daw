#include "PitchRoll.h"
#include <cmath>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace auralis {
void runPitchLoopTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, const String& name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Engine direct; auto& track = direct.addTrack("Loop fixture"); track.gain = 1; direct.master = 1;
    auto ramp = std::make_shared<SourceAudio>(); ramp->samples.setSize(2, 96000);
    for (int i = 0; i < 96000; ++i) { ramp->samples.setSample(0, i, float(i / 48000.0 * 0.1)); ramp->samples.setSample(1, i, float(i / 48000.0 * 0.05)); }
    Clip fixture; fixture.source = ramp; fixture.start = 12.5; fixture.offset = 0.25; fixture.length = 1.5; fixture.fadeIn = fixture.fadeOut = 0; track.clips.push_back(fixture);
    direct.loop = true; direct.loopStart = 0; direct.loopEnd = 0.2;
    check(direct.setPitchLoop("editor", 13.2, 13.4), "Pitch editor can arm its own loop without changing project loop controls");
    for (int size : {64, 128, 512}) {
        direct.seek(13.3995); direct.play(); AudioBuffer<float> block(2, size); direct.render(block.getArrayOfWritePointers(), 2, size);
        check(std::abs(direct.cursor - (13.2 + (size - 24) / 48000.0)) < 1e-9 && std::abs(block.getSample(0, 24) - 0.095f) < 1e-6f,
              String(size) + "-sample callbacks wrap editor-loop audio at the exact selected sample");
    }
    check(direct.loop && direct.loopStart == 0 && direct.loopEnd == 0.2, "Dedicated pitch looping preserves the original arrangement loop state and range");
    const auto info = direct.getPosition(); check(info->getIsLooping() && std::abs(info->getLoopPoints()->ppqStart * 60 / direct.bpm - 13.2) < 1e-9,
          "Hosted plugins receive the dedicated pitch-loop bounds in their transport information");
    direct.clearPitchLoop("other-editor"); check(direct.ownsPitchLoop("editor"), "Another editor cannot clear a loop it does not own");
    direct.stop(); check(direct.ownsPitchLoop("editor"), "Stop silences playback while leaving the chosen editor loop armed");
    const auto exportFile = directory.getChildFile("pitch-loop-full-export.wav"); direct.exportWav(exportFile, 12.5, 14, 24);
    AudioFormatManager formats; formats.registerBasicFormats(); std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(exportFile)); AudioBuffer<float> exported(2, 72000);
    check(reader && reader->lengthInSamples == 72000 && reader->read(&exported, 0, 72000, 0, true, true)
          && std::abs(exported.getSample(0, 71000) - float((0.25 + 71000 / 48000.0) * 0.1)) < 1e-5f && direct.ownsPitchLoop("editor"),
          "Offline export renders its full requested range and restores the dedicated editor loop afterwards");
    direct.clearPitchLoop("editor"); direct.seek(0.1); direct.play(); AudioBuffer<float> normal(2, 128); direct.render(normal.getArrayOfWritePointers(), 2, 128);
    check(!direct.ownsPitchLoop("editor") && std::abs(direct.cursor - (0.1 + 128 / 48000.0)) < 1e-9, "Disabling an editor loop restores the existing arrangement loop behavior"); direct.stop();
    direct.recording = true; check(!direct.setPitchLoop("blocked", 1, 2), "Recording rejects dedicated pitch-loop changes"); direct.recording = false;
    check(!direct.setPitchLoop("bad", 1, 1.001) && !direct.setPitchLoop("bad", 2, 1), "Empty, reversed and excessively short editor-loop ranges are rejected");
    direct.setPitchLoop("editor", 13.2, 13.4); direct.loadProject(directory.getChildFile("pitch-scoop-project.auralis"));
    check(!direct.ownsPitchLoop("editor"), "Loading a project releases any prior pitch-editor loop");
    direct.setPitchLoop("editor", 13.2, 13.4); direct.reset(); check(!direct.ownsPitchLoop("editor"), "New project reset also clears a dedicated editor loop");
    auto pump = [] {
       #if JUCE_WINDOWS
        MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
       #endif
        Thread::sleep(1);
    };
    Workstation w(false); w.stopTimer(); auto& e = w.engine; e.importAudio(directory.getChildFile("pitch-scoop-original.wav"), 0, 12.5);
    auto& clip = e.tracks[0]->clips[0]; clip.offset = 0.25; clip.length = 1.5; PitchCorrectionSettings settings; settings.manual = true;
    PitchRollWindow window(w, e.tracks[0]->id, clip.id, clip, analysePitch(clip, settings)); window.setAlpha(0);
    auto& p = window.panel(); p.stopTimer(); p.grid.pixels = 900; p.updateSize(); p.viewport.setViewPosition(120, p.viewport.getViewPositionY());
    const auto notes = p.notes; w.dirty = false; e.loop = true; e.loopStart = 2; e.loopEnd = 4;
    auto point = [&](double local) { return Point<float>(float(PitchRollPanel::keyWidth + local * p.grid.pixels - p.viewport.getViewPositionX()), float(PitchRollPanel::rangeTop + 10)); };
    auto event = [&](Point<float> at, Point<float> origin) {
        const auto now = Time::getCurrentTime(); return MouseEvent(Desktop::getInstance().getMainMouseSource(), at, ModifierKeys(ModifierKeys::leftButtonModifier),
            1, 0, 0, 0, 0, &p, &p, now, origin, now, 1, at != origin);
    };
    const auto a = point(0.3), b = point(1.1); p.mouseDown(event(a, a)); p.mouseDrag(event(b, a)); p.mouseUp(event(b, a));
    check(p.rangeLoop.getToggleState() && e.ownsPitchLoop(p.auditionId) && std::abs(p.loopRange.getStart() - 0.3) < 1e-9 && std::abs(p.loopRange.getEnd() - 1.1) < 1e-9,
          "Dragging the fixed LOOP lane selects and arms a part-local pitch range under horizontal scrolling");
    check(p.notes == notes && !w.dirty && e.loopStart == 2 && e.loopEnd == 4, "Selecting an editor loop preserves note edits, saved state and the project range");
    p.play.onClick(); check(e.playing && std::abs(e.cursor - 12.8) < 1e-9, "Editor Play starts inside its selected loop instead of an unrelated selected note");
    AudioBuffer<float> block(2, 128); e.render(block.getArrayOfWritePointers(), 2, 128);
    const auto end = point(0.5); p.mouseDown(event(a, a)); p.mouseDrag(event(end, a)); const int row = p.grid.row; p.zoomVertical(8);
    check(p.selectingLoop && p.grid.row == row && std::abs(e.getPosition()->getLoopPoints()->ppqStart * 60 / e.bpm - 12.8) < 1e-8,
          "Loop edge dragging keeps zoom and the sounding loop stable until release");
    p.grid.keyPressed(KeyPress(KeyPress::escapeKey)); check(!p.selectingLoop && std::abs(p.loopRange.getStart() - 0.3) < 1e-9, "Escape cancels a loop-range edit without changing the previous range");
    p.mouseDown(event(a, a)); p.mouseDrag(event(end, a)); p.mouseUp(event(end, a)); e.render(block.getArrayOfWritePointers(), 2, 128);
    check(std::abs(p.loopRange.getStart() - 0.5) < 1e-9 && e.cursor >= 13 && e.cursor < 13.6, "Dragging a loop edge updates the repeat interval on the next audio block");
    int body = 0; for (int i = 1; i < int(p.notes.size()); ++i) if (p.notes[size_t(i)].end - p.notes[size_t(i)].start > p.notes[size_t(body)].end - p.notes[size_t(body)].start) body = i;
    p.selected = {body}; p.moveSelected(0.1); check(p.apply(true), "Pitch correction can be applied while its dedicated loop is armed");
    const double timeout = Time::getMillisecondCounterHiRes() + 30000; while (w.busy && Time::getMillisecondCounterHiRes() < timeout) pump();
    check(!w.busy && e.playing && e.ownsPitchLoop(p.auditionId) && e.cursor >= 13 && e.cursor < 13.6,
          "Apply-and-play resumes corrected audio inside the same dedicated loop"); e.stop();
    p.rangeLoop.setToggleState(false, dontSendNotification); p.rangeLoop.onClick(); check(!e.ownsPitchLoop(p.auditionId) && e.loop && e.loopStart == 2 && e.loopEnd == 4,
          "The editor loop switch releases only its own range and preserves the arrangement loop");
    p.rangeLoop.setToggleState(true, dontSendNotification); p.rangeLoop.onClick(); window.setVisible(false);
    check(!e.ownsPitchLoop(p.auditionId) && !p.rangeLoop.getToggleState(), "Closing or hiding the pitch editor releases its transport override");
    window.setVisible(true); p.rangeLoop.setToggleState(true, dontSendNotification); p.rangeLoop.onClick(); clip.length = 0.7; p.timerCallback();
    check(p.stale && !e.ownsPitchLoop(p.auditionId), "A changed audio part releases its loop while the pitch editor requires re-analysis");
    p.setAnalysis(clip, analysePitch(clip, settings)); p.rangeLoop.setToggleState(true, dontSendNotification); p.rangeLoop.onClick();
    check(e.ownsPitchLoop(p.auditionId) && std::abs(e.getPosition()->getLoopPoints()->ppqEnd * 60 / e.bpm - 13.2) < 1e-8,
          "Re-analysis after trimming clamps the dedicated loop to the remaining audio");
    clip.length = 0.4; p.setAnalysis(clip, analysePitch(clip, settings)); check(!e.ownsPitchLoop(p.auditionId) && !p.rangeLoop.getToggleState(), "A trim that removes the selected loop disables it safely");
    p.mouseDoubleClick(event(point(0.2), point(0.2))); check(p.loopRange.isEmpty(), "Double-clicking the LOOP lane clears its local range");
    e.recording = true; const auto protectedRange = p.loopRange; p.mouseDown(event(point(0.1), point(0.1))); p.mouseDrag(event(point(0.3), point(0.1))); p.mouseUp(event(point(0.3), point(0.1)));
    check(p.loopRange == protectedRange && !e.ownsPitchLoop(p.auditionId), "Recording blocks pitch-loop range drawing"); e.recording = false;
    clip.length = 1.5; p.setAnalysis(clip, analysePitch(clip, settings)); p.mouseDown(event(point(0.3), point(0.3))); p.mouseDrag(event(point(1.1), point(0.3))); p.mouseUp(event(point(1.1), point(0.3)));
    p.zoomVertical(8); p.grid.pixels = 650; p.updateSize(); p.viewport.setViewPosition(0, int((PitchRollGrid::highest - 69 + 0.5) * p.grid.row - p.viewport.getViewHeight() * 0.5));
    p.curveEdit.setToggleState(true, dontSendNotification); p.curveEdit.onClick();
    if (auto stream = directory.getChildFile("pitch-editor-loop-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(p.createComponentSnapshot(p.getLocalBounds()), *stream); }
    check(!e.devices.getCurrentAudioDevice() && !direct.devices.getCurrentAudioDevice(), "Dedicated pitch-loop tests never open real audio hardware");
    directory.getChildFile("pitch-editor-loop-verification.txt").replaceWithText("Offline/native editor. Verified sample-exact 64/128/512 looping, project-loop isolation, plugin transport bounds, ownership, stop/disable/close/load/recording guards, export exclusion, dedicated fixed range lane, edge resize/cancel, scrolled/trimmed coordinates, Play and Apply-and-play, trim clamping and note/saved-state preservation.\n");
}
}
