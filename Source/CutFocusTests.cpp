#include "Interface.h"
#include <stdexcept>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace auralis {
void runCutFocusTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    auto pump = [] {
        const auto end = Time::getMillisecondCounterHiRes() + 80;
        while (Time::getMillisecondCounterHiRes() < end) {
           #if JUCE_WINDOWS
            MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
           #endif
            Thread::sleep(1);
        }
    };
    Workstation w(false); w.stopTimer(); auto& e = w.engine; auto& t = w.timeline;
    e.importAudio(directory.getChildFile("source.wav"), 0, 0); w.setTimelineZoom(200); w.refresh();
    auto* select = dynamic_cast<TextButton*>(w.rulerBar.findChildWithID("selectTool"));
    auto* cut = dynamic_cast<TextButton*>(w.rulerBar.findChildWithID("cutTool"));
    auto* row = t.findChildWithID(e.tracks[0]->id);
    auto* name = row ? dynamic_cast<TextEditor*>(row->findChildWithID("trackName")) : nullptr;
    check(select && cut && name && select->isVisible() && cut->isVisible() && select->getToggleState() && !cut->getToggleState(),
          "Fixed timeline ruler exposes select and cut controls with the current tool highlighted");
    check(select->getRight() <= cut->getX() && cut->getRight() <= Timeline::header && select->getY() >= 0 && cut->getBottom() <= Timeline::ruler,
          "Compact tool controls fit inside the fixed header without overlapping the time lanes");
    w.setAlpha(0); w.addToDesktop(ComponentPeer::windowIsTemporary | ComponentPeer::windowIgnoresMouseClicks);
    w.setVisible(true); w.toFront(true); pump(); auto* peer = w.getPeer();
    check(peer && e.devices.getCurrentAudioDevice() == nullptr, "Shortcut regression opens only an invisible native test window without audio hardware");
    name->setText("Original track", false); name->grabKeyboardFocus(); name->selectAll(); name->insertTextAtCaret("Renamed track"); name->onReturnKey(); pump();
    const String savedName = e.tracks[0]->name;
    check(savedName == "Renamed track", "Shortcut regression commits a real track-name edit with text undo history");
    name->grabKeyboardFocus(); cut->onClick();
    check(t.isCutMode() && cut->getToggleState() && !select->getToggleState() && t.hasKeyboardFocus(false),
          "Clicking the cut control switches tools and moves keyboard focus out of the name editor");
    cut->onClick(); check(!t.isCutMode() && select->getToggleState(), "Clicking the active cut control returns to selection");
    cut->onClick();
    auto event = [&](Component& target, float x, float y) {
        Point<float> p(x, y); const auto now = Time::getCurrentTime();
        return MouseEvent(Desktop::getInstance().getMainMouseSource(), p, ModifierKeys(ModifierKeys::leftButtonModifier),
                          1, 0, 0, 0, 0, &target, &target, now, p, now, 1, false);
    };
    name->grabKeyboardFocus();
    check(name->hasKeyboardFocus(false), "Regression starts with keyboard focus in the track-name editor");
    auto split = event(t, t.xAt(0.5), float(Timeline::ruler + 45)); t.mouseDown(split); t.mouseUp(split);
    check(e.tracks[0]->clips.size() == 2 && t.hasKeyboardFocus(false), "Cutting a part captures keyboard focus from a previously edited name");
    const KeyPress undo('Z', ModifierKeys::ctrlModifier, 0);
    check(peer->handleKeyPress(undo) && e.tracks[0]->clips.size() == 1 && e.tracks[0]->clips[0].length == 1
          && e.tracks[0]->name == savedName && name->getText() == savedName && t.isCutMode(),
          "Native Ctrl+Z during cutting undoes the cut and preserves the committed track name");
    peer->handleKeyPress(KeyPress('C'));
    check(!t.isCutMode() && select->getToggleState(), "Native C switches from cutting to selection after a cut undo");
    peer->handleKeyPress(KeyPress('C')); peer->handleKeyPress(KeyPress('V'));
    check(!t.isCutMode() && select->getToggleState() && !cut->getToggleState(), "Native C and V stay synchronized with visible tool controls");
    name->grabKeyboardFocus(); auto ruler = event(w.rulerBar, t.xAt(0.25), 12); w.rulerBar.mouseDown(ruler); w.rulerBar.mouseUp(ruler);
    peer->handleKeyPress(undo);
    check(t.hasKeyboardFocus(false) && e.tracks[0]->name == savedName && name->getText() == savedName,
          "Fixed ruler clicks also route subsequent undo shortcuts away from track names");
    name->setText({}, false); name->setText(savedName, false); name->grabKeyboardFocus(); name->selectAll(); name->insertTextAtCaret("Draft name");
    peer->handleKeyPress(undo);
    check(name->getText() == savedName && e.tracks[0]->clips.size() == 1 && !t.isCutMode(),
          "Ctrl+Z inside an actively edited name still undoes text without editing timeline parts");
    select->onClick();
    check(t.hasKeyboardFocus(false), "Select control restores timeline shortcuts after text editing");
    e.recording = true; w.refresh(); cut->onClick();
    check(!select->isEnabled() && !cut->isEnabled() && !t.isCutMode(), "Recording disables tool switches and rejects stale cut-button actions"); e.recording = false;
    w.busy = true; w.refresh(); cut->onClick();
    check(!cut->isEnabled() && !t.isCutMode(), "Busy operations reject stale tool-switch actions"); w.busy = false; w.refresh();
    for (int i = 0; i < 5; ++i) e.addTrack("Backing " + String(i + 1)); w.refresh();
    const auto position = select->getScreenPosition(); w.viewport.setViewPosition(0, 300);
    check(select->getScreenPosition() == position && select->isEnabled() && cut->isEnabled(), "Tool switches remain fixed and usable while tracks scroll vertically");
    w.setVisible(false); w.removeFromDesktop(); w.setAlpha(1); w.viewport.setViewPosition(0, 0); t.setCutMode(true);
    if (auto stream = directory.getChildFile("cut-controls-preview.png").createOutputStream()) {
        stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(w.createComponentSnapshot(w.getLocalBounds()), *stream);
    }
    directory.getChildFile("cut-focus-verification.txt").replaceWithText("Invisible native window, no audio hardware. Verified fixed select/cut controls, button toggle and focus, actual track-name edit history, native Ctrl+Z routing after click cuts, unchanged committed track names, native C/V switching, fixed ruler focus, normal text undo while editing, recording/busy guards and fixed controls during vertical scrolling.\n");
}
}
