#include "Interface.h"
#include <stdexcept>
#include <cmath>
namespace auralis {
void runTrackDragTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Workstation w(false); w.stopTimer(); auto& e = w.engine; auto& t = w.timeline;
    const String first = e.tracks[0]->id; e.addTrack("Second audio"); const String second = e.tracks[1]->id; e.addTrack("MIDI", true); const String midi = e.tracks[2]->id;
    e.recordingDirectory = directory.getChildFile("DragRecording"); e.tracks[0]->armed = true; e.cursor = 1; e.startRecordingWriters();
    AudioBuffer<float> input(2, 128), output(2, 128); for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) input.setSample(ch, i, 0.2f);
    for (int i = 0; i < 375; ++i) e.render(output.getArrayOfWritePointers(), 2, 128, input.getArrayOfReadPointers(), 2); e.finishRecording();
    e.tracks[0]->clips[0].gainDb = -4; const Clip original = e.tracks[0]->clips[0]; w.refresh(); w.setTimelineZoom(100);
    auto event = [&](Point<float> p, Point<float> origin) { auto now = Time::getCurrentTime(); return MouseEvent(Desktop::getInstance().getMainMouseSource(), p, ModifierKeys(ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, &t, &t, now, origin, now, 1, p != origin); };
    auto drag = [&](Point<float> a, Point<float> b) { t.mouseDown(event(a, a)); t.mouseDrag(event(b, a)); t.mouseUp(event(b, a)); };
    drag({24, float(t.trackTop(0) + 16)}, {24, float(t.trackTop(1) + 30)});
    check(e.tracks[1]->id == first && e.tracks[0]->id == second && w.selectedTrack == 1 && e.tracks[1]->clips[0].id == original.id,
          "Dragging a track header reorders the complete track while retaining selection, recording and identity");
    drag({24, float(t.trackTop(1) + 16)}, {24, float(t.trackTop(0) + 16)});
    check(e.tracks[0]->id == first, "Track dragging also moves a track upward");
    float bottom = float(t.trackTop(0) + t.trackHeight(0) - 2); drag({150, bottom}, {150, bottom - 124});
    w.setTrackHeight(second, 80);
    check(e.tracks[0]->height == 100 && t.trackTop(1) == Timeline::ruler + 100 && t.trackTop(2) == Timeline::ruler + 180
        && t.trackAt(float(t.trackTop(1) + 79)) == 1 && t.trackAt(float(t.trackTop(2))) == 2,
          "Dragging a track bottom resizes it and updates every following row and hit boundary");
    drag({t.xAt(1.5), float(t.trackTop(0) + 65)}, {t.xAt(1.5), float(t.trackTop(1) + 40)});
    check(e.tracks[0]->clips.empty() && e.tracks[1]->clips.size() == 1 && e.tracks[1]->clips[0].id == original.id
        && e.tracks[1]->clips[0].source == original.source && e.tracks[1]->clips[0].gainDb == -4 && e.tracks[1]->clips[0].start == original.start,
          "Dragging a recorded waveform to another audio track preserves its time, volume and source without treating it as a gain gesture");
    w.undoEdit(); check(e.tracks[0]->clips.size() == 1 && e.tracks[1]->clips.empty(), "One undo restores a recorded part moved across variable-height tracks");
    w.setTrackHeight(first, 64); auto* controls = t.findChildWithID(first);
    auto* gain = controls->findChildWithID("trackGain"); auto* name = controls->findChildWithID("trackName");
    check(!gain->isVisible() && name->isVisible() && t.trackHeight(0) == 64, "Minimum track height keeps basic controls and hides details that cannot fit");
    w.setTrackHeight(first, 999); check(t.trackHeight(0) == 480, "Track heights are bounded at their maximum");
    w.setTrackHeight(first, 224); w.moveTrack(midi, 0); auto project = directory.getChildFile("reordered-tracks.auralis"); e.saveProject(project); Engine restored; restored.loadProject(project);
    check(restored.tracks[0]->midi && restored.tracks[2]->height == 80 && restored.tracks[1]->clips.size() == 1,
          "Project save and reload preserve track order, individual heights and recorded parts");
    e.recording = true; check(!w.moveTrack(first, 0) && !w.setTrackHeight(first, 100), "Recording blocks track reordering and track resizing"); e.recording = false;
    check(!w.moveTrack("missing", 0) && !w.setTrackHeight("missing", 100), "Stale track IDs cannot reorder or resize another track");
    auto* firstRow = t.findChildWithID(first); auto* fold = dynamic_cast<TextButton*>(firstRow->findChildWithID("trackFold"));
    check(fold && fold->isVisible() && fold->getButtonText() == "-", "Each expanded track shows a collapse button beside its name");
    const auto selectedId = e.tracks[size_t(w.selectedTrack)]->id; w.setTrackHeight(first, 160); fold->onClick();
    check(e.tracks[1]->height == 64 && fold->getButtonText() == "+" && e.tracks[size_t(w.selectedTrack)]->id == selectedId
        && e.tracks[1]->clips[0].id == original.id, "Track close button collapses only its track while retaining selection and recorded parts");
    fold->onClick(); check(e.tracks[1]->height == 160 && fold->getButtonText() == "-", "Track open button restores the previous manually chosen height");
    fold->onClick(); e.saveProject(project); Workstation reopened(false); reopened.stopTimer(); reopened.engine.loadProject(project); reopened.refresh();
    const auto restoredId = reopened.engine.tracks[1]->id; reopened.toggleTrackExpanded(restoredId);
    check(reopened.engine.tracks[1]->height == 160, "Collapsed state and previous expanded height survive project save and reload");
    e.recording = true; w.refresh(); bool blocked = !fold->isEnabled() && !w.toggleTrackExpanded(first); e.recording = false; w.refresh();
    check(blocked && e.tracks[1]->height == 64, "Recording disables track open/close and rejects stale toggle actions");
    check(!w.toggleTrackExpanded("missing"), "A stale open/close target cannot change another track");
    e.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis")); w.setTrackHeight(e.tracks[0]->id, 64); w.setTrackHeight(e.tracks[1]->id, 100); w.setTrackHeight(e.tracks[2]->id, 160); w.refresh();
    if (auto stream = directory.getChildFile("track-drag-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(w.createComponentSnapshot(w.getLocalBounds()), *stream); }
    directory.getChildFile("track-drag-verification.txt").replaceWithText("Synthetic recording only. Native drags verify track reorder up/down, stable selection and clip identity, bottom-edge resize, variable row boundaries, recorded waveform transfer preserving source/time/gain, one-step clip undo, compact/max bounds, persisted order/heights, recording guards and stale IDs.\n");
}
}
