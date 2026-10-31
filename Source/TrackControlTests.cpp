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
    directory.getChildFile("track-control-verification.txt").replaceWithText("Verified per-track header REC/MUTE/SOLO placement, audio mix effects, raw recording routing, recording-time controls, inspector state retention, and stable track identity after removal.\n");
}
}
