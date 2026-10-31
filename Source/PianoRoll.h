#pragma once
#include "Interface.h"
#include <set>
#include <optional>
namespace auralis {
class PianoRoll : public Component, public Timer {
public:
    PianoRoll(Workstation&, String track, String clip);
    ~PianoRoll() override { stopTimer(); endPreview(); }
    Clip* resolve();
    void paint(Graphics&) override;
    void mouseDown(const MouseEvent&) override;
    void mouseDoubleClick(const MouseEvent&) override;
    void mouseDrag(const MouseEvent&) override;
    void mouseUp(const MouseEvent&) override;
    void mouseWheelMove(const MouseEvent&, const MouseWheelDetails&) override;
    void focusLost(FocusChangeType) override { endPreview(); }
    void visibilityChanged() override { if (!isShowing()) endPreview(); }
    void enablementChanged() override { if (!isEnabled()) endPreview(); }
    bool keyPressed(const KeyPress&) override;
    void timerCallback() override;
    void endPreview();
    void updateSize();
    void fitClip();
    void setVelocity(int);
    void setChannel(int);
    void quantize();
    bool seekPlayhead(float contentX, bool snapToGrid = false);
    void endPlayheadDrag();
    double getPlayheadX();
    int getNumSelectedNotes() const { return int(selectedNotes.size()); }
    bool isNoteSelected(int index) const { return selectedNotes.contains(index); }
    double grid = 0.25, pixels = 78;
    int selected = -1, velocity = 100, channel = 1;
    std::function<void()> selectionChanged;
private:
    Component::SafePointer<Workstation> owner;
    String trackId, clipId;
    MidiNote original;
    std::set<int> selectedNotes, selectionBeforeMarquee;
    std::vector<std::pair<int, MidiNote>> dragOriginals;
    std::optional<Range<double>> selectedBeatRange, rangeBeforeDrag;
    std::optional<double> playheadDragBeat;
    Rectangle<float> marquee;
    Point<float> origin;
    bool resizing = false, editInProgress = false, selectingRange = false, draggingPlayhead = false;
    int previewPitch = -1, previewChannel = 1;
    static constexpr int keyWidth = 62, rowHeight = 16, top = 28;
    Rectangle<float> noteRect(const MidiNote&, const Clip&) const;
    int pitchAt(float y) const { return jlimit(0, 127, 127 - int((y - top) / rowHeight)); }
    double snapBeat(double b) const { return grid > 0 ? std::round(b / grid) * grid : b; }
    void edited();
    void clearSelection();
    void syncSelection(const Clip&);
    void deleteSelection(Clip&);
    void duplicateSelection(Clip&);
};
class PianoRollPanel : public Component {
public:
    PianoRollPanel(Workstation&, String track, String clip);
    void resized() override;
    void paint(Graphics&) override;
    void mouseDown(const MouseEvent&) override;
    void mouseDrag(const MouseEvent&) override;
    void mouseUp(const MouseEvent&) override;
    void mouseWheelMove(const MouseEvent&, const MouseWheelDetails&) override;
    PianoRoll roll;
private:
    Viewport viewport;
    ComboBox grid, channel;
    Slider velocity;
    TextButton quantize {"Quantize clip"}, play {"Play / Stop"};
    bool draggingPlayhead = false;
};
class PianoRollWindow : public DocumentWindow {
public:
    PianoRollWindow(Workstation&, String track, String clip);
    String clipId;
    void closeButtonPressed() override { if (auto* p = dynamic_cast<PianoRollPanel*>(getContentComponent())) p->roll.endPreview(); setVisible(false); }
};
}
