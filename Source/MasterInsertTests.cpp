#include "Interface.h"
#include "Mixer.h"
#include <cmath>
#include <stdexcept>
namespace auralis {
void runMasterInsertTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Workstation w(false); w.stopTimer(); auto& e = w.engine;
    e.addTrack("Second"); for (int i = 0; i < 2; ++i) { e.importAudio(directory.getChildFile("source.wav"), i, 0); e.tracks[size_t(i)]->gain = 1; e.tracks[size_t(i)]->clips[0].fadeIn = e.tracks[size_t(i)]->clips[0].fadeOut = 0; } e.master = 1;
    OwnedArray<PluginDescription> descriptions; auto gainFile = File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("TestGain.vst3");
    for (auto* format : e.pluginFormats.getFormats()) format->findAllTypesForFile(descriptions, gainFile.getFullPathName());
    check(descriptions.size() == 1, "Master insert regression effect is available");
    e.addPlugin(-1, *descriptions[0]); auto first = e.masterInserts[0]->id;
    AudioBuffer<float> out(2, 128); auto sample = [&] { e.seek(0.25); e.play(); e.render(out.getArrayOfWritePointers(), 2, 128); e.stop(); return out.getSample(0, 40); };
    check(std::abs(sample() - 0.25f) < 1e-6, "Master effect processes the combined output of all tracks");
    e.addPlugin(-1, *descriptions[0]); auto second = e.masterInserts[1]->id;
    check(std::abs(sample() - 0.125f) < 1e-6, "Multiple master inserts process the mix in a serial chain");
    w.trackInsertAction({}, second, 2); check(std::abs(sample() - 0.25f) < 1e-6, "Master bypass restores the signal through the remaining active chain");
    e.masterInserts[0]->instance->getParameters()[0]->setValueNotifyingHost(0.25f); e.master = 0.5f;
    check(std::abs(sample() - 0.0625f) < 1e-6, "Master volume applies after master insert processing");
    auto project = directory.getChildFile("master-inserts.auralis"); w.saveToFile(project); Engine loaded; loaded.loadProject(project);
    check(loaded.masterInserts.size() == 2 && loaded.masterInserts[1]->bypass && std::abs(loaded.masterInserts[0]->instance->getParameters()[0]->getValue() - 0.25f) < 1e-6,
          "Project save and reload restore master insert order, parameter state and bypass");
    e.masterInserts[0]->instance->getParameters()[0]->setValueNotifyingHost(0.4f);
    check(std::abs(sample() - 0.1f) < 1e-6, "Master plugin parameter edits reach the audio processor");
    check(w.pluginStateSnapshot() != w.savedPluginState, "Unsaved parameter changes in master plugins are detected by save confirmation");
    e.masterInserts[0]->instance->getParameters()[0]->setValueNotifyingHost(0.25f);
    const auto wavFile = directory.getChildFile("master-processed.wav"); e.exportWav(wavFile, 0.1, 0.2, 32);
    std::unique_ptr<AudioFormatReader> reader(e.formats.createReaderFor(wavFile)); AudioBuffer<float> wav(2, 4800); reader->read(&wav, 0, 4800, 0, true, true);
    check(std::abs(wav.getSample(0, 100) - 0.0625f) < 1e-6, "WAV export includes the complete master effect chain and master level");
    Engine mp3; mp3.addTrack("Sine").gain = 1; mp3.master = 1; mp3.importAudio(directory.getChildFile("overlap-sine-old.wav"), 0, 0); mp3.addPlugin(-1, *descriptions[0]);
    mp3.exportMp3(directory.getChildFile("master-wet.mp3"), 0, 1); mp3.masterInserts[0]->bypass = true; mp3.exportMp3(directory.getChildFile("master-dry.mp3"), 0, 1);
    auto rms = [&](String file) { std::unique_ptr<AudioFormatReader> r(mp3.formats.createReaderFor(directory.getChildFile(file))); AudioBuffer<float> b(2, 48000); r->read(&b, 0, 48000, 0, true, true); return b.getRMSLevel(0, 4800, 38000); };
    const float ratio = rms("master-wet.mp3") / rms("master-dry.mp3"); check(ratio > 0.47f && ratio < 0.53f, "MP3 export includes master processing with the expected gain ratio");
    e.recordingDirectory = directory.getChildFile("MasterRaw"); e.tracks[0]->armed = true; e.seek(0); e.startRecordingWriters(); AudioBuffer<float> in(2, 128);
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) in.setSample(ch, i, 0.2f);
    e.render(out.getArrayOfWritePointers(), 2, 128, in.getArrayOfReadPointers(), 2); e.finishRecording();
    check(std::abs(e.tracks[0]->clips.back().source->samples.getSample(0, 40) - 0.2f) < 1e-6, "Master effects do not alter raw recorded input files");
    e.recording = true; check(!w.trackInsertAction({}, first, 3), "Recording prevents master insert deletion"); e.recording = false;
    w.toggleMixer(); auto* master = w.mixer->findChildWithID("mixerMaster"); auto* list = dynamic_cast<ListBox*>(master->findChildWithID("masterInserts"));
    check(list && list->isVisible() && list->getModel()->getNumRows() == 2 && master->findChildWithID("masterAddInsert")->isVisible(), "Mixer MASTER strip exposes its own add controls and insert list");
    check(w.trackInsertAction({}, first, 3) && !w.trackInsertAction({}, first, 2) && e.masterInserts.size() == 1, "Removing master effects rejects stale targets without changing another insert");
    PluginDescription instrument = *descriptions[0]; instrument.isInstrument = true; bool rejected = false; try { e.addPlugin(-1, instrument); } catch (...) { rejected = true; }
    check(rejected && e.masterInserts.size() == 1, "Master chain rejects instrument plugins");
    loaded.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis")); check(loaded.masterInserts.empty(), "Older projects load safely with an empty master chain");
    e.reset(); check(e.masterInserts.empty(), "New project resets master inserts");
    e.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis")); e.addPlugin(-1, *descriptions[0]); w.refresh();
    if (auto stream = directory.getChildFile("master-inserts-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(w.createComponentSnapshot(w.getLocalBounds()), *stream); }
    directory.getChildFile("master-inserts-verification.txt").replaceWithText("Synthetic/offline only. Verified summed master processing, serial chain, bypass, master fader order, plugin state/bypass persistence, unsaved-state detection, WAV/MP3 export, unchanged raw input, native MASTER controls, recording guards, stale targets, instrument rejection, old project compatibility and reset.\n");
}
}
