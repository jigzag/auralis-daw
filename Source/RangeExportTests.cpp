#include "Interface.h"
#include <cmath>
#include <stdexcept>
namespace auralis {
void runRangeExportTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    Workstation workstation(false); workstation.stopTimer(); auto& engine = workstation.engine; auto& timeline = workstation.timeline;
    auto event = [&](double time, int y = 28, int extra = 0) {
        auto now = Time::getCurrentTime(); Point<float> point(timeline.xAt(time), float(y));
        return MouseEvent(Desktop::getInstance().getMainMouseSource(), point, ModifierKeys(ModifierKeys::leftButtonModifier | extra),
                          1, 0, 0, 0, 0, &timeline, &timeline, now, point, now, 1, true);
    };
    auto drag = [&](double a, double b, int y = 28, int modifiers = 0) {
        auto down = event(a, y, modifiers), move = event(b, y, modifiers); timeline.mouseDown(down); timeline.mouseDrag(move); timeline.mouseUp(move);
    };
    engine.cursor = 0.25; workstation.dirty = false; drag(3, 1.2);
    check(workstation.hasTimeSelection && near(workstation.timeSelection.getStart(), 1) && near(workstation.timeSelection.getEnd(), 3)
          && near(engine.cursor, 0.25) && !workstation.dirty, "Ruler lower-lane drag selects time in either direction without moving transport");
    timeline.scroll = 4; workstation.refresh(); drag(5.123, 6.789, Timeline::ruler + 45, ModifierKeys::shiftModifier);
    check(near(workstation.timeSelection.getStart(), 5.123) && near(workstation.timeSelection.getEnd(), 6.789), "Shift-drag selects a precise range through tracks with horizontal scrolling");
    workstation.keyPressed(KeyPress('P'));
    check(engine.recordRange && near(engine.recordFrom, 5.123) && near(engine.recordTo, 6.789), "P uses selected range for punch-in");
    workstation.keyPressed(KeyPress('P')); check(!engine.recordRange, "P toggles punch-in off without losing the selected range");
    engine.recording = true; workstation.setTimeSelection(8, 9); engine.recording = false;
    check(near(workstation.timeSelection.getStart(), 5.123), "Recording protects the selected time range");
    timeline.scroll = 0; workstation.setTimeSelection(0.5, 1.5);
    auto source = std::make_shared<SourceAudio>(); source->rate = 48000; source->samples.setSize(2, 144000);
    for (int i = 0; i < 144000; ++i) {
        source->samples.setSample(0, i, 0.25f * float(std::sin(MathConstants<double>::twoPi * 440 * i / 48000.0)));
        source->samples.setSample(1, i, 0.2f * float(std::sin(MathConstants<double>::twoPi * 880 * i / 48000.0)));
    }
    source->buildWaveform(); Clip clip; clip.source = source; clip.length = 3; clip.name = "Range export reference"; clip.fadeIn = clip.fadeOut = 0;
    engine.tracks[0]->clips.push_back(clip); engine.master = 1; engine.tracks[0]->gain = 1;
    for (int kbps : {128, 192, 256, 320}) {
        File file = directory.getChildFile("range-" + String(kbps) + ".mp3");
        engine.metronome = true; engine.cursor = 2.25;
        engine.exportMp3(file, workstation.timeSelection.getStart(), workstation.timeSelection.getEnd(), kbps);
        std::unique_ptr<AudioFormatReader> reader(engine.formats.createReaderFor(file));
        check(reader && reader->numChannels == 2 && reader->sampleRate == 48000 && std::abs(reader->lengthInSamples / reader->sampleRate - 1) < 0.1,
              "MP3 " + String(kbps) + " kbps exports a playable stereo selected range");
        AudioBuffer<float> decoded(2, int(reader->lengthInSamples)); reader->read(&decoded, 0, decoded.getNumSamples(), 0, true, true);
        check(decoded.getMagnitude(0, decoded.getNumSamples()) > 0.15f && decoded.getMagnitude(0, decoded.getNumSamples()) < 0.4f
              && near(engine.cursor, 2.25) && !engine.exporting, "MP3 " + String(kbps) + " kbps preserves audible mix and transport state");
    }
    engine.rate = 96000; engine.exportMp3(directory.getChildFile("range-96k.mp3"), 0.5, 1.5);
    std::unique_ptr<AudioFormatReader> highRate(engine.formats.createReaderFor(directory.getChildFile("range-96k.mp3")));
    check(highRate && highRate->sampleRate == 48000, "High device sample rates are converted to MP3-compatible 48 kHz"); engine.rate = 48000;
    auto protectedFile = directory.getChildFile("protected.mp3"); protectedFile.replaceWithText("original"); bool rejected = false;
    try { engine.exportMp3(protectedFile, 1, 0, 320); } catch (...) { rejected = true; }
    check(rejected && protectedFile.loadFileAsString() == "original" && !engine.exporting, "Invalid MP3 export preserves existing destination and engine state");
    for (int blockSize : {64, 512}) {
        Engine punch; punch.rate = 48000; punch.recordRange = true; punch.recordFrom = 997.0 / 48000; punch.recordTo = 2497.0 / 48000;
        punch.recordingDirectory = directory.getChildFile("Punch" + String(blockSize)); auto& audio = punch.addTrack("Punch audio"); audio.armed = true;
        auto& midi = punch.addTrack("Punch MIDI", true); midi.armed = true; punch.startRecordingWriters();
        check(near(punch.cursor, 0) && near(audio.recordStart, punch.recordFrom), "Punch-in plays pre-roll while arming the selected In point (" + String(blockSize) + ")");
        AudioBuffer<float> input(2, blockSize), output(2, blockSize); int elapsed = 0;
        while (!punch.recordingComplete && elapsed < 10000) {
            punch.incomingMidi.clear();
            for (int i = 0; i < blockSize; ++i) {
                input.setSample(0, i, float(elapsed + i) / 10000); input.setSample(1, i, -float(elapsed + i) / 10000);
                if (elapsed + i == 900) punch.incomingMidi.addEvent(MidiMessage::noteOn(1, 50, uint8(100)), i);
                if (elapsed + i == 1000) punch.incomingMidi.addEvent(MidiMessage::noteOn(1, 60, uint8(100)), i);
                if (elapsed + i == 2000) punch.incomingMidi.addEvent(MidiMessage::noteOff(1, 60), i);
            }
            punch.render(output.getArrayOfWritePointers(), 2, blockSize, input.getArrayOfReadPointers(), 2); elapsed += blockSize;
        }
        punch.finishRecording();
        check(audio.clips.size() == 1 && audio.clips[0].source->samples.getNumSamples() == 1500
              && near(audio.clips[0].source->samples.getSample(0, 0), 0.0997) && near(audio.clips[0].source->samples.getSample(0, 1499), 0.2496),
              "Punch audio excludes pre-roll and stops sample-exactly at Out (" + String(blockSize) + ")");
        check(midi.clips.size() == 1 && midi.clips[0].notes.size() == 1 && midi.clips[0].notes[0].pitch == 60,
              "Punch MIDI excludes pre-roll note events (" + String(blockSize) + ")");
    }
    workstation.refresh();
    if (auto stream = directory.getChildFile("time-selection-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(workstation.createComponentSnapshot(workstation.getLocalBounds()), *stream);
    }
    directory.getChildFile("range-mp3-verification.txt").replaceWithText("Verified ruler/Shift drag time selection, P punch toggle, MP3 128/192/256/320 kbps decoding and range duration, high-rate resampling, failed-export protection and sample-accurate pre-roll punch audio/MIDI at 64/512 samples using synthetic input.\n");
}
}
