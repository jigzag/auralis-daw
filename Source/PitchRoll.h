#pragma once
#include "Interface.h"
#include "PitchCorrection.h"
#include <set>

namespace auralis {
class PitchRollPanel;
class PitchRollGrid : public Component, public SettableTooltipClient {
public:
    explicit PitchRollGrid(PitchRollPanel&);
    void paint(Graphics&) override;
    void mouseDown(const MouseEvent&) override;
    void mouseDrag(const MouseEvent&) override;
    void mouseUp(const MouseEvent&) override;
    void mouseDoubleClick(const MouseEvent&) override;
    void focusLost(FocusChangeType) override;
    void mouseWheelMove(const MouseEvent&, const MouseWheelDetails&) override;
    bool keyPressed(const KeyPress&) override;
    Rectangle<float> noteBounds(int) const;
    double pixels = 120;
    static constexpr int defaultRow = 22, highest = 96, lowest = 24;
    int row = defaultRow;
    bool interacting() const { return dragging || selecting || drawingCurve; }
private:
    PitchRollPanel& panel;
    bool dragging = false, selecting = false;
    Point<float> origin;
    std::vector<PitchSegment> dragBefore;
    std::set<int> selectionBefore;
    int clickedNote = -1;
    bool toggleOnUp = false;
    bool drawingCurve = false;
    bool insertedCurveRegion = false;
    double curveGrabOffset = 0;
    PitchSegment curveBase;
    std::vector<PitchCurvePoint> curveStroke;
    double curveAnchorTime = 0;
    void finishCurve(bool cancel);
    int curveNoteAt(float x) const;
    int noteAt(Point<float>) const;
};
class PitchRollPanel : public Component, public Timer {
public:
    PitchRollPanel(Workstation&, String track, String clip, const Clip&, std::vector<PitchFrame>);
    ~PitchRollPanel() override;
    void paint(Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void mouseDown(const MouseEvent&) override;
    void mouseDrag(const MouseEvent&) override;
    void mouseUp(const MouseEvent&) override;
    void mouseDoubleClick(const MouseEvent&) override;
    void focusLost(FocusChangeType) override;
    void visibilityChanged() override;
    void mouseWheelMove(const MouseEvent&, const MouseWheelDetails&) override;
    bool keyPressed(const KeyPress& key) override { return grid.keyPressed(key); }
    Clip* resolve();
    void setAnalysis(const Clip&, std::vector<PitchFrame>);
    bool hasPendingEdits() const;
    bool targetsTrack(const String&);
    bool apply(bool playAfter = false);
    void discard();
    void showCommitRequired();
    void beginAudition(int note, double sourceSeconds);
    void updateAudition();
    void endAudition(bool immediate = false);
    void suspendPlayback();
    double playheadX() const;
    void fit();
    void updateSize();
    void zoomVertical(double multiplier, double viewY = -1);
    void undoLocal();
    void redoLocal();
    void remember();
    void changed();
    void moveSelected(double semitones);
    void restoreSelected();
    void snapSelected();
    bool splitSelected(double sourceSeconds);
    void contextMenu(int note, double sourceSeconds);
    void numericPitch();
    void editScoop();
    bool mergeSelected();
    bool drawCurve(int index, const PitchSegment& before, const std::vector<PitchCurvePoint>& stroke);
    void smoothSelectedCurves();
    double curvePitchAt(int index, double sourceSeconds) const;
    PitchCorrectionSettings settings() const;
    const String clipId;
private:
    friend class PitchRollGrid;
    friend void runPitchRollTests(const File&, StringArray&);
    friend void runPitchPreviewTests(const File&, StringArray&);
    friend void runPitchFollowTests(const File&, StringArray&);
    friend void runPitchScoopTests(const File&, StringArray&);
    friend void runPitchCurveTests(const File&, StringArray&);
    friend void runPitchZoomTests(const File&, StringArray&);
    friend void runPitchLoopTests(const File&, StringArray&);
    friend void runUxTests(const File&, StringArray&);
    Component::SafePointer<Workstation> owner;
    String trackId;
    Clip snapshot;
    std::vector<PitchFrame> frames;
    std::vector<PitchSegment> notes, baseline;
    std::vector<std::vector<PitchSegment>> undo, redo;
    std::set<int> selected;
    Viewport viewport;
    PitchRollGrid grid {*this};
    TextButton play, stop, applyButton, undoButton, discardButton, fitButton, reloadButton, restoreButton;
    ToggleButton dragAudition, curveEdit;
    ToggleButton rangeLoop;
    Slider verticalZoom;
    Component::SafePointer<AlertWindow> numericPrompt;
    Component::SafePointer<AlertWindow> scoopPrompt;
    TooltipWindow tooltips {this, 600};
    String message;
    bool pending = false, stale = false, seeking = false;
    const String auditionId = Uuid().toString();
    int auditionNote = -1;
    double seekPosition = 0;
    double followResumeAt = 0;
    int followViewX = 0;
    Range<double> loopRange, loopBefore;
    bool selectingLoop = false;
    double loopAnchor = 0;
    static constexpr int keyWidth = 64, bodyTop = 116, rangeTop = 92, rulerTop = 68;
    void seek(float x);
    void finishSeek();
    void followTransport();
    void updateLoop();
    void dragLoop(float x);
    void finishLoop(bool cancel);
    void syncCurrent();
    void showSelectedMessage();
    std::shared_ptr<const PitchPreviewCurve> auditionCurve(const PitchSegment&) const;
    double curvePitchAt(const PitchSegment*, double sourceSeconds) const;
};
class PitchRollWindow : public DocumentWindow {
public:
    PitchRollWindow(Workstation&, String track, String clip, const Clip&, std::vector<PitchFrame>);
    ~PitchRollWindow() override;
    void closeButtonPressed() override;
    void activeWindowStatusChanged() override;
    void visibilityChanged() override;
    PitchRollPanel& panel() const { return *content; }
    const String clipId;
private:
    Component::SafePointer<Workstation> owner;
    PitchRollPanel* content = nullptr;
    Component::SafePointer<AlertWindow> closePrompt;
};
}
