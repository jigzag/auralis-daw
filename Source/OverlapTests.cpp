#include "Interface.h"
#include <cmath>
#include <stdexcept>
namespace auralis {
void runOverlapTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto fixture = [&](String name, float level, bool sine = false) {
        File file = directory.getChildFile(name + ".wav"); AudioBuffer<float> audio(2, 192000);
        for (int i = 0; i < audio.getNumSamples(); ++i) {
            float sample = level * (sine ? float(std::sin(MathConstants<double>::twoPi * 440 * i / 48000.0)) : 1);
            audio.setSample(0, i, sample); audio.setSample(1, i, -sample);
        }
        auto stream = file.createOutputStream(); stream->setPosition(0); stream->truncate(); WavAudioFormat wav;
        std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 48000, 2, 24, {}, 0)); stream.release(); writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
        return file;
    };
    File oldFile = fixture("overlap-old", 0.1f), hiddenFile = fixture("overlap-middle", 0.15f), newFile = fixture("overlap-new", 0.2f), latestFile = fixture("overlap-latest", 0.3f), silentFile = fixture("overlap-silence", 0);
    auto addClip = [&](Engine& engine, int track, const File& file, int start, int length) {
        Clip clip; clip.source = engine.readAudio(file); clip.start = start / 48000.0; clip.length = length / 48000.0;
        clip.fadeIn = clip.fadeOut = 0; clip.name = file.getFileNameWithoutExtension(); engine.stampNewClip(clip);
        engine.tracks[size_t(track)]->clips.push_back(clip);
    };
    auto sample = [](Engine& engine, int frame) {
        engine.seek(frame / 48000.0); engine.play(); AudioBuffer<float> output(2, 64); engine.render(output.getArrayOfWritePointers(), 2, 64); engine.stop(); return output.getSample(0, 0);
    };
    for (int block : {64, 128, 512}) {
        Engine engine; auto& track = engine.addTrack("Layer priority"); engine.master = track.gain = 1;
        addClip(engine, 0, oldFile, 0, 1500); addClip(engine, 0, hiddenFile, 500, 100); addClip(engine, 0, newFile, 137, 632); addClip(engine, 0, latestFile, 211, 126);
        AudioBuffer<float> output(2, block); float error = 0; engine.play();
        for (int frame = 0; frame < 1536; frame += block) {
            engine.render(output.getArrayOfWritePointers(), 2, block);
            for (int i = 0; i < block; ++i) {
                int at = frame + i; float expected = at >= 1500 ? 0 : at >= 211 && at < 337 ? 0.3f : at >= 137 && at < 769 ? 0.2f : 0.1f;
                error = jmax(error, std::abs(output.getSample(0, i) - expected), std::abs(output.getSample(1, i) + expected));
            }
        }
        engine.stop(); check(error < 1e-6f, "Only the newest audio part plays across nested overlaps and exact boundaries (" + String(block) + ")");
        addClip(engine, 0, silentFile, 400, 100);
        check(std::abs(sample(engine, 450)) < 1e-7, "A newest silent part masks older audio instead of mixing it (" + String(block) + ")");
        track.clips.pop_back(); check(std::abs(sample(engine, 450) - 0.2f) < 1e-6, "Removing the newest part reveals the older recording (" + String(block) + ")");
        engine.splitClip(0, 0, 600 / 48000.0);
        check(track.clips.back().order == track.clips[0].order && std::abs(sample(engine, 650) - 0.2f) < 1e-6, "Splitting an old recording does not raise its overlap priority (" + String(block) + ")");
        std::reverse(track.clips.begin(), track.clips.end());
        check(std::abs(sample(engine, 650) - 0.2f) < 1e-6, "Creation priority is independent of clip array order (" + String(block) + ")");
        File project = directory.getChildFile("overlap-" + String(block) + ".auralis"); engine.saveProject(project); Engine loaded; loaded.loadProject(project);
        check(std::abs(sample(loaded, 650) - 0.2f) < 1e-6, "Project reload retains the latest-part priority (" + String(block) + ")");
        addClip(loaded, 0, latestFile, 600, 100); check(std::abs(sample(loaded, 650) - 0.3f) < 1e-6, "A new part after reload receives higher priority (" + String(block) + ")");
        auto& independent = loaded.addTrack("Other track"); independent.gain = 1; addClip(loaded, 1, oldFile, 0, 1500);
        check(std::abs(sample(loaded, 650) - 0.4f) < 1e-6, "Different tracks continue to mix normally (" + String(block) + ")");
    }
    auto testTone = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory().getChildFile("TestTone.vst3");
    for (int block : {64, 512}) {
        Engine engine; auto& track = engine.addTrack("MIDI layers", true); engine.master = track.gain = 1;
        OwnedArray<PluginDescription> found; for (auto* format : engine.pluginFormats.getFormats()) format->findAllTypesForFile(found, testTone.getFullPathName());
        check(found.size() == 1, "Overlap regression test instrument is available"); engine.addPlugin(0, *found[0]);
        auto& old = engine.addMidiClip(0, 0); old.length = 1500.0 / 24000; old.notes = {{0, 1500.0 / 24000, 60, 127, 1}};
        auto& newest = engine.addMidiClip(0, 137.0 / 48000); newest.length = 632.0 / 24000; newest.notes = {{0, 632.0 / 24000, 67, 127, 2}};
        newest.events.push_back({0, MidiMessage::controllerEvent(2, 64, 127)});
        auto& silent = engine.addMidiClip(0, 211.0 / 48000); silent.length = 126.0 / 24000;
        AudioBuffer<float> output(2, block); engine.play(); float error = 0;
        for (int frame = 0; frame < 1536; frame += block) {
            engine.render(output.getArrayOfWritePointers(), 2, block);
            for (int i = 0; i < block; ++i) { int at = frame + i; float expected = at >= 1500 || (at >= 211 && at < 337) ? 0 : 0.25f; error = jmax(error, std::abs(output.getSample(0, i) - expected)); }
        }
        engine.stop(); check(error < 1e-6, "MIDI handoffs silence old notes, honor empty new parts and resume held notes afterward (" + String(block) + ")");
        track.clips.pop_back(); track.clips[0].notes[0].length = 300.0 / 24000; track.clips[1].notes[0].pitch = 60; track.clips[1].notes[0].channel = 1;
        engine.seek(0); engine.play(); error = 0;
        for (int frame = 0; frame < 1024; frame += block) {
            engine.render(output.getArrayOfWritePointers(), 2, block);
            for (int i = 0; i < block; ++i) error = jmax(error, std::abs(output.getSample(0, i) - (frame + i < 769 ? 0.25f : 0.0f)));
        }
        engine.stop(); check(error < 1e-6, "Hidden old note-offs cannot interrupt a newer note on the same MIDI channel (" + String(block) + ")");
    }
    Workstation workstation(false); workstation.stopTimer(); auto& engine = workstation.engine; auto& timeline = workstation.timeline;
    engine.master = engine.tracks[0]->gain = 1;
    File sineOld = fixture("overlap-sine-old", 0.1f, true), sineNew = fixture("overlap-sine-new", 0.2f, true);
    addClip(engine, 0, sineOld, 0, 144000); addClip(engine, 0, sineNew, 24000, 48000); engine.splitClip(0, 0, 1);
    workstation.setTimelineZoom(150);
    Point<float> p(timeline.xAt(1.25), float(Timeline::ruler + 65)); auto now = Time::getCurrentTime();
    MouseEvent e(Desktop::getInstance().getMainMouseSource(), p, ModifierKeys(ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, &timeline, &timeline, now, p, now, 1, false);
    timeline.mouseDown(e); timeline.mouseUp(e);
    check(timeline.selectedClip == 1, "Clicking overlapping parts selects the visibly newest recording even after an older part was split");
    workstation.rememberEdit(); engine.tracks[0]->clips.erase(engine.tracks[0]->clips.begin() + 1); workstation.undoEdit();
    check(engine.tracks[0]->clips.size() == 3 && engine.tracks[0]->clips[1].order > engine.tracks[0]->clips[2].order, "Undo restores overlap priorities together with clips");
    engine.exportWav(directory.getChildFile("overlap-mix.wav"), 0, 3, 32);
    std::unique_ptr<AudioFormatReader> reader(engine.formats.createReaderFor(directory.getChildFile("overlap-mix.wav")));
    AudioBuffer<float> wav(2, 144000); reader->read(&wav, 0, 144000, 0, true, true);
    float ratio = wav.getRMSLevel(0, 31200, 24000) / wav.getRMSLevel(0, 84000, 24000);
    check(ratio > 1.99f && ratio < 2.01f, "WAV export renders the latest part rather than summing overlapping recordings");
    engine.exportMp3(directory.getChildFile("overlap-mix.mp3"), 0, 3);
    reader.reset(engine.formats.createReaderFor(directory.getChildFile("overlap-mix.mp3"))); AudioBuffer<float> mp3(2, int(reader->lengthInSamples)); reader->read(&mp3, 0, mp3.getNumSamples(), 0, true, true);
    ratio = mp3.getRMSLevel(0, 31200, 24000) / mp3.getRMSLevel(0, 84000, 24000);
    check(ratio > 1.85f && ratio < 2.15f, "MP3 export follows the same newest-part overlap policy");
    timeline.selectedClip = 1; workstation.refresh();
    if (auto stream = directory.getChildFile("overlap-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(workstation.createComponentSnapshot(workstation.getLocalBounds()), *stream); }
    directory.getChildFile("overlap-verification.txt").replaceWithText("Verified exact audio overlap boundaries at 64/128/512 samples, nested and silent latest parts, split/array-order independence, persistence, new allocation after load, independent tracks, MIDI handoffs/sustain/same-note protection, UI selection, undo and WAV/MP3 exports.\n");
}
}
