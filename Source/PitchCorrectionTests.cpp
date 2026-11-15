#include "Interface.h"
#include "PitchCorrection.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace auralis {
void runPitchCorrectionTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto cents = [](double hz, double expected) { return 1200 * std::log2(hz / expected); };
    auto make = [&](String name, double frequency, bool harmonics = false, double rate = 48000.0, double secondFrequency = 0) {
        auto source = std::make_shared<SourceAudio>(); source->rate = rate; source->samples.setSize(2, int(rate * 2));
        for (int i = 0; i < source->samples.getNumSamples(); ++i) {
            const double time = i / rate, phase = MathConstants<double>::twoPi * (secondFrequency > 0 ? frequency * jmin(time, 1.0) + secondFrequency * jmax(0.0, time - 1.0) : frequency * time);
            const float envelope = float(jlimit(0.0, 1.0, (time - 0.2) / 0.005) * jlimit(0.0, 1.0, (1.8 - time) / 0.005));
            const float value = envelope * float(0.2 * std::sin(phase) + (harmonics ? 0.27 * std::sin(2 * phase) + 0.08 * std::sin(3 * phase) : 0));
            source->samples.setSample(0, i, value); source->samples.setSample(1, i, value * 0.5f);
        }
        source->file = directory.getChildFile(name); auto stream = source->file.createOutputStream(); stream->setPosition(0); stream->truncate();
        WavAudioFormat wav; std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(stream.get(), rate, 2, 32, {}, 0)); stream.release();
        if (!writer || !writer->writeFromAudioSampleBuffer(source->samples, 0, source->samples.getNumSamples())) throw std::runtime_error("Pitch fixture write failed");
        writer.reset(); source->buildWaveform(); return source;
    };
    auto frequency = [](const SourceAudio& audio, double low, double high, double start = 0.3, double length = 1.35) {
        constexpr int size = 65536; dsp::FFT fft(16); std::vector<float> data(size * 2, 0);
        const int first = int(start * audio.rate), count = jmin(size, int(length * audio.rate), audio.samples.getNumSamples() - first);
        for (int i = 0; i < count; ++i) data[size_t(i)] = audio.samples.getSample(0, first + i) * float(0.5 - 0.5 * std::cos(MathConstants<double>::twoPi * i / (count - 1)));
        fft.performFrequencyOnlyForwardTransform(data.data(), true);
        int peak = jmax(1, int(std::ceil(low * size / audio.rate)));
        for (int i = peak + 1; i <= int(std::floor(high * size / audio.rate)); ++i) if (data[size_t(i)] > data[size_t(peak)]) peak = i;
        const double a = std::log(jmax(1e-20f, data[size_t(peak - 1)])), b = std::log(jmax(1e-20f, data[size_t(peak)])), c = std::log(jmax(1e-20f, data[size_t(peak + 1)]));
        const double delta = (a - c) / (2 * (a - 2 * b + c)); return (peak + jlimit(-0.5, 0.5, delta)) * audio.rate / size;
    };
    auto peakError = [&](const std::shared_ptr<SourceAudio>& audio, double target) { return std::abs(cents(frequency(*audio, target * 0.95, target * 1.05), target)); };
    PitchCorrectionSettings settings; settings.amount = 1; settings.speedMs = 0; settings.preserveFormants = false;
    Clip clip; clip.source = make("pitch-detuned.wav", 440 * std::pow(2.0, 35.0 / 1200)); clip.length = 2; clip.fadeIn = clip.fadeOut = 0;
    MemoryBlock originalBytes; clip.source->file.loadFileAsData(originalBytes);
    auto measured = analysePitch(clip, settings); double note = 0; int voiced = 0;
    for (const auto& f : measured) if (f.voiced && f.seconds > 0.4 && f.seconds < 1.5) { note += f.note; ++voiced; }
    check(voiced > 80 && std::abs(note / voiced - 69.35) < 0.03, "Pitch analysis detects detuned single-note fundamentals in cents");
    const auto wet = renderPitchCorrection(clip, settings, directory.getChildFile("pitch-corrected.wav"));
    check(wet.correctedFrames > 80 && peakError(wet.source, 440) < 3, "Chromatic correction moves a detuned note to A4 within three cents");
    check(wet.source->samples.getNumSamples() == clip.source->samples.getNumSamples() && wet.source->rate == clip.source->rate,
          "Pitch correction preserves exact source frame count and sample rate");
    const float dryRms = clip.source->samples.getRMSLevel(0, 24000, 24000), wetRms = wet.source->samples.getRMSLevel(0, 24000, 24000);
    check(wetRms / dryRms > 0.7f && wetRms / dryRms < 1.3f && wet.source->samples.getMagnitude(0, 0, 4800) < 1e-4f,
          "Latency-compensated pitch rendering retains level and the initial silent region");
    auto stereo = wet.source->samples; stereo.addFrom(1, 0, stereo, 0, 0, stereo.getNumSamples(), -0.5f);
    check(stereo.getMagnitude(1, 12000, 60000) < 1e-4f, "Stereo pitch correction preserves linked channel balance");
    auto lowClip = clip; lowClip.source = make("pitch-guitar.wav", 82.406889 * std::pow(2.0, 27.0 / 1200), true);
    auto low = renderPitchCorrection(lowClip, settings, directory.getChildFile("pitch-guitar-corrected.wav"));
    check(peakError(low.source, 82.406889) < 5, "Low guitar fundamentals correct accurately even with a stronger second harmonic");
    auto formants = settings; formants.preserveFormants = true;
    auto formantResult = renderPitchCorrection(lowClip, formants, directory.getChildFile("pitch-guitar-formants.wav"));
    check(peakError(formantResult.source, 82.406889) < 5, "Formant-preserving pitch correction retains accurate low fundamentals");
    auto melody = clip; melody.source = make("pitch-melody.wav", 440 * std::pow(2.0, 35.0 / 1200), false, 48000, 523.251131 * std::pow(2.0, -30.0 / 1200));
    auto melodyResult = renderPitchCorrection(melody, settings, directory.getChildFile("pitch-melody-corrected.wav"));
    check(std::abs(cents(frequency(*melodyResult.source, 420, 460, 0.35, 0.5), 440)) < 3
          && std::abs(cents(frequency(*melodyResult.source, 500, 540, 1.2, 0.5), 523.251131)) < 3,
          "Melody correction follows note changes while keeping each note at its original time");
    auto partialSettings = settings; partialSettings.amount = 0.5;
    auto half = renderPitchCorrection(clip, partialSettings, directory.getChildFile("pitch-half.wav"));
    check(peakError(half.source, 440 * std::pow(2.0, 17.5 / 1200)) < 3, "Correction amount blends pitch displacement rather than doubling dry and wet audio");
    auto fixed = settings; fixed.targetNote = 70;
    auto fixedResult = renderPitchCorrection(clip, fixed, directory.getChildFile("pitch-target.wav"));
    check(peakError(fixedResult.source, 466.163762) < 3, "A user-selected target note corrects to the requested note instead of the nearest semitone");
    auto restricted = settings; restricted.maxCents = 10;
    auto limited = renderPitchCorrection(clip, restricted, directory.getChildFile("pitch-limit.wav"));
    check(limited.correctedFrames == 0 && limited.source == clip.source, "Maximum deviation guard skips notes outside the requested correction range");
    auto scaleClip = clip; scaleClip.source = make("pitch-scale.wav", 349.228231 * std::pow(2.0, 30.0 / 1200));
    auto major = settings; major.root = 2; major.scale = 1;
    auto majorResult = renderPitchCorrection(scaleClip, major, directory.getChildFile("pitch-d-major.wav"));
    check(peakError(majorResult.source, 369.994423) < 3, "Root and major scale select an allowed note in D major");
    scaleClip.source = make("pitch-minor.wav", 329.627557 * std::pow(2.0, -20.0 / 1200));
    auto minor = settings; minor.scale = 2;
    auto minorResult = renderPitchCorrection(scaleClip, minor, directory.getChildFile("pitch-c-minor.wav"));
    check(peakError(minorResult.source, 311.126984) < 3, "Natural minor correction follows its own scale notes");
    auto referenceClip = clip; referenceClip.source = make("pitch-442.wav", 442 * std::pow(2.0, 35.0 / 1200), false, 44100);
    auto reference = settings; reference.referenceHz = 442;
    auto referenceResult = renderPitchCorrection(referenceClip, reference, directory.getChildFile("pitch-442-corrected.wav"));
    check(peakError(referenceResult.source, 442) < 3 && referenceResult.source->rate == 44100, "Custom A4 tuning and 44.1 kHz audio retain accurate pitch");
    auto trimmed = clip; trimmed.offset = 0.5; trimmed.length = 0.75;
    auto trimResult = renderPitchCorrection(trimmed, settings, directory.getChildFile("pitch-trimmed.wav"));
    bool untouched = true;
    for (int ch = 0; ch < 2; ++ch) for (int i : {12000, 23999, 60000, 72000}) untouched &= trimResult.source->samples.getSample(ch, i) == clip.source->samples.getSample(ch, i);
    check(untouched, "Processing a trimmed part leaves hidden source samples unchanged");
    auto repeated = clip; repeated.source = wet.source; repeated.pitchOriginal = clip.source;
    auto rerender = renderPitchCorrection(repeated, fixed, directory.getChildFile("pitch-rerender.wav"));
    check(peakError(rerender.source, 466.163762) < 3, "Reapplying correction always starts from original audio without cumulative processing");
    auto silent = clip; silent.source = make("pitch-silent.wav", 0); bool rejected = false;
    try { renderPitchCorrection(silent, settings, directory.getChildFile("pitch-silent-result.wav")); } catch (...) { rejected = true; }
    check(rejected && !directory.getChildFile("pitch-silent-result.wav").existsAsFile(), "Unpitched silence fails safely without publishing corrected audio");
    Workstation w(false); w.stopTimer(); auto& e = w.engine; e.importAudio(clip.source->file, 0, 0); e.importAudio(clip.source->file, 0, 3); w.refresh();
    auto current = [&]() -> Clip& { return e.tracks[0]->clips[0]; };
    const String trackId = e.tracks[0]->id, clipId = current().id; const auto before = current();
    check(w.installPitchCorrection(trackId, clipId, before, settings, wet.source) && current().pitchOriginal == before.source && current().id == clipId
          && current().start == before.start && current().offset == before.offset && current().length == before.length && current().gainDb == before.gainDb
          && !e.tracks[0]->clips[1].pitchOriginal, "Installing correction changes only its target's audio while preserving timing, gain, identity and sibling parts");
    auto project = directory.getChildFile("pitch-project.auralis"); e.saveProject(project); Engine loaded; loaded.loadProject(project);
    check(loaded.tracks[0]->clips[0].pitchOriginal && loaded.tracks[0]->clips[0].pitchSettings.amount == 1
          && peakError(loaded.tracks[0]->clips[0].source, 440) < 3 && peakError(loaded.tracks[0]->clips[0].pitchOriginal, 440 * std::pow(2.0, 35.0 / 1200)) < 3,
          "Project reload preserves both processed and original audio plus correction settings");
    auto invalidProject = JSON::parse(project.loadFileAsString());
    invalidProject["tracks"][0]["clips"][0]["pitchCorrection"].getDynamicObject()->setProperty("scale", 99);
    auto invalidFile = directory.getChildFile("pitch-invalid.auralis"); invalidFile.replaceWithText(JSON::toString(invalidProject));
    const auto retained = loaded.tracks[0]->clips[0].source; bool invalidRejected = false;
    try { loaded.loadProject(invalidFile); } catch (...) { invalidRejected = true; }
    check(invalidRejected && loaded.tracks[0]->clips[0].source == retained, "Invalid saved correction settings preserve the current project on load failure");
    w.undoEdit(); check(!current().pitchOriginal && current().source == before.source, "One undo restores untouched original audio after correction");
    w.installPitchCorrection(trackId, clipId, current(), settings, wet.source);
    check(w.resetPitchCorrection(trackId, clipId) && !current().pitchOriginal && current().source == before.source,
          "Reset correction restores the original source without changing the part");
    w.undoEdit(); check(current().pitchOriginal && current().source == wet.source, "Undo can restore a removed pitch correction");
    const auto correctedSnapshot = current();
    check(!w.installPitchCorrection(trackId, "stale", correctedSnapshot, fixed, fixedResult.source), "Stale processing targets cannot change another part");
    e.recording = true; check(!w.startPitchCorrection(trackId, clipId, settings) && !w.resetPitchCorrection(trackId, clipId), "Recording blocks pitch processing and reset"); e.recording = false;
    auto badSettings = settings; badSettings.referenceHz = std::numeric_limits<double>::quiet_NaN();
    check(!w.startPitchCorrection(trackId, clipId, badSettings), "Invalid pitch settings cannot start processing");
    e.master = 1; e.tracks[0]->gain = 1; current().fadeIn = current().fadeOut = 0;
    e.exportWav(directory.getChildFile("pitch-export.wav"), 0, 2, 32);
    auto exported = e.readAudio(directory.getChildFile("pitch-export.wav"));
    check(peakError(exported, 440) < 3, "WAV export uses the corrected source audio");
    e.exportMp3(directory.getChildFile("pitch-export.mp3"), 0, 2);
    check(peakError(e.readAudio(directory.getChildFile("pitch-export.mp3")), 440) < 4, "MP3 export retains the corrected pitch");
    auto pump = [&] {
       #if JUCE_WINDOWS
        MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
       #endif
        Thread::sleep(1);
    };
    w.showPitchCorrectionDialog(trackId, clipId);
    check(w.pitchPrompt && w.pitchPrompt->getComboBoxComponent("target")->getNumItems() == 74, "Native pitch dialog exposes automatic scales and individual target notes");
    if (auto stream = directory.getChildFile("pitch-correction-dialog-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(w.pitchPrompt->createComponentSnapshot(w.pitchPrompt->getLocalBounds()), *stream);
    }
    auto dismiss = [&](String button) {
        w.pitchPrompt->setAlpha(0); w.pitchPrompt->triggerButtonClick(button);
        const double until = Time::getMillisecondCounterHiRes() + 120; while (Time::getMillisecondCounterHiRes() < until) pump();
    };
    const auto unchanged = current().source;
    dismiss(String::fromUTF8("キャンセル")); check(current().source == unchanged && !w.busy, "Cancel pitch dialog leaves audio unchanged");
    w.showPitchCorrectionDialog(trackId, clipId); w.pitchPrompt->getTextEditor("amount")->setText("bad", false); dismiss(String::fromUTF8("適用"));
    check(current().source == unchanged && !w.busy, "Malformed pitch dialog input cannot start processing");
    w.showPitchCorrectionDialog(trackId, clipId); w.pitchPrompt->getTextEditor("amount")->setText("100", false); w.pitchPrompt->getTextEditor("speed")->setText("0", false);
    w.pitchPrompt->getComboBoxComponent("target")->setSelectedItemIndex(70 - 23, dontSendNotification); dismiss(String::fromUTF8("適用"));
    const double timeout = Time::getMillisecondCounterHiRes() + 30000; while (w.busy && Time::getMillisecondCounterHiRes() < timeout) pump();
    check(!w.busy && current().pitchSettings.targetNote == 70 && peakError(current().source, 466.163762) < 4,
          "Native dialog Apply completes background processing and installs its selected note");
    MemoryBlock unchangedBytes; clip.source->file.loadFileAsData(unchangedBytes);
    check(unchangedBytes == originalBytes && e.devices.getCurrentAudioDevice() == nullptr, "Pitch tests preserve original recording bytes and never open audio hardware");
    Engine old; old.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis")); bool compatible = true;
    for (const auto& track : old.tracks) for (const auto& part : track->clips) compatible &= !part.pitchOriginal;
    check(compatible, "Older projects open without pitch correction metadata");
    directory.getChildFile("pitch-correction-verification.txt").replaceWithText("Synthetic/offline only. Verified fundamental/cents analysis, stereo balance, low guitar with strong harmonics, chromatic/major/minor/fixed-note targets, correction strength, deviation guard, custom A4/44.1 kHz, compensated duration/silence, trimmed source preservation, original-based reprocessing, silent rejection, stable identities, undo/reset, both sources and settings persistence, recording/invalid guards, WAV/MP3, real dialog Cancel/validation/background Apply and old project compatibility.\n");
}
}
