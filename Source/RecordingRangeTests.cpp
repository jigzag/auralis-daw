#include "Interface.h"
#include <cmath>
#include <stdexcept>

namespace auralis {
void runRecordingRangeTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-7; };
    for (int blockSize : {64, 128, 512}) {
        Engine engine; engine.master = 1; engine.rate = 48000; engine.setTempo(137);
        engine.recordingDirectory = directory.getChildFile("Range" + String(blockSize));
        auto& track = engine.addTrack("Audio range"); track.armed = true;
        auto& midi = engine.addTrack("MIDI range", true); midi.armed = true;
        engine.recordRange = true; engine.recordFrom = 1.25; engine.recordTo = 1.25 + 997.0 / 48000;
        engine.countInBars = 1; engine.cursor = 7; engine.startRecordingWriters();
        const auto count = std::llround(4 * 60.0 / 137 * 48000);
        check(engine.countInRemaining == count && near(engine.cursor, 1.25), "Count-in starts at range In with exact beat duration (" + String(blockSize) + ")");
        AudioBuffer<float> input(2, blockSize), output(2, blockSize);
        int64 elapsed = 0; float countPeak = 0; int guard = 0; bool countIsClean = true;
        std::vector<float> expected;
        while (!engine.recordingComplete && ++guard < 10000) {
            engine.incomingMidi.clear();
            for (int i = 0; i < blockSize; ++i) {
                float sample = float((elapsed + i) % 997) / 2000.0f;
                input.setSample(0, i, sample); input.setSample(1, i, -sample);
                int64 local = elapsed + i - count;
                if (local >= 0 && local < 997) expected.push_back(sample);
                if (local == -1) engine.incomingMidi.addEvent(MidiMessage::noteOn(1, 50, uint8(100)), i);
                if (local == 0) engine.incomingMidi.addEvent(MidiMessage::noteOn(1, 60, uint8(90)), i);
                if (local == 300) engine.incomingMidi.addEvent(MidiMessage::noteOff(1, 60), i);
                if (local == 400) engine.incomingMidi.addEvent(MidiMessage::noteOn(1, 64, uint8(95)), i);
                if (local == 997) engine.incomingMidi.addEvent(MidiMessage::noteOn(1, 72, uint8(100)), i);
            }
            engine.render(output.getArrayOfWritePointers(), 2, blockSize, input.getArrayOfReadPointers(), 2);
            if (elapsed + blockSize < count) {
                countPeak = jmax(countPeak, output.getMagnitude(0, blockSize));
                countIsClean &= track.recordedSamples == 0 && midi.recordedMidi.empty() && near(engine.cursor, 1.25);
            }
            elapsed += blockSize;
        }
        check(countIsClean, "Count-in excludes input and keeps transport at In (" + String(blockSize) + ")");
        check(engine.recordingComplete && !engine.playing && near(engine.cursor, engine.recordTo), "Recording stops at exact Out within an audio block (" + String(blockSize) + ")");
        check(countPeak > 0.05f && !engine.metronome && track.recordedSamples == 997, "Count-in sounds with metronome off and audio length excludes count (" + String(blockSize) + ")");
        engine.incomingMidi.clear(); engine.render(output.getArrayOfWritePointers(), 2, blockSize);
        check(track.recordedSamples == 997, "Pending UI finalization never records beyond Out (" + String(blockSize) + ")");
        engine.finishRecording();
        auto& take = track.clips.back(); float error = 0;
        for (int i = 0; i < 997; ++i) error = jmax(error, std::abs(take.source->samples.getSample(0, i) - expected[size_t(i)]), std::abs(take.source->samples.getSample(1, i) + expected[size_t(i)]));
        check(near(take.start, 1.25) && take.source->samples.getNumSamples() == 997 && error < 2e-7f,
              "Range audio captures exact source samples across count-in and Out boundaries (" + String(blockSize) + ")");
        auto& midiTake = midi.clips.back();
        check(midiTake.notes.size() == 2 && midiTake.notes[0].pitch == 60 && midiTake.notes[1].pitch == 64
              && near(midiTake.start, 1.25 * 137 / 60.0) && near(midiTake.length, 997.0 / 48000 * 137 / 60.0),
              "MIDI range rejects events before In and at Out and closes held notes at Out (" + String(blockSize) + ")");
        check(near(midiTake.notes[0].beat, 0) && near(midiTake.notes[0].length, 300.0 / 48000 * 137 / 60.0)
              && near(midiTake.notes[1].length, 597.0 / 48000 * 137 / 60.0), "MIDI range preserves within-block timing and held-note duration (" + String(blockSize) + ")");
    }
    {
        Engine engine; engine.recordingDirectory = directory.getChildFile("CancelledCount");
        auto& track = engine.addTrack("Cancel"); track.armed = true; engine.countInBars = 2; engine.cursor = 3;
        engine.startRecordingWriters(); AudioBuffer<float> output(2, 512); engine.render(output.getArrayOfWritePointers(), 2, 512);
        auto emptyFile = track.recordedFile; engine.stop();
        check(track.clips.empty() && !emptyFile.existsAsFile() && engine.countInRemaining == 0 && !engine.recording && near(engine.cursor, 3),
              "Stop during two-bar count-in creates no empty take and preserves transport position");
    }
    {
        Workstation workstation(false); workstation.stopTimer(); auto& engine = workstation.engine;
        auto* count = dynamic_cast<ComboBox*>(workstation.findChildWithID("countIn"));
        auto* range = dynamic_cast<TextButton*>(workstation.findChildWithID("recordRange"));
        auto* from = dynamic_cast<TextEditor*>(workstation.findChildWithID("recordFrom"));
        auto* to = dynamic_cast<TextEditor*>(workstation.findChildWithID("recordTo"));
        check(count && range && from && to && !engine.recordRange && engine.countInBars == 0, "Count-in and range controls default off");
        count->setSelectedId(3, sendNotificationSync); range->onClick(); from->setText("2.5"); to->setText("2.6"); to->onFocusLost();
        check(engine.countInBars == 2 && engine.recordRange && near(engine.recordFrom, 2.5) && near(engine.recordTo, 2.6), "Recording range GUI updates engine settings");
        auto project = directory.getChildFile("range-settings.auralis"); engine.saveProject(project); engine.reset(); engine.loadProject(project); workstation.refresh();
        check(engine.countInBars == 2 && range->getToggleState() && near(from->getText().getDoubleValue(), 2.5), "Count-in and recording range survive project save and reload");
        engine.countInBars = 0; engine.recordingDirectory = directory.getChildFile("AutoFinish"); engine.tracks[0]->armed = true;
        engine.startRecordingWriters(); workstation.timerCallback();
        check(!count->isEnabled() && !range->isEnabled() && !from->isEnabled() && !to->isEnabled(), "Recording locks count-in and range controls");
        AudioBuffer<float> input(2, 512), output(2, 512); input.clear();
        while (!engine.recordingComplete) engine.render(output.getArrayOfWritePointers(), 2, 512, input.getArrayOfReadPointers(), 2);
        workstation.timerCallback();
        check(!engine.recording && engine.tracks[0]->clips.size() == 1 && count->isEnabled() && range->isEnabled(), "UI finalizes completed range recording and restores controls");
        range->onClick(); check(!engine.recordRange, "Recording range can be disabled for ordinary recording");
        engine.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis"));
        check(engine.countInBars == 0 && !engine.recordRange, "Older projects load with count-in and recording range off");
        if (auto stream = directory.getChildFile("recording-controls-preview.png").createOutputStream()) {
            workstation.refresh(); stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(workstation.createComponentSnapshot(workstation.getLocalBounds()), *stream);
        }
        workstation.setSize(1280, 820);
        auto snapshot = [&](String name) {
            if (auto stream = directory.getChildFile(name).createOutputStream()) {
                stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(workstation.createComponentSnapshot(workstation.getLocalBounds()), *stream);
            }
        };
        snapshot("layout-compact-1280.png");
        if (auto* options = dynamic_cast<TextButton*>(workstation.findChildWithID("transportOptions"))) options->onClick();
        snapshot("recording-controls-preview.png");
    }
    directory.getChildFile("recording-range-verification.txt").replaceWithText("Verified count-in, cancellation, sub-block audio and MIDI range capture, held notes, exact Out stop, project compatibility, controls and UI finalization at 64/128/512 samples using synthetic input only.\n");
}
}
