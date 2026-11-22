#pragma once
#include "Interface.h"
namespace auralis {
class MixerStrip : public Component, private ListBoxModel {
public:
    MixerStrip(Workstation&, String trackId);
    ~MixerStrip() override { inserts.setModel(nullptr); }
    void sync();
    void paint(Graphics&) override;
    void resized() override;
    String trackId;
    Slider fader, pan;
    TextButton name;
    TrackControlButton arm {"REC"}, mute {"M"}, solo {"S"}, monitor {"MON"};
private:
    Workstation& owner;
    Colour colour {0xff80e3c0};
    String detail;
    float peak = 0;
    bool selected = false;
    TextButton add {TRANS("+ Add")}, more {"..."};
    ListBox inserts {"Master inserts", this};
    int getNumRows() override;
    void paintListBoxItem(int, Graphics&, int, int, bool) override;
    void listBoxItemDoubleClicked(int, const MouseEvent&) override;
    String getTooltipForRow(int) override;
};
class MixerPanel : public Component {
public:
    explicit MixerPanel(Workstation&);
    ~MixerPanel() override { viewport.setViewedComponent(nullptr, false); }
    void sync();
    void paint(Graphics&) override;
    void resized() override;
private:
    friend void runMixerTests(const File&, StringArray&);
    Workstation& owner;
    Viewport viewport;
    Component channels;
    std::vector<std::unique_ptr<MixerStrip>> strips;
    MixerStrip master;
    TextButton close {TRANS("Hide")};
};
}
