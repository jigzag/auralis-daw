#include "SpaceEffects.h"
#include "Interface.h"
#include "PluginInfo.h"
#include <limits>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace auralis {
namespace {
class SpaceTestHead : public AudioPlayHead {
public:
    double bpm = 120;
    Optional<PositionInfo> getPosition() const override { PositionInfo p; p.setBpm(bpm); p.setIsPlaying(true); return p; }
};
}
void runSpaceEffectsTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, const String& name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    MidiBuffer midi;
    auto impulse = [&](auto& fx, double rate, int samples, int block = 128) {
        fx.prepareToPlay(rate, block); AudioBuffer<float> result(2, samples); result.clear();
        for (int start = 0; start < samples; start += block) { const int count = jmin(block, samples - start); AudioBuffer<float> b(2, count); b.clear(); if (start == 0) b.setSample(0, 0, 1); fx.processBlock(b, midi); for (int ch = 0; ch < 2; ++ch) result.copyFrom(ch, start, b, ch, 0, count); }
        return result;
    };
    auto area = [](const AudioBuffer<float>& b, int ch, int at, int count = 256) { double sum = 0; for (int i = at; i < jmin(at + count, b.getNumSamples()); ++i) sum += b.getSample(ch, i); return sum; };
    auto onset = [](const AudioBuffer<float>& b) { for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs(b.getSample(0, i)) > 1e-8f || std::abs(b.getSample(1, i)) > 1e-8f) return i; return -1; };
    auto difference = [](const AudioBuffer<float>& a, const AudioBuffer<float>& b) { double error = 0; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < a.getNumSamples(); ++i) error = jmax(error, std::abs(double(a.getSample(ch, i)) - b.getSample(ch, i))); return error; };
    DelayEffect delay; auto d = delay.settings(); d.timeMs = 100; d.feedback = 50; d.highCut = 20000; d.mix = 100; delay.setSettings(d);
    auto echoes = impulse(delay, 48000, 24000);
    check(delay.getParameters().size() == 9 && delay.getLatencySamples() == 0, "Native delay exposes persistent controls without delaying the dry path");
    check(std::abs(onset(echoes) - 4800) <= 1 && echoes.getMagnitude(0, 4700) == 0, "The first delay echo occurs at the requested one-hundred-millisecond time");
    check(std::abs(area(echoes, 0, 4800) - 1) < 0.002 && std::abs(area(echoes, 0, 9600) - 0.5) < 0.002 && std::abs(area(echoes, 0, 14400) - 0.25) < 0.002,
          "Delay feedback produces correctly decaying first, second and third repetitions");
    check(echoes.getMagnitude(1, 0, echoes.getNumSamples()) == 0, "Normal stereo delay keeps the two input channels isolated");
    d.pingPong = true; delay.setSettings(d); echoes = impulse(delay, 48000, 24000);
    check(std::abs(area(echoes, 0, 4800) - 0.5) < 0.002 && std::abs(area(echoes, 1, 9600) - 0.25) < 0.002 && std::abs(area(echoes, 0, 14400) - 0.125) < 0.002
          && std::abs(area(echoes, 1, 4800)) < 1e-6 && std::abs(area(echoes, 0, 9600)) < 1e-6, "Ping-pong delay alternates successive echoes between left and right");
    d.pingPong = false; d.feedback = 0; d.mix = 25; delay.setSettings(d); echoes = impulse(delay, 48000, 12000);
    check(std::abs(echoes.getSample(0, 0) - 0.75) < 1e-6 && std::abs(area(echoes, 0, 4800) - 0.25) < 0.002 && echoes.getMagnitude(0, 9600, 2000) == 0, "Dry/wet mix retains immediate dry sound and zero feedback produces just one echo");
    SpaceTestHead head; delay.setPlayHead(&head); d.sync = true; d.division = 3; d.mix = 100; delay.setSettings(d); echoes = impulse(delay, 48000, 30000);
    check(std::abs(onset(echoes) - 12000) <= 2 && std::abs(delay.effectiveTimeMs() - 250) < 0.001, "Tempo-synced eighth notes follow the host at one-hundred-twenty BPM");
    head.bpm = 90; d.division = 5; delay.setSettings(d); echoes = impulse(delay, 48000, 36000);
    check(std::abs(onset(echoes) - 24000) <= 2, "Dotted-eighth delay follows a changed project tempo after transport reset");
    d.division = 6; delay.setSettings(d); check(std::abs(delay.delayMs(120) - 166.666667) < 0.001, "Triplet note divisions have the correct beat duration");
    head.bpm = 20; d.division = 0; delay.setSettings(d); echoes = impulse(delay, 8000, 100000, 512);
    check(std::abs(onset(echoes) - 96000) <= 2 && std::abs(delay.effectiveTimeMs() - 12000) < 0.001, "Whole-note sync supports the DAW's slowest tempo without wrapping its delay buffer");
    delay.setPlayHead(nullptr); d.sync = false; d.timeMs = 100; d.feedback = 35; d.highCut = 6000; delay.setSettings(d);
    for (double rate : {8000., 44100., 48000., 96000.}) { auto result = impulse(delay, rate, int(rate * 0.25), 64); check(std::abs(onset(result) - rate * 0.1) <= 2, String(rate, 0) + " Hz delay timing remains correct with sixty-four-sample buffers"); }
    auto splitDelay = impulse(delay, 48000, 18000, 64), largeDelay = impulse(delay, 48000, 18000, 9000);
    check(difference(splitDelay, largeDelay) < 1e-6, "Delay output is independent of host block partitioning, including oversized offline blocks");
    d.feedback = 90; d.timeMs = 1; delay.setSettings(d); echoes = impulse(delay, 48000, 96000); bool stable = true;
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < echoes.getNumSamples(); ++i) stable &= std::isfinite(echoes.getSample(ch, i));
    check(stable && echoes.getMagnitude(0, 0, 96000) < 1.01f && echoes.getMagnitude(0, 90000, 6000) < 1e-6, "Maximum allowed feedback stays stable and decays rather than self-oscillating");

    ReverbEffect reverb; auto r = reverb.settings(); r.mix = 100; r.preDelayMs = 0; reverb.setSettings(r); auto room = impulse(reverb, 48000, 96000);
    check(reverb.getParameters().size() == 9 && reverb.getLatencySamples() == 0 && room.getSample(0, 0) == 0 && room.getRMSLevel(0, 4800, 30000) > 0.0001f, "Native reverb creates a wet-only decaying tail without leaking initial dry audio or buffering the dry path");
    const int first = onset(room); r.preDelayMs = 80; reverb.setSettings(r); auto pre = impulse(reverb, 48000, 96000);
    check(std::abs(onset(pre) - first - 3840) <= 2, "Reverb pre-delay postpones the wet tail by eighty milliseconds (onsets " + String(first) + " and " + String(onset(pre)) + ")");
    r.preDelayMs = 0; r.width = 0; reverb.setSettings(r); auto mono = impulse(reverb, 48000, 24000); double widthError = 0;
    for (int i = 0; i < mono.getNumSamples(); ++i) widthError += std::abs(mono.getSample(0, i) - mono.getSample(1, i)); check(widthError < 1e-6, "Zero-width reverb creates a centred mono tail");
    r.width = 100; r.size = 20; reverb.setSettings(r); auto smallRoom = impulse(reverb, 48000, 96000); const auto shortTail = reverb.getTailLengthSeconds();
    r.size = 85; reverb.setSettings(r); auto hall = impulse(reverb, 48000, 96000); double stereo = 0;
    for (int i = 0; i < hall.getNumSamples(); ++i) stereo += std::abs(hall.getSample(0, i) - hall.getSample(1, i));
    check(stereo > 0.01 && hall.getRMSLevel(0, 48000, 24000) > smallRoom.getRMSLevel(0, 48000, 24000) * 2 && reverb.getTailLengthSeconds() > shortTail * 2, "Room size lengthens the reverb decay and stereo width spreads the tail");
    r.size = 55; reverb.setSettings(r); auto splitReverb = impulse(reverb, 48000, 18000, 64), largeReverb = impulse(reverb, 48000, 18000, 9000);
    check(difference(splitReverb, largeReverb) < 1e-6, "Reverb is independent of callback size and safely splits large offline blocks");
    for (double rate : {8000., 44100., 96000.}) { auto result = impulse(reverb, rate, int(rate * 0.5), 64); check(result.getMagnitude(0, result.getNumSamples()) > 0 && std::abs(onset(result) / rate - first / 48000.0) < 0.001, String(rate, 0) + " Hz reverb preserves its physical onset time"); }
    auto sineRms = [&](float cutoff) { auto settings = reverb.settings(); settings.highCut = cutoff; settings.lowCut = 20; reverb.setSettings(settings); reverb.prepareToPlay(48000, 128); double energy = 0;
        for (int start = 0; start < 48000; start += 128) { const int count = jmin(128, 48000 - start); AudioBuffer<float> b(2, count); for (int i = 0; i < count; ++i) for (int ch = 0; ch < 2; ++ch) b.setSample(ch, i, float(0.1 * std::sin(MathConstants<double>::twoPi * 8000 * (start + i) / 48000))); reverb.processBlock(b, midi); if (start > 24000) energy += std::pow(b.getRMSLevel(0, 0, count), 2) * count; } return std::sqrt(energy); };
    const double bright = sineRms(20000), dark = sineRms(1000); check(dark < bright * 0.2, "Reverb high cut removes treble from the wet tail without altering its timing");
    auto checkCommon = [&](auto& fx, const String& name) {
        auto s = fx.settings(); s.mix = 0; s.output = 0; s.bypass = false; fx.setSettings(s); auto dry = impulse(fx, 48000, 2000);
        check(dry.getSample(0, 0) == 1 && dry.getMagnitude(0, 1, 1999) == 0 && fx.getTailLengthSeconds() == 0, name + " zero mix is transparent and reports no audible tail");
        s.mix = 100; s.output = 6; s.bypass = true; fx.setSettings(s); dry = impulse(fx, 48000, 2000);
        check(dry.getSample(0, 0) == 1 && dry.getMagnitude(0, 1, 1999) == 0 && fx.getTailLengthSeconds() == 0, name + " bypass preserves the original level including output trim");
        s.bypass = false; s.mix = 0; fx.setSettings(s); dry = impulse(fx, 48000, 2000); check(std::abs(dry.getSample(0, 0) - Decibels::decibelsToGain(6.0f)) < 1e-6, name + " output trim applies the requested six-decibel gain");
        fx.reset(); AudioBuffer<float> invalid(2, 256); invalid.clear(); invalid.setSample(0, 1, std::numeric_limits<float>::quiet_NaN()); invalid.setSample(1, 2, std::numeric_limits<float>::infinity()); fx.processBlock(invalid, midi); bool finite = true;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) finite &= std::isfinite(invalid.getSample(ch, i)); check(finite, name + " rejects invalid upstream samples without poisoning its output");
        s.mix = 100; s.output = 0; fx.setSettings(s); impulse(fx, 48000, 3000); fx.reset(); AudioBuffer<float> silence(2, 1024); silence.clear(); fx.processBlock(silence, midi); check(silence.getMagnitude(0, 1024) == 0, name + " transport reset clears pending echoes and tails");
        MemoryBlock state, restored; fx.getStateInformation(state); fx.restoreDefaults(); fx.setStateInformation(state.getData(), int(state.getSize())); fx.getStateInformation(restored); check(state == restored, name + " state round-trips all parameters");
        const auto bad = String("{\"kind\":\"") + fx.getPluginDescription().fileOrIdentifier + "\",\"values\":[2]}"; fx.setStateInformation(bad.toRawUTF8(), bad.getNumBytesAsUTF8()); fx.getStateInformation(restored); check(state == restored, name + " malformed state cannot overwrite saved parameters");
    };
    checkCommon(delay, "Delay"); checkCommon(reverb, "Reverb");
    auto badD = delay.settings(); badD.feedback = 100; auto badR = reverb.settings(); badR.preDelayMs = -1; check(!delay.setSettings(badD) && !reverb.setSettings(badR), "Unstable feedback and invalid pre-delay values are rejected");

    Workstation w(false); w.stopTimer(); auto& e = w.engine; const auto id = e.tracks[0]->id;
    check(w.knownPlugins.getTypes().isEmpty() && w.trackInsertAction(id, {}, 8), "Track Add opens the native reverb without a VST scan"); w.pluginWindows.back()->setAlpha(0);
    auto* trackReverb = dynamic_cast<ReverbEffect*>(e.tracks[0]->inserts[0]->instance.get()); auto* reverbEditor = w.pluginWindows.back()->getContentComponent();
    auto* preControl = dynamic_cast<Slider*>(reverbEditor->findChildWithID("reverbPreDelay")); auto* roomControl = dynamic_cast<Slider*>(reverbEditor->findChildWithID("reverbSize")); auto* preset = dynamic_cast<ComboBox*>(reverbEditor->findChildWithID("spacePreset"));
    check(trackReverb && preControl && roomControl && preset && describePlugin(e.tracks[0]->inserts[0]->description).kind == String::fromUTF8("リバーブ"), "Reverb editor exposes room, pre-delay and presets with a clear insert description");
    preset->setSelectedId(3, sendNotificationSync); check(std::abs(trackReverb->settings().size - 72) < 0.01, "Hall preset changes the real reverb processor"); preControl->setValue(55, sendNotificationSync); check(std::abs(trackReverb->settings().preDelayMs - 55) < 0.01 && preset->getSelectedId() == 1, "Manual reverb edits update audio settings and return the preset display to Custom");
    reverbEditor->setSize(760, 600); check(reverbEditor->getLocalBounds().contains(preControl->getBounds()), "Reverb controls fit at the minimum editor size"); reverbEditor->setSize(860, 640);
    check(w.trackInsertAction({}, {}, 9), "MASTER Add opens the native delay without scanning plugins"); w.pluginWindows.back()->setAlpha(0);
    auto* masterDelay = dynamic_cast<DelayEffect*>(e.masterInserts[0]->instance.get()); auto* delayEditor = w.pluginWindows.back()->getContentComponent();
    auto* timeControl = dynamic_cast<Slider*>(delayEditor->findChildWithID("delayTime")); auto* syncControl = dynamic_cast<ToggleButton*>(delayEditor->findChildWithID("delaySync")); auto* divisionControl = dynamic_cast<ComboBox*>(delayEditor->findChildWithID("delayDivision")); auto* pingControl = dynamic_cast<ToggleButton*>(delayEditor->findChildWithID("delayPingPong"));
    check(masterDelay && timeControl && syncControl && divisionControl && pingControl && describePlugin(e.masterInserts[0]->description).kind == String::fromUTF8("ディレイ"), "Delay editor provides tempo sync, note division and ping-pong controls");
    syncControl->setToggleState(true, dontSendNotification); syncControl->onClick(); divisionControl->setSelectedId(6, sendNotificationSync); pingControl->setToggleState(true, dontSendNotification); pingControl->onClick();
    check(masterDelay->settings().sync && masterDelay->settings().division == 5 && masterDelay->settings().pingPong && !timeControl->isEnabled() && divisionControl->isEnabled(), "Tempo-sync controls change the sounding delay and make free-time editing unavailable");
    delayEditor->setSize(760, 600); check(delayEditor->getLocalBounds().contains(pingControl->getBounds()) && !divisionControl->getBounds().intersects(pingControl->getBounds()), "Delay controls fit without overlap at the minimum editor width"); delayEditor->setSize(860, 640);
    w.markProjectSaved(); AudioBuffer<float> quiet(2, 128); quiet.clear(); masterDelay->processBlock(quiet, midi); trackReverb->processBlock(quiet, midi); check(w.pluginStateSnapshot() == w.savedPluginState, "Delay tempo telemetry and reverb processing do not dirty the project");
    roomControl->setValue(80, sendNotificationSync); check(w.pluginStateSnapshot() != w.savedPluginState, "Reverb parameter edits participate in unsaved-project detection");
    auto project = directory.getChildFile("space-effects.auralis"); e.masterInserts[0]->bypass = true; e.saveProject(project); Engine loaded; loaded.loadProject(project);
    auto* savedReverb = dynamic_cast<ReverbEffect*>(loaded.tracks[0]->inserts[0]->instance.get()); auto* savedDelay = dynamic_cast<DelayEffect*>(loaded.masterInserts[0]->instance.get());
    check(savedReverb && savedDelay && std::abs(savedReverb->settings().size - 80) < 0.01 && savedDelay->settings().division == 5 && savedDelay->settings().pingPong && loaded.masterInserts[0]->bypass, "Project reload restores reverb, tempo-synced delay and insert bypass without external plugin lookup");
    check(w.trackInsertAction(id, {}, 9) && w.trackInsertAction({}, {}, 8), "Delay is also available on tracks and reverb is available on MASTER"); for (auto& window : w.pluginWindows) window->setAlpha(0);
    e.tracks[0]->inserts[1]->bypass = true; e.masterInserts[1]->bypass = true; e.masterInserts[0]->bypass = false;
    auto sourceFile = directory.getChildFile("space-original.wav"); AudioBuffer<float> source(2, 9600); source.clear();
    for (int i = 0; i < 4800; ++i) { const float v = float(0.25 * std::sin(MathConstants<double>::twoPi * 440 * i / 48000) * std::sin(MathConstants<double>::pi * i / 4800)); source.setSample(0, i, v); source.setSample(1, i, v * 0.5f); }
    { auto stream = sourceFile.createOutputStream(); stream->setPosition(0); stream->truncate(); WavAudioFormat wav; std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 48000, 2, 32, {}, 0)); stream.release(); check(writer && writer->writeFromAudioSampleBuffer(source, 0, 9600), "Space-effect integration uses only synthetic audio"); }
    e.importAudio(sourceFile, 0, 0); e.tracks[0]->gain = 1; e.master = 1; e.tracks[0]->clips[0].fadeIn = e.tracks[0]->clips[0].fadeOut = 0;
    r = trackReverb->settings(); r.mix = 35; r.size = 70; trackReverb->setSettings(r); d = masterDelay->settings(); d.sync = false; d.timeMs = 180; d.feedback = 50; d.mix = 35; masterDelay->setSettings(d);
    const auto expectedTail = jmin(120.0, trackReverb->getTailLengthSeconds() + masterDelay->getTailLengthSeconds());
    check(std::abs(e.effectTailSeconds() - expectedTail) < 0.001 && std::abs(w.defaultExportRange().getEnd() - 1 - jmax(2.0, expectedTail)) < 0.001, "Whole-project export defaults include serial track and MASTER effect tails");
    w.setTimeSelection(0.25, 0.75); check(w.defaultExportRange() == Range<double>(0.25, 0.75), "Explicit export selection keeps its exact start and end despite long effect tails"); w.hasTimeSelection = false;
    e.exportWav(directory.getChildFile("space-wet.wav"), 0, 1.5, 32); e.exportMp3(directory.getChildFile("space-wet.mp3"), 0, 1.5, 192);
    auto fileRms = [&](const String& name) { std::unique_ptr<AudioFormatReader> reader(e.formats.createReaderFor(directory.getChildFile(name))); if (!reader) throw std::runtime_error("Space effect export reader failed"); AudioBuffer<float> b(2, 72000); reader->read(&b, 0, 72000, 0, true, true); return b.getRMSLevel(0, 24000, 24000); };
    const auto wavTail = fileRms("space-wet.wav"), mp3Tail = fileRms("space-wet.mp3"); check(wavTail > 0.0001 && std::abs(20 * std::log10(mp3Tail / wavTail)) < 0.5, "WAV and MP3 exports contain matching reverb and delay tails beyond the last clip");
    e.exportWav(directory.getChildFile("space-exact-range.wav"), 0.25, 0.75, 32); std::unique_ptr<AudioFormatReader> exactReader(e.formats.createReaderFor(directory.getChildFile("space-exact-range.wav"))); check(exactReader && exactReader->lengthInSamples == 24000, "Range export never silently extends the selected duration to add effect tails");
    AudioBuffer<float> selection(2, 24000), fullRange(2, 24000); exactReader->read(&selection, 0, 24000, 0, true, true); std::unique_ptr<AudioFormatReader> fullReader(e.formats.createReaderFor(directory.getChildFile("space-wet.wav"))); fullReader->read(&fullRange, 0, 24000, 12000, true, true);
    check(selection.getMagnitude(0, 24000) > 0.0001 && difference(selection, fullRange) < 1e-5, "Selected-range export reconstructs earlier reverb and delay history to match the same part of a full export");
    d = masterDelay->settings(); d.sync = true; d.division = 5; masterDelay->setSettings(d); e.bpm = 60;
    check(std::abs(e.effectTailSeconds() - trackReverb->getTailLengthSeconds() - masterDelay->tailSeconds(60)) < 0.001, "Export tail estimates use the current project tempo even before the next audio callback"); d.sync = false; masterDelay->setSettings(d); e.bpm = 120;
    e.tracks[0]->inserts[0]->bypass = true; e.masterInserts[0]->bypass = true; e.exportWav(directory.getChildFile("space-dry.wav"), 0, 1.5, 32); check(fileRms("space-dry.wav") == 0 && e.effectTailSeconds() == 0, "Bypassed inserts contribute neither audible tails nor export padding"); e.tracks[0]->inserts[0]->bypass = false; e.masterInserts[0]->bypass = false;
    Engine rec; auto& input = rec.addTrack("Synthetic input"); input.armed = input.monitor = true; input.gain = 1; rec.master = 1; rec.addPlugin(0, nativeDescription(NativeEffectKind::reverb)); rec.addPlugin(0, nativeDescription(NativeEffectKind::delay));
    auto* recDelay = dynamic_cast<DelayEffect*>(input.inserts[1]->instance.get()); d.sync = false; d.timeMs = 80; d.mix = 100; recDelay->setSettings(d); recDelay->reset(); rec.recordingDirectory = directory.getChildFile("SpaceRaw"); rec.startRecordingWriters();
    // Half-scale is exactly representable in the recorder's 24-bit PCM format.
    AudioBuffer<float> in(2, 128), out(2, 128); double monitoredTail = 0; for (int k = 0; k < 160; ++k) { in.clear(); if (k == 0) in.setSample(0, 0, 0.5f); rec.render(out.getArrayOfWritePointers(), 2, 128, in.getArrayOfReadPointers(), 2); if (k > 30) monitoredTail += out.getRMSLevel(0, 0, 128); } rec.finishRecording();
    check(monitoredTail > 0.001 && input.clips.back().source->samples.getSample(0, 0) == 0.5f && input.clips.back().source->samples.getMagnitude(0, 1, 20000) == 0, "Input monitoring hears reverb and delay while recorded audio stays dry");
    e.recording = true; check(!w.trackInsertAction(id, {}, 8) && !w.trackInsertAction({}, {}, 9), "Recording prevents reverb and delay insert-chain changes"); e.recording = false;
    auto* reset = dynamic_cast<TextButton*>(delayEditor->findChildWithID("spaceReset")); reset->onClick(); check(std::abs(masterDelay->settings().timeMs - 350) < 0.01 && !masterDelay->settings().sync && !masterDelay->settings().pingPong, "Delay Reset restores its normal free-time configuration");
    auto* delayPreset = dynamic_cast<ComboBox*>(delayEditor->findChildWithID("spacePreset")); delayPreset->setSelectedId(4, sendNotificationSync);
   #if JUCE_WINDOWS
    const auto until = Time::getMillisecondCounterHiRes() + 100; while (Time::getMillisecondCounterHiRes() < until) { MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } Thread::sleep(1); }
   #endif
    for (auto item : {std::pair(reverbEditor, String("reverb-preview.png")), std::pair(delayEditor, String("delay-preview.png"))}) if (auto stream = directory.getChildFile(item.second).createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(item.first->createComponentSnapshot(item.first->getLocalBounds()), *stream); }
    check(!e.devices.getCurrentAudioDevice() && !loaded.devices.getCurrentAudioDevice() && !rec.devices.getCurrentAudioDevice(), "All reverb and delay checks use offline audio without opening hardware");
    directory.getChildFile("space-effects-verification.txt").replaceWithText("Synthetic/offline only. Delay impulse timing, exponential feedback decay, stereo isolation, ping-pong direction, wet/dry, tempo sync/dotted/triplet notes, 20 BPM whole notes, sample rates, 64/9000-sample partitions, feedback stability. Reverb onset/pre-delay, decay length, stereo width, spectral high cut, sample rates and block partitioning. Both: bypass, trim, invalid samples/state, reset, persistence, native menus/editor controls/presets/minimum sizing, track and MASTER hosting, unsaved detection, WAV/MP3 tails, exact range duration, export-tail estimate, raw recording preservation and wet input monitoring. No audio device opened.\n");
}
}
