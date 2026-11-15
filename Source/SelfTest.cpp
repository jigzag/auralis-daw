#include "Interface.h"
#include <cmath>
#include <stdexcept>

namespace auralis {
int runSelfTests(const File& directory) {
    StringArray passed;
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    directory.createDirectory();
    try {
        File source = directory.getChildFile("source.wav");
        {
            WavAudioFormat wav; auto stream = source.createOutputStream(); stream->setPosition(0); stream->truncate();
            std::unique_ptr<AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 48000, 2, 24, {}, 0)); stream.release();
            AudioBuffer<float> audio(2, 48000);
            for (int i = 0; i < 48000; ++i) { audio.setSample(0, i, 0.25f); audio.setSample(1, i, -0.125f); }
            check(writer && writer->writeFromAudioSampleBuffer(audio, 0, 48000), "Create stereo reference WAV");
        }
        Engine engine; engine.addTrack("Reference"); engine.importAudio(source, 0, 0.25);
        engine.master = 1; engine.tracks[0]->gain = 1;
        auto& clip = engine.tracks[0]->clips[0]; clip.fadeIn = clip.fadeOut = 0;
        AudioBuffer<float> output(2, 512);
        engine.play(); engine.cursor = 0;
        engine.render(output.getArrayOfWritePointers(), 2, 512);
        check(output.getMagnitude(0, 512) < 1e-6f, "Clip start offset produces silence");
        engine.cursor = 0.5; engine.render(output.getArrayOfWritePointers(), 2, 512);
        check(std::abs(output.getSample(0, 100) - 0.25f) < 1e-5 && std::abs(output.getSample(1, 100) + 0.125f) < 1e-5, "Stereo source preserves channel samples");
        engine.tracks[0]->mute = true; engine.cursor = 0.5; engine.render(output.getArrayOfWritePointers(), 2, 512);
        check(output.getMagnitude(0, 512) == 0, "Track mute silences mix");
        engine.tracks[0]->mute = false; engine.tracks[0]->pan = 1; engine.cursor = 0.5; engine.render(output.getArrayOfWritePointers(), 2, 512);
        check(output.getMagnitude(0, 0, 512) == 0 && output.getMagnitude(1, 0, 512) > 0.12f, "Hard-right pan removes left channel");
        engine.tracks[0]->pan = 0;
        engine.addTrack("Second"); engine.importAudio(source, 1, 0.25); engine.tracks[1]->gain = 0.5f;
        engine.tracks[1]->clips[0].fadeIn = engine.tracks[1]->clips[0].fadeOut = 0;
        engine.cursor = 0.5; engine.render(output.getArrayOfWritePointers(), 2, 512);
        check(std::abs(output.getSample(0, 100) - 0.375f) < 1e-5, "Two-track sum uses track gain");
        engine.tracks[1]->solo = true; engine.cursor = 0.5; engine.render(output.getArrayOfWritePointers(), 2, 512);
        check(std::abs(output.getSample(0, 100) - 0.125f) < 1e-5, "Solo excludes other tracks");
        engine.tracks[1]->solo = false;
        clip = engine.tracks[0]->clips[0]; // first track remains allocated after addTrack
        engine.tracks[0]->clips[0].fadeIn = 0.1; engine.cursor = 0.25;
        engine.render(output.getArrayOfWritePointers(), 2, 512);
        check(std::abs(output.getSample(0, 0) - 0.125f) < 1e-4, "Clip fade starts from silence within summed mix");
        engine.stop();
        File project = directory.getChildFile("Roundtrip.auralis"); engine.saveProject(project);
        Engine restored; restored.loadProject(project);
        check(restored.tracks.size() == 2 && std::abs(restored.duration() - 1.25) < 1e-6, "Project restores tracks and clip timing");
        check(project.getSiblingFile("Roundtrip_media").findChildFiles(File::findFiles, false).size() == 1, "Project copies media and deduplicates shared sources");
        restored.saveProject(project);
        check(project.getSiblingFile("Roundtrip_media").findChildFiles(File::findFiles, false).size() == 1, "Saving a reopened project does not duplicate its media");
        File wav = directory.getChildFile("mix-24.wav"); restored.exportWav(wav, 0, 1.5, 24);
        std::unique_ptr<AudioFormatReader> reader(restored.formats.createReaderFor(wav));
        check(reader && reader->lengthInSamples == 72000 && reader->numChannels == 2 && reader->bitsPerSample == 24, "Export has exact frame count, stereo and 24-bit PCM");
        AudioBuffer<float> actual(2, 72000); reader->read(&actual, 0, 72000, 0, true, true);
        check(actual.getMagnitude(0, 12000) < 1e-5 && std::abs(actual.getSample(0, 24000) - 0.375f) < 1e-5, "Export matches timeline and mix samples");
        check(actual.getMagnitude(61000, 1000) < 1e-6, "Export includes silent tail");
        reader.reset(); restored.exportWav(directory.getChildFile("mix-32.wav"), 0, 0.1, 32);
        reader.reset(restored.formats.createReaderFor(directory.getChildFile("mix-32.wav")));
        check(reader && reader->usesFloatingPointData && reader->lengthInSamples == 4800, "32-bit export uses float WAV");
        restored.rate = 44100; restored.exportWav(directory.getChildFile("mix-44100.wav"), 0, 1.5);
        reader.reset(restored.formats.createReaderFor(directory.getChildFile("mix-44100.wav")));
        check(reader && reader->sampleRate == 44100 && reader->lengthInSamples == 66150, "48 kHz source exports at 44.1 kHz with exact duration");
        restored.rate = 48000;
        auto before = restored.tracks.size();
        File broken = directory.getChildFile("broken.auralis"); broken.replaceWithText("{\"format\":\"auralis-1\",\"tracks\":[{\"clips\":[{\"file\":\"missing.wav\"}]}]}");
        bool rejected = false; try { restored.loadProject(broken); } catch (...) { rejected = true; }
        check(rejected && restored.tracks.size() == before, "Failed project load preserves the current session");
        {
            Engine capture; capture.recordingDirectory = directory.getChildFile("Recordings"); capture.addTrack("Mono capture"); capture.addTrack("Unarmed");
            capture.tracks[0]->armed = true; capture.tracks[0]->input = 2; capture.cursor = 0.75;
            capture.startRecordingWriters();
            AudioBuffer<float> liveInput(2, 512); for (int i = 0; i < 512; ++i) { liveInput.setSample(0, i, 0.1f); liveInput.setSample(1, i, 0.3f); }
            capture.render(output.getArrayOfWritePointers(), 2, 512, liveInput.getArrayOfReadPointers(), 2);
            capture.render(output.getArrayOfWritePointers(), 2, 512, liveInput.getArrayOfReadPointers(), 2);
            check(output.getMagnitude(0, 512) == 0, "Recording with monitor off does not feed input to output");
            capture.finishRecording();
            check(capture.tracks[0]->clips.size() == 1 && capture.tracks[1]->clips.empty(), "Only armed tracks create recorded clips");
            auto& recorded = capture.tracks[0]->clips[0];
            check(std::abs(recorded.start - 0.75) < 1e-8 && recorded.source->samples.getNumSamples() == 1024, "Recorded clip retains timeline position and exact frame count");
            check(std::abs(recorded.source->samples.getSample(0, 100) - 0.3f) < 1e-5 && std::abs(recorded.source->samples.getSample(1, 100) - 0.3f) < 1e-5, "Selected mono input records into both stereo channels");
            check(capture.dropped == 0 && !capture.recording, "Recording writer flushes without dropped samples");
        }
        auto testPlugin = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory().getChildFile("TestGain.vst3");
        if (testPlugin.exists()) {
            OwnedArray<PluginDescription> found;
            for (auto* format : engine.pluginFormats.getFormats()) format->findAllTypesForFile(found, testPlugin.getFullPathName());
            check(found.size() == 1, "VST3 scan discovers the test effect");
            engine.addPlugin(0, *found[0]); engine.tracks[0]->inserts[0]->instance->getParameters()[0]->setValueNotifyingHost(0.25f);
            engine.tracks[1]->mute = true; engine.cursor = 0.5; engine.play(); engine.render(output.getArrayOfWritePointers(), 2, 512);
            check(std::abs(output.getSample(0, 100) - 0.0625f) < 1e-4, "VST3 insert processes audio with parameter gain");
            engine.tracks[0]->inserts[0]->bypass = true; engine.cursor = 0.5; engine.render(output.getArrayOfWritePointers(), 2, 512);
            check(std::abs(output.getSample(0, 100) - 0.25f) < 1e-4, "VST3 bypass leaves source audio unprocessed");
            engine.tracks[0]->inserts[0]->bypass = false;
            engine.stop(); engine.saveProject(project); Engine withPlugin; withPlugin.loadProject(project);
            withPlugin.cursor = 0.5; withPlugin.play(); withPlugin.render(output.getArrayOfWritePointers(), 2, 512);
            check(std::abs(output.getSample(0, 100) - 0.0625f) < 1e-4, "Project restores VST3 parameter state");
            withPlugin.stop(); withPlugin.exportWav(directory.getChildFile("plugin-mix.wav"), 0, 1.5);
            reader.reset(withPlugin.formats.createReaderFor(directory.getChildFile("plugin-mix.wav")));
            AudioBuffer<float> fx(2, 72000); reader->read(&fx, 0, 72000, 0, true, true);
            check(std::abs(fx.getSample(0, 24000) - 0.0625f) < 1e-4, "Offline WAV export includes VST3 processing");
        } else passed.add("SKIP TestGain.vst3 not present");
        runMidiTests(directory, passed);
        runRealtimeTests(directory, passed);
        runPianoEditTests(directory, passed);
        runTrackControlTests(directory, passed);
        runRecordingRangeTests(directory, passed);
        runCutTests(directory, passed);
        runCutFocusTests(directory, passed);
        runRangeExportTests(directory, passed);
        runProjectOpenTests(directory, passed);
        runTrackDeleteTests(directory, passed);
        runClipGainTests(directory, passed);
        runNormalizeTests(directory, passed);
        runPitchCorrectionTests(directory, passed);
        runPitchRollTests(directory, passed);
        runPitchPreviewTests(directory, passed);
        runPitchFollowTests(directory, passed);
        runPitchScoopTests(directory, passed);
        runPitchCurveTests(directory, passed);
        runPitchZoomTests(directory, passed);
        runPitchLoopTests(directory, passed);
        runClipTrimTests(directory, passed);
        runOverlapTests(directory, passed);
        runLoopTests(directory, passed);
        runOverlapTrimTests(directory, passed);
        runRecordingAlignmentTests(directory, passed);
        runRecordingWaveformTests(directory, passed);
        runPluginRegistryTests(directory, passed);
        runTimelineLaneTests(directory, passed);
        runMixerTests(directory, passed);
        runRecentProjectTests(directory, passed);
        runTrackDetailsTests(directory, passed);
        runFollowTests(directory, passed);
        runTrackDragTests(directory, passed);
        runFixedRulerTests(directory, passed);
        runPluginInfoTests(directory, passed);
        runMasterInsertTests(directory, passed);
        runEqualizerTests(directory, passed);
        {
            Workstation workstation(false); workstation.stopTimer(); auto& timeline = workstation.timeline;
            workstation.engine.addTrack("Long arrangement", true);
            auto& clip = workstation.engine.addMidiClip(1, 0, 1200);
            for (int beat = 0; beat < 1200; beat += 4) clip.notes.push_back({double(beat), 1, 60 + beat % 12, 100, 1});
            workstation.refresh(); workstation.dirty = false; timeline.scroll = 10;
            const float anchor = Timeline::header + 300;
            double time = timeline.timeAt(anchor);
            auto now = Time::getCurrentTime();
            MouseEvent event(Desktop::getInstance().getMainMouseSource(), {anchor, 80}, ModifierKeys(ModifierKeys::ctrlModifier),
                             1, 0, 0, 0, 0, &timeline, &timeline, now, {anchor, 80}, now, 1, false);
            MouseWheelDetails wheel; wheel.deltaY = -0.1f; timeline.mouseWheelMove(event, wheel);
            check(timeline.zoom < 70 && std::abs(timeline.timeAt(anchor) - time) < 1e-7, "Arrangement Ctrl+wheel zoom keeps pointer time fixed");
            workstation.keyPressed(KeyPress('F'));
            check(timeline.scroll == 0 && timeline.xAt(600) < timeline.getWidth() - 20 && timeline.zoom < 2 && !workstation.dirty,
                  "F fits a ten-minute arrangement without modifying the project");
            auto* slider = dynamic_cast<Slider*>(workstation.findChildWithID("timelineZoom"));
            check(slider && slider->getMinimum() == 0.25 && slider->getMaximum() == 300, "Arrangement zoom slider extends to 0.25 pixels per second");
            slider->setValue(0.25, sendNotificationSync); workstation.timerCallback();
            check(timeline.zoom == 0.25 && std::abs(timeline.scroll) < 1e-7 && timeline.timeAt(float(timeline.getWidth())) > 3000,
                  "Maximum arrangement zoom-out shows over fifty minutes without scrollbar displacement");
            workstation.fitProject();
            if (auto stream = directory.getChildFile("arrangement-wide-zoom-preview.png").createOutputStream()) {
                stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(workstation.createComponentSnapshot(workstation.getLocalBounds()), *stream);
            }
        }
        {
            Workstation workstation(false); workstation.stopTimer(); auto& timeline = workstation.timeline;
            auto& transport = workstation.engine; workstation.dirty = false;
            timeline.scroll = 3; timeline.zoom = 100; timeline.selectedClip = 0;
            auto event = [&](float x, int extra = 0) {
                auto now = Time::getCurrentTime();
                return MouseEvent(Desktop::getInstance().getMainMouseSource(), {x, 12}, ModifierKeys(ModifierKeys::leftButtonModifier | extra),
                                  1, 0, 0, 0, 0, &timeline, &timeline, now, {x, 12}, now, 1, true);
            };
            auto down = event(float(Timeline::header) + 24.5f), move = event(float(Timeline::header) + 183.75f);
            timeline.mouseDown(down); timeline.mouseDrag(move);
            check(std::abs(double(transport.cursor) - 4.8375) < 1e-7 && !workstation.dirty && timeline.selectedClip == 0,
                  "Arrangement playhead follows ruler drag precisely despite Snap and scroll, preserving selection and saved state");
            transport.play(); transport.cursor = 5;
            timeline.mouseUp(move);
            check(std::abs(double(transport.cursor) - 4.8375) < 1e-7 && transport.playing,
                  "Arrangement playhead release resumes from pointer position without stopping playback");
            auto snapped = event(float(Timeline::header) + 183.75f, ModifierKeys::shiftModifier); timeline.mouseDown(snapped); timeline.mouseUp(snapped);
            check(std::abs(double(transport.cursor) - 5) < 1e-7, "Shift optionally snaps arrangement playhead to beats");
            transport.recording = true; timeline.mouseDown(down); timeline.mouseDrag(move); timeline.mouseUp(move); transport.recording = false;
            check(std::abs(double(transport.cursor) - 5) < 1e-7, "Arrangement ruler cannot seek while recording");
            timeline.mouseDown(down); auto left = event(-200); timeline.mouseDrag(left); timeline.mouseUp(left);
            check(transport.cursor == 0, "Arrangement playhead dragging clamps at project start"); transport.stop();
        }
        {
            Workstation workstation(false); workstation.stopTimer(); auto& click = workstation.engine;
            auto* button = dynamic_cast<TextButton*>(workstation.findChildWithID("metronome"));
            check(button && !click.metronome && button->getBounds().getBottom() <= workstation.viewport.getY(), "Metronome control is visible above the tracks and defaults off");
            button->onClick();
            check(click.metronome && button->getToggleState(), "Metronome button enables the engine and shows its active state");
            click.master = 1; click.rate = 48000; click.play(); click.render(output.getArrayOfWritePointers(), 2, 512);
            AudioBuffer<float> accented(output);
            check(output.getMagnitude(0, 512) > 0.05f && output.getSample(0, 100) == output.getSample(1, 100), "Metronome produces a stereo click at the first beat");
            click.seek(0.5); click.render(output.getArrayOfWritePointers(), 2, 512);
            check(std::abs(output.getSample(0, 100) - accented.getSample(0, 100)) > 0.01f, "Metronome accents the first beat of each four-beat bar");
            click.seek(2); click.render(output.getArrayOfWritePointers(), 2, 512);
            check(std::abs(output.getSample(0, 100) - accented.getSample(0, 100)) < 1e-6f, "Metronome accent repeats on the next bar");
            click.seek(0.1); click.render(output.getArrayOfWritePointers(), 2, 512);
            check(output.getMagnitude(0, 512) == 0, "Metronome is silent between beats");
            click.stop(); click.seek(0); click.render(output.getArrayOfWritePointers(), 2, 512);
            check(output.getMagnitude(0, 512) == 0, "Stop and Panic silence the metronome immediately");
            auto span = [&](int blockSize) {
                click.setTempo(137); click.seek(0.4); click.play(); AudioBuffer<float> result(2, 6000);
                for (int offset = 0; offset < 6000; offset += blockSize) {
                    int count = jmin(blockSize, 6000 - offset); AudioBuffer<float> block(2, count);
                    click.render(block.getArrayOfWritePointers(), 2, count);
                    for (int ch = 0; ch < 2; ++ch) result.copyFrom(ch, offset, block, ch, 0, count);
                }
                return result;
            };
            auto small = span(64), large = span(512); large.addFrom(0, 0, small, 0, 0, 6000, -1);
            check(small.getMagnitude(0, 6000) > 0.05f && large.getMagnitude(0, 0, 6000) < 1e-6f,
                  "Metronome stays sample accurate across 64 and 512 sample blocks at fractional beat positions");
            click.stop(); click.seek(0); click.setTempo(120); click.tracks[0]->armed = true;
            click.recordingDirectory = directory.getChildFile("MetronomeRecording"); click.startRecordingWriters();
            AudioBuffer<float> silence(2, 512); silence.clear();
            click.render(output.getArrayOfWritePointers(), 2, 512, silence.getArrayOfReadPointers(), 2);
            check(output.getMagnitude(0, 512) > 0.05f, "Metronome remains audible during recording with input monitoring off");
            click.finishRecording();
            check(click.tracks[0]->clips[0].source->samples.getMagnitude(0, 512) == 0, "Metronome is excluded from raw audio recordings");
            click.exportWav(directory.getChildFile("metronome-excluded.wav"), 0, 0.6);
            std::unique_ptr<AudioFormatReader> exported(click.formats.createReaderFor(directory.getChildFile("metronome-excluded.wav")));
            AudioBuffer<float> exportedAudio(2, 28800); exported->read(&exportedAudio, 0, 28800, 0, true, true);
            check(exportedAudio.getMagnitude(0, 28800) == 0 && click.metronome, "WAV export excludes enabled metronome and preserves its setting");
            auto projectFile = directory.getChildFile("metronome.auralis"); click.saveProject(projectFile);
            click.metronome = false; click.loadProject(projectFile); workstation.refresh();
            check(click.metronome && button->getToggleState(), "Project save and load restore metronome state and button");
            button->onClick(); click.seek(0); click.play(); click.render(output.getArrayOfWritePointers(), 2, 512);
            check(!click.metronome && output.getMagnitude(0, 512) == 0, "Metronome can be disabled during playback");
            click.reset(); check(!click.metronome, "New projects reset metronome to off");
        }
        passed.add("ALL TESTS PASSED"); directory.getChildFile("results.txt").replaceWithText(passed.joinIntoString("\n")); return 0;
    } catch (const std::exception& e) {
        passed.add("FAIL " + String(e.what())); directory.getChildFile("results.txt").replaceWithText(passed.joinIntoString("\n")); return 1;
    }
}
}
