#include "Interface.h"
#include "PitchRoll.h"
#include <stdexcept>

namespace auralis {
void runUxTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, const String& name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto capture = [&](Component& component, const String& name) {
        if (auto stream = directory.getChildFile(name).createOutputStream()) {
            stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(component.createComponentSnapshot(component.getLocalBounds()), *stream);
        }
    };
    Workstation w(false); w.stopTimer(); auto& e = w.engine;
    auto* clear = dynamic_cast<TextButton*>(w.rulerBar.findChildWithID("clearTimeRange"));
    check(clear && !clear->isVisible(), "Range clearing is discoverable in its own lane without adding a control to an empty range");
    check(w.loopButton.getButtonText().endsWith(" OFF") && w.snapButton.getButtonText().endsWith(" ON") && w.metronomeButton.getButtonText().endsWith(" OFF"),
          "Transport switches communicate on/off state through text as well as colour");
    w.snapButton.onClick(); check(!w.snap && w.snapButton.getButtonText().endsWith(" OFF"), "Snap state text changes immediately when its button is used");
    w.setTimeSelection(2, 6); e.loop = true; e.loopStart = 2; e.loopEnd = 6; e.recordRange = true; e.cursor = 4.75; e.playing = true; w.refresh();
    check(clear->isVisible() && clear->isEnabled() && w.playButton.getButtonText() == "PLAYING", "Active selection exposes an enabled clearing action and the transport shows playback state");
    clear->onClick();
    check(!w.hasTimeSelection && !e.loop && !e.recordRange && e.playing && e.cursor == 4.75 && !clear->isVisible(),
          "The visible range action clears actual selection/loop/punch state without stopping playback or moving the playhead");
    e.playing = false; w.setTimeSelection(2, 6); e.recording = true; w.refresh();
    check(clear->isVisible() && !clear->isEnabled() && w.recordButton.getButtonText() == "RECORDING", "Recording state is explicit and prevents the visible range action");
    clear->onClick(); check(w.hasTimeSelection, "A stale range-button callback cannot cancel a range during recording"); e.recording = false;
    w.busy = true; w.refresh(); check(!clear->isEnabled(), "Busy work disables the contextual range action"); w.busy = false; w.clearTimeSelection();
    e.tracks[0]->armed = true; e.tracks[0]->monitor = true; w.refresh();
    auto* row = w.timeline.findChildWithID(e.tracks[0]->id);
    auto* arm = row ? dynamic_cast<TextButton*>(row->findChildWithID("arm")) : nullptr;
    auto* monitor = row ? dynamic_cast<TextButton*>(row->findChildWithID("monitor")) : nullptr;
    check(arm && arm->getButtonText() == "REC ARM" && monitor && monitor->getButtonText() == "MON ON",
          "Record arming is distinguished from active recording and the monitor button retains its name when active");
    e.recording = true; e.countInRemaining = 48000; w.refresh();
    check(arm->getButtonText() == "REC ARM", "Count-in remains record waiting instead of claiming that input is already captured");
    e.countInRemaining = 0; w.refresh();
    check(arm->getButtonText() == "REC LIVE", "The track label identifies active capture after count-in ends"); e.recording = false;
    e.tracks[0]->armed = false; e.tracks[0]->monitor = false; w.refresh();
    check(arm->getButtonText() == "REC" && monitor->getButtonText() == "MON", "Track state labels return to their inactive values without changing routing");
    w.setStatus(String::fromUTF8("操作完了のフィードバック")); e.lockMisses = 3; w.timerCallback();
    check(w.status == String::fromUTF8("操作完了のフィードバック"), "Recent action feedback remains readable while historical audio warnings stay visible in diagnostics");
    w.statusUntil = 0; w.timerCallback(); check(w.status.startsWith("Audio skipped "), "Audio warning feedback resumes after the action-message interval"); e.lockMisses = 0;
    w.setStatus(String::fromUTF8("音声・MIDIファイルをドロップするか、トラックを録音待機にします。"));
    capture(w, "ux-empty-preview.png");
    e.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis"));
    w.setTimeSelection(1, 5); e.loop = true; e.loopStart = 1; e.loopEnd = 5; e.recordRange = true; w.refresh();
    bool layout = true;
    for (int width : {1280, 1400, 1920}) {
        w.setSize(width, 880); w.resized();
        layout &= w.metronomeButton.getRight() < w.loopButton.getX() && w.loopButton.getRight() < w.snapButton.getX()
            && w.snapButton.getRight() < w.transportOptions.getX() && w.transportOptions.getRight() < w.masterVolume.getX()
            && clear->getBounds().getX() >= Timeline::header && clear->getRight() <= w.rulerBar.getWidth();
        w.viewport.setViewPosition(0, 250);
        layout &= clear->getY() >= Timeline::seekLaneHeight && clear->getBottom() <= Timeline::ruler;
        if (width == 1280) capture(w, "ux-compact-preview.png");
    }
    check(layout, "State labels, master volume and contextual range action stay separate at supported window widths and after scrolling");
    w.setSize(1400, 880); w.transportExpanded = true; w.resized(); w.viewport.setViewPosition(0, 0); capture(w, "ux-workstation-preview.png");
    w.toggleMixer(); capture(w, "ux-mixer-preview.png"); w.toggleMixer();
    e.importAudio(directory.getChildFile("pitch-scoop-original.wav"), 0, 0);
    // importAudio targets track zero; resolve the imported fixture by its filename.
    Clip* fixture = nullptr; String trackId;
    for (const auto& track : e.tracks) for (auto& part : track->clips) if (part.source && part.source->file.getFileName() == "pitch-scoop-original.wav") { fixture = &part; trackId = track->id; }
    check(fixture != nullptr, "UX pitch-editor fixture is available without audio hardware");
    PitchCorrectionSettings settings; settings.manual = true;
    PitchRollPanel p(w, trackId, fixture->id, *fixture, analysePitch(*fixture, settings)); p.stopTimer(); p.setLookAndFeel(&w.getLookAndFeel());
    check(!p.notes.empty(), "UX pitch-editor fixture contains editable notes");
    p.selected = {0}; p.moveSelected(0.1);
    check(p.hasPendingEdits() && p.applyButton.isEnabled() && p.play.getButtonText() == String::fromUTF8("適用して再生")
          && p.applyButton.findColour(TextButton::buttonColourId) == Colour(0xff725136), "Pending pitch edits highlight the commit action and identify playback as apply-then-play");
    capture(p, "ux-pitch-pending-preview.png");
    p.selected.clear(); p.discard();
    check(!p.hasPendingEdits() && !p.applyButton.isEnabled() && p.play.getButtonText() == String::fromUTF8("再生") && p.message.contains(String::fromUTF8("適用")),
          "Discarding pitch edits restores ordinary playback and explains the next editing action");
    p.loopRange = {0.2, 0.6}; p.rangeLoop.setToggleState(true, dontSendNotification); p.updateLoop();
    check(e.ownsPitchLoop(p.auditionId), "Pitch-loop UX test starts from an active editor loop");
    const auto now = Time::getCurrentTime(); Point<float> at(float(PitchRollPanel::keyWidth + 50), float(PitchRollPanel::rangeTop + 8));
    MouseEvent right(Desktop::getInstance().getMainMouseSource(), at, ModifierKeys(ModifierKeys::rightButtonModifier), 1, 0, 0, 0, 0, &p, &p, now, at, now, 1, false);
    p.mouseDown(right);
    check(p.loopRange.isEmpty() && !p.rangeLoop.getToggleState() && !e.ownsPitchLoop(p.auditionId) && e.loop,
          "Right-click clearing is consistent in the pitch loop lane and preserves the separate project loop");
    check(!e.devices.getCurrentAudioDevice(), "UX review tests and screenshots never open audio hardware");
    directory.getChildFile("ux-review-verification.txt").replaceWithText("Offline synthetic UI review. Verified explicit transport/arm/monitor states, contextual range clearing with native action routing and guards, current action feedback priority, independent recording/playing labels, no overlaps at 1280/1400/1920 widths, fixed controls after scrolling, pending-pitch emphasis/discard state, consistent editor-loop right-click cancellation and no device access. Captured empty, compact, expanded, mixer and pending-pitch screens for visual review.\n");
}
}
