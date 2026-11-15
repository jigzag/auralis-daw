#include "PitchRoll.h"
#include <cmath>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace auralis {
void runPitchCurveTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, const String& name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto pump = [] {
       #if JUCE_WINDOWS
        MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
       #endif
        Thread::sleep(1);
    };
    Workstation w(false); w.stopTimer(); auto& e = w.engine; e.importAudio(directory.getChildFile("pitch-scoop-original.wav"), 0, 12.5);
    auto& clip = e.tracks[0]->clips[0]; clip.offset = 0.25; clip.length = 1.5; const auto original = clip.source; const auto originalId = clip.id;
    PitchCorrectionSettings analyse; analyse.manual = true;
    PitchRollWindow window(w, e.tracks[0]->id, clip.id, clip, analysePitch(clip, analyse)); window.setAlpha(0);
    auto& p = window.panel(); p.stopTimer(); auto& grid = p.grid; grid.pixels = 700; p.updateSize();
    int body = 0; for (int i = 1; i < int(p.notes.size()); ++i) if (p.notes[size_t(i)].end - p.notes[size_t(i)].start > p.notes[size_t(body)].end - p.notes[size_t(body)].start) body = i;
    check(!p.curveEdit.getToggleState() && p.notes[size_t(body)].end - p.notes[size_t(body)].start > 1, "Direct pitch-curve editing is separate from ordinary whole-note movement");
    p.curveEdit.setToggleState(true, dontSendNotification); p.curveEdit.onClick();
    auto event = [&](Point<float> at, Point<float> origin, int mods = 0) {
        const auto now = Time::getCurrentTime(); return MouseEvent(Desktop::getInstance().getMainMouseSource(), at, ModifierKeys(ModifierKeys::leftButtonModifier | mods),
            1, 0, 0, 0, 0, &grid, &grid, now, origin, now, 1, at != origin);
    };
    auto point = [&](double seconds, double pitch) { return Point<float>(float((seconds - clip.offset) * grid.pixels), float((PitchRollGrid::highest - pitch + 0.5) * grid.row)); };
    const auto a = point(0.9, p.curvePitchAt(body, 0.9)), b = point(1.4, p.curvePitchAt(body, 0.9));
    const auto before = p.notes; e.seek(13.1); e.play(); grid.mouseDown(event(a, a));
    check(grid.interacting() && e.pitchPreviewActive, "Grabbing the pitch line starts audition and protects the editor from automatic scrolling");
    grid.mouseDrag(event(b, a)); check(!p.notes[size_t(body)].curve.empty() && p.hasPendingEdits() && clip.source == original && !clip.pitchOriginal && e.cursor == 13.1,
          "Drawing across vibrato stages a local pitch curve without changing original audio or transport time");
    grid.mouseUp(event(b, a)); AudioBuffer<float> block(2, 512); e.render(block.getArrayOfWritePointers(), 2, 512);
    check(!grid.interacting() && !e.pitchPreviewActive && e.cursor == 13.1, "Releasing a curve stroke ends its preview at the preserved playhead"); e.stop();
    const auto flat = p.notes; const auto target = p.notes[size_t(body)].target;
    const double leftPitch = p.curvePitchAt(body, 0.95), rightPitch = p.curvePitchAt(body, 1.3);
    check(std::abs(leftPitch - rightPitch) < 0.005 && p.notes[size_t(body)].target == before[size_t(body)].target,
          "A horizontal stroke flattens only the drawn pitch region without moving the whole-note target");
    p.undoLocal(); check(p.notes == before, "One local Undo restores an entire pitch-curve stroke"); p.redoLocal(); check(p.notes == flat, "Local Redo restores the drawn pitch curve");
    grid.mouseDown(event(a, a)); grid.mouseDrag(event(a.translated(0, -22), a)); grid.keyPressed(KeyPress(KeyPress::escapeKey));
    check(p.notes == flat && !grid.interacting() && !e.pitchPreviewActive, "Escape cancels an in-progress curve stroke and audition");
    grid.mouseDown(event(a, a)); grid.mouseDrag(event(a.translated(0, -22), a, ModifierKeys::ctrlModifier)); grid.mouseUp(event(a.translated(0, -22), a));
    check(std::abs(p.curvePitchAt(body, 0.9) - flat[size_t(body)].curve[0].note) > 0.01 && std::abs(p.curvePitchAt(body, 0.9) - leftPitch - 0.1) < 0.025,
          "Ctrl-drag adjusts the local curve in cents rather than moving it a full semitone"); p.undoLocal();
    grid.mouseDown(event(a, a)); grid.mouseDrag(event(a.translated(0, -12), a)); grid.focusLost(Component::focusChangedDirectly);
    check(p.notes == flat && !grid.interacting(), "Losing focus cancels a partial curve drag without leaving editing stuck");
    p.selected = {body}; p.moveSelected(0.5);
    check(std::abs(p.notes[size_t(body)].curve[0].note - flat[size_t(body)].curve[0].note - (p.notes[size_t(body)].target - flat[size_t(body)].target)) < 1e-8,
          "Whole-note transposition carries its drawn pitch curve with it"); p.undoLocal();
    const auto valid = p.settings(); check(valid.valid(), "Drawn curve points produce valid source-relative correction metadata");
    auto invalid = valid; for (auto& n : invalid.notes) if (!n.curve.empty()) { n.curve[1].seconds = n.curve[0].seconds; break; }
    check(!invalid.valid(), "Duplicate or unordered pitch curve times are rejected before rendering");
    auto wait = [&] { const double end = Time::getMillisecondCounterHiRes() + 30000; while (w.busy && Time::getMillisecondCounterHiRes() < end) pump(); check(!w.busy, "Pitch-curve processing completes without opening audio hardware"); };
    check(p.apply(), "Applying a directly drawn curve starts background pitch rendering"); wait();
    check(clip.pitchOriginal == original && !p.hasPendingEdits(), "Curve Apply preserves the original recording for reset and future edits");
    auto rendered = clip; rendered.pitchOriginal.reset(); auto f0 = analysePitch(rendered, analyse);
    double low = 100, high = 0; int count = 0;
    for (const auto& f : f0) if (f.voiced && clip.offset + f.seconds >= 1.0 && clip.offset + f.seconds < 1.3) { low = jmin(low, f.note); high = jmax(high, f.note); ++count; }
    directory.getChildFile("pitch-curve-measurements.txt").replaceWithText("Rendered pitch range: " + String(low, 5) + " to " + String(high, 5) + "; drawn target: " + String(leftPitch, 5) + "; frames: " + String(count) + "\n");
    check(count > 15 && high - low < 0.07 && std::abs((low + high) * 0.5 - leftPitch) < 0.08,
          "Rendered audio follows the drawn flat line and suppresses the original vibrato in that region");
    bool unchanged = true;
    for (int ch = 0; ch < 2; ++ch) for (int i = 26000; i < 33000; ++i) unchanged &= clip.source->samples.getSample(ch, i) == original->samples.getSample(ch, i);
    check(unchanged, "Undrawn audio before the stroke remains sample-identical to the original recording");
    body = -1; for (int i = 0; i < int(p.notes.size()); ++i) if (!p.notes[size_t(i)].curve.empty()) body = i;
    check(body >= 0, "Re-analysis restores a drawn curve on its original note");
    p.selected = {body}; check(p.splitSelected(1.15) && p.settings().valid(), "Drawn pitch curves survive note splitting without changing their source timing"); p.apply(); wait();
    auto project = directory.getChildFile("pitch-curve-project.auralis"); e.saveProject(project); Engine loaded; loaded.loadProject(project);
    int savedCurves = 0; for (const auto& n : loaded.tracks[0]->clips[0].pitchSettings.notes) if (!n.curve.empty()) { ++savedCurves; check(std::abs(pitchEditShift(69, 1.1, n) + 69 - leftPitch) < 0.01, "Reloaded curve retains its drawn pitch and source-relative time"); }
    check(savedCurves == 2 && loaded.tracks[0]->clips[0].offset == 0.25 && loaded.tracks[0]->clips[0].start == 12.5, "Project save/reload preserves split curves on moved and trimmed audio parts");
    p.selected = {body}; p.restoreSelected(); check(p.notes[size_t(body)].curve.empty() && !p.notes[size_t(body + 1)].curve.empty(), "Restore selected note clears only its drawn curve"); p.undoLocal(); p.discard();
    e.recording = true; const auto protectedNotes = p.notes; grid.mouseDown(event(a, a)); grid.mouseDrag(event(b, a)); check(p.notes == protectedNotes && !p.apply(), "Recording blocks direct curve edits and application"); e.recording = false;
    p.fit(); if (auto stream = directory.getChildFile("pitch-curve-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(p.createComponentSnapshot(p.getLocalBounds()), *stream); }
    check(w.resetPitchCorrection(e.tracks[0]->id, originalId) && clip.source == original, "Reset pitch correction restores original audio after direct curve editing");
    check(!e.devices.getCurrentAudioDevice() && !loaded.devices.getCurrentAudioDevice(), "Direct pitch-curve tests never open real audio hardware");
    directory.getChildFile("pitch-curve-verification.txt").replaceWithText("Synthetic/offline only. Verified native line grabbing/drawing, scoped vibrato flattening, live preview and preserved transport, stroke undo/redo, Escape/focus cancellation, fine Ctrl-drag, whole-note transposition, metadata validation, background rendering with measured pitch, sample-identical undrawn audio, split/re-analysis/persistence on moved and trimmed parts, selected-note restore, full reset and recording guards.\n");
}
}
