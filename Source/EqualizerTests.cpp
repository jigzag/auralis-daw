#include "Equalizer.h"
#include "Interface.h"
#include "PluginInfo.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace auralis {
void runEqualizerTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, const String& name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto measure = [](Equalizer& eq, double hz, double rate = 48000, int block = 128) {
        eq.prepareToPlay(rate, block); MidiBuffer midi; double wet = 0, dry = 0, stereoError = 0;
        const int total = int(rate * 0.6), warm = int(rate * 0.3);
        for (int at = 0; at < total; at += block) {
            const int count = jmin(block, total - at); AudioBuffer<float> b(2, count);
            for (int i = 0; i < count; ++i) { const float v = 0.1f * float(std::sin(MathConstants<double>::twoPi * hz * (at + i) / rate)); b.setSample(0, i, v); b.setSample(1, i, v * 0.5f); }
            eq.processBlock(b, midi);
            for (int i = 0; i < count; ++i) if (at + i >= warm) { const double v = 0.1 * std::sin(MathConstants<double>::twoPi * hz * (at + i) / rate); wet += std::pow(b.getSample(0, i), 2); dry += v * v; stereoError = jmax(stereoError, std::abs(double(b.getSample(1, i)) - b.getSample(0, i) * 0.5)); }
        }
        if (stereoError > 1e-6) throw std::runtime_error("EQ stereo balance changed"); return 10 * std::log10(wet / dry);
    };
    Equalizer eq;
    check(eq.getParameters().size() == 22 && eq.getLatencySamples() == 0, "Built-in EQ exposes stable hosted parameters and adds no buffering latency");
    check(std::abs(measure(eq, 1000)) < 0.001 && std::abs(measure(eq, 50)) < 0.001, "Default four-band EQ is transparent at low and mid frequencies");
    eq.setBand(1, {1000, 6, 1, 0, true});
    check(std::abs(measure(eq, 1000) - 6) < 0.03 && std::abs(eq.responseDb(1000) - 6) < 0.01, "Bell filter and displayed response agree with a measured six-decibel boost");
    eq.setBand(1, {1000, -12, 1, 0, true}); check(std::abs(measure(eq, 1000) + 12) < 0.03, "Bell filter accurately cuts the selected centre frequency");
    eq.setBand(1, {1000, 12, 0.5f, 0, true}); const auto wide = measure(eq, 500); eq.setBand(1, {1000, 12, 4, 0, true});
    check(wide > measure(eq, 500) + 4, "Q controls the audible width of an EQ band");
    eq.restoreDefaults(); eq.setBand(0, {200, 6, 0.7071f, 1, true});
    check(measure(eq, 20) > 5.8 && std::abs(measure(eq, 10000)) < 0.05, "Low shelf changes bass while retaining high frequencies");
    eq.restoreDefaults(); eq.setBand(3, {5000, -6, 0.7071f, 2, true});
    check(measure(eq, 20000) < -5.8 && std::abs(measure(eq, 100)) < 0.05, "High shelf changes treble while retaining low frequencies");
    eq.restoreDefaults(); eq.setBand(0, {1000, 0, 0.7071f, 3, true});
    check(std::abs(measure(eq, 1000) + 3.0103) < 0.05 && measure(eq, 100) < -36 && measure(eq, 4000) > -0.1,
          "Low-cut filter rejects rumble with the expected twelve-decibel-per-octave response");
    eq.restoreDefaults(); eq.setBand(3, {1000, 0, 0.7071f, 4, true});
    check(measure(eq, 10000) < -40 && measure(eq, 100) > -0.05, "High-cut filter rejects high frequencies without reducing the bass");
    eq.restoreDefaults(); eq.setBand(1, {1000, 12, 1, 0, false}); check(std::abs(measure(eq, 1000)) < 0.001, "Disabling one band removes only that band's processing");
    eq.setOutputDb(-6); check(std::abs(measure(eq, 1000) + 6) < 0.01, "Output trim sets the final EQ level");
    eq.setBypassed(true); check(std::abs(measure(eq, 1000)) < 0.001, "Internal bypass restores the original signal including its level");
    eq.restoreDefaults(); eq.setBand(1, {1000, 6, 1, 0, true});
    for (double rate : {8000., 44100., 48000., 96000.}) check(std::abs(measure(eq, 1000, rate, 64) - 6) < 0.05,
          String(rate, 0) + " Hz EQ processing retains the requested gain with 64-sample buffers");
    eq.prepareToPlay(48000, 128); MidiBuffer midi; AudioBuffer<float> impulse(2, 128); impulse.clear(); impulse.setSample(0, 0, 1); eq.processBlock(impulse, midi);
    check(impulse.getMagnitude(1, 0, 128) == 0, "Stereo EQ filter states do not leak sound between channels");
    eq.reset(); impulse.clear(); eq.processBlock(impulse, midi); check(impulse.getMagnitude(0, 128) == 0, "Transport reset clears EQ filter tails");
    impulse.setSample(0, 10, std::numeric_limits<float>::quiet_NaN()); impulse.setSample(1, 20, std::numeric_limits<float>::infinity()); eq.processBlock(impulse, midi);
    bool finite = true; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) finite &= std::isfinite(impulse.getSample(ch, i));
    check(finite, "Invalid upstream samples cannot poison EQ filter state");
    eq.setBand(0, {80, 0, 0.7071f, 3, true}); eq.setBand(2, {3200, -4, 2.5f, 0, false}); eq.setOutputDb(-2); eq.setBypassed(true);
    MemoryBlock state; eq.getStateInformation(state); Equalizer restored; restored.setStateInformation(state.getData(), int(state.getSize())); MemoryBlock restoredState; restored.getStateInformation(restoredState);
    check(restoredState == state && restored.band(0).type == 3 && !restored.band(2).enabled && restored.bypassed(), "EQ state round-trips all bands, types, bypass, Q and output level");
    const auto invalid = String::fromUTF8("{\"kind\":\"auralis-eq-v1\",\"values\":[2]}"); restored.setStateInformation(invalid.toRawUTF8(), invalid.getNumBytesAsUTF8()); restored.getStateInformation(restoredState);
    check(restoredState == state && !restored.setBand(0, {0, 0, 1, 0, true}), "Malformed EQ state and out-of-range band values leave existing settings intact");
    Workstation w(false); w.stopTimer(); auto& e = w.engine; const auto trackId = e.tracks[0]->id;
    check(w.knownPlugins.getTypes().isEmpty() && w.trackInsertAction(trackId, {}, 5), "Track Add exposes the native EQ without scanning or installing a plugin");
    auto* trackEq = dynamic_cast<Equalizer*>(e.tracks[0]->inserts[0]->instance.get()); check(trackEq && describePlugin(e.tracks[0]->inserts[0]->description).kind == "EQ", "The built-in insert is identified as an equalizer");
    auto& window = *w.pluginWindows.back(); window.setAlpha(0); auto* editor = window.getContentComponent();
    auto* type = dynamic_cast<ComboBox*>(editor->findChildWithID("eqType")); auto* frequency = dynamic_cast<Slider*>(editor->findChildWithID("eqFrequency"));
    auto* gain = dynamic_cast<Slider*>(editor->findChildWithID("eqGain")); auto* graph = editor->findChildWithID("eqGraph");
    check(type && frequency && gain && graph, "Native EQ editor provides response graph, frequency, gain, Q and filter type controls");
    type->setSelectedId(1, sendNotificationSync); frequency->setValue(1000, sendNotificationSync); gain->setValue(6, sendNotificationSync);
    check(std::abs(trackEq->band(0).frequency - 1000) < 0.1 && std::abs(trackEq->band(0).gain - 6) < 0.01, "Numeric EQ controls update the sounding processor immediately");
    const auto area = graph->getLocalBounds().toFloat().withTrimmedLeft(46).withTrimmedRight(16).withTrimmedTop(18).withTrimmedBottom(24);
    auto at = [&](double hz, double db) { return Point<float>(area.getX() + float(std::log(hz / 20) / std::log(1000.0)) * area.getWidth(), area.getCentreY() - float(db / 48) * area.getHeight()); };
    auto event = [&](Point<float> pos, Point<float> origin) { const auto now = Time::getCurrentTime(); return MouseEvent(Desktop::getInstance().getMainMouseSource(), pos, ModifierKeys(ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, graph, graph, now, origin, now, 1, pos != origin); };
    const auto start = at(1000, 6), end = at(2000, -3); graph->mouseDown(event(start, start)); graph->mouseDrag(event(end, start)); graph->mouseUp(event(end, start));
    check(std::abs(trackEq->band(0).frequency - 2000) < 1 && std::abs(trackEq->band(0).gain + 3) < 0.02, "Dragging an EQ graph point adjusts frequency and gain in the real processor");
    MouseWheelDetails wheel; wheel.deltaY = 0.2f; const auto oldQ = trackEq->band(0).q; graph->mouseWheelMove(event(end, end), wheel);
    check(trackEq->band(0).q > oldQ, "Graph wheel adjustment changes the selected band's Q");
    auto* reset = dynamic_cast<TextButton*>(editor->findChildWithID("eqReset")); reset->onClick();
    check(trackEq->band(0).type == 1 && trackEq->band(0).gain == 0 && trackEq->outputDb() == 0, "EQ Reset restores the flat default configuration");
    w.markProjectSaved(); trackEq->setBand(1, {1000, 6, 1, 0, true}); check(w.pluginStateSnapshot() != w.savedPluginState, "Native EQ parameter edits are detected by unsaved-project confirmation");
    auto source = std::make_shared<SourceAudio>(); source->samples.setSize(2, 96000);
    for (int i = 0; i < 96000; ++i) { const float v = 0.1f * float(std::sin(MathConstants<double>::twoPi * 1000 * i / 48000)); source->samples.setSample(0, i, v); source->samples.setSample(1, i, v * 0.5f); }
    source->file = directory.getChildFile("eq-original.wav");
    { auto stream = source->file.createOutputStream(); stream->setPosition(0); stream->truncate(); WavAudioFormat wav; std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 48000, 2, 32, {}, 0)); stream.release(); check(writer && writer->writeFromAudioSampleBuffer(source->samples, 0, 96000), "EQ integration uses a synthetic audio fixture"); }
    e.importAudio(source->file, 0, 0); e.tracks[0]->gain = 1; e.master = 1; e.tracks[0]->clips[0].fadeIn = e.tracks[0]->clips[0].fadeOut = 0;
    auto engineRms = [&] { e.seek(0.3); e.play(); AudioBuffer<float> block(2, 128); double energy = 0; for (int k = 0; k < 128; ++k) { e.render(block.getArrayOfWritePointers(), 2, 128); if (k >= 32) energy += std::pow(block.getRMSLevel(0, 0, 128), 2); } e.stop(); return std::sqrt(energy / 96); };
    e.tracks[0]->inserts[0]->bypass = true; const double dry = engineRms(); e.tracks[0]->inserts[0]->bypass = false;
    check(std::abs(20 * std::log10(engineRms() / dry) - 6) < 0.1, "Track playback passes through the native EQ in its insert chain");
    check(w.trackInsertAction({}, {}, 5), "MASTER Add also exposes the native equalizer"); w.pluginWindows.back()->setAlpha(0);
    auto* masterEq = dynamic_cast<Equalizer*>(e.masterInserts[0]->instance.get()); masterEq->setBand(1, {1000, -6, 1, 0, true});
    check(std::abs(20 * std::log10(engineRms() / dry)) < 0.1, "Track and master EQs process serially with the expected combined frequency gain");
    e.masterInserts[0]->bypass = true;
    auto project = directory.getChildFile("equalizer-project.auralis"); e.saveProject(project); Engine loaded; loaded.loadProject(project);
    auto* savedTrack = dynamic_cast<Equalizer*>(loaded.tracks[0]->inserts[0]->instance.get()); auto* savedMaster = dynamic_cast<Equalizer*>(loaded.masterInserts[0]->instance.get());
    check(savedTrack && savedMaster && std::abs(savedTrack->band(1).gain - 6) < 0.01 && std::abs(savedMaster->band(1).gain + 6) < 0.01 && loaded.masterInserts[0]->bypass,
          "Project reload restores native EQ processors, all settings and insert bypass without VST lookup");
    e.exportWav(directory.getChildFile("eq-wet.wav"), 0, 1, 32); e.exportMp3(directory.getChildFile("eq-wet.mp3"), 0, 1, 192);
    e.tracks[0]->inserts[0]->bypass = true; e.exportWav(directory.getChildFile("eq-dry.wav"), 0, 1, 32); e.exportMp3(directory.getChildFile("eq-dry.mp3"), 0, 1, 192);
    auto fileRms = [&](const String& name) { std::unique_ptr<AudioFormatReader> reader(e.formats.createReaderFor(directory.getChildFile(name))); if (!reader) throw std::runtime_error("EQ export reader failed"); AudioBuffer<float> b(2, 48000); reader->read(&b, 0, 48000, 0, true, true); return b.getRMSLevel(0, 10000, 30000); };
    check(std::abs(20 * std::log10(fileRms("eq-wet.wav") / fileRms("eq-dry.wav")) - 6) < 0.1, "WAV export includes native EQ processing");
    check(std::abs(20 * std::log10(fileRms("eq-wet.mp3") / fileRms("eq-dry.mp3")) - 6) < 0.25, "MP3 export includes native EQ processing");
    e.recording = true; check(!w.trackInsertAction(trackId, {}, 5), "Recording prevents native EQ insertion or removal"); e.recording = false;
    Engine rec; auto& input = rec.addTrack("Input"); input.armed = input.monitor = true; input.gain = 1; rec.master = 1; rec.addPlugin(0, equalizerDescription());
    auto* inputEq = dynamic_cast<Equalizer*>(input.inserts[0]->instance.get()); inputEq->setBand(1, {1000, 6, 1, 0, true}); rec.recordingDirectory = directory.getChildFile("EqRaw"); rec.startRecordingWriters();
    AudioBuffer<float> in(2, 128), out(2, 128); double monitored = 0, rawEnergy = 0;
    for (int k = 0; k < 64; ++k) { for (int i = 0; i < 128; ++i) { const float v = 0.1f * float(std::sin(MathConstants<double>::twoPi * 1000 * (k * 128 + i) / 48000)); in.setSample(0, i, v); in.setSample(1, i, v * 0.5f); } rec.render(out.getArrayOfWritePointers(), 2, 128, in.getArrayOfReadPointers(), 2); if (k >= 32) { monitored += std::pow(out.getRMSLevel(0, 0, 128), 2); rawEnergy += std::pow(in.getRMSLevel(0, 0, 128), 2); } }
    rec.finishRecording();
    check(std::abs(10 * std::log10(monitored / rawEnergy) - 6) < 0.1 && std::abs(input.clips.back().source->samples.getSample(0, 1000) - 0.1f * float(std::sin(MathConstants<double>::twoPi * 1000 * 1000 / 48000))) < 1e-6,
          "Input monitoring hears EQ while the recorded audio file retains the unprocessed input");
    e.tracks[0]->inserts[0]->bypass = false; trackEq->setBand(0, {80, 0, 0.7071f, 3, true}); trackEq->setBand(1, {700, -4, 1.2f, 0, true}); trackEq->setBand(2, {2600, 3, 1.5f, 0, true});
   #if JUCE_WINDOWS
    const auto until = Time::getMillisecondCounterHiRes() + 100;
    while (Time::getMillisecondCounterHiRes() < until) { MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } Thread::sleep(1); }
   #endif
    if (auto stream = directory.getChildFile("equalizer-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds()), *stream); }
    check(!e.devices.getCurrentAudioDevice() && !loaded.devices.getCurrentAudioDevice() && !rec.devices.getCurrentAudioDevice(), "All equalizer tests use offline synthetic audio without opening hardware");
    directory.getChildFile("equalizer-verification.txt").replaceWithText("Synthetic/offline only. Verified flat/default identity, measured bell boost/cut and plotted response, Q width, shelves, low/high cuts, band/global bypass, trim, stereo isolation, sample rates and 64-sample processing, reset/finite state, hosted parameters and project round-trip, native editor controls and graph gestures, no-scan track/master insertion, unsaved state detection, serial track/master processing, WAV/MP3 exports, raw recording preservation and input monitoring.\n");
}
}
