#include "Interface.h"
#include <cmath>
#include <limits>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif
namespace auralis {
void runClipGainTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto approx = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    auto pump = [] {
        auto end = Time::getMillisecondCounterHiRes() + 120;
        while (Time::getMillisecondCounterHiRes() < end) {
           #if JUCE_WINDOWS
            MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
           #endif
            Thread::sleep(1);
        }
    };
    File fixture = directory.getChildFile("clip-gain-source.wav"); AudioBuffer<float> source(2, 96000);
    for (int i = 0; i < 96000; ++i) {
        source.setSample(0, i, 0.25f * float(std::sin(MathConstants<double>::twoPi * 440 * i / 48000.0)));
        source.setSample(1, i, 0.125f * float(std::sin(MathConstants<double>::twoPi * 880 * i / 48000.0)));
    }
    { auto stream = fixture.createOutputStream(); stream->setPosition(0); stream->truncate(); WavAudioFormat wav;
      std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 48000, 2, 32, {}, 0)); stream.release(); writer->writeFromAudioSampleBuffer(source, 0, 96000); }
    Workstation workstation(false); workstation.stopTimer(); auto& engine = workstation.engine; auto& timeline = workstation.timeline;
    engine.importAudio(fixture, 0, 0); engine.importAudio(fixture, 0, 2); engine.master = 1; engine.tracks[0]->gain = 1;
    auto& clips = engine.tracks[0]->clips; String trackId = engine.tracks[0]->id, firstId = clips[0].id;
    workstation.setTimelineZoom(140); workstation.dirty = false;
    auto event = [&](Point<float> point, Point<float> origin, int extra = 0) {
        auto now = Time::getCurrentTime(); return MouseEvent(Desktop::getInstance().getMainMouseSource(), point, ModifierKeys(ModifierKeys::leftButtonModifier | extra),
                                                           1, 0, 0, 0, 0, &timeline, &timeline, now, origin, now, 1, point != origin);
    };
    Point<float> wave(timeline.xAt(0.7), float(Timeline::ruler + 64)); auto selected = event(wave, wave); timeline.mouseDown(selected); timeline.mouseUp(selected);
    check(!workstation.dirty && workstation.undo.empty() && timeline.selectedClip == 0, "Selecting an audio part alone creates no gain edit or undo step");
    timeline.mouseDown(selected);
    for (int dy : {4, 8, 12}) timeline.mouseDrag(event(wave.translated(0, float(-dy)), wave));
    timeline.mouseUp(event(wave.translated(0, -12), wave));
    check(approx(clips[0].gainDb, 3) && approx(clips[1].gainDb, 0) && approx(clips[0].start, 0) && approx(clips[0].length, 2),
          "Vertical waveform dragging adjusts only its audio part without moving or trimming it");
    AudioBuffer<float> rendered(2, 512); engine.seek(0.5); engine.play(); engine.render(rendered.getArrayOfWritePointers(), 2, 512); engine.stop();
    check(std::abs(rendered.getSample(0, 100) - source.getSample(0, 24100) * Decibels::decibelsToGain(3.0f)) < 1e-5f,
          "Part volume changes audible playback before track mixing");
    engine.seek(2.5); engine.play(); engine.render(rendered.getArrayOfWritePointers(), 2, 512); engine.stop();
    check(std::abs(rendered.getSample(0, 100) - source.getSample(0, 24100)) < 1e-5f, "Other parts on the same track retain their original volume");
    workstation.undoEdit(); check(approx(clips[0].gainDb, 0) && workstation.undo.empty(), "One undo restores an entire multi-event volume drag");
    Point<float> handle(timeline.xAt(2) - 40, float(Timeline::ruler + 19));
    timeline.mouseDown(event(handle, handle, ModifierKeys::ctrlModifier));
    timeline.mouseDrag(event(handle.translated(0, 20), handle, ModifierKeys::ctrlModifier)); timeline.mouseUp(event(handle.translated(0, 20), handle));
    check(approx(clips[0].gainDb, -1), "The dB handle supports fine Ctrl-drag volume adjustment");
    workstation.setClipGain(trackId, firstId, 0); check(approx(clips[0].gainDb, 0), "Part volume can reset to original 0 dB");
    auto dismiss = [&](String button, String value) {
        auto* prompt = workstation.clipGainPrompt.getComponent(); check(prompt != nullptr, "Part volume numeric dialog opens for its target");
        prompt->setAlpha(0); prompt->getTextEditor("gain")->setText(value, false); prompt->triggerButtonClick(button); pump();
    };
    workstation.showClipGainDialog(trackId, firstId); dismiss("Apply", "-6.5");
    check(approx(clips[0].gainDb, -6.5) && approx(clips[1].gainDb, 0), "Numeric Apply sets the exact target part volume in dB");
    workstation.showClipGainDialog(trackId, firstId); dismiss("Cancel", "-12");
    check(approx(clips[0].gainDb, -6.5), "Cancelling numeric volume leaves the original part level unchanged");
    check(!workstation.setClipGain(trackId, firstId, std::numeric_limits<double>::quiet_NaN()) && !workstation.setClipGain(trackId, firstId, 13)
          && !workstation.setClipGain(trackId, "missing", -3), "Invalid dB values and stale part IDs cannot change another clip");
    workstation.cutClipAt(0, 0, 1);
    check(clips.size() == 3 && approx(clips[0].gainDb, -6.5) && approx(clips[2].gainDb, -6.5), "Cut parts inherit their original clip volume");
    workstation.setClipGain(trackId, clips[2].id, -3);
    check(approx(clips[0].gainDb, -6.5) && approx(clips[2].gainDb, -3), "Split parts can have independent volumes");
    workstation.undoEdit(); workstation.undoEdit();
    File project = directory.getChildFile("clip-gain.auralis"); engine.saveProject(project); Engine loaded; loaded.loadProject(project);
    check(loaded.tracks[0]->clips.size() == 2 && approx(loaded.tracks[0]->clips[0].gainDb, -6.5) && approx(loaded.tracks[0]->clips[1].gainDb, 0),
          "Project save and reload retain per-part volume");
    File exported = directory.getChildFile("clip-gain-mix.wav"); engine.exportWav(exported, 0, 4, 32);
    std::unique_ptr<AudioFormatReader> reader(engine.formats.createReaderFor(exported)); AudioBuffer<float> mix(2, 192000); reader->read(&mix, 0, 192000, 0, true, true);
    check(std::abs(mix.getSample(0, 24100) - source.getSample(0, 24100) * Decibels::decibelsToGain(-6.5f)) < 1e-5f
          && std::abs(mix.getSample(0, 120100) - source.getSample(0, 24100)) < 1e-5f, "WAV export includes independent part levels");
    engine.exportMp3(directory.getChildFile("clip-gain-mix.mp3"), 0, 4);
    reader.reset(engine.formats.createReaderFor(directory.getChildFile("clip-gain-mix.mp3")));
    AudioBuffer<float> mp3(2, int(reader->lengthInSamples)); reader->read(&mp3, 0, mp3.getNumSamples(), 0, true, true);
    float ratio = mp3.getRMSLevel(0, 19200, 19200) / mp3.getRMSLevel(0, 115200, 19200);
    check(ratio > 0.35f && ratio < 0.6f, "MP3 export includes the same independent part volume changes");
    timeline.mouseDown(event(wave, wave)); timeline.mouseDrag(event(wave.translated(0, 500), wave)); timeline.mouseUp(event(wave.translated(0, 500), wave));
    check(approx(clips[0].gainDb, -60) && approx(clips[0].start, 0), "Dragging down clamps part gain at -60 dB without moving tracks");
    timeline.mouseDown(event(wave, wave)); timeline.mouseDrag(event(wave.translated(0, -1000), wave)); timeline.mouseUp(event(wave.translated(0, -1000), wave));
    check(approx(clips[0].gainDb, 12) && approx(clips[0].start, 0), "Dragging up clamps part gain at +12 dB");
    timeline.mouseDown(event(wave, wave)); timeline.mouseDrag(event(wave.translated(70, 0), wave)); timeline.mouseUp(event(wave.translated(70, 0), wave));
    check(approx(clips[0].start, 0.5) && approx(clips[0].gainDb, 12), "Horizontal waveform dragging still moves a clip without changing volume"); workstation.undoEdit();
    engine.recording = true; check(!workstation.setClipGain(trackId, firstId, 0), "Recording blocks part volume edits"); engine.recording = false;
    workstation.setClipGain(trackId, firstId, -6.5); timeline.selectedClip = 0; workstation.refresh();
    if (auto stream = directory.getChildFile("clip-gain-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(workstation.createComponentSnapshot(workstation.getLocalBounds()), *stream);
    }
    Engine old; old.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis")); bool defaultGain = true;
    for (auto& track : old.tracks) for (auto& clip : track->clips) defaultGain &= clip.gainDb == 0;
    check(defaultGain, "Older projects load with unchanged 0 dB part volumes");
    directory.getChildFile("clip-gain-verification.txt").replaceWithText("Verified waveform/handle dragging, numeric Apply/Cancel, undo coalescing, sibling independence, audible playback, WAV/MP3 exports, cut inheritance, persistence, gain limits, horizontal movement and recording guards using synthetic audio and actual dialog dismissal.\n");
}
}
