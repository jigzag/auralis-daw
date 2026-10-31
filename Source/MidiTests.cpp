#include "Engine.h"
#include <cmath>
#include <stdexcept>
#include <thread>
namespace auralis {
void runMidiTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Engine engine;
    bool asio = false; StringArray driverNames;
    for (auto* type : engine.devices.getAvailableDeviceTypes()) if (type->getTypeName() == "ASIO") { asio = true; type->scanForDevices(); driverNames = type->getDeviceNames(); }
    check(asio, "ASIO audio backend is compiled and registered");
    directory.getChildFile("asio-diagnostics.txt").replaceWithText("ASIO backend: enabled\nInstalled drivers: " + driverNames.joinIntoString(", ") + "\nHardware streaming was not exercised by this offline test.\n");
    auto path = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory().getChildFile("TestTone.vst3");
    check(path.exists(), "VST3 test instrument is available");
    OwnedArray<PluginDescription> found;
    for (auto* format : engine.pluginFormats.getFormats()) format->findAllTypesForFile(found, path.getFullPathName());
    check(found.size() == 1 && found[0]->isInstrument, "VST3 instrument is detected as an instrument");
    engine.addTrack("Instrument", true); engine.master = 1; engine.tracks[0]->gain = 1;
    auto& c = engine.addMidiClip(0, 0); c.notes.push_back({100.0 / 24000, 500.0 / 24000, 60, 127, 3});
    engine.addPlugin(0, *found[0]); engine.play();
    AudioBuffer<float> output(2, 512);
    engine.render(output.getArrayOfWritePointers(), 2, 512);
    check(output.getSample(0, 99) == 0 && std::abs(output.getSample(0, 100) - 0.25f) < 1e-5, "MIDI note-on reaches VST3 at its exact sample offset");
    engine.render(output.getArrayOfWritePointers(), 2, 512);
    check(std::abs(output.getSample(0, 87) - 0.25f) < 1e-5 && output.getSample(0, 88) == 0, "MIDI note-off crosses audio blocks at its exact sample offset");
    engine.seek(240.0 / 48000); engine.render(output.getArrayOfWritePointers(), 2, 512);
    check(std::abs(output.getSample(0, 0) - 0.25f) < 1e-5 && output.getSample(0, 360) == 0, "Seeking into a held note chases it and preserves its note-off");
    engine.seek(360.0 / 48000); engine.render(output.getArrayOfWritePointers(), 2, 64);
    engine.setTempo(300); engine.render(output.getArrayOfWritePointers(), 2, 512);
    check(output.getMagnitude(0, 512) == 0, "Live tempo changes clear a note whose new end is before the playhead");
    engine.setTempo(120);
    engine.stop(); engine.render(output.getArrayOfWritePointers(), 2, 512);
    check(output.getMagnitude(0, 512) == 0, "Stopping clears held notes in the VST3 instrument");
    engine.exportWav(directory.getChildFile("midi-instrument.wav"), 0, 0.1);
    auto read = [&](const File& file) {
        std::unique_ptr<AudioFormatReader> r(engine.formats.createReaderFor(file));
        if (!r) throw std::runtime_error("Could not read MIDI render.");
        AudioBuffer<float> audio(2, int(r->lengthInSamples)); r->read(&audio, 0, audio.getNumSamples(), 0, true, true); return audio;
    };
    auto audio = read(directory.getChildFile("midi-instrument.wav"));
    check(audio.getSample(0, 99) == 0 && std::abs(audio.getSample(0, 100) - 0.25f) < 1e-5 && audio.getSample(0, 600) == 0, "WAV export renders MIDI through the VST3 instrument sample-accurately");
    engine.bpm = 60; engine.exportWav(directory.getChildFile("midi-tempo.wav"), 0, 0.1); audio = read(directory.getChildFile("midi-tempo.wav"));
    check(audio.getSample(0, 199) == 0 && std::abs(audio.getSample(0, 200) - 0.25f) < 1e-5 && audio.getSample(0, 1200) == 0, "Changing BPM scales MIDI timing in WAV export");
    engine.bpm = 120; engine.splitClip(0, 0, 300.0 / 48000);
    engine.exportWav(directory.getChildFile("midi-split.wav"), 0, 0.1); audio = read(directory.getChildFile("midi-split.wav"));
    check(engine.tracks[0]->clips.size() == 2 && std::abs(audio.getSample(0, 299) - 0.25f) < 1e-5 && std::abs(audio.getSample(0, 300) - 0.25f) < 1e-5 && audio.getSample(0, 600) == 0, "Splitting a MIDI clip preserves cropped note playback without stuck notes");
    {
        Engine capture; auto& t = capture.addTrack("MIDI recording", true); t.armed = true; t.midiChannel = 3;
        capture.recordingDirectory = directory.getChildFile("MidiRecordings"); capture.cursor = 0.5; capture.startRecordingWriters();
        check(t.recordingMidi && !t.recorder, "MIDI recording starts without an audio-input writer");
        capture.incomingMidi.addEvent(MidiMessage::noteOn(4, 72, uint8(127)), 20);
        capture.incomingMidi.addEvent(MidiMessage::noteOn(3, 60, uint8(100)), 64);
        capture.incomingMidi.addEvent(MidiMessage::controllerEvent(3, 64, 127), 80);
        capture.incomingMidi.addEvent(MidiMessage::pitchWheel(3, 9000), 128);
        capture.render(output.getArrayOfWritePointers(), 2, 512);
        capture.incomingMidi.clear(); capture.incomingMidi.addEvent(MidiMessage::noteOff(3, 60), 96); capture.incomingMidi.addEvent(MidiMessage::noteOn(3, 67, uint8(80)), 200);
        capture.render(output.getArrayOfWritePointers(), 2, 512); capture.incomingMidi.clear(); capture.finishRecording();
        check(t.clips.size() == 1 && t.clips[0].notes.size() == 2 && !t.recordingMidi, "MIDI recording filters channels and creates editable notes");
        auto& take = t.clips[0];
        check(std::abs(take.startTime(120) - 0.5) < 1e-9 && std::abs(take.notes[0].beat * 24000 - 64) < 1e-6 && std::abs(take.notes[0].length * 24000 - 544) < 1e-6, "MIDI recording preserves block offsets, note duration and timeline placement");
        check(take.notes[0].velocity == 100 && take.notes[0].channel == 3 && std::abs(take.notes[1].length * 24000 - 312) < 1e-6, "MIDI recording preserves velocity and closes held notes on stop");
        check(take.events.size() == 2 && take.events[0].message.isController() && take.events[1].message.isPitchWheel(), "MIDI recording captures sustain and pitch bend events");
        auto project = directory.getChildFile("Midi-roundtrip.auralis"); capture.saveProject(project); Engine restored; restored.loadProject(project);
        check(restored.tracks[0]->midi && restored.tracks[0]->midiChannel == 3 && restored.tracks[0]->clips[0].notes.size() == 2 && restored.tracks[0]->clips[0].events.size() == 2, "Project restores MIDI tracks, notes, input channels and controllers");
        auto midiFile = directory.getChildFile("Midi-roundtrip.mid"); capture.exportMidi(midiFile); Engine imported; imported.importMidi(midiFile, 0);
        check(imported.tracks.size() == 1 && imported.tracks[0]->midi && imported.tracks[0]->clips[0].notes.size() == 2, "Standard MIDI file roundtrip preserves recorded notes");
        auto& n = imported.tracks[0]->clips[0].notes[0];
        check(n.pitch == 60 && n.channel == 3 && n.velocity == 100 && std::abs(n.beat - (1 + 64.0 / 24000)) < 1.0 / 960, "Standard MIDI export preserves absolute timing, pitch, channel and velocity");
    }
    {
        Engine synth; synth.addTrack("Built-in", true); auto& clip = synth.addMidiClip(0, 0); clip.notes.push_back({0, 0.01, 69, 100, 1});
        synth.play(); synth.render(output.getArrayOfWritePointers(), 2, 512);
        check(output.getMagnitude(0, 512) > 0.01f, "MIDI tracks can audition through the built-in synth without a plugin");
        synth.stop(); clip.notes = {{0.13, 0.31, 72, 88, 2}}; synth.quantizeClip(clip, 0.25);
        auto mid = directory.getChildFile("quantized.mid"); synth.exportMidi(mid); Engine imported; imported.importMidi(mid, 0);
        auto& n = imported.tracks[0]->clips[0].notes[0];
        check(std::abs(n.beat - 0.25) < 1e-6 && std::abs(n.length - 0.25) < 1e-6, "Quantized note positions and lengths survive MIDI file export");
    }
    {
        Engine routing; routing.addTrack("External 1", true); routing.addTrack("External 2", true);
        for (int i = 0; i < 2; ++i) { routing.tracks[size_t(i)]->sendMidi = true; routing.addMidiClip(i, 0).notes.push_back({0, 1, 60 + i, 100, i + 1}); }
        routing.play(); routing.render(output.getArrayOfWritePointers(), 2, 512);
        int noteOns = 0; bool resetAfterNote = false;
        for (const auto event : routing.externalMidi) { auto m = event.getMessage(); if (m.isNoteOn()) ++noteOns; if (noteOns > 0 && m.isAllSoundOff()) resetAfterNote = true; }
        check(noteOns == 2 && !resetAfterNote, "External MIDI routing merges tracks without resetting another track's note-on");
        routing.stop(); routing.tracks[0]->mute = true; routing.play(); routing.render(output.getArrayOfWritePointers(), 2, 512);
        noteOns = 0; for (const auto event : routing.externalMidi) if (event.getMessage().isNoteOn()) ++noteOns;
        check(noteOns == 1, "External MIDI routing respects track mute");
        routing.stop(); routing.exportWav(directory.getChildFile("external-midi-offline.wav"), 0, 0.1);
        check(routing.externalMidi.isEmpty(), "Offline rendering does not send notes to external MIDI hardware");
    }
    {
        Engine live; live.addTrack("Live keyboard", true); live.master = 1; live.tracks[0]->gain = 1;
        live.midiCollector.reset(48000); AudioBuffer<float> block(2, 128); AudioIODeviceCallbackContext context;
        live.keyboard.noteOn(1, 69, 1.0f);
        live.audioDeviceIOCallbackWithContext(nullptr, 0, block.getArrayOfWritePointers(), 2, 128, context);
        check(block.getMagnitude(0, 128) > 0.01f, "Live keyboard note-on reaches the real audio callback");
        live.keyboard.noteOff(1, 69, 0.0f);
        for (int i = 0; i < 128; ++i) live.audioDeviceIOCallbackWithContext(nullptr, 0, block.getArrayOfWritePointers(), 2, 128, context);
        directory.getChildFile("live-midi-release.txt").replaceWithText("Output peak after live keyboard release: " + String(block.getMagnitude(0, 128), 9) + "\nNo audio hardware was opened.\n");
        check(block.getMagnitude(0, 128) == 0, "Live keyboard release stays silent across subsequent audio callbacks");
        auto process = [&] { live.audioDeviceIOCallbackWithContext(nullptr, 0, block.getArrayOfWritePointers(), 2, 128, context); };
        live.keyboard.noteOn(1, 69, 1.0f); process(); process();
        float expected = float(0.18 * std::sin(MathConstants<double>::twoPi * 440 * 128 / 48000));
        check(std::abs(block.getSample(0, 0) - expected) < 1e-5, "A held live note is not retriggered at each audio block boundary");

        WaitableEvent locked, release;
        std::thread editing([&] { const ScopedLock guard(live.lock); locked.signal(); release.wait(5000); });
        locked.wait(5000); live.keyboard.noteOff(1, 69, 0); process(); release.signal(); editing.join();
        for (int i = 0; i < 128; ++i) process();
        check(live.lockMisses == 1 && block.getMagnitude(0, 128) == 0, "A skipped audio block does not discard the live keyboard note-off");

        for (int click = 0; click < 8; ++click) { live.keyboard.noteOn(1, 60 + click, 0.8f); live.keyboard.noteOff(1, 60 + click, 0); process(); }
        for (int i = 0; i < 128; ++i) process();
        check(block.getMagnitude(0, 128) == 0, "Rapid live keyboard press/release pairs do not leave sustained voices");
        live.keyboard.noteOn(1, 72, 1); live.stop(); process(); process();
        check(block.getMagnitude(0, 128) == 0 && !live.keyboard.isNoteOn(1, 72), "Stop clears queued keyboard presses before they can sound");

        live.addTrack("Other instrument", true); live.keyboard.noteOn(1, 74, 1); live.selectMidiTrack(1); process();
        check(block.getMagnitude(0, 128) == 0, "Switching tracks does not replay an old queued press on the new instrument");
        live.tracks[1]->sendMidi = true; live.keyboard.noteOn(1, 60, 1); process();
        check(block.getMagnitude(0, 128) > 0.01f, "Live keyboard plays the newly selected instrument track");
        live.keyboard.noteOff(1, 60, 0); process();
        bool sentOff = false, repeatedOn = false;
        for (const auto event : live.externalMidi) { sentOff |= event.getMessage().isNoteOff(); repeatedOn |= event.getMessage().isNoteOn(); }
        check(sentOff && !repeatedOn, "External MIDI release contains a note-off without replaying the previous note-on");
        process(); check(live.externalMidi.isEmpty(), "Idle callbacks do not repeat external MIDI messages");
        for (int i = 0; i < 128; ++i) process();
        check(block.getMagnitude(0, 128) == 0, "The newly selected instrument stops after releasing its live key");
    }
    {
        Engine vst; vst.addTrack("Live VST3", true); vst.master = 1; vst.tracks[0]->gain = 1; vst.addPlugin(0, *found[0]); vst.midiCollector.reset(48000);
        AudioBuffer<float> block(2, 128); AudioIODeviceCallbackContext context;
        vst.keyboard.noteOn(1, 60, 1); vst.audioDeviceIOCallbackWithContext(nullptr, 0, block.getArrayOfWritePointers(), 2, 128, context);
        check(std::abs(block.getSample(0, 64) - 0.25f) < 1e-6, "Live keyboard drives a VST3 instrument through the audio callback");
        vst.keyboard.noteOff(1, 60, 0);
        for (int i = 0; i < 8; ++i) vst.audioDeviceIOCallbackWithContext(nullptr, 0, block.getArrayOfWritePointers(), 2, 128, context);
        check(block.getMagnitude(0, 128) == 0, "VST3 instrument remains silent after live keyboard release");
    }
}
}
