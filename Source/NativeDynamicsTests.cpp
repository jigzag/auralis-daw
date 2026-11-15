#include "Compressor.h"
#include "Tuner.h"
#include "Interface.h"
#include "PluginInfo.h"
#include <limits>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace auralis {
void runNativeDynamicsTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, const String& name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    MidiBuffer midi;
    auto constant = [&](Compressor& comp, float level, int samples, int block = 128) {
        float last = 0; double stereoError = 0;
        for (int at = 0; at < samples; at += block) { const int count = jmin(block, samples - at); AudioBuffer<float> b(2, count);
            for (int i = 0; i < count; ++i) { b.setSample(0, i, level); b.setSample(1, i, level * 0.5f); }
            comp.processBlock(b, midi); last = b.getSample(0, count - 1);
            for (int i = 0; i < count; ++i) stereoError = jmax(stereoError, std::abs(double(b.getSample(1, i)) - b.getSample(0, i) * 0.5));
        }
        if (stereoError > 1e-6) throw std::runtime_error("Compressor changed stereo balance"); return double(last);
    };
    Compressor comp; auto s = comp.settings(); s.threshold = -18; s.ratio = 4; s.attackMs = 1; s.releaseMs = 50; s.knee = 0; comp.setSettings(s); comp.reset();
    const double expected = -18 + (20 * std::log10(0.5) + 18) / 4;
    check(comp.getParameters().size() == 8 && comp.getLatencySamples() == 0, "Compressor exposes eight persistent controls with zero buffering latency");
    check(std::abs(20 * std::log10(constant(comp, 0.5f, 48000)) - expected) < 0.01 && std::abs(comp.reductionDb() - (20 * std::log10(0.5) - expected)) < 0.01,
          "Measured compressor ratio and gain-reduction meter agree at steady state while retaining stereo balance");
    comp.reset(); check(std::abs(constant(comp, 0.01f, 24000) - 0.01) < 1e-7, "Signals below the compressor threshold retain their original level");
    s.ratio = 1; comp.setSettings(s); comp.reset(); check(std::abs(constant(comp, 0.5f, 1000) - 0.5) < 1e-6, "A one-to-one compressor ratio is transparent");
    s.ratio = 4; s.knee = 12; comp.setSettings(s); comp.reset();
    check(std::abs(comp.transferDb(-18) + 19.125) < 0.0001 && std::abs(20 * std::log10(constant(comp, Decibels::decibelsToGain(-18.0f), 24000)) + 19.125) < 0.01,
          "Soft knee starts compression gradually and matches the displayed transfer curve");
    s.knee = 0; s.makeup = 6; comp.setSettings(s); comp.reset(); check(std::abs(20 * std::log10(constant(comp, 0.5f, 24000)) - expected - 6) < 0.01, "Compressor makeup gain is applied after gain reduction");
    s.makeup = 0; s.mix = 50; comp.setSettings(s); comp.reset(); const double parallel = 0.25 + 0.5 * std::pow(10.0, expected / 20);
    check(std::abs(constant(comp, 0.5f, 24000) - parallel) < 1e-6, "Parallel mix blends the unprocessed signal with compressed audio");
    s.mix = 0; comp.setSettings(s); check(std::abs(constant(comp, 0.5f, 1000) - 0.5) < 1e-6, "Zero-percent mix restores dry audio after a short smoothing ramp");
    s.mix = 100; s.bypass = true; comp.setSettings(s); check(std::abs(constant(comp, 0.5f, 1000) - 0.5) < 1e-6 && comp.reductionDb() == 0, "Compressor bypass restores dry sound and clears displayed reduction");
    s.bypass = false; s.attackMs = 1; comp.setSettings(s); comp.reset(); constant(comp, 0.5f, 240); const auto fastAttack = comp.reductionDb();
    s.attackMs = 100; comp.setSettings(s); comp.reset(); constant(comp, 0.5f, 240); check(fastAttack > 8 && comp.reductionDb() < 0.01, "Attack time controls how quickly a new transient is compressed");
    s.attackMs = 1; s.releaseMs = 20; comp.setSettings(s); comp.reset(); constant(comp, 0.5f, 24000); constant(comp, 0.02f, 4800); const auto fastRelease = comp.reductionDb();
    s.releaseMs = 500; comp.setSettings(s); comp.reset(); constant(comp, 0.5f, 24000); constant(comp, 0.02f, 4800); check(fastRelease < 0.01 && comp.reductionDb() > 7, "Release time controls recovery after a loud passage");
    for (double rate : {8000., 44100., 48000., 96000.}) for (int block : {64, 512}) { comp.prepareToPlay(rate, block);
        check(std::abs(20 * std::log10(constant(comp, 0.5f, int(rate * 0.5), block)) - expected) < 0.01 && comp.getLatencySamples() == 0,
              String(rate, 0) + " Hz compressor is accurate at " + String(block) + " samples without extra latency"); }
    AudioBuffer<float> invalid(2, 128); invalid.clear(); invalid.setSample(0, 2, std::numeric_limits<float>::quiet_NaN()); invalid.setSample(1, 3, std::numeric_limits<float>::infinity()); comp.processBlock(invalid, midi);
    bool finite = true; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) finite &= std::isfinite(invalid.getSample(ch, i)); check(finite, "Invalid input samples do not contaminate compressor output");
    MemoryBlock state, cloneState; comp.getStateInformation(state); Compressor clone; clone.setStateInformation(state.getData(), int(state.getSize())); clone.getStateInformation(cloneState);
    check(state == cloneState, "Compressor settings survive a complete state round-trip");
    auto rejected = comp.settings(); rejected.ratio = 0; check(!comp.setSettings(rejected), "Out-of-range compressor settings are rejected without modifying parameters");
    const String badState("{\"kind\":\"auralis:compressor:v1\",\"values\":[0,0,0,0,0,0,0,2]}"); comp.setStateInformation(badState.toRawUTF8(), badState.getNumBytesAsUTF8()); comp.getStateInformation(cloneState);
    check(state == cloneState, "Malformed native-effect state cannot partially modify existing settings");

    Tuner tuner; tuner.prepareToPlay(48000, 128);
    auto tone = [&](double hz, double rate, int samples, int block = 128, int mode = 0) {
        bool transparent = true; float lastPeak = 0;
        for (int at = 0; at < samples; at += block) { const int count = jmin(block, samples - at); AudioBuffer<float> b(2, count);
            for (int i = 0; i < count; ++i) { const float v = float(0.2 * std::sin(MathConstants<double>::twoPi * hz * (at + i) / rate));
                b.setSample(0, i, mode == 1 ? v * 0.01f : v); b.setSample(1, i, mode == 2 ? -v : v); }
            AudioBuffer<float> before; before.makeCopyOf(b); tuner.processBlock(b, midi); lastPeak = b.getMagnitude(0, count);
            if (!tuner.muted()) for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < count; ++i) transparent &= b.getSample(ch, i) == before.getSample(ch, i);
        }
        return std::pair(transparent, lastPeak);
    };
    auto awaitPitch = [&](double expectedHz) { for (int i = 0; i < 55; ++i) { const auto r = tuner.reading(); if (r.frequency > 0 && std::abs(1200 * std::log2(r.frequency / expectedHz)) < 3) return true; Thread::sleep(5); } return false; };
    check(tuner.getLatencySamples() == 0 && tuner.getParameters().size() == 3, "Tuner has three persistent controls and adds no audio buffering");
    bool allTransparent = true;
    for (double hz : {30.867706, 41.203445, 82.406889, 110., 440., 880.}) { tuner.reset(); allTransparent &= tone(hz, 48000, 38400).first;
        check(awaitPitch(hz), "Background tuner detects " + String(hz, 3) + " Hz within three cents, including low bass strings"); }
    check(allTransparent, "Tuner passes unmuted stereo audio through bit for bit");
    tuner.reset(); const auto offsetHz = 440 * std::pow(2.0, 17.0 / 1200); tone(offsetHz, 48000, 38400);
    check(awaitPitch(offsetHz) && std::abs(1200 * std::log2(tuner.reading().frequency / 440) - 17) < 2, "Tuner resolves small tuning offsets in cents");
    tuner.reset(); tone(440, 48000, 38400, 128, 2); check(awaitPitch(440), "Opposite-phase stereo input does not cancel the tuner's analysis signal");
    tuner.reset(); tone(110, 48000, 38400, 64, 1); check(awaitPitch(110), "Auto input selects the stronger stereo channel at a 64-sample host buffer");
    tuner.reset();
    for (int at = 0; at < 38400; at += 128) { AudioBuffer<float> harmonics(2, 128); for (int i = 0; i < 128; ++i) { const double phase = MathConstants<double>::twoPi * 110 * (at + i) / 48000; const float sample = float(0.15 * std::sin(phase) + 0.3 * std::sin(phase * 2) + 0.1 * std::sin(phase * 3)); for (int ch = 0; ch < 2; ++ch) harmonics.setSample(ch, i, sample); } tuner.processBlock(harmonics, midi); }
    check(awaitPitch(110), "Tuner follows the fundamental when a guitar-like second harmonic is louder");
    tuner.setInputChannel(1); tuner.reset(); tone(110, 48000, 38400, 128, 1);
    check(awaitPitch(110), "Explicit left-channel analysis retains quiet input rather than switching channels");
    tuner.setInputChannel(2); tuner.setReferenceHz(442); tuner.setMuted(true); tuner.reset(); const auto mutedResult = tone(440, 48000, 38400);
    check(mutedResult.second == 0 && awaitPitch(440), "Mute tuning silences the output while pitch analysis continues");
    check(std::abs(1200 * std::log2(tuner.reading().frequency / tuner.referenceHz()) + 7.8514) < 2, "Changing the A4 reference changes the displayed cents without retuning the source");
    tuner.getStateInformation(state); Tuner tunerClone; tunerClone.setStateInformation(state.getData(), int(state.getSize())); tunerClone.getStateInformation(cloneState);
    check(state == cloneState && tunerClone.referenceHz() == 442 && tunerClone.inputChannel() == 2 && tunerClone.muted(), "Tuner project state preserves reference pitch, channel and mute without persisting detected notes");
    tuner.setMuted(false); tone(0, 48000, 38400); Thread::sleep(100); check(tuner.reading().frequency == 0, "Silence clears the last detected note");
    for (double rate : {44100., 96000.}) { tuner.prepareToPlay(rate, 128); tone(220, rate, int(rate * 0.8)); check(awaitPitch(220), String(rate, 0) + " Hz tuner correctly accounts for its analysis sample rate"); }
    tuner.reset(); check(tuner.reading().frequency == 0, "Transport reset immediately clears the tuner's reading");
    tone(220, 96000, 76800); check(awaitPitch(220), "Tuner recovers after resetting during active analysis"); Thread::sleep(370); check(tuner.reading().frequency == 0, "Bypassed or stopped processing cannot leave a stale note on the tuner display"); tuner.releaseResources();

    Workstation w(false); w.stopTimer(); auto& e = w.engine; const auto trackId = e.tracks[0]->id;
    check(w.knownPlugins.getTypes().isEmpty() && w.trackInsertAction(trackId, {}, 6), "Track Add opens the native tuner without scanning VST plugins"); w.pluginWindows.back()->setAlpha(0);
    auto* trackTuner = dynamic_cast<Tuner*>(e.tracks[0]->inserts[0]->instance.get()); auto* tunerEditor = w.pluginWindows.back()->getContentComponent();
    auto* reference = dynamic_cast<Slider*>(tunerEditor->findChildWithID("tunerReference")); auto* input = dynamic_cast<ComboBox*>(tunerEditor->findChildWithID("tunerChannel")); auto* mute = dynamic_cast<ToggleButton*>(tunerEditor->findChildWithID("tunerMute"));
    check(trackTuner && reference && input && mute && describePlugin(e.tracks[0]->inserts[0]->description).kind == String::fromUTF8("チューナー"), "Native tuner editor exposes reference, channel and mute with a clear plugin purpose");
    reference->setValue(442, sendNotificationSync); input->setSelectedId(3, sendNotificationSync); mute->setToggleState(true, dontSendNotification); mute->onClick();
    check(trackTuner->referenceHz() == 442 && trackTuner->inputChannel() == 2 && trackTuner->muted(), "Tuner GUI controls update the real processor");
    auto* standard = dynamic_cast<TextButton*>(tunerEditor->findChildWithID("tuner440")); standard->onClick(); check(trackTuner->referenceHz() == 440 && trackTuner->muted(), "The 440 Hz button resets reference pitch without unexpectedly unmuting the input"); trackTuner->setMuted(false);
    tunerEditor->setSize(620, 420); check(tunerEditor->getLocalBounds().contains(reference->getBounds()) && tunerEditor->getLocalBounds().contains(mute->getBounds()) && tunerEditor->getLocalBounds().contains(input->getBounds()), "Tuner controls remain accessible at the minimum editor size"); tunerEditor->setSize(760, 470);
    check(w.trackInsertAction(trackId, {}, 7), "Track Add opens the native compressor without a plugin scan"); w.pluginWindows.back()->setAlpha(0);
    auto* trackComp = dynamic_cast<Compressor*>(e.tracks[0]->inserts[1]->instance.get()); auto* compEditor = w.pluginWindows.back()->getContentComponent();
    auto* threshold = dynamic_cast<Slider*>(compEditor->findChildWithID("compThreshold")); auto* mix = dynamic_cast<Slider*>(compEditor->findChildWithID("compMix"));
    check(trackComp && threshold && mix && compEditor->findChildWithID("compressorGraph"), "Compressor editor contains numeric controls and a live transfer graph"); threshold->setValue(-24, sendNotificationSync); mix->setValue(75, sendNotificationSync);
    compEditor->setSize(720, 560); check(compEditor->getLocalBounds().contains(threshold->getBounds()) && compEditor->getLocalBounds().contains(mix->getBounds()), "Compressor controls remain accessible at the minimum editor size"); compEditor->setSize(860, 650);
    check(std::abs(trackComp->settings().threshold + 24) < 0.01 && trackComp->settings().mix == 75, "Compressor GUI edits change threshold and parallel mix immediately");
    w.markProjectSaved(); constant(*trackComp, 0.5f, 48000); check(w.pluginStateSnapshot() == w.savedPluginState, "Compressor meter activity does not mark the project as modified");
    auto* reset = dynamic_cast<TextButton*>(compEditor->findChildWithID("compReset")); reset->onClick(); check(w.pluginStateSnapshot() != w.savedPluginState && trackComp->settings().threshold == -18, "Compressor Reset restores defaults and parameter changes are detected as unsaved");
    check(w.trackInsertAction({}, {}, 6) && w.trackInsertAction({}, {}, 7), "MASTER Add also supports the native tuner and compressor"); for (auto& window : w.pluginWindows) window->setAlpha(0);
    trackTuner->setReferenceHz(442); auto project = directory.getChildFile("native-dynamics.auralis"); e.saveProject(project); Engine loaded; loaded.loadProject(project);
    auto* loadedTuner = dynamic_cast<Tuner*>(loaded.tracks[0]->inserts[0]->instance.get()); auto* loadedComp = dynamic_cast<Compressor*>(loaded.masterInserts[1]->instance.get());
    check(loadedTuner && loadedComp && loadedTuner->referenceHz() == 442 && loadedComp->settings().ratio == 3, "Project reload restores tuner and compressor on tracks and MASTER without external plugin lookup");

    auto source = std::make_shared<SourceAudio>(); source->samples.setSize(2, 96000);
    for (int i = 0; i < 96000; ++i) { const float v = 0.5f * float(std::sin(MathConstants<double>::twoPi * 1000 * i / 48000)); source->samples.setSample(0, i, v); source->samples.setSample(1, i, v * 0.5f); }
    source->file = directory.getChildFile("compressor-original.wav"); { auto stream = source->file.createOutputStream(); stream->setPosition(0); stream->truncate(); WavAudioFormat wav; std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 48000, 2, 32, {}, 0)); stream.release(); check(writer && writer->writeFromAudioSampleBuffer(source->samples, 0, 96000), "Compressor integration uses only synthetic audio fixtures"); }
    e.importAudio(source->file, 0, 0); e.tracks[0]->gain = 1; e.master = 1; e.tracks[0]->clips[0].fadeIn = e.tracks[0]->clips[0].fadeOut = 0; e.masterInserts[1]->bypass = true;
    s = trackComp->settings(); s.threshold = -24; s.ratio = 4; s.attackMs = 1; s.knee = 0; s.makeup = 0; trackComp->setSettings(s);
    e.exportWav(directory.getChildFile("comp-wet.wav"), 0, 1, 32); e.exportMp3(directory.getChildFile("comp-wet.mp3"), 0, 1, 192); e.tracks[0]->inserts[1]->bypass = true;
    e.exportWav(directory.getChildFile("comp-dry.wav"), 0, 1, 32); e.exportMp3(directory.getChildFile("comp-dry.mp3"), 0, 1, 192);
    auto fileRms = [&](const String& name) { std::unique_ptr<AudioFormatReader> reader(e.formats.createReaderFor(directory.getChildFile(name))); if (!reader) throw std::runtime_error("Compressor export reader failed"); AudioBuffer<float> b(2, 48000); reader->read(&b, 0, 48000, 0, true, true); return b.getRMSLevel(0, 10000, 30000); };
    const double wavReduction = 20 * std::log10(fileRms("comp-wet.wav") / fileRms("comp-dry.wav")), mp3Reduction = 20 * std::log10(fileRms("comp-wet.mp3") / fileRms("comp-dry.mp3"));
    check(wavReduction < -12 && std::abs(wavReduction - mp3Reduction) < 0.3, "WAV and MP3 exports both contain the audible compressor gain reduction");
    Engine rec; auto& track = rec.addTrack("Synthetic input"); track.armed = track.monitor = true; track.gain = 1; rec.master = 1; rec.addPlugin(0, nativeDescription(NativeEffectKind::compressor));
    auto* monitorComp = dynamic_cast<Compressor*>(track.inserts[0]->instance.get()); monitorComp->setSettings(s); rec.recordingDirectory = directory.getChildFile("CompressorRaw"); rec.startRecordingWriters();
    AudioBuffer<float> in(2, 128), out(2, 128); for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) in.setSample(ch, i, 0.5f);
    for (int i = 0; i < 64; ++i) rec.render(out.getArrayOfWritePointers(), 2, 128, in.getArrayOfReadPointers(), 2); rec.finishRecording();
    check(out.getMagnitude(0, 128) < 0.15f && std::abs(track.clips.back().source->samples.getSample(0, 1000) - 0.5f) < 1e-6, "Input monitoring hears compression while recorded files preserve the dry input");
    e.recording = true; check(!w.trackInsertAction(trackId, {}, 6) && !w.trackInsertAction({}, {}, 7), "Recording prevents tuner and compressor insert-chain changes"); e.recording = false;
    // Drive each native display directly; no audio device is opened and no sound is played.
    trackTuner->setReferenceHz(440); trackTuner->setInputChannel(0); AudioBuffer<float> toneBlock(2, 128);
    for (int at = 0; at < 38400; at += 128) { for (int i = 0; i < 128; ++i) for (int ch = 0; ch < 2; ++ch) toneBlock.setSample(ch, i, float(0.2 * std::sin(MathConstants<double>::twoPi * 82.5 * (at + i) / 48000))); trackTuner->processBlock(toneBlock, midi); }
    constant(*trackComp, 0.5f, 24000);
   #if JUCE_WINDOWS
    const auto until = Time::getMillisecondCounterHiRes() + 100; while (Time::getMillisecondCounterHiRes() < until) { MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } Thread::sleep(1); }
   #endif
    for (auto item : {std::pair(tunerEditor, String("tuner-preview.png")), std::pair(compEditor, String("compressor-preview.png"))}) if (auto stream = directory.getChildFile(item.second).createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(item.first->createComponentSnapshot(item.first->getLocalBounds()), *stream); }
    check(!e.devices.getCurrentAudioDevice() && !loaded.devices.getCurrentAudioDevice() && !rec.devices.getCurrentAudioDevice(), "Tuner and compressor verification never opens audio hardware");
    directory.getChildFile("native-dynamics-verification.txt").replaceWithText("Synthetic/offline only. Compressor: measured threshold, ratio, stereo link, attack/release, knee, makeup, dry/parallel mix, bypass, sample rates/buffers, zero buffering, malformed state, meters, GUI controls, project reload, WAV/MP3 export, raw recording preservation. Tuner: low B/E bass, guitar and treble notes, cents accuracy, sample-rate changes, stereo phase/input selection, mute analysis, reference, reset, silence/staleness, background worker lifecycle, bit-transparent unmuted output, native UI and project state. Track/MASTER insertion needs no VST scan.\n");
}
}
