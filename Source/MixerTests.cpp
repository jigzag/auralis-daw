#include "Mixer.h"
#include <cmath>
#include <limits>
#include <stdexcept>
namespace auralis {
void runMixerTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto approx = [](double a, double b) { return std::abs(a - b) < 1e-5; };
    Workstation w(false); w.stopTimer(); auto& e = w.engine;
    e.importAudio(directory.getChildFile("source.wav"), 0, 0); auto& clip = e.tracks[0]->clips[0]; clip.fadeIn = clip.fadeOut = 0;
    const String firstId = e.tracks[0]->id;
    e.addTrack("MIDI keys", true); const String midiId = e.tracks[1]->id;
    e.master = 1; w.refresh(); w.dirty = false;
    const int fullHeight = w.viewport.getHeight(); w.mixerButton.onClick();
    check(w.mixerVisible && w.mixer->isVisible() && w.mixer->strips.size() == 2 && w.viewport.getHeight() == fullHeight - 310 && !w.dirty,
          "Mixer button opens a dock with audio and MIDI strips without changing the project");
    auto& strip = *w.mixer->strips[0];
    strip.fader.setValue(-6, sendNotificationSync);
    check(approx(e.tracks[0]->gain, Decibels::decibelsToGain(-6.0f)) && approx(w.volume.getValue(), -6) && w.dirty,
          "Mixer fader changes the actual track gain and mirrors the inspector");
    AudioBuffer<float> out(2, 64);
    auto render = [&] { e.seek(0.25); e.play(); e.render(out.getArrayOfWritePointers(), 2, 64); e.stop(); };
    render(); check(approx(out.getSample(0, 20), 0.25 * Decibels::decibelsToGain(-6.0f)), "Mixer fader gain is applied to rendered audio");
    strip.pan.setValue(1, sendNotificationSync); render();
    check(approx(e.tracks[0]->pan, 1) && approx(w.pan.getValue(), 1) && out.getMagnitude(0, 0, 64) == 0 && out.getMagnitude(1, 0, 64) > 0,
          "Mixer pan changes the stereo signal and stays synchronized with the inspector");
    w.pan.setValue(-0.5, sendNotificationSync); w.volume.setValue(-3, sendNotificationSync);
    check(approx(strip.pan.getValue(), -0.5) && approx(strip.fader.getValue(), -3), "Inspector gain and pan changes immediately update mixer controls");
    strip.mute.onClick(); render();
    check(e.tracks[0]->mute && strip.mute.getToggleState() && out.getMagnitude(0, 64) == 0, "Mixer mute changes track state and silences audio");
    w.toggleTrackControl(firstId, TrackControl::mute);
    check(!strip.mute.getToggleState(), "Timeline mute changes immediately synchronize the mixer");
    w.mixer->strips[1]->solo.onClick(); render();
    check(e.tracks[1]->solo && out.getMagnitude(0, 64) == 0, "Mixer solo excludes other tracks from the mix");
    w.mixer->strips[1]->solo.onClick();
    strip.arm.onClick(); check(e.tracks[0]->armed && strip.arm.getToggleState(), "Mixer REC mirrors the track record-arm state");
    e.recording = true; w.mixer->sync();
    strip.arm.onClick(); strip.fader.setValue(-9, sendNotificationSync); strip.pan.setValue(0, sendNotificationSync);
    check(e.tracks[0]->armed && !strip.arm.isEnabled() && strip.mute.isEnabled() && approx(e.tracks[0]->gain, Decibels::decibelsToGain(-9.0f)),
          "Recording locks mixer record-arm while allowing volume, pan, mute and solo adjustments");
    e.recording = false; w.mixer->sync();
    w.mixer->master.fader.setValue(-6, sendNotificationSync);
    check(approx(e.master, Decibels::decibelsToGain(-6.0f)) && approx(w.masterVolume.getValue(), -6), "Mixer master fader updates master output and the transport control");
    w.masterVolume.setValue(-3, sendNotificationSync);
    check(approx(w.mixer->master.fader.getValue(), -3), "Transport master volume updates the mixer master strip");
    e.master = 0.5f; w.dirty = false; w.refresh();
    check(approx(w.masterVolume.getValue(), -6) && approx(w.mixer->master.fader.getValue(), -6) && !w.dirty,
          "Restoring engine master level refreshes both displays without changing the saved state");
    strip.fader.setValue(-60, sendNotificationSync); render();
    check(e.tracks[0]->gain == 0 && out.getMagnitude(0, 64) == 0, "Bottom of the mixer fader produces complete silence");
    strip.fader.setValue(0, sendNotificationSync); strip.pan.setValue(0.3, sendNotificationSync);
    const auto project = directory.getChildFile("mixer-settings.auralis"); e.saveProject(project); Engine restored; restored.loadProject(project);
    check(approx(restored.tracks[0]->gain, 1) && approx(restored.tracks[0]->pan, 0.3) && approx(restored.master, e.master), "Project save and reload retain mixer gain, pan and master settings");
    auto* row = w.timeline.findChildWithID(firstId);
    auto* monitorButton = row ? dynamic_cast<TextButton*>(row->findChildWithID("monitor")) : nullptr;
    check(monitorButton && monitorButton->isVisible() && !monitorButton->getToggleState() && !w.monitor.isVisible(),
          "Audio input monitor switch is located in the track header instead of the inspector");
    if (!monitorButton) throw std::runtime_error("Missing track monitor button");
    auto bounds = w.timeline.getLocalArea(monitorButton, monitorButton->getLocalBounds());
    check(bounds.getRight() <= Timeline::header && monitorButton->getWidth() >= 40 && strip.monitor.getRight() <= strip.getWidth(),
          "MON buttons fit beside REC, mute and solo in both track header and mixer");
    w.selectTrack(1); monitorButton->onClick();
    check(w.selectedTrack == 1 && e.tracks[0]->monitor && strip.monitor.getToggleState() && monitorButton->getToggleState(),
          "Track MON monitors an unselected audio track and immediately updates its mixer switch");
    e.stop(); e.master = 1; w.setMixerValue(firstId, 0); w.setMixerValue(firstId, 0, true);
    AudioBuffer<float> input(2, 64); for (int i = 0; i < 64; ++i) { input.setSample(0, i, 0.2f); input.setSample(1, i, -0.3f); }
    e.render(out.getArrayOfWritePointers(), 2, 64, input.getArrayOfReadPointers(), 2);
    check(approx(out.getSample(0, 20), 0.2) && approx(out.getSample(1, 20), -0.3), "Track MON enables the actual live stereo input signal");
    strip.monitor.onClick(); e.render(out.getArrayOfWritePointers(), 2, 64, input.getArrayOfReadPointers(), 2);
    check(!e.tracks[0]->monitor && !monitorButton->getToggleState() && !w.monitor.getToggleState() && out.getMagnitude(0, 64) == 0,
          "Mixer MON disables live input and synchronizes the track header and inspector model");
    e.recording = true; w.refresh(); monitorButton->onClick();
    check(monitorButton->isEnabled() && strip.monitor.isEnabled() && e.tracks[0]->monitor, "Input monitor remains switchable during recording");
    monitorButton->onClick(); e.recording = false; w.refresh();
    auto* midiRow = w.timeline.findChildWithID(midiId); auto* midiMonitor = midiRow ? dynamic_cast<TextButton*>(midiRow->findChildWithID("monitor")) : nullptr;
    w.toggleTrackControl(midiId, TrackControl::monitor);
    check(midiMonitor && !midiMonitor->isEnabled() && !w.mixer->strips[1]->monitor.isEnabled() && !e.tracks[1]->monitor,
          "Audio MON is disabled for MIDI tracks and cannot accidentally enable microphone input");
    w.busy = true; w.toggleTrackControl(firstId, TrackControl::monitor); w.busy = false;
    check(!e.tracks[0]->monitor, "Busy operations reject stale monitor-switch actions");
    const float gainBefore = e.tracks[0]->gain; w.dirty = false; w.busy = true;
    check(!w.setMixerValue(firstId, -12) && approx(e.tracks[0]->gain, gainBefore), "Busy operations block stale mixer changes"); w.busy = false;
    check(!w.setMixerValue("missing", -12) && !w.setMixerValue(firstId, std::numeric_limits<double>::quiet_NaN()) && !w.setMixerValue(firstId, 7) && !w.dirty,
          "Mixer rejects missing tracks, nonfinite values and out-of-range levels without dirtying the project");
    w.mixer->strips[1]->name.onClick();
    check(w.selectedTrack == 1 && w.engine.midiTrack == 1, "Clicking a mixer track name selects its inspector and MIDI target");
    e.tracks[1]->name = "Renamed keys"; w.refresh(); check(w.mixer->strips[1]->name.getButtonText() == "Renamed keys", "Mixer names follow track renaming");
    check(w.removeTrackById(firstId) && w.mixer->strips.size() == 1 && w.mixer->strips[0]->trackId == midiId, "Track deletion removes its mixer strip while preserving surviving track identity");
    check(!w.setMixerValue(firstId, -20), "A deleted track's stable ID cannot modify a surviving mixer channel");
    for (int i = 0; i < 12; ++i) e.addTrack("Extra " + String(i)); w.refresh();
    w.mixer->viewport.setViewPosition(10000, 0);
    check(w.mixer->strips.size() == 13 && w.mixer->viewport.getViewPositionX() > 0 && w.mixer->master.getRight() <= w.mixer->getWidth(), "Many mixer channels scroll horizontally while the master remains fixed");
    w.setSize(1280, 820); w.transportExpanded = true; w.resized();
    check(w.viewport.getHeight() >= Timeline::ruler + 170 && w.horizontal.getBottom() <= w.mixer->getY() && w.mixer->getBottom() <= 820 - 27,
          "Mixer and expanded transport fit the minimum window without overlapping timeline controls");
    w.viewport.setViewPosition(0, 0);
    w.timeline.selectedClip = 0;
    const auto lastTrack = int(e.tracks.size()) - 1;
    w.mixer->strips.back()->name.onClick();
    const auto lastTop = w.timeline.trackTop(lastTrack);
    const auto viewTop = w.viewport.getViewPositionY();
    check(w.selectedTrack == lastTrack && w.timeline.selectedClip == -1 && viewTop > 0
          && lastTop >= viewTop + Timeline::ruler && lastTop + 64 <= viewTop + w.viewport.getViewHeight(),
          "Selecting an offscreen mixer track reveals its controls below the fixed ruler and clears the previous track's clip selection");
    w.keyPressed(KeyPress(KeyPress::F3Key)); check(!w.mixerVisible && !w.mixer->isVisible(), "F3 hides the mixer and restores timeline space");
    e.reset(); e.addTrack("Temporary"); w.refresh(); w.toggleMixer(); auto emptyId = e.tracks[0]->id; w.removeTrackById(emptyId);
    check(w.mixer->strips.empty() && w.setMixerValue({}, -2), "An empty session keeps a safe usable master mixer strip");
    w.engine.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis"));
    w.transportExpanded = false; w.setSize(1400, 880); w.resized(); w.setTimelineZoom(70); w.mixer->viewport.setViewPosition(0, 0);
    w.engine.tracks[0]->peak = 0.7f; w.engine.tracks[1]->peak = 0.4f; w.engine.tracks[2]->peak = 0.25f; w.engine.tracks[3]->peak = 0.5f; w.engine.outputPeak = 0.8f; w.refresh();
    w.dirty = false; w.status = "Ready.";
    if (auto stream = directory.getChildFile("mixer-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(w.createComponentSnapshot(w.getLocalBounds()), *stream); }
    directory.getChildFile("mixer-verification.txt").replaceWithText("Synthetic/offline only. Verified dock/F3 layout, audio and MIDI strips, gain/pan signal changes, inspector synchronization both directions, mute/solo/REC/MON, live stereo input toggling from track and mixer, recording guards, MIDI MON disabled, master synchronization, silence at minimum, save/reload, invalid/stale targets, track selection/rename/delete, horizontal scrolling, fixed master and minimum-size layout.\n");
}
}
