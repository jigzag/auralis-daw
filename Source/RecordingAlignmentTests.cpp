#include "Interface.h"
#include <cmath>
#include <stdexcept>
namespace auralis {
void runRecordingAlignmentTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    for (int hz : {44100, 48000}) for (int blockSize : {64, 512}) {
        Engine e; e.rate = hz; e.master = 1;
        e.inputLatencySamples = 101; e.outputLatencySamples = 1667;
        e.recordingDirectory = directory.getChildFile("Alignment-" + String(hz) + "-" + String(blockSize));
        auto& audio = e.addTrack("Delayed audio"); audio.armed = true;
        auto& midi = e.addTrack("Unshifted MIDI", true); midi.armed = true;
        const int start = hz / 4, length = 997, delay = 1768;
        e.recordRange = true; e.recordFrom = double(start) / hz; e.recordTo = double(start + length) / hz;
        e.cursor = double(start - 300) / hz; e.countInBars = blockSize == 64 ? 1 : 0;
        e.startRecordingWriters(); const int64 count = e.countInRemaining;
        AudioBuffer<float> in(2, blockSize), out(2, blockSize);
        auto value = [](int64 index) { return index == 113 ? 0.8f : float(index % 97) / 1000; };
        int64 elapsed = 0; bool cursorHeld = true, noHardware = e.devices.getCurrentAudioDevice() == nullptr;
        int guard = 0;
        while (!e.recordingComplete && ++guard < 10000) {
            e.incomingMidi.clear();
            for (int i = 0; i < blockSize; ++i) {
                const int64 raw = start - 300 + elapsed + i - count;
                const int64 index = raw - delay - start;
                float sample = index >= 0 && index < length ? value(index) : -0.4f;
                in.setSample(0, i, sample); in.setSample(1, i, -sample);
                if (raw == start + 17) e.incomingMidi.addEvent(MidiMessage::noteOn(1, 60, uint8(100)), i);
                if (raw == start + 117) e.incomingMidi.addEvent(MidiMessage::noteOff(1, 60), i);
            }
            e.render(out.getArrayOfWritePointers(), 2, blockSize, in.getArrayOfReadPointers(), 2);
            cursorHeld &= e.cursor <= e.recordTo + 1e-9;
            elapsed += blockSize;
        }
        String suffix = " (" + String(hz) + " Hz / " + String(blockSize) + ")";
        check(noHardware && e.recordingComplete && cursorHeld && std::abs(e.cursor - e.recordTo) < 1e-9,
              "Compensated recording drains delayed input while the playhead stays at Out" + suffix);
        check(audio.recordedSamples == length && elapsed >= count + 300 + length + delay,
              "Punch recording waits for the complete input/output round trip including its final sample" + suffix);
        e.finishRecording(); auto& take = audio.clips.back(); float error = 0;
        for (int i = 0; i < length; ++i) error = jmax(error, std::abs(take.source->samples.getSample(0, i) - value(i)), std::abs(take.source->samples.getSample(1, i) + value(i)));
        check(take.source->samples.getNumSamples() == length && std::abs(take.start - double(start) / hz) < 1e-9 && error < 2e-7f,
              "Audio alignment cancels round-trip delay sample accurately without losing either punch boundary" + suffix);
        auto& midiTake = midi.clips.back();
        check(midiTake.notes.size() == 1 && std::abs(midiTake.notes[0].beat - 17.0 / hz * 2) < 1e-9
            && std::abs(midiTake.notes[0].length - 100.0 / hz * 2) < 1e-9 && std::abs(midiTake.length - double(length) / hz * 2) < 1e-9,
              "Mixed audio recording compensation leaves MIDI positions and duration unchanged" + suffix);
        e.incomingMidi.clear(); e.render(out.getArrayOfWritePointers(), 2, blockSize, in.getArrayOfReadPointers(), 2);
        check(audio.clips.size() == 1 && !e.recording, "Finalized alignment cannot capture a duplicate delayed tail" + suffix);
        if (hz == 48000 && blockSize == 512) {
            File file = directory.getChildFile("aligned.auralis"); e.saveProject(file); Engine loaded; loaded.loadProject(file);
            check(std::abs(loaded.tracks[0]->clips[0].start - take.start) < 1e-9
                && loaded.tracks[0]->clips[0].source->samples.getNumSamples() == length, "Save and reload retain the aligned recording without reapplying correction");
        }
    }
    {
        Engine e; e.inputLatencySamples = 100; e.outputLatencySamples = 200; e.recordingDirectory = directory.getChildFile("Alignment-free");
        auto& t = e.addTrack(); t.armed = true; e.cursor = 5; e.startRecordingWriters();
        AudioBuffer<float> in(2, 128), out(2, 128); int elapsed = 0;
        for (int block = 0; block < 8; ++block) {
            in.clear(); for (int i = 0; i < 128; ++i) if (elapsed + i == 400) { in.setSample(0, i, 0.5f); in.setSample(1, i, 0.5f); }
            e.render(out.getArrayOfWritePointers(), 2, 128, in.getArrayOfReadPointers(), 2); elapsed += 128;
        }
        e.stop(); auto& c = t.clips.back();
        check(std::abs(c.start - 5) < 1e-9 && c.source->samples.getNumSamples() == 724 && std::abs(c.source->samples.getSample(0, 100) - 0.5f) < 2e-7f,
              "Ordinary manually stopped audio recording aligns its impulse and discards pre-take input");
        check(e.audioDiagnostics().contains("300 samples"), "Diagnostics reports the automatic recording correction in samples");
    }
    {
        Engine e; e.inputLatencySamples = 500; e.outputLatencySamples = 1000; e.recordingDirectory = directory.getChildFile("Alignment-midi-only");
        e.addTrack("MIDI only", true).armed = true; e.recordRange = true; e.recordTo = 128.0 / 48000; e.startRecordingWriters();
        AudioBuffer<float> out(2, 128); e.render(out.getArrayOfWritePointers(), 2, 128);
        check(e.recordingComplete && e.recordingLatencySamples == 0, "Audio round-trip correction does not delay MIDI-only recording completion"); e.finishRecording();
    }
    directory.getChildFile("recording-alignment-verification.txt").replaceWithText("Synthetic delayed input only; no audio hardware opened. Verified exact input+output latency cancellation at 44.1/48 kHz and 64/512 samples, count-in, pre-roll, complete punch boundaries, held Out playhead, mixed MIDI timing, manual Stop, persistence and diagnostics. Hardware loopback calibration remains device-dependent.\n");
}
}
