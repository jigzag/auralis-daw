#include "Interface.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace auralis {
void runNormalizeTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto approx = [](double a, double b) { return std::abs(a - b) < 1e-5; };
    auto write = [&](String name, const AudioBuffer<float>& samples) {
        File file = directory.getChildFile(name); auto stream = file.createOutputStream(); stream->setPosition(0); stream->truncate();
        WavAudioFormat wav; std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 48000, 2, 32, {}, 0)); stream.release();
        if (!writer || !writer->writeFromAudioSampleBuffer(samples, 0, samples.getNumSamples())) throw std::runtime_error("Normalization fixture write failed");
        return file;
    };
    AudioBuffer<float> source(2, 2000);
    for (int i = 0; i < 2000; ++i) {
        source.setSample(0, i, i >= 500 && i < 1500 ? 0.1f : 0.9f);
        source.setSample(1, i, i >= 500 && i < 1500 ? -0.25f : -0.8f);
    }
    File fixture = write("normalize-source.wav", source);
    MemoryBlock originalFile; fixture.loadFileAsData(originalFile);
    Workstation w(false); w.stopTimer(); auto& e = w.engine;
    e.importAudio(fixture, 0, 0); e.importAudio(fixture, 0, 1); e.master = 1; e.tracks[0]->gain = 1;
    const String trackId = e.tracks[0]->id, clipId = e.tracks[0]->clips[0].id;
    auto clip = [&]() -> Clip& { return e.tracks[0]->clips[0]; };
    clip().offset = 500.0 / 48000; clip().length = 1000.0 / 48000; clip().fadeIn = clip().fadeOut = 0; clip().gainDb = 6;
    auto immutable = clip().source; w.refresh(); w.dirty = false;
    check(w.normalizeClip(trackId, clipId) && approx(clip().gainDb, -1 - 20 * std::log10(0.25)),
          "Normalization measures both channels in the trimmed part and replaces existing clip gain");
    check(w.dirty && w.undo.size() == 1 && clip().source == immutable && approx(e.tracks[0]->clips[1].gainDb, 0),
          "Normalization is one non-destructive undoable edit independent of sibling parts");
    AudioBuffer<float> out(2, 128); e.seek(0); e.play(); e.render(out.getArrayOfWritePointers(), 2, 128); e.stop();
    const double target = std::pow(10.0, -1.0 / 20);
    check(approx(out.getSample(1, 40), -target) && approx(out.getSample(0, 40) / out.getSample(1, 40), -0.4),
          "Normalized playback reaches the requested sample peak and preserves stereo balance");
    check(!w.normalizeClip(trackId, clipId) && w.undo.size() == 1, "Repeated normalization is stable and adds no redundant undo step");
    w.undoEdit(); check(approx(clip().gainDb, 6) && w.undo.empty(), "Undo restores the complete pre-normalization part level");
    auto pump = [] {
        const auto end = Time::getMillisecondCounterHiRes() + 140;
        while (Time::getMillisecondCounterHiRes() < end) {
           #if JUCE_WINDOWS
            MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
           #endif
            Thread::sleep(1);
        }
    };
    auto dismiss = [&](int result, String value) {
        auto* prompt = w.normalizePrompt.getComponent(); if (!prompt) throw std::runtime_error("Missing normalize dialog");
        prompt->setAlpha(0); prompt->getTextEditor("target")->setText(value, false); prompt->triggerButtonClick(String::fromUTF8(result ? "適用" : "キャンセル")); pump();
    };
    w.showNormalizeDialog(trackId, clipId);
    check(w.normalizePrompt && w.normalizePrompt->getTextEditorContents("target") == "-1.0", "Native normalization dialog defaults to minus one dBFS");
    if (auto stream = directory.getChildFile("normalize-dialog-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(w.normalizePrompt->createComponentSnapshot(w.normalizePrompt->getLocalBounds()), *stream);
    }
    dismiss(1, "-6"); check(approx(clip().gainDb, -6 - 20 * std::log10(0.25)), "Dialog Apply normalizes the intended part to a custom target");
    const float previousGain = clip().gainDb; const auto previousUndo = w.undo.size();
    w.showNormalizeDialog(trackId, clipId); dismiss(0, "0");
    check(clip().gainDb == previousGain && w.undo.size() == previousUndo, "Cancelling normalization leaves audio and undo unchanged");
    w.showNormalizeDialog(trackId, clipId); dismiss(1, "-1oops");
    check(clip().gainDb == previousGain && w.undo.size() == previousUndo, "Malformed normalization dialog input cannot change the part");
    check(!w.normalizeClip(trackId, clipId, 1) && !w.normalizeClip(trackId, clipId, -61)
          && !w.normalizeClip(trackId, clipId, std::numeric_limits<double>::quiet_NaN()) && !w.normalizeClip(trackId, "missing"),
          "Normalization rejects invalid targets and stale clip IDs");
    e.recording = true; check(!w.normalizeClip(trackId, clipId), "Recording blocks normalization edits"); e.recording = false;
    w.busy = true; check(!w.normalizeClip(trackId, clipId), "Background operations block normalization edits"); w.busy = false;
    w.normalizeClip(trackId, clipId, -24);
    check(clip().gainDb < 0 && approx(clip().gainDb, -24 - 20 * std::log10(0.25)), "Normalization attenuates peaks above the requested target");
    w.normalizeClip(trackId, clipId, 0);
    check(approx(clip().gainDb, -20 * std::log10(0.25)), "Zero dBFS is accepted as an exact normalization target");
    MemoryBlock unchangedFile; fixture.loadFileAsData(unchangedFile);
    check(originalFile == unchangedFile && approx(immutable->samples.getSample(1, 600), -0.25), "Normalization preserves source file bytes and immutable sample data");
    AudioBuffer<float> quiet(2, 2000); for (int ch = 0; ch < 2; ++ch) FloatVectorOperations::fill(quiet.getWritePointer(ch), 0.001f, 2000);
    e.importAudio(write("normalize-quiet.wav", quiet), 0, 2); const String quietId = e.tracks[0]->clips.back().id;
    check(w.normalizeClip(trackId, quietId) && e.tracks[0]->clips.back().gainDb > 58.9f,
          "Quiet recordings can normalize beyond the previous plus twelve dB gain limit");
    File project = directory.getChildFile("normalize.auralis"); e.saveProject(project); Engine loaded; loaded.loadProject(project);
    check(approx(loaded.tracks[0]->clips.back().gainDb, e.tracks[0]->clips.back().gainDb), "Project reload preserves high-gain normalization precisely");
    File exported = directory.getChildFile("normalize-mix.wav"); e.exportWav(exported, 2, 2.03, 32);
    std::unique_ptr<AudioFormatReader> reader(e.formats.createReaderFor(exported)); AudioBuffer<float> actual(2, 1440);
    check(reader && reader->read(&actual, 0, 1440, 0, true, true) && approx(actual.getSample(0, 600), target), "WAV export retains normalized peak levels");
    AudioBuffer<float> silence(2, 2000); silence.clear(); e.importAudio(write("normalize-silence.wav", silence), 0, 3);
    const auto undoBeforeSilence = w.undo.size();
    check(!w.normalizeClip(trackId, e.tracks[0]->clips.back().id) && e.tracks[0]->clips.back().gainDb == 0 && w.undo.size() == undoBeforeSilence,
          "Silent parts are skipped without creating gain or undo changes");
    e.addTrack("MIDI", true); auto midiId = e.addMidiClip(1, 0).id;
    check(!w.normalizeClip(e.tracks[1]->id, midiId), "Normalization rejects MIDI parts");
    w.showNormalizeDialog(trackId, quietId); e.tracks[0]->clips.erase(e.tracks[0]->clips.begin() + 2); dismiss(1, "-3");
    check(e.tracks[0]->clips.size() == 3 && e.tracks[0]->clips.back().gainDb == 0, "A stale normalization dialog cannot retarget another part after deletion");
    directory.getChildFile("normalize-verification.txt").replaceWithText("Synthetic/offline audio only. Verified trimmed stereo sample peaks, default/custom/zero targets, linked stereo gain, existing gain replacement, attenuation, idempotence, undo, unchanged source bytes, native dialog Apply/Cancel/validation, invalid/stale/MIDI targets, recording/busy guards, quiet high-gain audio, persistence, WAV export, silent input and stale dialog identity.\n");
}
}
