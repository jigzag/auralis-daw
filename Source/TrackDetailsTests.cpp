#include "Interface.h"
#include <cmath>
#include <stdexcept>
namespace auralis {
void runTrackDetailsTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Workstation w(false); w.stopTimer(); auto& e = w.engine;
    e.importAudio(directory.getChildFile("source.wav"), 0, 0); e.tracks[0]->clips[0].fadeIn = e.tracks[0]->clips[0].fadeOut = 0;
    e.addTrack("Keys", true); const String first = e.tracks[0]->id, second = e.tracks[1]->id; w.refresh(); w.selectTrack(1);
    auto* row = w.timeline.findChildWithID(first); auto* midiRow = w.timeline.findChildWithID(second);
    auto* name = dynamic_cast<TextEditor*>(row->findChildWithID("trackName")); auto* input = dynamic_cast<ComboBox*>(row->findChildWithID("trackInput"));
    auto* gain = dynamic_cast<Slider*>(row->findChildWithID("trackGain")); auto* pan = dynamic_cast<Slider*>(row->findChildWithID("trackPan")); auto* inserts = dynamic_cast<ListBox*>(row->findChildWithID("trackInserts"));
    check(name && input && gain && pan && inserts && row->getWidth() == Timeline::header && row->getHeight() < Timeline::row,
          "Each track owns editable name, input, gain, pan and insert list inside its left header");
    check(!w.trackName.isVisible() && !w.input.isVisible() && !w.pluginList.isVisible() && w.viewport.getWidth() == w.getWidth(),
          "Former right inspector is hidden and the timeline spans the complete window width");
    name->setText("Vocal input", false); name->onReturnKey(); input->setSelectedId(3, sendNotificationSync);
    check(e.tracks[0]->name == "Vocal input" && e.tracks[0]->input == 2 && w.selectedTrack == 1 && e.tracks[1]->midiChannel == 0,
          "Track name and input editing target their own stable track without changing selection");
    gain->setValue(-6, sendNotificationSync); pan->setValue(1, sendNotificationSync); e.master = 1;
    e.seek(0.25); e.play(); AudioBuffer<float> out(2, 64); e.render(out.getArrayOfWritePointers(), 2, 64); e.stop();
    check(out.getMagnitude(0, 0, 64) == 0 && std::abs(out.getSample(1, 20) + 0.125f * Decibels::decibelsToGain(-6.0f)) < 1e-5,
          "Per-track gain and pan widgets control the actual stereo signal");
    auto* midiInput = dynamic_cast<ComboBox*>(midiRow->findChildWithID("trackInput")); midiInput->setSelectedId(17, sendNotificationSync);
    check(e.tracks[1]->midiChannel == 16 && midiInput->getNumItems() == 17 && input->getNumItems() == 33,
          "MIDI and audio tracks display their own channel selectors");
    e.recording = true; w.refresh(); w.setTrackDetails(first, "Vocal input", 1, false);
    check(!input->isEnabled() && e.tracks[0]->input == 2 && gain->isEnabled(), "Recording protects track input routing while retaining live volume control"); e.recording = false;
    const auto testGain = File::getSpecialLocation(File::currentExecutableFile).getSiblingFile("TestGain.vst3"); OwnedArray<PluginDescription> found;
    for (auto* format : e.pluginFormats.getFormats()) format->findAllTypesForFile(found, testGain.getFullPathName());
    check(found.size() == 1, "Track insert regression effect is available"); e.addPlugin(0, *found[0]); e.addPlugin(1, *found[0]); w.refresh();
    const String effect = e.tracks[0]->inserts[0]->id, other = e.tracks[1]->inserts[0]->id;
    check(inserts->getModel()->getNumRows() == 1 && w.trackInsertAction(first, effect, 2) && e.tracks[0]->inserts[0]->bypass && !e.tracks[1]->inserts[0]->bypass,
          "Track insert lists and bypass actions address the intended effect independently of selected track");
    check(w.trackInsertAction(first, effect, 3) && !w.trackInsertAction(first, effect, 2) && e.tracks[1]->inserts[0]->id == other,
          "Removing an insert rejects stale actions and preserves the other track's insert");
    auto project = directory.getChildFile("track-details.auralis"); e.saveProject(project); Engine restored; restored.loadProject(project, {true, false}); // Trust only our compiled TestGain fixture.
    check(restored.tracks[0]->name == "Vocal input" && restored.tracks[0]->input == 2 && restored.tracks[1]->midiChannel == 16 && restored.tracks[1]->inserts.size() == 1,
          "Project save and reload preserve per-track routing, names and insert chains");
    w.engine.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis")); w.refresh(); w.setTimelineZoom(55);
    if (auto stream = directory.getChildFile("track-details-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(w.createComponentSnapshot(w.getLocalBounds()), *stream); }
    directory.getChildFile("track-details-verification.txt").replaceWithText("Synthetic/offline only. Verified all controls inside per-track left headers, full-width timeline, stable unselected-track routing/name edits, actual gain/pan audio, MIDI selectors, recording guards, real VST3 insert lists, stable bypass/remove targets, stale action rejection and project persistence.\n");
}
}
