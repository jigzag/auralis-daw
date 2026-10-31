#include "Interface.h"
#include <cmath>
#include <stdexcept>
namespace auralis {
void runFixedRulerTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Workstation w(false); w.stopTimer(); auto& t = w.timeline; auto& bar = w.rulerBar;
    w.engine.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis")); w.refresh(); w.setTimelineZoom(100);
    const auto bounds = bar.getBounds(); w.viewport.setViewPosition(0, 300);
    check(w.viewport.getViewPositionY() == 300 && bar.getBounds() == bounds && bar.getY() == w.viewport.getY() && bar.getHeight() == Timeline::ruler,
          "PLAYHEAD and RANGE stay fixed above tracks when the timeline scrolls vertically");
    auto event = [&](double seconds, int y) { Point<float> p(t.xAt(seconds), float(y)); auto now = Time::getCurrentTime();
        return MouseEvent(Desktop::getInstance().getMainMouseSource(), p, ModifierKeys(ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, &bar, &bar, now, p, now, 1, true); };
    auto down = event(3.25, 12), move = event(4.75, 12); bar.mouseDown(down); bar.mouseDrag(move); bar.mouseUp(move);
    check(std::abs(w.engine.cursor - 4.75) < 1e-9 && !w.hasTimeSelection, "Fixed PLAYHEAD dragging seeks correctly without including the vertical viewport offset");
    auto from = event(2, 38), to = event(6, 38); bar.mouseDown(from); bar.mouseDrag(to); bar.mouseUp(to);
    check(w.timeSelection == Range<double>(2, 6) && std::abs(w.engine.cursor - 4.75) < 1e-9, "Fixed RANGE dragging selects time without moving the playhead after scrolling");
    w.engine.recording = true; bar.mouseDown(down); bar.mouseDrag(move); bar.mouseUp(move); bar.mouseDown(from); bar.mouseUp(from); w.engine.recording = false;
    check(w.timeSelection == Range<double>(2, 6) && std::abs(w.engine.cursor - 4.75) < 1e-9, "Fixed ruler respects recording seek and range-selection guards");
    t.scroll = 5; w.refresh(); auto scrolled = event(7.25, 12); bar.mouseDown(scrolled); bar.mouseUp(scrolled);
    check(std::abs(w.engine.cursor - 7.25) < 1e-9, "Fixed ruler remains horizontally aligned with the scrolled timeline");
    const double anchor = t.timeAt(float(Timeline::header + 170));
    auto now = Time::getCurrentTime(); Point<float> p(float(Timeline::header + 170), 12);
    MouseEvent wheelEvent(Desktop::getInstance().getMainMouseSource(), p, ModifierKeys(ModifierKeys::ctrlModifier), 1, 0, 0, 0, 0, &bar, &bar, now, p, now, 1, false);
    MouseWheelDetails wheel; wheel.deltaY = 0.1f; bar.mouseWheelMove(wheelEvent, wheel);
    check(std::abs(t.timeAt(p.x) - anchor) < 1e-7 && t.zoom > 100, "Ctrl+wheel over the fixed ruler zooms around the pointer's time");
    w.transportExpanded = true; w.setSize(1280, 820); w.resized(); w.toggleMixer();
    check(bar.getY() == w.viewport.getY() && bar.getWidth() == t.getWidth() && bar.getBottom() < w.horizontal.getY(), "Fixed lanes remain aligned after transport expansion, mixer toggle and window resize");
    w.toggleMixer(); w.transportExpanded = false; w.setSize(1400, 880); w.resized(); w.setTimelineZoom(70); t.scroll = 0; w.refresh(); w.viewport.setViewPosition(0, 300);
    if (auto stream = directory.getChildFile("fixed-ruler-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(w.createComponentSnapshot(w.getLocalBounds()), *stream); }
    directory.getChildFile("fixed-ruler-verification.txt").replaceWithText("Native GUI handlers with no hardware opened. Verified fixed lane bounds during vertical scrolling, seek/range coordinate forwarding, recording guards, horizontal alignment, pointer-anchored zoom and mixer/transport/window relayout. Fixed ruler painting skips all track waveform loops.\n");
}
}
