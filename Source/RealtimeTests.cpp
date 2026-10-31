#include "Interface.h"
#include "PianoRoll.h"
#include <thread>
#include <stdexcept>

namespace auralis {
void runRealtimeTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Workstation workstation(false);
    workstation.stopTimer();
    auto& engine = workstation.engine;
    check(engine.devices.getCurrentAudioDevice() == nullptr, "Input-monitor regression test does not open audio hardware");
    auto& track = *engine.tracks[0];
    track.monitor = true; track.gain = 1; engine.master = 1;
    auto source = std::make_shared<SourceAudio>(); source->samples.setSize(2, 192000); source->rate = 48000;
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < source->samples.getNumSamples(); ++i) source->samples.setSample(ch, i, float(std::sin(i * 0.02) * 0.5));
    source->buildWaveform();
    for (int i = 0; i < 8; ++i) { Clip clip; clip.source = source; clip.start = i * 4; clip.length = 4; clip.name = "Waveform stress"; track.clips.push_back(clip); }
    auto& midi = engine.addTrack("Piano repaint stress", true); auto& clip = engine.addMidiClip(1, 0, 16);
    for (int i = 0; i < 256; ++i) clip.notes.push_back({i * 0.0625, 0.125, 48 + i % 36, 100, 1});
    PianoRoll piano(workstation, midi.id, clip.id); piano.stopTimer();
    workstation.refresh();
    AudioBuffer<float> input(2, 128), output(2, 128);
    for (int i = 0; i < 128; ++i) { input.setSample(0, i, 0.25f); input.setSample(1, i, -0.125f); }
    std::atomic<bool> running {true}; std::atomic<int> blocks {0}, silentBlocks {0}; WaitableEvent started;
    std::thread audio([&] {
        started.signal();
        while (running) {
            engine.render(output.getArrayOfWritePointers(), 2, 128, input.getArrayOfReadPointers(), 2);
            if (output.getSample(0, 0) != 0.25f || output.getSample(1, 127) != -0.125f) ++silentBlocks;
            ++blocks; std::this_thread::yield();
        }
    });
    started.wait(5000);
    Image image(Image::RGB, workstation.timeline.getWidth(), workstation.timeline.getHeight(), true);
    Image pianoImage(Image::RGB, piano.getWidth(), piano.getHeight(), true);
    for (int frame = 0; frame < 100; ++frame) {
        { Graphics g(image); workstation.timeline.paint(g); }
        { Graphics g(pianoImage); piano.paint(g); }
        piano.timerCallback();
    }
    running = false; audio.join();
    directory.getChildFile("monitor-paint-stress.txt").replaceWithText("Rendered 128-sample input-monitor blocks: " + String(blocks.load()) + "\nSilent/corrupted blocks during 100 waveform + piano repaints and timer updates: " + String(silentBlocks.load()) + "\nSkipped audio blocks: " + String(engine.lockMisses.load()) + "\nNo audio hardware was opened.\n");
    check(blocks > 0, "Input-monitor regression test renders concurrent audio blocks");
    check(silentBlocks == 0, "Input monitor stays continuous during timeline repaint (" + String(silentBlocks.load()) + " silent blocks)");
    check(engine.lockMisses == 0, "Waveform and piano repaint/timer activity never takes the audio lock");

    auto& waveformTrack = engine.addTrack("Waveform display", false); waveformTrack.mute = true;
    waveformTrack.clips = track.clips; track.clips.clear(); midi.mute = true;
    track.armed = true; engine.recordingDirectory = directory.getChildFile("Recordings");
    String recordingResults;
    for (int bufferSize : {64, 128, 512}) {
        track.clips.clear(); engine.startRecordingWriters();
        AudioBuffer<float> recordingInput(2, bufferSize), recordingOutput(2, bufferSize);
        for (int i = 0; i < bufferSize; ++i) { recordingInput.setSample(0, i, 0.25f); recordingInput.setSample(1, i, -0.125f); }
        std::atomic<int> recordingErrors {0}; WaitableEvent recordingStarted;
        std::thread recordAudio([&] {
            recordingStarted.signal();
            for (int blockIndex = 0; blockIndex < 128; ++blockIndex) {
                engine.render(recordingOutput.getArrayOfWritePointers(), 2, bufferSize, recordingInput.getArrayOfReadPointers(), 2);
                if (recordingOutput.getSample(0, 0) != 0.25f || recordingOutput.getSample(1, bufferSize - 1) != -0.125f) ++recordingErrors;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        recordingStarted.wait(5000);
        for (int frame = 0; frame < 100; ++frame) { { Graphics g(image); workstation.timeline.paint(g); } { Graphics g(pianoImage); piano.paint(g); } }
        recordAudio.join(); engine.finishRecording();
        check(recordingErrors == 0 && engine.dropped == 0 && engine.lockMisses == 0,
              String(bufferSize) + "-sample recording and input monitoring stay continuous during waveform/piano repaint");
        const auto& take = track.clips.back().source->samples;
        auto range = FloatVectorOperations::findMinAndMax(take.getReadPointer(0), take.getNumSamples());
        check(take.getNumSamples() == 128 * bufferSize && std::abs(range.getStart() - 0.25f) < 1e-6 && std::abs(range.getEnd() - 0.25f) < 1e-6,
              String(bufferSize) + "-sample recorded WAV retains every synthetic input sample without silent gaps");
        recordingResults += "Recording/monitor at " + String(bufferSize) + " samples: 128 blocks, skipped " + String(engine.lockMisses.load()) + ", dropped " + String(engine.dropped.load()) + ", corrupted blocks " + String(recordingErrors.load()) + "\n";
    }
    directory.getChildFile("monitor-paint-stress.txt").appendText(recordingResults);

    // Hold the edit lock on another thread. UI paint must still render complete views.
    WaitableEvent locked, release;
    std::thread editLock([&] { const ScopedLock guard(engine.lock); locked.signal(); release.wait(5000); });
    locked.wait(5000);
    { Graphics g(image); workstation.timeline.paint(g); }
    { Graphics g(pianoImage); piano.paint(g); }
    bool timelineVisible = image.getPixelAt(0, Timeline::ruler + 10) == track.colour;
    bool pianoVisible = pianoImage.getPixelAt(0, 40) != Colour(0xff11151b);
    engine.render(output.getArrayOfWritePointers(), 2, 128, input.getArrayOfReadPointers(), 2);
    release.signal(); editLock.join();
    check(timelineVisible && pianoVisible, "Audio-lock ownership does not blank waveform or piano rendering");
    check(engine.lockMisses == 1 && engine.dropped == 0, "Skipped monitor blocks are counted even when not recording");
    auto diagnostics = engine.audioDiagnostics();
    check(diagnostics.contains("Blocks skipped due to editing lock: 1"), "Diagnostics report identifies skipped audio blocks");
    engine.resetAudioDiagnostics(); check(engine.lockMisses == 0 && engine.audioXRuns() == 0, "Audio diagnostics counters can be reset");

    SourceAudio peaks; peaks.samples.setSize(1, 4097); peaks.samples.clear(); peaks.samples.setSample(0, 777, -0.6f); peaks.samples.setSample(0, 4096, 0.9f); peaks.buildWaveform();
    check(std::abs(peaks.displayPeak(768, 64) - 0.6f) < 1e-6 && std::abs(peaks.displayPeak(0, 4097) - 0.9f) < 1e-6,
          "Waveform cache retains transients and a partial final bucket when zoomed out");

    midi.mute = false; engine.midiCollector.reset(48000);
    Point<float> keyPosition(10, float(28 + (127 - 69) * 16 + 5));
    auto now = Time::getCurrentTime();
    MouseEvent key(Desktop::getInstance().getMainMouseSource(), keyPosition, ModifierKeys(ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, &piano, &piano, now, keyPosition, now, 1, false);
    AudioIODeviceCallbackContext context;
    auto callback = [&] { engine.audioDeviceIOCallbackWithContext(nullptr, 0, output.getArrayOfWritePointers(), 2, 128, context); };
    auto releaseTail = [&] { for (int i = 0; i < 128; ++i) callback(); return output.getMagnitude(0, 128) == 0; };
    piano.mouseDown(key); callback();
    check(output.getMagnitude(0, 128) > 0.01f, "Piano-roll mouse press auditions its instrument through the live callback");
    piano.mouseUp(key);
    check(releaseTail(), "Piano-roll mouse release ends its preview without retriggering");
    piano.mouseDown(key); callback(); piano.focusLost(Component::focusChangedDirectly);
    check(releaseTail(), "Losing piano-roll focus releases the preview note");
    piano.mouseDown(key); callback(); piano.setEnabled(false);
    check(releaseTail(), "Disabling the piano roll releases the preview note");
    piano.setEnabled(true); piano.setVisible(true); piano.mouseDown(key); callback(); piano.setVisible(false);
    check(releaseTail(), "Hiding the piano roll releases the preview note");
}
}
