#pragma once
#include "Interface.h"
namespace auralis {
class ProjectStartPanel : public Component, public ListBoxModel {
public:
    explicit ProjectStartPanel(Workstation&);
    ~ProjectStartPanel() override { list.setModel(nullptr); }
    void refresh();
    void resized() override;
    void paint(Graphics&) override;
    bool keyPressed(const KeyPress&) override;
    int getNumRows() override;
    void paintListBoxItem(int row, Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged(int) override;
    void listBoxItemDoubleClicked(int row, const MouseEvent&) override;
    String getTooltipForRow(int row) override;
    void openSelected();
private:
    friend void runRecentProjectTests(const File&, StringArray&);
    Workstation& owner;
    Rectangle<int> card;
    ListBox list {"Recent projects", this};
    Label empty;
    TextButton create {"New project"}, browse {"Open project..."}, openRecent {"Open selected"}, remove {"Remove from list"}, close {"Continue session"};
};
}
