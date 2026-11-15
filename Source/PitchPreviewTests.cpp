#include "PitchRoll.h"
#include <cmath>
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace auralis {
void runPitchPreviewTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto capture = [](Engine& e, int count, int block = 128) {
        AudioBuffer<float> result(2, count);
        for (int at = 0; at < count; at += block) {
            const int n = jmin(block, count - at); AudioBuffer<float> part(2, n); e.render(part.getArrayOfWritePointers(), 2, n);
            for (int ch = 0; ch < 2; ++ch) result.copyFrom(ch, at, part, ch, 0, n);
        } return result;
    };
    auto frequency = [](const AudioBuffer<float>& buffer, double rate, double target) {
        constexpr int size = 65536; dsp::FFT fft(16); std::vector<float> data(size * 2, 0); const int count = jmin(size, buffer.getNumSamples());
        for (int i = 0; i < count; ++i) data[size_t(i)] = buffer.getSample(0, i) * float(0.5 - 0.5 * std::cos(MathConstants<double>::twoPi * i / (count - 1)));
        fft.performFrequencyOnlyForwardTransform(data.data(), true); int k = int(target * 0.94 * size / rate);
        for (int i = k + 1; i < int(target * 1.06 * size / rate); ++i) if (data[size_t(i)] > data[size_t(k)]) k = i;
        const double a = std::log(jmax(1e-20f, data[size_t(k - 1)])), b = std::log(jmax(1e-20f, data[size_t(k)])), c = std::log(jmax(1e-20f, data[size_t(k + 1)]));
        return (k + (a - c) / (2 * (a - 2 * b + c))) * rate / size;
    };
    auto closePitch = [&](const AudioBuffer<float>& b, double rate, double target) { return std::abs(1200 * std::log2(frequency(b, rate, target) / target)) < 5; };
    auto sine = std::make_shared<SourceAudio>(); sine->samples.setSize(2, 96000);
    for (int i = 0; i < 96000; ++i) { const float v = 0.2f * float(std::sin(MathConstants<double>::twoPi * 440 * i / 48000)); sine->samples.setSample(0, i, v); sine->samples.setSample(1, i, v * 0.5f); }
    Engine direct; const auto id = direct.addTrack("Audition").id; direct.tracks[0]->gain = 0.5f; direct.master = 0.8f;
    auto backing = std::make_shared<SourceAudio>(); backing->samples.setSize(2, 96000); for (int ch = 0; ch < 2; ++ch) FloatVectorOperations::fill(backing->samples.getWritePointer(ch), 0.3f, 96000);
    auto& other = direct.addTrack("Backing"); Clip backingClip; backingClip.source = backing; backingClip.length = 2; backingClip.fadeIn = backingClip.fadeOut = 0; other.clips.push_back(backingClip);
    direct.seek(0.75); direct.play();
    check(direct.startPitchPreview("test", id, sine, 0, 2, 12, 0, 0.2), "Pitch audition accepts an original audio note without a rendered file");
    capture(direct, 2048); auto octave = capture(direct, 16384);
    check(closePitch(octave, 48000, 880) && direct.cursor == 0.75 && direct.playing,
          "Live audition changes pitch while keeping the existing transport position and playing state");
    check(std::abs(octave.getRMSLevel(0, 0, octave.getNumSamples()) - 0.2f * 0.5f * 0.8f / std::sqrt(2.0f)) < 0.002f
          && std::abs(octave.getRMSLevel(1, 0, octave.getNumSamples()) / octave.getRMSLevel(0, 0, octave.getNumSamples()) - 0.5f) < 1e-4f,
          "Audition retains track/master level and stereo balance without mixing other arrangement tracks");
    direct.updatePitchPreview("test", 0); capture(direct, 4096); auto unison = capture(direct, 16384);
    check(closePitch(unison, 48000, 440), "Changing the drag target retunes the sounding original audio without restarting transport");
    direct.stopPitchPreview("another-window", true); check(direct.pitchPreviewActive, "Another editor cannot stop an audition it does not own");
    direct.stop(); check(!direct.pitchPreviewActive && direct.cursor == 0.75, "Transport Stop cancels pitch audition without seeking");
    for (int block : {64, 128, 512}) {
        direct.startPitchPreview("release", id, sine, 0.2, 1.7, 0); capture(direct, 1024, block); direct.stopPitchPreview("release"); capture(direct, 1024, block);
        auto silence = capture(direct, 512, block); check(!direct.pitchPreviewActive && silence.getMagnitude(0, 512) == 0,
              String(block) + "-sample callbacks release preview smoothly and leave no stuck sound");
    }
    direct.rate = 44100; direct.startPitchPreview("rate", id, sine, 0.2, 1.7, 7); capture(direct, 2048);
    auto converted = capture(direct, 16384); check(closePitch(converted, 44100, 440 * std::pow(2.0, 7.0 / 12)), "Pitch audition preserves tuning across different source and device sample rates"); direct.stop(); direct.rate = 48000;
    OwnedArray<PluginDescription> found; auto gainFile = File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("TestGain.vst3");
    for (auto* format : direct.pluginFormats.getFormats()) format->findAllTypesForFile(found, gainFile.getFullPathName());
    check(found.size() == 1, "Audition routing regression effect is available"); direct.addPlugin(0, *found[0]); direct.addPlugin(-1, *found[0]);
    direct.startPitchPreview("effects", id, sine, 0.2, 1.7, 0); capture(direct, 1024); auto effected = capture(direct, 8192);
    check(std::abs(effected.getRMSLevel(0, 0, 8192) - 0.2f * 0.5f * 0.8f * 0.25f / std::sqrt(2.0f)) < 0.001f,
          "Pitch audition passes through the selected track and master effect chains"); direct.stop();
    direct.recording = true; check(!direct.startPitchPreview("blocked", id, sine, 0, 1, 0), "Recording rejects audition starts"); direct.recording = false;
    direct.exporting = true; check(!direct.startPitchPreview("blocked", id, sine, 0, 1, 0), "Offline export rejects audition starts"); direct.exporting = false;
    check(!direct.startPitchPreview("invalid", "missing", sine, 0, 1, 0) && !direct.startPitchPreview("invalid", id, sine, 1, 0, 0), "Invalid audition ranges and removed tracks cannot start sound");
    auto pump = [] {
       #if JUCE_WINDOWS
        MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
       #endif
        Thread::sleep(1);
    };
    Workstation w(false); w.stopTimer(); auto& e = w.engine; e.importAudio(directory.getChildFile("pitch-melody.wav"), 0, 0);
    auto& clip = e.tracks[0]->clips[0]; clip.start = 12.5; clip.offset = 0.25; clip.length = 1.5; w.refresh();
    w.showPitchRoll(e.tracks[0]->id, clip.id);
    auto wait = [&] { const auto end = Time::getMillisecondCounterHiRes() + 30000; while (w.busy && Time::getMillisecondCounterHiRes() < end) pump(); check(!w.busy, "Pitch preview UI work completes using offline audio only"); };
    wait(); check(w.pitchWindows.size() == 1, "Pitch preview editor opens for a moved and trimmed audio part");
    auto& window = *w.pitchWindows[0]; window.setAlpha(0); auto& p = window.panel(); p.stopTimer(); auto& grid = p.grid;
    auto event = [&](Component& target, Point<float> at, Point<float> origin) {
        const auto now = Time::getCurrentTime(); return MouseEvent(Desktop::getInstance().getMainMouseSource(), at, ModifierKeys(ModifierKeys::leftButtonModifier),
            1, 0, 0, 0, 0, &target, &target, now, origin, now, 1, at != origin);
    };
    int first = -1; for (int i = 0; i < int(p.notes.size()); ++i) if (std::abs(p.notes[size_t(i)].original - 69.35) < 0.2) { first = i; break; }
    check(first >= 0 && p.dragAudition.getToggleState(), "Dragging audition is enabled by default for detected notes");
    const auto original = clip.source; e.seek(13.0); e.play();
    const auto start = Point<float>(float((p.notes[size_t(first)].start - clip.offset + 0.14) * grid.pixels), grid.noteBounds(first).getCentreY());
    const auto moved = start.translated(0, -float(grid.row));
    grid.mouseDown(event(grid, start, start)); grid.mouseDrag(event(grid, moved, start)); capture(e, 4096); auto heard = capture(e, 16384);
    check(e.pitchPreviewActive && closePitch(heard, 48000, 466.163762) && e.cursor == 13.0 && clip.source == original && !clip.pitchOriginal,
          "Holding a dragged note auditions its target pitch immediately without applying edits or moving the playhead");
    grid.mouseUp(event(grid, moved, start)); capture(e, 512, 512); check(!e.pitchPreviewActive && e.cursor == 13.0, "Mouse release ends audition at the preserved transport position");
    capture(e, 128); check(std::abs(e.cursor - (13.0 + 128.0 / 48000)) < 1e-8, "Transport continues from the preserved position after audition ends"); e.stop(); p.discard();
    p.dragAudition.setToggleState(false, dontSendNotification); grid.mouseDown(event(grid, start, start)); check(!e.pitchPreviewActive, "The drag-audition switch can disable sound during editing"); grid.mouseUp(event(grid, start, start));
    p.dragAudition.setToggleState(true, dontSendNotification); grid.mouseDown(event(grid, start, start)); grid.focusLost(Component::focusChangedDirectly);
    check(!e.pitchPreviewActive, "Losing editor keyboard focus immediately stops held-note audition"); grid.mouseUp(event(grid, start, start));
    p.discard(); grid.pixels = 900; p.updateSize(); p.viewport.setViewPosition(210, p.viewport.getViewPositionY());
    const double local = 0.9; const float x = float(PitchRollPanel::keyWidth + local * grid.pixels - p.viewport.getViewPositionX());
    const auto ruler = Point<float>(x, 78); p.selected = {first}; p.mouseDown(event(p, ruler, ruler));
    e.play(); capture(e, 512); check(std::abs(p.playheadX() - local * grid.pixels) < 0.001, "Held ruler dragging stays under the mouse even while the audio callback advances");
    p.mouseUp(event(p, ruler, ruler)); check(!p.seeking && std::abs(e.cursor - (clip.start + local)) < 1e-8
          && std::abs(PitchRollPanel::keyWidth + p.playheadX() - p.viewport.getViewPositionX() - x) < 0.001,
          "Ruler release aligns fixed and scrolling playheads with moved/trimmed part coordinates");
    p.play.onClick(); check(std::abs(e.cursor - (clip.start + local)) < 1e-8 && e.playing, "Play after a ruler seek starts at the playhead instead of the selected note"); e.stop();
    p.selected = {first}; p.moveSelected(0.25); p.mouseDown(event(p, ruler, ruler)); p.mouseUp(event(p, ruler, ruler));
    check(p.apply(true), "Apply-and-play accepts staged note edits after an explicit ruler seek"); wait();
    check(std::abs(e.cursor - (clip.start + local)) < 1e-8 && e.playing && !p.hasPendingEdits(), "Apply-and-play preserves the chosen playhead position across background correction"); e.stop();
    p.fit(); if (auto stream = directory.getChildFile("pitch-drag-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(p.createComponentSnapshot(p.getLocalBounds()), *stream); }
    check(!e.devices.getCurrentAudioDevice() && !direct.devices.getCurrentAudioDevice(), "Pitch drag previews are verified without opening audio hardware");
    directory.getChildFile("pitch-preview-verification.txt").replaceWithText("Synthetic/offline only. Verified original-audio pitch audition, live retuning, stereo/levels, isolated track/master FX, owner identity, callback releases at 64/128/512, sample-rate conversion, recording/export/invalid guards, real note drag before Apply, release/focus/toggle cancellation, frozen and resumed transport, fixed/scrolled ruler alignment with moved and trimmed parts, and Play/Apply-and-play preserving explicit seeks.\n");
}
}
