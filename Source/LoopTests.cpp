#include "Interface.h"
#include <cmath>
#include <stdexcept>
namespace auralis {
void runLoopTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto approx = [](double a, double b) { return std::abs(a - b) < 1e-7; };
    auto addRamp = [](Engine& engine) {
        auto& track = engine.addTrack("Loop reference"); track.gain = 1; engine.master = 1;
        auto source = std::make_shared<SourceAudio>(); source->rate = 48000; source->samples.setSize(2, 2048);
        for (int i = 0; i < 2048; ++i) { source->samples.setSample(0, i, float(i + 1) / 4096); source->samples.setSample(1, i, -float(i + 1) / 4096); }
        source->buildWaveform(); Clip clip; clip.source = source; clip.length = 2048.0 / 48000; clip.fadeIn = clip.fadeOut = 0; track.clips.push_back(clip);
    };
    for (int block : {64, 128, 512}) {
        Engine engine; addRamp(engine); engine.loop = true; engine.loopStart = 137.0 / 48000; engine.loopEnd = 769.0 / 48000;
        engine.cursor = 500.0 / 48000; engine.midiCollector.reset(48000); engine.play(); AudioBuffer<float> output(2, block); float error = 0;
        for (int frame = 0; frame < 4096; frame += block) {
            int n = jmin(block, 4096 - frame);
            if (block == 128) { AudioIODeviceCallbackContext context; engine.audioDeviceIOCallbackWithContext(nullptr, 0, output.getArrayOfWritePointers(), 2, n, context); }
            else engine.render(output.getArrayOfWritePointers(), 2, n);
            for (int i = 0; i < n; ++i) { float expected = float(138 + (363 + frame + i) % 632) / 4096; error = jmax(error, std::abs(output.getSample(0, i) - expected), std::abs(output.getSample(1, i) + expected)); }
        }
        check(error < 1e-6f && approx(engine.cursor, (137 + (363 + 4096) % 632) / 48000.0), "Range repeat wraps sample-exactly with no overshoot or drift (" + String(block) + ")");
        engine.stop();
    }
    {
        Engine engine; addRamp(engine); engine.loop = true; engine.loopStart = 101.0 / 48000; engine.loopEnd = 138.0 / 48000; engine.play();
        AudioBuffer<float> output(2, 512); engine.render(output.getArrayOfWritePointers(), 2, 512); float error = 0;
        for (int i = 0; i < 512; ++i) error = jmax(error, std::abs(output.getSample(0, i) - float(102 + i % 37) / 4096));
        check(error < 1e-6f && approx(engine.cursor, (101 + 512 % 37) / 48000.0), "Short ranges can repeat multiple times inside one device block");
        engine.stop(); engine.exportWav(directory.getChildFile("loop-export.wav"), 0, 1024.0 / 48000, 32);
        std::unique_ptr<AudioFormatReader> reader(engine.formats.createReaderFor(directory.getChildFile("loop-export.wav"))); AudioBuffer<float> audio(2, 1024); reader->read(&audio, 0, 1024, 0, true, true);
        check(reader->lengthInSamples == 1024 && std::abs(audio.getSample(0, 900) - 901.0f / 4096) < 1e-6f && engine.loop,
              "Offline export uses its requested range once while preserving Loop state");
    }
    {
        Engine monitor; auto& track = monitor.addTrack("Monitor"); track.monitor = true; track.gain = 1; monitor.master = 1;
        monitor.loop = true; monitor.loopStart = 0; monitor.loopEnd = 37.0 / 48000; monitor.play(); AudioBuffer<float> input(2, 512), output(2, 512);
        for (int i = 0; i < 512; ++i) { input.setSample(0, i, float(i) / 2048); input.setSample(1, i, -float(i) / 2048); }
        monitor.render(output.getArrayOfWritePointers(), 2, 512, input.getArrayOfReadPointers(), 2); float error = 0;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i) error = jmax(error, std::abs(output.getSample(ch, i) - input.getSample(ch, i)));
        check(error == 0, "Loop splitting preserves continuous live input-monitor offsets");
        input.clear(); input.applyGain(0); for (int i = 0; i < 512; ++i) input.setSample(0, i, 2);
        monitor.render(output.getArrayOfWritePointers(), 2, 512, input.getArrayOfReadPointers(), 2);
        check(monitor.clipped == 1 && monitor.outputPeak == 2, "Loop meter and clip counter cover a complete device callback"); monitor.stop();
    }
    {
        Engine engine; auto& track = engine.addTrack("Loop MIDI", true); track.gain = 1; track.sendMidi = true; engine.master = 1;
        auto tone = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory().getChildFile("TestTone.vst3"); OwnedArray<PluginDescription> found;
        for (auto* format : engine.pluginFormats.getFormats()) format->findAllTypesForFile(found, tone.getFullPathName());
        check(found.size() == 1, "Loop MIDI regression instrument is available"); engine.addPlugin(0, *found[0]);
        auto& clip = engine.addMidiClip(0, 0); clip.length = 1000.0 / 24000;
        clip.notes = {{137.0 / 24000, 63.0 / 24000, 60, 127, 1}, {700.0 / 24000, 69.0 / 24000, 64, 127, 1}};
        engine.loop = true; engine.loopStart = 137.0 / 48000; engine.loopEnd = 769.0 / 48000; engine.play(); AudioBuffer<float> output(2, 512); float error = 0;
        for (int frame = 0; frame < 4096; frame += 512) {
            engine.render(output.getArrayOfWritePointers(), 2, 512);
            for (int i = 0; i < 512; ++i) { int at = 137 + (frame + i) % 632; float expected = at < 200 || at >= 700 ? 0.25f : 0; error = jmax(error, std::abs(output.getSample(0, i) - expected)); }
        }
        check(error < 1e-6f, "MIDI notes repeat at exact boundaries without accumulating or sticking"); engine.stop();
        track.clips.clear(); engine.loopStart = 0; engine.loopEnd = 127.0 / 48000; engine.seek(0);
        engine.incomingMidi.addEvent(MidiMessage::noteOn(1, 60, uint8(127)), 10); engine.incomingMidi.addEvent(MidiMessage::noteOff(1, 60), 300); engine.play();
        engine.render(output.getArrayOfWritePointers(), 2, 512); int on = 0, off = 0; bool correctOffsets = true;
        for (auto event : engine.externalMidi) { auto m = event.getMessage(); if (m.isNoteOn()) { ++on; correctOffsets &= event.samplePosition == 10; } if (m.isNoteOff()) { ++off; correctOffsets &= event.samplePosition == 300; } }
        check(on == 1 && off == 1 && correctOffsets && engine.incomingMidi.getNumEvents() == 2, "Loop slicing neither duplicates live MIDI nor loses note-offs and retains hardware event offsets"); engine.stop();
    }
    {
        Engine recording; auto& track = recording.addTrack("Punch"); track.armed = true; recording.recordingDirectory = directory.getChildFile("LoopPunch");
        recording.loop = true; recording.loopStart = 137.0 / 48000; recording.loopEnd = 769.0 / 48000;
        recording.recordRange = true; recording.recordFrom = 200.0 / 48000; recording.recordTo = 900.0 / 48000;
        recording.startRecordingWriters(); AudioBuffer<float> input(2, 512), output(2, 512); input.clear();
        for (int i = 0; i < 4 && !recording.recordingComplete; ++i) recording.render(output.getArrayOfWritePointers(), 2, 512, input.getArrayOfReadPointers(), 2);
        check(recording.recordingComplete && approx(recording.cursor, 900.0 / 48000) && track.recordedSamples == 700, "Loop never repeats a punch-in recording or prevents its Out stop"); recording.finishRecording();
    }
    Workstation workstation(false); workstation.stopTimer(); auto& engine = workstation.engine;
    workstation.setTimeSelection(2, 4); workstation.keyPressed(KeyPress('L'));
    check(engine.loop && approx(engine.loopStart, 2) && approx(engine.loopEnd, 4) && approx(engine.cursor, 2) && workstation.loopButton.getToggleState(), "L enables repeat using the selected range and positions an outside playhead at In");
    workstation.keyPressed(KeyPress('L')); check(!engine.loop && workstation.hasTimeSelection, "L disables range repeat without losing the selection");
    workstation.loopButton.onClick(); workstation.setTimeSelection(3, 3.5);
    check(engine.loop && approx(engine.loopStart, 3) && approx(engine.loopEnd, 3.5) && approx(engine.cursor, 3), "Loop button uses selection and follows range changes while enabled");
    engine.recording = true; workstation.toggleLoopSelection(); engine.recording = false;
    check(engine.loop && approx(engine.loopStart, 3), "Recording protects Loop toggle and repeat range");
    workstation.setTimeSelection(1, 1 + 37.0 / 48000); auto project = directory.getChildFile("loop-selection.auralis"); engine.saveProject(project); Engine loaded; loaded.loadProject(project);
    check(loaded.loop && approx(loaded.loopEnd - loaded.loopStart, 37.0 / 48000), "Saved repeat range retains sub-100ms precision");
    workstation.toggleLoopSelection(); workstation.hasTimeSelection = false; engine.loopStart = 5; engine.loopEnd = 6; engine.cursor = 0; workstation.toggleLoopSelection();
    check(engine.loop && approx(engine.loopStart, 5) && approx(engine.loopEnd, 6) && approx(engine.cursor, 5), "Without selection, Loop retains the manual range from Options");
    engine.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis")); engine.loop = false; workstation.setTimeSelection(4, 8); workstation.toggleLoopSelection();
    if (auto stream = directory.getChildFile("range-repeat-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(workstation.createComponentSnapshot(workstation.getLocalBounds()), *stream); }
    directory.getChildFile("loop-verification.txt").replaceWithText("Verified L/Loop selection routing, live selection updates, exact wrap at 64/128/512 samples, multiple wraps per block, MIDI restart/offsets/note-off preservation, input monitoring, meters, one-pass export, recording bypass and persisted loop precision using synthetic input.\n");
}
}
