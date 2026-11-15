#include "ProjectStart.h"
#include <stdexcept>
#if JUCE_WINDOWS
#include <windows.h>
#endif
namespace auralis {
void runRecentProjectTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto pump = [] {
        auto until = Time::getMillisecondCounterHiRes() + 140;
        while (Time::getMillisecondCounterHiRes() < until) {
            #if JUCE_WINDOWS
            MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            #endif
            Thread::sleep(1);
        }
    };
    const auto root = directory.getChildFile("Recent-" + Uuid().toString()); root.createDirectory();
    const auto registry = root.getChildFile("Settings/plugins.xml");
    const auto first = root.getChildFile("First light.auralis"), second = root.getChildFile("Midnight sketches.auralis");
    Workstation w(false, registry); w.stopTimer();
    w.showProjectStart(); auto& start = *w.projectStart;
    check(start.isVisible() && start.empty.isVisible() && !start.openRecent.isEnabled() && start.create.isEnabled(),
          "Startup screen handles an empty recent list with new/open actions and clear guidance");
    check(start.getBounds() == w.getLocalBounds() && !w.dirty, "Startup screen covers the workspace without editing the project");
    w.keyPressed(KeyPress(KeyPress::spaceKey)); w.keyPressed(KeyPress('R'));
    check(!w.engine.playing && !w.engine.recording, "Startup screen prevents transport shortcuts from running behind it");
    w.showProjectStart(false); w.engine.tracks[0]->name = "First session"; w.saveToFile(first);
    check(w.recentProjects.size() == 1 && w.recentProjects[0] == first && w.recentProjectsFile.existsAsFile() && !w.dirty,
          "Successful project save immediately persists its path in shared recent history");
    { Engine e; e.addTrack("Second session"); e.saveProject(second); }
    w.loadProjectFile(second); w.loadProjectFile(first);
    check(w.recentProjects.size() == 2 && w.recentProjects[0] == first && w.recentProjects[1] == second,
          "Successful opens promote recent projects without duplicate entries");
    { Workstation restarted(false, registry); restarted.stopTimer(); check(restarted.recentProjects == w.recentProjects, "Recent history restores in order across application restarts and version-independent settings"); }
    w.showProjectStart(); start.list.selectRow(1); start.openRecent.onClick();
    check(!start.isVisible() && w.projectFile == second && w.engine.tracks[0]->name == "Second session" && !w.dirty && w.recentProjects[0] == second,
          "Opening a recent entry loads its project, promotes history and closes the startup screen");
    w.showProjectStart(); start.list.selectRow(1); start.keyPressed(KeyPress(KeyPress::returnKey));
    check(w.projectFile == first && !start.isVisible(), "Enter opens the selected recent project through the normal project workflow");
    w.trackName.setText("Unsaved recent edit", false); w.trackName.onFocusLost(); w.showProjectStart();
    start.list.selectRow(1); start.openRecent.onClick();
    check(w.savePrompt && w.savePrompt->isCurrentlyModal() && start.isVisible(), "Opening a recent project still asks about unsaved changes");
    w.savePrompt->setAlpha(0); w.savePrompt->triggerButtonClick(TRANS("Cancel")); pump();
    check(w.projectFile == first && w.dirty && start.isVisible() && w.recentProjects[0] == first,
          "Cancelling a recent-project switch preserves edits, current project and history order");
    start.create.onClick(); check(w.savePrompt != nullptr, "New project from the startup screen also protects unsaved edits");
    w.savePrompt->setAlpha(0); w.savePrompt->triggerButtonClick(TRANS("Discard changes")); pump();
    check(w.projectFile == File() && !w.dirty && w.engine.tracks.size() == 1 && !start.isVisible() && w.recentProjects.size() == 2,
          "Confirmed new project opens a clean session while retaining recent history");
    const auto missing = root.getChildFile("Moved project.auralis");
    { Engine e; e.addTrack(); e.saveProject(missing); } w.rememberProject(missing); missing.deleteFile();
    w.loadRecentProjects(); w.showProjectStart();
    check(w.recentProjects.size() == 3 && !start.openRecent.isEnabled() && start.remove.isEnabled(),
          "Missing recent projects remain visible but cannot be opened through the disabled action");
    start.remove.onClick(); check(w.recentProjects.size() == 2 && first.existsAsFile() && second.existsAsFile(), "Remove from list forgets only the history entry without deleting project files");
    { Workstation restarted(false, registry); restarted.stopTimer(); check(restarted.recentProjects.size() == 2, "Removing a stale history entry persists across restart"); }
    const auto before = w.recentProjects;
    check(!w.rememberProject(root.getChildFile("never-opened.auralis")) && w.recentProjects == before, "A nonexistent project is never added to successful-open history");
    w.showProjectStart(false); w.engine.recording = true; w.showProjectStart();
    check(!start.isVisible(), "Recording cannot be interrupted by opening the startup screen"); w.engine.recording = false;
    for (int i = 0; i < 15; ++i) { auto file = root.getChildFile("Session-" + String(i) + ".auralis"); w.engine.saveProject(file); w.rememberProject(file); }
    check(w.recentProjects.size() == 12 && w.recentProjects.front().getFileName() == "Session-14.auralis" && w.recentProjects.back().getFileName() == "Session-3.auralis",
          "Recent history keeps the twelve most recent projects and drops the oldest entries");
    auto historyFile = w.recentProjectsFile; w.recentProjectsFile = root.getChildFile("Unwritable/history.xml");
    root.getChildFile("Unwritable").replaceWithText("file"); check(!w.saveRecentProjects(), "Recent history reports write failure without modifying project files"); w.recentProjectsFile = historyFile;
    const auto brokenRegistry = root.getChildFile("Broken/plugins.xml"); brokenRegistry.getParentDirectory().createDirectory(); brokenRegistry.getSiblingFile("recent-projects.xml").replaceWithText("<broken");
    { Workstation broken(false, brokenRegistry); broken.stopTimer(); check(broken.recentProjects.empty(), "Malformed recent history starts safely with an empty list"); }
    w.recentProjects = {first, second, missing}; w.saveRecentProjects(); w.showProjectStart();
    if (auto stream = directory.getChildFile("recent-projects-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(w.createComponentSnapshot(w.getLocalBounds()), *stream); }
    directory.getChildFile("recent-projects-verification.txt").replaceWithText("Isolated settings only. Verified empty startup, shortcut protection, save/open history updates, deduplication and order, persistence, opening through native controls and Enter, unsaved Cancel/Discard protection, new session, missing entries, metadata-only removal, recording guard, twelve-entry cap, write failure and malformed XML.\n");
}
}
