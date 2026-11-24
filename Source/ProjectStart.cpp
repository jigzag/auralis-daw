#include "ProjectStart.h"
#include "PianoRoll.h"
#include "PitchRoll.h"
namespace auralis {
void Workstation::loadRecentProjects() {
    recentProjects.clear(); if (recentProjectsFile == File()) return;
    auto xml = parseXML(recentProjectsFile); if (!xml || !xml->hasTagName("RECENTPROJECTS")) return;
    for (auto* item : xml->getChildIterator()) {
        const String path = item->getStringAttribute("path");
        if (!item->hasTagName("PROJECT") || !File::isAbsolutePath(path)) continue;
        File file(path); if (!file.hasFileExtension("auralis") || std::find(recentProjects.begin(), recentProjects.end(), file) != recentProjects.end()) continue;
        recentProjects.push_back(file); if (recentProjects.size() == 12) break;
    }
}
bool Workstation::saveRecentProjects() {
    if (recentProjectsFile == File()) return true;
    if (recentProjectsFile.getParentDirectory().createDirectory().failed()) return false;
    XmlElement xml("RECENTPROJECTS");
    for (const auto& file : recentProjects) xml.createNewChildElement("PROJECT")->setAttribute("path", file.getFullPathName());
    TemporaryFile temporary(recentProjectsFile);
    return xml.writeTo(temporary.getFile()) && temporary.overwriteTargetFileWithTemporary();
}
bool Workstation::rememberProject(const File& file) {
    if (!file.existsAsFile() || !file.hasFileExtension("auralis")) return false;
    std::erase(recentProjects, file); recentProjects.insert(recentProjects.begin(), file);
    if (recentProjects.size() > 12) recentProjects.resize(12);
    if (projectStart) projectStart->refresh();
    return saveRecentProjects();
}
void Workstation::showProjectStart(bool visible) {
    if (busy || engine.recording || !projectStart) return;
    if (visible) { engine.stop(); projectStart->refresh(); projectStart->setBounds(getLocalBounds()); projectStart->setVisible(true); projectStart->toFront(true); }
    else { projectStart->setVisible(false); grabKeyboardFocus(); }
    repaint();
}
void Workstation::newProject() {
    requestClose([safe = Component::SafePointer<Workstation>(this)] {
        if (!safe) return;
        safe->pitchWindows.clear(); safe->pianoWindows.clear(); safe->pluginWindows.clear(); safe->engine.reset(); safe->hasTimeSelection = false; safe->timeSelection = {}; safe->timeline.cancelTimeSelection(); safe->engine.addTrack(); safe->undo.clear(); safe->projectFile = {};
        safe->timeline.selectedClip = -1; safe->selectTrack(0); safe->markProjectSaved(); safe->tempo.setText("120"); safe->refresh(); safe->showProjectStart(false); safe->setStatus(TRANS("New project."));
    });
}
ProjectStartPanel::ProjectStartPanel(Workstation& w) : owner(w) {
    setComponentID("projectStart"); setWantsKeyboardFocus(true);
    addAndMakeVisible(list); list.setRowHeight(57); list.setOutlineThickness(1);
    addAndMakeVisible(empty); empty.setText(TRANS("Projects you open or save will appear here."), dontSendNotification);
    empty.setJustificationType(Justification::centred); empty.setInterceptsMouseClicks(false, false);
    empty.setColour(Label::textColourId, Colour(0xff8d9cab));
    list.setColour(ListBox::backgroundColourId, Colour(0xff151c25)); list.setColour(ListBox::outlineColourId, Colour(0xff34414e));
    for (auto* button : {&create, &browse, &openRecent, &remove, &close}) { addAndMakeVisible(button); button->setWantsKeyboardFocus(false); }
    create.onClick = [this] { owner.newProject(); };
    browse.onClick = [this] { owner.open(); };
    close.onClick = [this] { owner.showProjectStart(false); };
    openRecent.onClick = [this] { openSelected(); };
    remove.onClick = [this] {
        int row = list.getSelectedRow(); if (!isPositiveAndBelow(row, int(owner.recentProjects.size()))) return;
        owner.recentProjects.erase(owner.recentProjects.begin() + row);
        if (!owner.saveRecentProjects()) owner.error(TRANS("The recent project list could not be saved."));
        refresh();
    };
    openRecent.setColour(TextButton::buttonColourId, Colour(0xff337b65));
}
void ProjectStartPanel::refresh() {
    list.updateContent(); list.selectRow(owner.recentProjects.empty() ? -1 : 0); empty.setVisible(owner.recentProjects.empty()); selectedRowsChanged(list.getSelectedRow()); repaint();
}
int ProjectStartPanel::getNumRows() { return int(owner.recentProjects.size()); }
String ProjectStartPanel::getTooltipForRow(int row) { return isPositiveAndBelow(row, int(owner.recentProjects.size())) ? owner.recentProjects[size_t(row)].getFullPathName() : String(); }
void ProjectStartPanel::selectedRowsChanged(int row) {
    bool selected = isPositiveAndBelow(row, int(owner.recentProjects.size()));
    openRecent.setEnabled(selected && owner.recentProjects[size_t(row)].existsAsFile()); remove.setEnabled(selected);
}
void ProjectStartPanel::openSelected() {
    const int row = list.getSelectedRow(); if (!isPositiveAndBelow(row, int(owner.recentProjects.size()))) return;
    const File file = owner.recentProjects[size_t(row)];
    if (!file.existsAsFile()) { refresh(); owner.error(TRANS("This project was moved or deleted. Use Open project to locate it.")); return; }
    owner.openSelectedProject(file);
}
void ProjectStartPanel::listBoxItemDoubleClicked(int row, const MouseEvent&) { list.selectRow(row); openSelected(); }
bool ProjectStartPanel::keyPressed(const KeyPress& key) {
    if (key == KeyPress::returnKey) { openSelected(); return true; }
    if (key == KeyPress::escapeKey) { owner.showProjectStart(false); return true; }
    return false;
}
void ProjectStartPanel::paintListBoxItem(int row, Graphics& g, int width, int height, bool selected) {
    if (!isPositiveAndBelow(row, int(owner.recentProjects.size()))) return;
    const File& file = owner.recentProjects[size_t(row)]; const bool exists = file.existsAsFile();
    g.fillAll(Colour(selected ? 0xff294338 : 0xff151c25));
    g.setColour(Colour(exists ? 0xff80e3c0 : 0xffa16a70)); g.fillRoundedRectangle(12, 13, 4, float(height - 26), 2);
    g.setColour(Colour(exists ? 0xffe2e9ee : 0xffa6a9ae)); g.setFont(15);
    g.drawText(file.getFileNameWithoutExtension() + (exists ? "" : TRANS("  (not found)")), 27, 5, width - 42, 25, Justification::centredLeft);
    g.setColour(Colour(0xff8d9cab)); g.setFont(11); g.drawText(file.getFullPathName(), 27, 30, width - 42, 20, Justification::centredLeft);
}
void ProjectStartPanel::resized() {
    card = Rectangle<int>(0, 0, 790, 600).withCentre(getLocalBounds().getCentre());
    const int x = card.getX() + 30, width = card.getWidth() - 60;
    create.setBounds(x, card.getY() + 104, 164, 36); browse.setBounds(x + 176, card.getY() + 104, 164, 36);
    list.setBounds(x, card.getY() + 193, width, 320);
    empty.setBounds(list.getBounds());
    remove.setBounds(x, card.getBottom() - 59, 150, 32); close.setBounds(card.getRight() - 334, card.getBottom() - 59, 144, 32);
    openRecent.setBounds(card.getRight() - 178, card.getBottom() - 59, 148, 32);
}
void ProjectStartPanel::paint(Graphics& g) {
    g.fillAll(Colour(0xff10151c));
    g.setColour(Colour(0xff1a2029)); g.fillRoundedRectangle(card.toFloat(), 12);
    const int x = card.getX() + 30;
    g.setColour(Colour(0xff80e3c0)); g.setFont(28); g.drawText("AURALIS", x, card.getY() + 21, 300, 37, Justification::centredLeft);
    g.setColour(Colour(0xffe2e9ee)); g.setFont(15); g.drawText(TRANS("Start a project or pick up where you left off"), x, card.getY() + 62, 700, 25, Justification::centredLeft);
    g.setColour(Colour(0xff8d9cab)); g.setFont(11); g.drawText(TRANS("RECENT PROJECTS  /  Double-click or select and press Open"), x, card.getY() + 158, 710, 25, Justification::centredLeft);
}
}
