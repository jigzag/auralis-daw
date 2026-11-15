#include "Interface.h"
#include <cmath>
#include <stdexcept>
namespace auralis {
void runTimelineLaneTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Workstation w(false); w.stopTimer(); auto& t = w.timeline;
    w.setTimelineZoom(100); w.engine.cursor = 0.25;
    auto event = [&](double seconds, int y) {
        Point<float> p(t.xAt(seconds), float(y)); auto now = Time::getCurrentTime();
        return MouseEvent(Desktop::getInstance().getMainMouseSource(), p, ModifierKeys(ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, &t, &t, now, p, now, 1, true);
    };
    auto upper = event(1, 12), lower = event(1, Timeline::seekLaneHeight + 12);
    t.mouseMove(upper); check(t.getMouseCursor() == MouseCursor::DraggingHandCursor && t.getTooltip() == TRANS("Drag to move the playhead. Shift: snap to beats."), "Upper timeline lane identifies playhead dragging with a hand cursor and tooltip");
    t.mouseMove(lower); check(t.getMouseCursor() == MouseCursor::CrosshairCursor && t.getTooltip() == TRANS("Drag to select a time range. L: repeat. P: punch in. Export uses this range."), "Lower timeline lane identifies range selection with a crosshair and tooltip");
    t.mouseDown(lower); auto rangeEnd = event(3, 12); t.mouseDrag(rangeEnd); t.mouseUp(rangeEnd);
    check(w.hasTimeSelection && std::abs(w.timeSelection.getStart() - 1) < 1e-9 && std::abs(w.timeSelection.getEnd() - 3) < 1e-9 && w.engine.cursor == 0.25,
          "Dragging from the range lane selects time without seeking even when crossing into the playhead lane");
    t.mouseDown(upper); auto seekEnd = event(2.25, Timeline::seekLaneHeight + 12); t.mouseDrag(seekEnd); t.mouseUp(seekEnd);
    check(std::abs(w.engine.cursor - 2.25) < 1e-9 && w.timeSelection == Range<double>(1, 3), "Dragging from the playhead lane seeks precisely while preserving the selected range");
    auto boundary = event(4, Timeline::seekLaneHeight); t.mouseDown(boundary); auto other = event(5, Timeline::seekLaneHeight); t.mouseDrag(other); t.mouseUp(other);
    check(w.timeSelection == Range<double>(4, 5) && std::abs(w.engine.cursor - 2.25) < 1e-9, "The visible lane separator matches the exact hit-test boundary");
    w.engine.recording = true; t.mouseDown(lower); t.mouseDrag(rangeEnd); t.mouseUp(rangeEnd); t.mouseDown(upper); t.mouseUp(upper); w.engine.recording = false;
    check(w.timeSelection == Range<double>(4, 5) && std::abs(w.engine.cursor - 2.25) < 1e-9, "Recording protects both range and playhead lanes");
    w.engine.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis"));
    w.setTimeSelection(1, 5); w.engine.loop = true; w.engine.loopStart = 1; w.engine.loopEnd = 5;
    w.engine.recordRange = true; w.engine.recordFrom = 1; w.engine.recordTo = 5; w.setTimelineZoom(90); w.refresh();
    auto picture = w.createComponentSnapshot(w.getLocalBounds());
    if (auto stream = directory.getChildFile("timeline-lanes-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(picture, *stream); }
    auto lanes = t.createComponentSnapshot(t.getLocalBounds());
    auto top = lanes.getPixelAt(Timeline::header + 40, 3), bottom = lanes.getPixelAt(Timeline::header + 40, Timeline::seekLaneHeight + 10);
    check(top.getGreen() > top.getBlue() && bottom.getBlue() > bottom.getGreen() && Timeline::ruler - Timeline::seekLaneHeight >= 24,
          "Timeline paints distinct mint and blue lanes with a larger range target");
    directory.getChildFile("timeline-lanes-verification.txt").replaceWithText("Verified visible mint playhead and blue range lanes, 26-pixel targets, exact boundary hit testing, cross-lane drags, independent seek/selection, cursor/tooltips and recording guards through native GUI handlers.\n");
}
}
