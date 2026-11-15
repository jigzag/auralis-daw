#include "PitchRoll.h"
#include <cmath>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace auralis {
void runPitchRollTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto approx = [](double a, double b) { return std::abs(a - b) < 0.015; };
    auto pump = [] {
       #if JUCE_WINDOWS
        MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
       #endif
        Thread::sleep(1);
    };
    Workstation w(false); w.stopTimer(); auto& e = w.engine; e.importAudio(directory.getChildFile("pitch-melody.wav"), 0, 0); w.refresh();
    const auto original = e.tracks[0]->clips[0].source; const String clipId = e.tracks[0]->clips[0].id;
    MemoryBlock originalBytes; original->file.loadFileAsData(originalBytes);
    auto wait = [&] { const auto end = Time::getMillisecondCounterHiRes() + 30000; while (w.busy && Time::getMillisecondCounterHiRes() < end) pump(); check(!w.busy, "Pitch editor background work finishes without opening audio hardware"); };
    auto event = [&](Component& target, Point<float> p, Point<float> start, int modifiers = 0, int clicks = 1) {
        const auto now = Time::getCurrentTime(); return MouseEvent(Desktop::getInstance().getMainMouseSource(), p, ModifierKeys(ModifierKeys::leftButtonModifier | modifiers),
            1, 0, 0, 0, 0, &target, &target, now, start, now, clicks, p != start);
    };
    auto point = Point<float>(w.timeline.xAt(0.5), float(Timeline::ruler + 45)); w.timeline.mouseDoubleClick(event(w.timeline, point, point, 0, 2)); wait();
    check(w.pitchWindows.size() == 1 && w.pitchWindows[0]->clipId == clipId, "Double-clicking an audio part opens its own piano-style pitch editor");
    auto& window = *w.pitchWindows[0]; window.setAlpha(0); auto& p = window.panel(); p.stopTimer(); auto& grid = p.grid;
    check(p.notes.size() >= 2 && grid.getWantsKeyboardFocus(), "Pitch editor detects individual notes and owns its editing shortcuts");
    auto find = [&](double pitch) { for (int i = 0; i < int(p.notes.size()); ++i) if (std::abs(p.notes[size_t(i)].original - pitch) < 0.2 && p.notes[size_t(i)].end - p.notes[size_t(i)].start > 0.25) return i; throw std::runtime_error("Pitch editor did not find the expected melody note"); };
    int first = find(69.35), second = find(71.7); const auto initial = p.notes;
    auto click = [&](int index, int modifiers = 0) { const auto q = grid.noteBounds(index).getCentre(); grid.mouseDown(event(grid, q, q, modifiers)); grid.mouseUp(event(grid, q, q, modifiers)); };
    click(first); check(p.selected == std::set<int>{first} && !p.hasPendingEdits() && w.undo.empty(), "Selecting a detected audio note does not edit the project or create undo history");
    const auto at = grid.noteBounds(first).getCentre(), moved = at.translated(45, -float(grid.row));
    grid.mouseDown(event(grid, at, at)); grid.mouseDrag(event(grid, moved.translated(0, 4), at)); grid.mouseDrag(event(grid, moved, at)); grid.mouseUp(event(grid, moved, at));
    check(approx(p.notes[size_t(first)].target, 70) && p.notes[size_t(first)].start == initial[size_t(first)].start && p.notes[size_t(first)].end == initial[size_t(first)].end
          && p.notes[size_t(second)] == initial[size_t(second)] && p.hasPendingEdits(), "Vertical note dragging edits one pitch without moving its time or modifying another note");
    check(e.tracks[0]->clips[0].source == original && !e.tracks[0]->clips[0].pitchOriginal && w.undo.empty(), "Unapplied pitch edits leave the recorded audio untouched");
    auto* peer = grid.getPeer(); check(peer && peer->handleKeyPress(KeyPress('Z', ModifierKeys::ctrlModifier, 0)) && p.notes == initial && !p.hasPendingEdits()
          && e.tracks[0]->name == "Audio 1", "Native Ctrl+Z undoes a complete note drag without changing the track name");
    peer->handleKeyPress(KeyPress('Y', ModifierKeys::ctrlModifier, 0)); check(approx(p.notes[size_t(first)].target, 70), "Native redo restores a staged note pitch");
    p.undoLocal(); click(first); const auto fineAt = grid.noteBounds(first).getCentre(), fineTo = fineAt.translated(0, -float(grid.row * 2));
    grid.mouseDown(event(grid, fineAt, fineAt, ModifierKeys::ctrlModifier)); grid.mouseDrag(event(grid, fineTo, fineAt, ModifierKeys::ctrlModifier)); grid.mouseUp(event(grid, fineTo, fineAt, ModifierKeys::ctrlModifier));
    check(approx(p.notes[size_t(first)].target, initial[size_t(first)].target + 0.2) && p.selected.contains(first), "Ctrl-drag supports fine cent adjustments on an already selected audio note");
    p.undoLocal(); click(first); click(second, ModifierKeys::ctrlModifier);
    check(p.selected.contains(first) && p.selected.contains(second), "Ctrl-click selects multiple detected audio notes");
    p.moveSelected(1); check(approx(p.notes[size_t(first)].target, initial[size_t(first)].target + 1) && approx(p.notes[size_t(second)].target, initial[size_t(second)].target + 1), "Selected audio notes can transpose together while retaining their relative pitches");
    p.undoLocal(); click(first);
    const double oldPixels = grid.pixels, time = grid.noteBounds(first).getCentreX() / oldPixels;
    const auto wheelAt = grid.noteBounds(first).getCentre(); MouseWheelDetails wheel; wheel.deltaY = 0.2f;
    grid.mouseWheelMove(event(grid, wheelAt, wheelAt, ModifierKeys::ctrlModifier), wheel);
    check(grid.pixels > oldPixels && std::abs((p.viewport.getViewPositionX() + wheelAt.x) / grid.pixels - time) < 0.005, "Pitch editor Ctrl+wheel zoom preserves the pointer's source time");
    p.fit(); p.numericPitch(); check(p.numericPrompt != nullptr, "An individual audio note exposes a musical-note and cents dialog");
    p.numericPrompt->getComboBoxComponent("note")->setSelectedItemIndex(70 - 24, dontSendNotification); p.numericPrompt->getTextEditor("cents")->setText("12", false);
    auto dismiss = [&](String button) { p.numericPrompt->setAlpha(0); p.numericPrompt->triggerButtonClick(TRANS(button)); const auto end = Time::getMillisecondCounterHiRes() + 120; while (Time::getMillisecondCounterHiRes() < end) pump(); };
    dismiss(String::fromUTF8("設定")); check(approx(p.notes[size_t(first)].target, 70.12), "Numeric note editing accepts an exact note plus cents");
    const auto staged = p.notes; p.numericPitch(); dismiss(String::fromUTF8("キャンセル")); check(p.notes == staged, "Cancelling numeric pitch entry leaves staged notes unchanged");
    p.numericPitch(); p.numericPrompt->getTextEditor("cents")->setText("bad", false); dismiss(String::fromUTF8("設定")); check(p.notes == staged, "Malformed per-note numeric input cannot corrupt note targets");
    bool closed = false; w.requestClose([&] { closed = true; });
    auto guarded = directory.getChildFile("unapplied-" + Uuid().toString() + ".auralis"); w.saveToFile(guarded);
    check(!closed && !guarded.existsAsFile() && p.hasPendingEdits(), "Project close and save preserve unapplied note edits and return to the editor");
    check(peer->handleKeyPress(KeyPress('S', ModifierKeys::ctrlModifier, 0)) && p.message.contains(String::fromUTF8("変更を破棄")) && p.hasPendingEdits(),
          "Native Ctrl+S explains the required Apply or Discard action inside the pitch editor");
    e.recording = true; const auto beforeRecording = p.notes; p.moveSelected(1); check(p.notes == beforeRecording && !p.apply(), "Recording prevents staged pitch edits and rendering"); e.recording = false;
    check(p.apply(), "Per-note Apply starts background rendering"); wait();
    auto& corrected = e.tracks[0]->clips[0];
    check(!p.hasPendingEdits() && corrected.pitchOriginal == original && corrected.pitchSettings.manual && corrected.pitchSettings.notes.size() == 1
          && approx(corrected.pitchSettings.notes[0].target, 70.12) && corrected.id == clipId && corrected.start == 0 && corrected.length == 2,
          "Apply commits the individual note target, source identity, original recording and unchanged part timing");
    auto estimate = [](const SourceAudio& audio, double target) {
        constexpr int size = 65536; dsp::FFT fft(16); std::vector<float> data(size * 2, 0); const int count = int(audio.rate * 0.5), first = int(audio.rate * 0.35);
        for (int i = 0; i < count; ++i) data[size_t(i)] = audio.samples.getSample(0, first + i) * float(0.5 - 0.5 * std::cos(MathConstants<double>::twoPi * i / (count - 1)));
        fft.performFrequencyOnlyForwardTransform(data.data(), true); int k = int(target * 0.95 * size / audio.rate);
        for (int i = k + 1; i < int(target * 1.05 * size / audio.rate); ++i) if (data[size_t(i)] > data[size_t(k)]) k = i;
        const double a = std::log(jmax(1e-20f, data[size_t(k - 1)])), b = std::log(jmax(1e-20f, data[size_t(k)])), c = std::log(jmax(1e-20f, data[size_t(k + 1)]));
        return (k + (a - c) / (2 * (a - 2 * b + c))) * audio.rate / size;
    };
    const double targetHz = 440 * std::pow(2.0, (70.12 - 69) / 12);
    check(std::abs(1200 * std::log2(estimate(*corrected.source, targetHz) / targetHz)) < 5, "Rendered selected note reaches the manually requested pitch within five cents");
    bool unchanged = true; for (int ch = 0; ch < 2; ++ch) for (int i = 57600; i < 80000; ++i) unchanged &= corrected.source->samples.getSample(ch, i) == original->samples.getSample(ch, i);
    check(unchanged, "Unselected melody notes remain sample-identical to the original recording");
    const auto editedSource = corrected.source; w.undoEdit(); p.timerCallback();
    check(e.tracks[0]->clips[0].source == original && !e.tracks[0]->clips[0].pitchOriginal && !p.hasPendingEdits() && !p.stale, "Project undo restores the original audio and refreshes the pitch editor after Apply");
    first = find(69.35); second = find(71.7); p.selected = {first}; p.moveSelected(70.12 - p.notes[size_t(first)].target); p.apply(); wait();
    first = find(69.35); p.selected = {first}; const double splitTime = (p.notes[size_t(first)].start + p.notes[size_t(first)].end) * 0.5;
    check(p.splitSelected(splitTime) && p.notes[size_t(first)].end == splitTime && p.notes[size_t(first + 1)].start == splitTime, "Detected notes can be split for independent pitch editing without cutting the audio part");
    p.moveSelected(0.5); p.apply(); wait();
    auto project = directory.getChildFile("pitch-roll-project.auralis"); w.saveToFile(project); Engine loaded; loaded.loadProject(project);
    const auto& saved = loaded.tracks[0]->clips[0];
    check(saved.pitchSettings.manual && saved.pitchSettings.notes.size() == 2 && saved.pitchSettings.notes[0].end == saved.pitchSettings.notes[1].start,
          "Project reload preserves per-note targets and manual split boundaries");
    const auto savedFrames = analysePitch(saved, saved.pitchSettings); const auto savedNotes = pitchSegments(saved, savedFrames);
    check(savedNotes.size() >= 3 && savedNotes[0].edited && savedNotes[1].edited && approx(savedNotes[0].target, 70.12) && approx(savedNotes[1].target, 70.62),
          "Re-analysis restores edited note blocks and individual target pitches from the project");
    auto invalid = saved.pitchSettings; invalid.notes[1].start = invalid.notes[0].start; check(!invalid.valid(), "Overlapping manual note instructions are rejected before rendering");
    auto trimmed = saved; trimmed.offset = 0.45; trimmed.length = 1.3;
    auto trimResult = renderPitchCorrection(trimmed, trimmed.pitchSettings, directory.getChildFile("pitch-roll-trim.wav"));
    check(trimResult.source && trimResult.correctedFrames > 0 && saved.pitchSettings.notes[0].start < trimmed.offset, "Manual note coordinates remain valid after trimming the source start");
    first = find(69.35); p.selected = {first}; p.moveSelected(0.01); window.closeButtonPressed();
    auto* closePrompt = dynamic_cast<AlertWindow*>(Component::getCurrentlyModalComponent()); check(closePrompt != nullptr, "Closing a pitch editor with pending changes asks before discarding them");
    closePrompt->setAlpha(0); closePrompt->triggerButtonClick(TRANS(String::fromUTF8("戻る")));
    const auto modalEnd = Time::getMillisecondCounterHiRes() + 140; while (Time::getMillisecondCounterHiRes() < modalEnd) pump();
    check(window.isVisible() && p.hasPendingEdits(), "Cancelling editor close keeps unapplied note edits available");
    p.discard(); p.fit(); p.selected = {first}; p.showSelectedMessage();
    if (auto stream = directory.getChildFile("pitch-roll-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(p.createComponentSnapshot(p.getLocalBounds()), *stream); }
    MemoryBlock unchangedBytes; original->file.loadFileAsData(unchangedBytes);
    check(unchangedBytes == originalBytes && e.devices.getCurrentAudioDevice() == nullptr, "Per-note editing preserves recording file bytes and never streams real test audio");
    p.selected = {first}; p.moveSelected(0.01); e.tracks[0]->clips[0].offset = 0.1; p.timerCallback();
    check(p.stale && !p.apply(), "A changed source range cannot silently receive stale note edits");
    e.tracks[0]->clips[0].offset = 0; p.timerCallback(); p.discard();
    const String movedTrack = e.addTrack("Moved melody").id;
    e.tracks.back()->clips.push_back(e.tracks[0]->clips[0]); e.tracks[0]->clips.clear();
    check(p.resolve() && p.targetsTrack(movedTrack) && !p.stale, "Pitch editor resolves its original clip identity after moving it to another track");
    directory.getChildFile("pitch-roll-verification.txt").replaceWithText("Synthetic/offline only. Verified audio double-click opening, detected note blocks, native Ctrl+Z/redo, one-note dragging with unchanged timing and neighbors, cent drag, multi-selection, anchored zoom, numeric Apply/Cancel/validation, pending-edit save/close guards, recording/stale guards, background Apply, spectral target accuracy, sample-identical unselected notes, project undo, note split persistence and reload, trimmed source coordinates, close cancellation, original bytes and stable identity after track transfer.\n");
}
}
