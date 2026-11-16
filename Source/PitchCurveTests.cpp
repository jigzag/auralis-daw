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
    {
        const auto savedUndo = p.undo, savedRedo = p.redo;
        p.notes = before; p.undo.clear(); p.redo.clear(); p.changed();
        grid.mouseDown(event(a, a));
        for (int i = 1; i <= 300; ++i) grid.mouseDrag(event(a + (b - a) * (float(i) / 300), a));
        grid.mouseUp(event(b, a));
        double eventError = 0;
        const auto manyEvents = p.notes;
        for (int i = 0; i <= 100; ++i) {
            const double time = 0.85 + i * 0.006;
            eventError = jmax(eventError, std::abs(pitchEditShift(69, time, manyEvents[size_t(body)]) - pitchEditShift(69, time, flat[size_t(body)])));
        }
        check(eventError < 1e-8 && manyEvents[size_t(body)].curve.size() <= flat[size_t(body)].curve.size() + 2,
              "A 300-event horizontal stroke matches a single-event stroke without cumulative feathering or extra curve points");
        int flatPoints = 0; for (const auto& point : manyEvents[size_t(body)].curve) if (point.seconds >= 0.9 - 1e-6 && point.seconds <= 1.4 + 1e-6) ++flatPoints;
        check(flatPoints <= 3, "A half-second flat gesture stores endpoints instead of dozens of analysis or mouse-event points");
        p.notes = flat; p.undo.clear(); p.redo.clear(); p.changed();
        const auto raised = a.translated(0, -float(grid.row));
        grid.mouseDown(event(a, a)); grid.mouseDrag(event(raised, a)); const auto oneMove = p.notes;
        for (int i = 0; i < 300; ++i) grid.mouseDrag(event(raised, a));
        grid.mouseUp(event(raised, a));
        check(p.notes == oneMove && p.undo.size() == 1, "Holding a vertical drag steady cannot sharpen its curve or accumulate correction over repeated events");
        p.undoLocal(); check(p.notes == flat, "One Undo restores a complete smoothed stroke regardless of event count");
        p.notes = before; p.undo.clear(); p.redo.clear(); p.changed();
        grid.mouseDown(event(a, a));
        for (int i = 1; i <= 300; ++i) {
            auto noisy = a + (b - a) * (float(i) / 300); noisy.y += float(std::sin(i * 1.7) * grid.row * 0.06);
            grid.mouseDrag(event(noisy, a));
        }
        grid.mouseUp(event(b, a));
        double jitter = 0; for (int i = 0; i <= 100; ++i) jitter = jmax(jitter, std::abs(p.curvePitchAt(body, 0.96 + i * 0.0038) - leftPitch));
        check(jitter < 0.045, "Dense small hand jitter is filtered instead of becoming rapid pitch steps");
        bool preserved = true;
        for (const auto& frame : p.frames) if (frame.voiced && clip.offset + frame.seconds < 0.83 && clip.offset + frame.seconds >= before[size_t(body)].start)
            preserved &= std::abs(pitchEditShift(frame.note, clip.offset + frame.seconds, p.notes[size_t(body)])) < 1e-8;
        check(preserved, "Stroke smoothing preserves measured vibrato outside its local transition shoulders");
        p.notes = flat; auto& dense = p.notes[size_t(body)]; dense.curve.clear();
        const double start = dense.start, end = dense.end, denseTarget = dense.target;
        for (double time = start; time < end - 1e-7; time += 0.001)
            dense.curve.push_back({time, dense.target + 0.15 * std::sin((time - start) * MathConstants<double>::twoPi * 5) + 0.08 * std::sin((time - start) * MathConstants<double>::twoPi * 80)});
        dense.curve.push_back({end, dense.target}); dense.edited = true;
        const auto denseBefore = p.notes; p.selected = {body}; p.undo.clear(); p.redo.clear(); p.changed(); p.smoothSelectedCurves();
        const auto smooth = p.notes;
        check(smooth[size_t(body)].curve.size() < denseBefore[size_t(body)].curve.size() / 5 && p.settings().valid(),
              "Smooth selected curve reduces dense saved control points while preserving valid source-relative metadata");
        double squared = 0, lowVibrato = 100, highVibrato = 0; int samples = 0;
        for (double time = start + 0.07; time < end - 0.07; time += 0.001) {
            const double value = p.curvePitchAt(body, time), expected = denseTarget + 0.15 * std::sin((time - start) * MathConstants<double>::twoPi * 5);
            squared += (value - expected) * (value - expected); ++samples; lowVibrato = jmin(lowVibrato, value); highVibrato = jmax(highVibrato, value);
        }
        check(std::sqrt(squared / samples) < 0.025 && highVibrato - lowVibrato > 0.2,
              "Smoothing suppresses rapid pitch chatter while retaining the slower musical vibrato");
        check(smooth[size_t(body)].curve.front() == denseBefore[size_t(body)].curve.front() && smooth[size_t(body)].curve.back() == denseBefore[size_t(body)].curve.back(),
              "Smooth selected curve preserves endpoint pitches and timing");
        p.undoLocal(); check(p.notes == denseBefore, "Undo restores the exact dense curve before smoothing");
        p.redoLocal(); check(p.notes == smooth, "Redo restores the simplified smooth curve");
        e.recording = true; p.smoothSelectedCurves(); check(p.notes == smooth, "Recording blocks curve smoothing"); e.recording = false;
        p.notes = flat; p.undo = savedUndo; p.redo = savedRedo; p.selected = {body}; p.changed();
    }
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
    // Reproduce a stable note followed by an unboxed tail, a silent gap and a
    // separate 20 ms voiced fragment. All times remain relative to the source.
    std::vector<PitchFrame> fragments;
    for (int i = 0; i <= 150; ++i) fragments.push_back({i * 0.01, i >= 70 && i <= 73 ? 72.0 : 69.0, 0, 0.1,
        (i >= 10 && i <= 73) || i == 100 || i == 101});
    auto sloped = fragments; for (int i = 70; i <= 73; ++i) sloped[size_t(i)].note = 72 + (i - 70) * 0.2;
    p.setAnalysis(clip, sloped); grid.pixels = 700; const int oldRow = grid.row; grid.row = 704; p.updateSize();
    const auto betweenFrames = point(0.965, 72.3); grid.mouseDown(event(betweenFrames, betweenFrames));
    check(grid.interacting(), "At maximum vertical zoom an unboxed sloping line is grabbed between analysis frames"); grid.mouseUp(event(betweenFrames, betweenFrames));
    grid.row = oldRow; p.setAnalysis(clip, fragments); p.updateSize();
    const auto boxed = p.notes;
    check(boxed.size() == 1 && !uncoveredPitchRegion(clip, fragments, boxed, 0.65), "Existing note blocks retain their boundaries when exposing unboxed pitch lines");
    const auto tail = uncoveredPitchRegion(clip, fragments, boxed, 0.96);
    check(tail && std::abs(tail->start - 0.945) < 1e-7 && std::abs(tail->end - 0.985) < 1e-7,
          "An unboxed voiced tail is editable up to its actual detection boundary without overlapping the preceding note");
    check(!uncoveredPitchRegion(clip, fragments, boxed, 1.1) && !uncoveredPitchRegion(clip, fragments, boxed, 0.1)
          && !uncoveredPitchRegion(clip, fragments, boxed, 1.8), "Silence and times outside a trimmed part never create a pitch region");
    const auto tailPoint = point(0.96, 72), tailMoved = tailPoint.translated(0, -float(grid.row));
    grid.mouseDown(event(tailPoint, tailPoint)); grid.mouseUp(event(tailPoint, tailPoint));
    check(p.notes == boxed && !p.hasPendingEdits() && p.undo.empty(), "Clicking an unboxed line without dragging leaves no edit or phantom note");
    grid.mouseDown(event(tailPoint, tailPoint)); grid.mouseDrag(event(tailMoved, tailPoint)); grid.keyPressed(KeyPress(KeyPress::escapeKey));
    check(p.notes == boxed && !p.hasPendingEdits() && !grid.interacting(), "Escape removes a newly created tail region and its partial curve");
    grid.mouseDown(event(tailPoint, tailPoint)); grid.mouseDrag(event(tailMoved, tailPoint)); grid.focusLost(Component::focusChangedDirectly);
    check(p.notes == boxed && !p.hasPendingEdits(), "Focus cancellation restores an unboxed region without leaving stale note indices");
    grid.mouseDown(event(tailPoint, tailPoint));
    check(grid.interacting() && e.pitchPreviewActive, "Grabbing a line outside a note block starts direct editing and drag audition");
    grid.mouseDrag(event(tailMoved, tailPoint)); grid.mouseUp(event(tailMoved, tailPoint));
    const auto editedTail = p.notes;
    check(p.notes.size() == 2 && p.notes[0] == boxed[0] && !p.notes[1].curve.empty() && p.settings().valid()
          && std::abs(p.curvePitchAt(1, 0.96) - 73) < 0.01, "Dragging an unboxed tail stages its own curve while preserving the neighbouring note");
    p.undoLocal(); check(p.notes == boxed && !p.hasPendingEdits(), "A single Undo removes both the unboxed region and its curve stroke");
    p.redoLocal(); check(p.notes == editedTail, "Redo restores an unboxed curve with its source-relative boundaries");
    const auto silent = point(1.1, 69); grid.mouseDown(event(silent, silent)); grid.mouseDrag(event(silent.translated(0, -22), silent)); grid.mouseUp(event(silent, silent));
    check(p.notes == editedTail && !grid.interacting(), "Dragging true silence cannot fabricate a detected pitch or alter adjacent curves");
    const auto island = point(1.25, 69), islandMoved = island.translated(0, -float(grid.row));
    grid.mouseDown(event(island, island)); grid.mouseDrag(event(islandMoved, island, ModifierKeys::ctrlModifier)); grid.mouseUp(event(islandMoved, island));
    check(p.notes.size() == 3 && std::abs(p.curvePitchAt(2, 1.25) - 69.1) < 0.025 && p.settings().valid(),
          "A standalone pitch fragment shorter than note detection supports fine Ctrl-drag editing");
    const auto fragmentSettings = p.settings(); auto saved = clip; saved.pitchSettings = fragmentSettings;
    const auto restoredFragments = pitchSegments(saved, fragments);
    check(restoredFragments.size() == 3 && restoredFragments[1].curve == p.notes[1].curve && restoredFragments[2].curve == p.notes[2].curve,
          "Re-analysis reconstructs saved unboxed tails and short islands instead of discarding their curves");
    check(p.apply(), "Applying unboxed curve edits renders the original source in the background"); wait();
    bool shortChanged = false, surroundingDry = true;
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < original->samples.getNumSamples(); ++i) {
        const double time = i / original->rate;
        const bool different = clip.source->samples.getSample(ch, i) != original->samples.getSample(ch, i);
        if (time >= 1.245 && time < 1.265) shortChanged |= different;
        if (time < 0.93 || (time > 1.0 && time < 1.23) || time > 1.28) surroundingDry &= !different;
    }
    check(shortChanged && surroundingDry && clip.pitchOriginal == original, "A 20 ms fragment changes rendered samples while all surrounding undrawn audio stays identical");
    check(p.notes.size() == 3 && !p.hasPendingEdits() && p.notes[1].curve == fragmentSettings.notes[0].curve,
          "Apply retains unboxed curve regions in the editor after refreshing its baseline");
    auto fragmentProject = directory.getChildFile("pitch-unboxed-project.auralis"); e.saveProject(fragmentProject); Engine fragmentLoaded; fragmentLoaded.loadProject(fragmentProject);
    auto reopenedSettings = fragmentLoaded.tracks[0]->clips[0].pitchSettings, expectedSettings = fragmentSettings;
    bool sameCurves = reopenedSettings.notes.size() == expectedSettings.notes.size();
    for (size_t i = 0; sameCurves && i < expectedSettings.notes.size(); ++i) {
        const auto& a = reopenedSettings.notes[i]; const auto& b = expectedSettings.notes[i];
        sameCurves = std::abs(a.start - b.start) < 1e-12 && std::abs(a.end - b.end) < 1e-12
            && std::abs(a.original - b.original) < 1e-12 && std::abs(a.target - b.target) < 1e-12 && a.scoop == b.scoop && a.curve.size() == b.curve.size();
        for (size_t j = 0; sameCurves && j < b.curve.size(); ++j) sameCurves = std::abs(a.curve[j].seconds - b.curve[j].seconds) < 1e-12 && std::abs(a.curve[j].note - b.curve[j].note) < 1e-12;
    }
    reopenedSettings.notes.clear(); expectedSettings.notes.clear();
    check(sameCurves && reopenedSettings == expectedSettings && fragmentLoaded.tracks[0]->clips[0].offset == 0.25
          && fragmentLoaded.tracks[0]->clips[0].start == 12.5, "Saving and reopening a moved, trimmed part preserves unboxed pitch edits");
    if (auto stream = directory.getChildFile("pitch-unboxed-preview.png").createOutputStream()) {
        p.viewport.setViewPosition(420, int((PitchRollGrid::highest - 73) * grid.row));
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(p.createComponentSnapshot(p.getLocalBounds()), *stream);
    }
    directory.getChildFile("pitch-unboxed-verification.txt").replaceWithText("Synthetic/offline only. Verified direct grabbing of unboxed voiced tails and isolated short fragments, silence/trim boundaries, click without mutation, Escape/focus cancellation, scoped edits and audition, Undo/Redo, fine Ctrl-drag, re-analysis/Apply/project persistence, and rendering of a 20 ms edit with sample-identical surrounding audio. No audio hardware opened.\n");
    directory.getChildFile("pitch-smoothing-verification.txt").replaceWithText("Synthetic/offline only. Verified event-count-independent stroke reconstruction, sparse flat curves, stable stationary drags, filtered hand jitter, preserved undrawn vibrato, dense saved-curve smoothing with retained 5 Hz vibrato and suppressed 80 Hz chatter, endpoint preservation, Undo/Redo and recording guards. Existing offline pitch render and persistence checks also pass.\n");
    check(!e.devices.getCurrentAudioDevice() && !loaded.devices.getCurrentAudioDevice(), "Direct pitch-curve tests never open real audio hardware");
    directory.getChildFile("pitch-curve-verification.txt").replaceWithText("Synthetic/offline only. Verified native line grabbing/drawing, scoped vibrato flattening, live preview and preserved transport, stroke undo/redo, Escape/focus cancellation, fine Ctrl-drag, whole-note transposition, metadata validation, background rendering with measured pitch, sample-identical undrawn audio, split/re-analysis/persistence on moved and trimmed parts, selected-note restore, full reset and recording guards.\n");
}
}
