#include "Interface.h"
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif
namespace auralis {
void runTrackDeleteTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto pump = [] {
        auto end = Time::getMillisecondCounterHiRes() + 120;
        while (Time::getMillisecondCounterHiRes() < end) {
           #if JUCE_WINDOWS
            MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
           #endif
            Thread::sleep(1);
        }
    };
    Workstation workstation(false); workstation.stopTimer(); auto& engine = workstation.engine; auto& timeline = workstation.timeline;
    auto first = engine.tracks[0]->id; auto second = engine.addTrack("Keep MIDI", true).id;
    engine.addMidiClip(1, 0).notes.push_back({0, 4, 60, 100, 1}); workstation.refresh(); workstation.dirty = false;
    auto event = [&](Component& component, Point<float> point) {
        auto now = Time::getCurrentTime(); return MouseEvent(Desktop::getInstance().getMainMouseSource(), point, ModifierKeys(ModifierKeys::rightButtonModifier),
                                                           1, 0, 0, 0, 0, &component, &component, now, point, now, 1, false);
    };
    auto rightClick = [&](Point<float> point) { auto e = event(timeline, point); timeline.mouseDown(e); timeline.mouseUp(e); };
    auto closeMenu = [&] { PopupMenu::dismissAllActiveMenus(); pump(); };
    pump();
    check(!workstation.dirty && engine.master == 0.8f, "Initial control notifications do not modify project state when opening track menus");
    rightClick({44, float(Timeline::ruler + Timeline::row + 15)});
    check(workstation.trackMenuTarget == second && engine.tracks.size() == 2 && !workstation.dirty, "Right-clicking a track header opens its own delete menu without deleting anything"); closeMenu();
    engine.cursor = 1; rightClick({timeline.xAt(8), float(Timeline::ruler + Timeline::row + 35)});
    check(workstation.trackMenuTarget == second && engine.cursor == 1, "Right-clicking an empty track lane opens its track menu without moving playback"); closeMenu();
    auto* row = timeline.findChildWithID(second); auto* mute = dynamic_cast<TrackControlButton*>(row ? row->findChildWithID("mute") : nullptr);
    check(mute != nullptr, "Track-header controls support context menus");
    auto buttonEvent = event(*mute, {8, 8}); mute->mouseDown(buttonEvent); mute->mouseUp(buttonEvent);
    check(workstation.trackMenuTarget == second && !engine.tracks[1]->mute && !mute->getToggleState(), "Right-clicking MUTE opens the menu without changing mute state"); closeMenu();
    auto dismiss = [&](String label) {
        auto* prompt = workstation.trackRemovalPrompt.getComponent(); check(prompt && prompt->isCurrentlyModal(), "Track removal requires confirmation");
        prompt->setAlpha(0); prompt->triggerButtonClick(TRANS(label)); pump();
    };
    workstation.confirmRemoveTrack(second); dismiss("Cancel");
    check(engine.tracks.size() == 2 && !workstation.dirty, "Cancelling track removal preserves all tracks and project state");
    workstation.selectTrack(1); workstation.confirmRemoveTrack(first); dismiss("Delete track");
    check(engine.tracks.size() == 1 && engine.tracks[0]->id == second && workstation.selectedTrack == 0
          && engine.tracks[0]->clips.size() == 1 && engine.tracks[0]->clips[0].notes.size() == 1,
          "Confirmed removal deletes only its target and preserves the selected MIDI track and clips");
    auto third = engine.addTrack("Third").id, fourth = engine.addTrack("Fourth").id; workstation.refresh();
    workstation.confirmRemoveTrack(fourth); workstation.removeTrackById(second); workstation.selectTrack(0); dismiss("Delete track");
    check(engine.tracks.size() == 1 && engine.tracks[0]->id == third, "Track confirmation resolves a stable ID after indices or selection change");
    workstation.dirty = false; engine.recording = true; workstation.confirmRemoveTrack(third);
    check(!workstation.trackRemovalPrompt && !workstation.removeTrackById(third) && engine.tracks.size() == 1, "Recording prevents track deletion and its confirmation dialog"); engine.recording = false;
    check(!workstation.removeTrackById("missing") && !workstation.dirty, "Stale track targets cannot delete a different track or dirty the project");
    workstation.confirmRemoveTrack(third); dismiss("Delete track");
    check(engine.tracks.empty() && !workstation.volume.isEnabled() && workstation.trackName.getText().isEmpty(), "Deleting the final track leaves a safe empty arrangement and clears its inspector");
    engine.addTrack("New track"); workstation.selectTrack(0); workstation.refresh();
    check(workstation.volume.isEnabled() && workstation.trackName.getText() == "New track", "A new track restores inspector controls after deleting the final track");
    directory.getChildFile("track-delete-verification.txt").replaceWithText("Verified header/empty-lane/control right-click routing, mute-state protection, confirmation cancellation, exact target deletion, stable identity, recording guard, stale IDs, final-track removal and inspector recovery using GUI handlers and actual modal dismissal.\n");
}
}
