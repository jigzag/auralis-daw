#include "Interface.h"
#include <stdexcept>
#include <cmath>
namespace auralis {
void runTrackControlTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Workstation workstation(false); workstation.stopTimer(); auto& engine = workstation.engine;
    engine.addTrack("Other audio"); engine.master = 1;
    auto firstId = engine.tracks[0]->id, secondId = engine.tracks[1]->id;
    for (int track = 0; track < 2; ++track) {
        auto source = std::make_shared<SourceAudio>(); source->rate = 48000; source->samples.setSize(2, 48000);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 48000; ++i) source->samples.setSample(ch, i, track == 0 ? 0.25f : 0.125f);
        source->buildWaveform(); Clip clip; clip.source = source; clip.length = 1; clip.fadeIn = clip.fadeOut = 0;
        engine.tracks[size_t(track)]->clips.push_back(clip); engine.tracks[size_t(track)]->gain = 1;
    }
    workstation.refresh();
    auto control = [&](const String& trackId, const String& id) -> TextButton& {
        auto* row = workstation.timeline.findChildWithID(trackId);
        auto* button = row ? dynamic_cast<TextButton*>(row->findChildWithID(id)) : nullptr;
        if (!button) throw std::runtime_error("Missing track-header control."); return *button;
    };
    auto click = [&](const String& id, const String& kind) { control(id, kind).onClick(); };
    bool positioned = true;
    for (auto id : {firstId, secondId}) for (auto kind : {"arm", "mute", "solo"}) {
        auto& b = control(id, kind); auto bounds = workstation.timeline.getLocalArea(&b, b.getLocalBounds());
        positioned &= bounds.getX() >= 0 && bounds.getRight() <= Timeline::header && bounds.getY() >= Timeline::ruler;
    }
    check(positioned, "REC, MUTE and SOLO are placed in each left track header");
    AudioBuffer<float> out(2, 128);
    auto sample = [&] { engine.cursor = 0.6; engine.play(); engine.render(out.getArrayOfWritePointers(), 2, 128); return out.getSample(0, 100); };
    check(std::abs(sample() - 0.375f) < 1e-6, "Track-header test starts with both audio tracks audible");
    click(secondId, "mute");
    check(workstation.selectedTrack == 0 && std::abs(sample() - 0.25f) < 1e-6 && control(secondId, "mute").getToggleState(),
          "Left MUTE silences an unselected track without changing the selected track");
    click(secondId, "mute"); click(secondId, "solo");
    check(std::abs(sample() - 0.125f) < 1e-6 && control(secondId, "solo").getToggleState(), "Left SOLO makes its track audible and excludes other tracks");
    click(firstId, "solo"); check(std::abs(sample() - 0.375f) < 1e-6, "Multiple left SOLO buttons can be enabled together");
    click(firstId, "solo"); click(secondId, "solo"); engine.stop();

    click(secondId, "arm");
    check(control(secondId, "arm").getToggleState() && !control(firstId, "arm").getToggleState() && workstation.selectedTrack == 0,
          "Left REC arms the intended unselected track without arming another track");
    engine.recordingDirectory = directory.getChildFile("TrackControlRecordings"); engine.cursor = 0.2; engine.startRecordingWriters(); workstation.refresh();
    check(!control(secondId, "arm").isEnabled() && control(secondId, "mute").isEnabled() && control(secondId, "solo").isEnabled(),
          "Recording disables REC changes while keeping track-header MUTE and SOLO available");
    click(secondId, "arm");
    check(engine.tracks[1]->armed, "Recording cannot accidentally disarm its track through a stale REC click");
    click(secondId, "mute"); click(secondId, "solo");
    AudioBuffer<float> input(2, 128);
    for (int i = 0; i < 128; ++i) { input.setSample(0, i, 0.2f); input.setSample(1, i, 0.3f); }
    engine.render(out.getArrayOfWritePointers(), 2, 128, input.getArrayOfReadPointers(), 2);
    check(out.getMagnitude(0, 128) == 0, "MUTE and SOLO changes during recording immediately affect the monitor mix");
    engine.finishRecording(); workstation.refresh();
    const auto& take = engine.tracks[1]->clips.back().source->samples;
    check(engine.tracks[0]->clips.size() == 1 && engine.tracks[1]->clips.size() == 2 && take.getNumSamples() == 128
          && std::abs(take.getSample(0, 50) - 0.2f) < 1e-5 && std::abs(take.getSample(1, 50) - 0.3f) < 1e-5,
          "Left REC records only its armed track and MUTE does not erase the raw recording");
    check(control(secondId, "arm").isEnabled(), "Left REC is available again after recording stops");
    workstation.selectTrack(1);
    auto* gain = dynamic_cast<Slider*>(workstation.findChildWithID("trackGain"));
    check(gain != nullptr, "Inspector gain control remains available after moving record/mute/solo");
    gain->setValue(-3, sendNotificationSync);
    check(engine.tracks[1]->mute && engine.tracks[1]->solo && engine.tracks[1]->armed && sample() == 0,
          "Inspector changes preserve the left-header REC, MUTE and SOLO states");
    engine.stop();
    { const ScopedLock guard(engine.lock); engine.tracks.erase(engine.tracks.begin()); }
    workstation.selectTrack(0); workstation.refresh(); click(secondId, "mute");
    check(!engine.tracks[0]->mute && engine.tracks[0]->solo && std::abs(sample() - 0.125f * Decibels::decibelsToGain(-3.0f)) < 1e-5,
          "Track-header controls still address the correct track after another track is removed");
    engine.stop();
    {
        Workstation monitor(false); monitor.stopTimer(); auto& e = monitor.engine;
        e.addTrack("Backing"); e.master = 1;
        for (int i = 0; i < 2; ++i) {
            e.importAudio(directory.getChildFile("source.wav"), i, 0);
            auto& t = *e.tracks[size_t(i)]; t.gain = i == 0 ? 1.0f : 0.5f;
            t.clips[0].fadeIn = t.clips[0].fadeOut = 0;
        }
        monitor.refresh();
        auto& t = *e.tracks[0];
        auto* row = monitor.timeline.findChildWithID(t.id);
        auto* mon = row ? dynamic_cast<TextButton*>(row->findChildWithID("monitor")) : nullptr;
        check(mon != nullptr, "Monitor playback test finds the native MON control");
        AudioBuffer<float> live(2, 128), mix(2, 128);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) live.setSample(ch, i, 0.2f);
        auto render = [&](bool withInput = true) {
            e.cursor = 0.25; e.play();
            e.render(mix.getArrayOfWritePointers(), 2, 128, withInput ? live.getArrayOfReadPointers() : nullptr, withInput ? 2 : 0);
            return mix.getSample(0, 40);
        };
        check(std::abs(render() - 0.375f) < 1e-6, "MON off plays clips without including live input");
        mon->onClick();
        check(t.monitor && !t.mute && std::abs(render() - 0.325f) < 1e-6,
              "MON on replaces only its track's clips with live input and preserves other tracks");
        check(std::abs(render(false) - 0.125f) < 1e-6, "MON on keeps its clips silent even when no input is available");
        t.mute = true;
        check(std::abs(render() - 0.125f) < 1e-6, "Explicit MUTE still silences the monitored input");
        t.mute = false; t.solo = true;
        check(std::abs(render() - 0.2f) < 1e-6, "SOLO includes the monitored input without restoring its clips");
        t.solo = false;
        e.stop(); e.render(mix.getArrayOfWritePointers(), 2, 128, live.getArrayOfReadPointers(), 2);
        check(std::abs(mix.getSample(0, 40) - 0.2f) < 1e-6, "MON remains audible while transport is stopped");
        const auto wavFile = directory.getChildFile("monitor-clips-export.wav"); e.exportWav(wavFile, 0.1, 0.2, 32);
        std::unique_ptr<AudioFormatReader> reader(e.formats.createReaderFor(wavFile)); AudioBuffer<float> exported(2, 4800);
        check(reader && reader->read(&exported, 0, 4800, 0, true, true) && std::abs(exported.getSample(0, 40) - 0.375f) < 1e-6 && t.monitor,
              "Export includes clips from MON-enabled tracks and preserves the monitoring switch");
        mon->onClick();
        check(!t.monitor && !t.mute && std::abs(render() - 0.375f) < 1e-6, "MON off immediately restores clip playback without changing MUTE");
        t.mute = true; mon->onClick(); mon->onClick();
        check(t.mute, "Toggling MON preserves a pre-existing explicit MUTE state");
        t.mute = false; mon->onClick(); e.stop(); e.seek(0.25); t.armed = true;
        e.recordingDirectory = directory.getChildFile("MonitorOnlyRecordings"); e.startRecordingWriters();
        e.render(mix.getArrayOfWritePointers(), 2, 128, live.getArrayOfReadPointers(), 2);
        check(std::abs(mix.getSample(0, 40) - 0.325f) < 1e-6, "Recording with MON hears live input and backing without the existing take");
        e.finishRecording();
        const auto& recorded = t.clips.back().source->samples;
        check(recorded.getNumSamples() == 128 && std::abs(recorded.getSample(0, 40) - 0.2f) < 1e-6,
              "MON clip suppression preserves every raw recorded input sample");
    }
    directory.getChildFile("track-control-verification.txt").replaceWithText("Verified per-track header REC/MUTE/SOLO placement, audio mix effects, raw recording routing, recording-time controls, inspector state retention, stable track identity after removal, MON input-only playback, missing-input silence, explicit mute/solo, stopped monitoring, export with MON enabled, toggle restoration and recording over an existing take. Synthetic/offline only.\n");
}
}
