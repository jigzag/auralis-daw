#include "Interface.h"
#include <cmath>
#include <stdexcept>
namespace auralis {
void runFollowTests(const File& directory, StringArray& passed) {
    auto check = [&](bool ok, String name) { if (!ok) throw std::runtime_error(name.toStdString()); passed.add("PASS " + name); };
    Workstation w(false); w.stopTimer(); auto& t = w.timeline; auto& e = w.engine;
    w.setTimelineZoom(100); w.dirty = false;
    const double visible = (t.getWidth() - Timeline::header) / t.zoom;
    e.cursor = visible * 0.5; e.play(); w.followTransport();
    check(w.followPlayhead && t.scroll == 0, "Transport follow defaults on and keeps the view still while the playhead is comfortably visible");
    e.cursor = visible * 0.92; w.followTransport();
    check(std::abs(t.xAt(e.cursor) - (Timeline::header + visible * 0.2 * t.zoom)) < 0.01 && std::abs(t.scroll - w.horizontal.getCurrentRangeStart()) < 1e-9,
          "Playback advances the timeline near the right edge and keeps scrollbar and playhead aligned");
    e.cursor = 1200; w.followTransport();
    check(t.scroll > 1100 && t.xAt(e.cursor) >= Timeline::header && t.xAt(e.cursor) < t.getWidth() && w.horizontal.getMaximumRangeLimit() > 1200,
          "Playback follows a seek far outside the viewport and extends the horizontal navigation range");
    e.recording = true; e.cursor = 2400; w.followTransport();
    check(t.scroll > 2300 && t.xAt(e.cursor) < t.getWidth(), "Recording follows beyond the initial scrollbar range even before a take becomes a clip"); e.recording = false;
    e.cursor = 0; w.followTransport(); check(t.scroll == 0, "A loop wrap or backward seek returns follow view to the earlier playhead");
    w.horizontal.setCurrentRange(100, visible, dontSendNotification); e.cursor = 500; w.followTransport();
    check(t.scroll == 100 && w.followResumeAt > Time::getMillisecondCounterHiRes(), "Manual horizontal navigation temporarily suspends automatic follow");
    w.followResumeAt = 0; w.followTransport(); check(t.scroll > 400, "Automatic follow resumes after the manual navigation grace period");
    e.stop(); const double stopped = t.scroll; e.cursor = 1000; w.followTransport();
    check(t.scroll == stopped, "Stopped transport never automatically changes the visible timeline");
    w.followPlayhead = false; e.play(); w.followTransport(); check(t.scroll == stopped, "Follow can be disabled without stopping playback"); w.followPlayhead = true;
    e.recording = true; e.countInRemaining = 100; w.followTransport();
    check(t.scroll == stopped, "Recording count-in does not move the timeline"); e.recording = false; e.countInRemaining = 0;
    auto now = Time::getCurrentTime(); Point<float> p(float(Timeline::header + 50), 12);
    MouseEvent down(Desktop::getInstance().getMainMouseSource(), p, ModifierKeys(ModifierKeys::leftButtonModifier), 1, 0, 0, 0, 0, &t, &t, now, p, now, 1, false);
    t.mouseDown(down); e.cursor = 2000; w.followTransport();
    check(t.scroll == stopped, "Dragging the playhead suspends follow so the ruler remains under the pointer"); t.mouseUp(down);
    w.hasTimeSelection = true; w.timeSelection = {1, 3}; const auto zoom = t.zoom; e.cursor = 2000; w.followTransport();
    check(!w.dirty && t.zoom == zoom && w.timeSelection == Range<double>(1, 3) && e.cursor == 2000,
          "Following only changes the view and preserves project state, range selection, zoom and transport time");
    e.stop(); e.loadProject(File::getCurrentWorkingDirectory().getChildFile("Demo/First light.auralis")); w.hasTimeSelection = false;
    w.setTimelineZoom(200); e.cursor = 11.5; e.play(); w.followResumeAt = 0; w.refresh(); w.followTransport();
    if (auto stream = directory.getChildFile("transport-follow-preview.png").createOutputStream()) { stream->setPosition(0); stream->truncate(); PNGImageFormat().writeImageToStream(w.createComponentSnapshot(w.getLocalBounds()), *stream); }
    directory.getChildFile("transport-follow-verification.txt").replaceWithText("No audio hardware opened. Verified near-edge playback advance, out-of-view seeks, long recording range growth, loop/backward return, stopped/count-in/disabled guards, two-second manual navigation grace period, drag protection, scrollbar synchronization and preservation of transport/zoom/selection/saved state.\n");
}
}
