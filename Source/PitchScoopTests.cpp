#include "PitchRoll.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace auralis {
void runPitchScoopTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, const String& name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto source = std::make_shared<SourceAudio>(); source->samples.setSize(2, 96000); double phase = 0;
    for (int i = 0; i < 96000; ++i) {
        const double time = i / 48000.0;
        const double note = 69 - 1.8 * jlimit(0.0, 1.0, 1 - (time - 0.15) / 0.35) + (time > 0.7 ? 0.1 * std::sin(MathConstants<double>::twoPi * 5 * time) : 0);
        phase += MathConstants<double>::twoPi * 440 * std::pow(2.0, (note - 69) / 12) / 48000;
        const double envelope = jlimit(0.0, 1.0, (time - 0.15) / 0.008) * jlimit(0.0, 1.0, (1.8 - time) / 0.008);
        const float sample = float(envelope * (0.18 * std::sin(phase) + 0.04 * std::sin(2 * phase)));
        source->samples.setSample(0, i, sample); source->samples.setSample(1, i, sample * 0.5f);
    }
    source->buildWaveform(); source->file = directory.getChildFile("pitch-scoop-original.wav");
    { auto stream = source->file.createOutputStream(); stream->setPosition(0); stream->truncate(); WavAudioFormat wav;
      std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 48000, 2, 32, {}, 0)); stream.release();
      check(writer && writer->writeFromAudioSampleBuffer(source->samples, 0, 96000), "Scoop regression fixture contains an actual rising attack and later vibrato"); }
    auto frequency = [](const AudioBuffer<float>& audio, double start, double length, double target) {
        constexpr int size = 65536; dsp::FFT fft(16); std::vector<float> data(size * 2, 0); const int first = int(start * 48000), count = jmin(int(length * 48000), audio.getNumSamples() - first);
        for (int i = 0; i < count; ++i) data[size_t(i)] = audio.getSample(0, first + i) * float(0.5 - 0.5 * std::cos(MathConstants<double>::twoPi * i / (count - 1)));
        fft.performFrequencyOnlyForwardTransform(data.data(), true); int peak = int(target * 0.85 * size / 48000);
        for (int i = peak + 1; i < int(target * 1.15 * size / 48000); ++i) if (data[size_t(i)] > data[size_t(peak)]) peak = i;
        const double a = std::log(jmax(1e-20f, data[size_t(peak - 1)])), b = std::log(jmax(1e-20f, data[size_t(peak)])), c = std::log(jmax(1e-20f, data[size_t(peak + 1)]));
        return (peak + (a - c) / (2 * (a - 2 * b + c))) * 48000 / size;
    };
    Clip clip; clip.source = source; clip.length = 2;
    PitchCorrectionSettings settings; settings.manual = true; settings.amount = 1; settings.speedMs = 0;
    PitchNoteEdit edit {0.15, 1.8, 69, 69, {true, 0.15, 0, 400, 1}}; settings.notes = {edit};
    check(settings.valid(), "A zero-depth scoop correction is valid and distinct from preserving the original attack");
    const auto flat = renderPitchCorrection(clip, settings, directory.getChildFile("pitch-scoop-flat.wav"));
    check(flat.source != source && flat.source->samples.getNumSamples() == 96000
          && std::abs(1200 * std::log2(frequency(flat.source->samples, 0.25, 0.15, 440) / 440)) < 15,
          "Zero scoop depth removes an existing recorded scoop while keeping the audio duration unchanged");
    bool dryTail = true, stereo = true;
    for (int i = 0; i < 96000; ++i) {
        if (i < 6500 || (i >= 35000 && i < 81000)) for (int ch = 0; ch < 2; ++ch) dryTail &= flat.source->samples.getSample(ch, i) == source->samples.getSample(ch, i);
        stereo &= std::abs(flat.source->samples.getSample(1, i) - flat.source->samples.getSample(0, i) * 0.5f) < 1e-6f;
    }
    check(dryTail && stereo, "Scoop processing preserves the later vibrato, untouched audio and linked stereo balance");
    settings.notes[0].scoop.depthCents = 200; settings.notes[0].scoop.durationMs = 600;
    const auto shaped = renderPitchCorrection(clip, settings, directory.getChildFile("pitch-scoop-shaped.wav"));
    auto analysedClip = clip; analysedClip.source = shaped.source; auto measured = analysePitch(analysedClip, PitchCorrectionSettings{});
    double early = 0, late = 0; int earlyCount = 0, lateCount = 0;
    for (const auto& f : measured) if (f.voiced) { if (f.seconds >= 0.28 && f.seconds <= 0.36) { early += f.note; ++earlyCount; } if (f.seconds >= 0.62 && f.seconds <= 0.68) { late += f.note; ++lateCount; } }
    check(earlyCount > 5 && lateCount > 3 && early / earlyCount < 67.9 && late / lateCount > 68.5 && late / lateCount > early / earlyCount + 0.65,
          "Rendered depth and duration create the requested rising pitch contour in the original recording");
    auto bent = settings.notes[0]; const double middle = bent.scoop.start + bent.scoop.durationMs * 0.0005;
    const double linear = 69 + pitchEditShift(69, middle, bent); bent.scoop.curve = 2;
    check(69 + pitchEditShift(69, middle, bent) > linear + 0.4 && pitchEditShift(69.1, 1.2, bent) == 0,
          "Rise-speed adjustment changes the attack contour and leaves the stable note untouched");
    auto trimmed = clip; trimmed.offset = 0.35; trimmed.length = 1.2;
    auto trimmedFrames = analysePitch(trimmed, settings); double trimPitch = 0; int trimCount = 0;
    for (const auto& f : trimmedFrames) if (f.voiced && f.seconds >= 0.12 && f.seconds < 0.2) { trimPitch += f.note + f.shift; ++trimCount; }
    check(trimCount > 3 && trimPitch / trimCount > 68.0, "Trimming the part start retains the original scoop time instead of restarting its low attack");
    auto invalid = settings; invalid.notes[0].scoop.depthCents = 1201; check(!invalid.valid(), "Out-of-range scoop depth is rejected");
    invalid = settings; invalid.notes[0].scoop.durationMs = std::numeric_limits<double>::quiet_NaN(); check(!invalid.valid(), "Non-finite scoop timing is rejected before audio processing");
    auto pump = [] {
       #if JUCE_WINDOWS
        MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
       #endif
        Thread::sleep(1);
    };
    auto settle = [&] { const double end = Time::getMillisecondCounterHiRes() + 130; while (Time::getMillisecondCounterHiRes() < end) pump(); };
    Workstation w(false); w.stopTimer(); auto& e = w.engine; e.importAudio(source->file, 0, 12.5);
    auto& current = e.tracks[0]->clips[0]; PitchRollWindow window(w, e.tracks[0]->id, current.id, current, analysePitch(current, settings)); window.setAlpha(0);
    auto& p = window.panel(); p.stopTimer(); const int detected = int(p.notes.size());
    check(detected >= 2, "Scoop editor exposes the detected semitone fragments of a recorded rising attack");
    for (int i = 0; i < detected; ++i) p.selected.insert(i);
    check(p.mergeSelected() && p.notes.size() == 1, "Adjacent detected fragments can be joined into one note for scoop editing");
    p.undoLocal(); check(int(p.notes.size()) == detected, "Joining attack fragments supports local Undo"); p.redoLocal(); p.selected = {0};
    const auto before = p.notes; p.editScoop(); check(p.scoopPrompt != nullptr, "A selected note opens depth, duration, rise-speed and loop-preview controls");
    auto* dialog = p.scoopPrompt.getComponent(); dialog->setAlpha(0); auto* controls = dialog->getCustomComponent(0); dynamic_cast<Timer*>(controls)->stopTimer();
    auto slider = [&](const char* id) { return dynamic_cast<Slider*>(controls->findChildWithID(id)); };
    check(slider("scoopDepth") && slider("scoopDuration") && slider("scoopCurve"), "Scoop adjustments use editable sliders with numerical values");
    slider("scoopDepth")->setValue(0, sendNotificationSync); slider("scoopDuration")->setValue(400, sendNotificationSync);
    auto* preview = dynamic_cast<TextButton*>(controls->findChildWithID("scoopPreview")); preview->setToggleState(true, dontSendNotification); preview->onClick();
    check(e.pitchPreviewActive && p.notes == before && current.source->file == source->file, "Scoop dialog auditions its temporary curve without committing staged edits or changing the source");
    AudioBuffer<float> audition(2, 16000); AudioBuffer<float> block(2, 128);
    for (int at = 0; at < 16000; at += 128) { e.render(block.getArrayOfWritePointers(), 2, 128); for (int ch = 0; ch < 2; ++ch) audition.copyFrom(ch, at, block, ch, 0, jmin(128, 16000 - at)); }
    check(std::abs(1200 * std::log2(frequency(audition, 0.08, 0.15, 440) / 440)) < 15, "Immediate loop audition follows the temporary zero-depth contour before Apply");
    slider("scoopDepth")->setValue(200, sendNotificationSync); slider("scoopCurve")->setValue(2, sendNotificationSync);
    check(e.pitchPreviewActive && p.notes == before, "Moving scoop sliders retunes a running preview without changing the note until confirmation");
    if (auto stream = directory.getChildFile("pitch-scoop-dialog-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(dialog->createComponentSnapshot(dialog->getLocalBounds()), *stream); }
    dialog->triggerButtonClick(String::fromUTF8("キャンセル")); settle();
    check(!p.scoopPrompt && !e.pitchPreviewActive && p.notes == before, "Cancel stops scoop audition and keeps the previous note settings");
    p.editScoop(); dialog = p.scoopPrompt.getComponent(); dialog->setAlpha(0); controls = dialog->getCustomComponent(0);
    slider("scoopDepth")->setValue(100, sendNotificationSync); slider("scoopDuration")->setValue(400, sendNotificationSync); slider("scoopCurve")->setValue(2, sendNotificationSync);
    dialog->triggerButtonClick(String::fromUTF8("設定")); settle();
    check(p.notes[0].scoop.enabled && p.notes[0].scoop.depthCents == 100 && p.notes[0].scoop.durationMs == 400 && p.notes[0].scoop.curve == 2 && p.hasPendingEdits(),
          "Confirming scoop settings stages the chosen depth, length and rise speed");
    const auto staged = p.notes; p.undoLocal(); check(p.notes == before, "Scoop settings support local Undo before Apply"); p.redoLocal(); check(p.notes == staged, "Scoop settings support local Redo");
    check(p.apply(), "Scoop Apply starts background correction from the original audio");
    auto wait = [&] { const double end = Time::getMillisecondCounterHiRes() + 30000; while (w.busy && Time::getMillisecondCounterHiRes() < end) pump(); check(!w.busy, "Scoop background correction completes offline"); }; wait();
    check(!p.hasPendingEdits() && current.pitchOriginal && current.pitchSettings.notes.size() == 1 && p.notes.size() == 1 && p.notes[0].scoop.enabled,
          "Apply retains the joined note and scoop settings after re-analysis");
    p.selected = {0}; const double anchor = p.notes[0].scoop.start;
    check(p.splitSelected(p.notes[0].start + 0.031) && !p.notes[0].scoop.enabled && p.notes[1].scoop.start == anchor && p.settings().valid(),
          "Splitting before the audible scoop onset leaves a valid unchanged leading fragment"); p.undoLocal(); p.selected = {0};
    check(p.splitSelected(anchor + 0.25) && p.notes[0].scoop.start == anchor && p.notes[1].scoop.start == anchor && p.settings().valid(),
          "Splitting a scoop preserves its source-relative onset on both sides without restarting the bend");
    p.apply(); wait(); auto project = directory.getChildFile("pitch-scoop-project.auralis"); e.saveProject(project); Engine loaded; loaded.loadProject(project);
    const auto& saved = loaded.tracks[0]->clips[0];
    check(saved.pitchSettings.notes.size() == 2 && saved.pitchSettings.notes[0].scoop.enabled
          && std::abs(saved.pitchSettings.notes[0].scoop.start - current.pitchSettings.notes[0].scoop.start) < 1e-9
          && saved.pitchSettings.notes[0].scoop.depthCents == 100 && saved.pitchSettings.notes[0].scoop.durationMs == 400
          && std::abs(saved.pitchSettings.notes[1].scoop.start - anchor) < 1e-9 && saved.pitchSettings.notes[0].scoop.curve == 2,
          "Project save/reload preserves split scoop curves, timing and original audio identity");
    p.selected = {0}; p.restoreSelected(); check(!p.notes[0].scoop.enabled && p.notes[1].scoop.enabled, "Restore selected note removes only its scoop adjustment"); p.undoLocal(); p.discard();
    e.recording = true; p.editScoop(); check(!p.scoopPrompt && !p.apply(), "Recording blocks scoop edits and rendering"); e.recording = false;
    p.fit(); if (auto stream = directory.getChildFile("pitch-scoop-curve-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(p.createComponentSnapshot(p.getLocalBounds()), *stream); }
    check(!e.devices.getCurrentAudioDevice() && !loaded.devices.getCurrentAudioDevice(), "Scoop editing and audio verification never open real audio hardware");
    directory.getChildFile("pitch-scoop-verification.txt").replaceWithText("Synthetic/offline only. Verified flattening actual recorded scoops, rendered rising contours, later vibrato and stereo preservation, curve speed, source-relative trimming/splits, numerical validation, fragment merge/undo, native slider preview/cancel/confirm, temporary live curves, local undo/redo, background Apply, project persistence, note restore and recording guards.\n");
}
}
