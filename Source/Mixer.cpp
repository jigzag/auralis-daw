#include "Mixer.h"
#include "PluginInfo.h"
#include <cmath>
namespace auralis {
namespace {
void text(Graphics& g, const String& value, Rectangle<int> bounds, float size, Colour colour = Colour(0xff8d9cab)) {
    g.setFont(size); g.setColour(colour); g.drawText(value, bounds, Justification::centred);
}
bool editing(Slider& slider) {
    auto* focus = Component::getCurrentlyFocusedComponent();
    return slider.isMouseButtonDown() || (dynamic_cast<TextEditor*>(focus) && slider.isParentOf(focus));
}
}
MixerStrip::MixerStrip(Workstation& w, String id) : trackId(std::move(id)), owner(w) {
    setComponentID(trackId.isEmpty() ? "mixerMaster" : trackId);
    for (auto* slider : {&fader, &pan}) { addAndMakeVisible(slider); slider->setWantsKeyboardFocus(false); }
    fader.setComponentID("mixerGain"); fader.setSliderStyle(Slider::LinearVertical); fader.setRange(-60, 6, 0.1);
    fader.setSkewFactorFromMidPoint(-12); fader.setTextBoxStyle(Slider::TextBoxBelow, false, 70, 21);
    fader.setTextValueSuffix(" dB"); fader.setDoubleClickReturnValue(true, 0);
    fader.setTooltip(TRANS("Volume. Double-click: 0 dB. Type a value below the fader. -60 dB: silence."));
    pan.setComponentID("mixerPan"); pan.setSliderStyle(Slider::LinearHorizontal); pan.setRange(-1, 1, 0.01);
    pan.setTextBoxStyle(Slider::NoTextBox, false, 0, 0); pan.setDoubleClickReturnValue(true, 0);
    pan.setTooltip(TRANS("Pan left / right. Double-click: center."));
    fader.onValueChange = [this] { owner.setMixerValue(trackId, fader.getValue()); };
    pan.onValueChange = [this] { owner.setMixerValue(trackId, pan.getValue(), true); };
    addAndMakeVisible(name); name.setWantsKeyboardFocus(false);
    name.onClick = [this] { if (!owner.busy && trackId.isNotEmpty()) for (int i = 0; i < int(owner.engine.tracks.size()); ++i)
        if (owner.engine.tracks[size_t(i)]->id == trackId) { owner.selectTrack(i); return; } };
    name.setTooltip(TRANS("Select this track in the arrangement"));
    for (auto entry : {std::pair<TextButton*, TrackControl>{&arm, TrackControl::recordArm}, {&mute, TrackControl::mute}, {&solo, TrackControl::solo}, {&monitor, TrackControl::monitor}}) {
        auto& button = *entry.first; addAndMakeVisible(button); button.setWantsKeyboardFocus(false);
        button.onClick = [this, action = entry.second] { if (!owner.busy) owner.toggleTrackControl(trackId, action); };
    }
    arm.setColour(TextButton::buttonOnColourId, Colour(0xffa63e52)); mute.setColour(TextButton::buttonOnColourId, Colour(0xff9b742c)); solo.setColour(TextButton::buttonOnColourId, Colour(0xff3c788e));
    arm.setTooltip(TRANS("Arm track for recording")); mute.setTooltip(TRANS("Mute track")); solo.setTooltip(TRANS("Solo track"));
    monitor.setComponentID("mixerMonitor"); monitor.setTooltip(TRANS("Input monitor: hear live input instead of this track's clips")); monitor.setColour(TextButton::buttonOnColourId, Colour(0xff337b65));
    if (trackId.isEmpty()) { pan.setVisible(false); arm.setVisible(false); mute.setVisible(false); solo.setVisible(false); monitor.setVisible(false); name.setButtonText(TRANS("MASTER")); }
    if (trackId.isEmpty()) {
        addAndMakeVisible(add); addAndMakeVisible(more); addAndMakeVisible(inserts); inserts.setComponentID("masterInserts");
        add.setComponentID("masterAddInsert"); more.setComponentID("masterInsertMenu"); inserts.setRowHeight(18);
        inserts.setColour(ListBox::backgroundColourId, Colour(0xff11151b));
        add.onClick = [this] { owner.trackInsertMenu({}, {}, true); };
        more.onClick = [this] { int i = inserts.getSelectedRow(); owner.trackInsertMenu({}, isPositiveAndBelow(i, int(owner.engine.masterInserts.size())) ? owner.engine.masterInserts[size_t(i)]->id : String()); };
    }
}
void MixerStrip::sync() {
    const Track* track = nullptr; int index = 0;
    if (trackId.isNotEmpty()) for (int i = 0; i < int(owner.engine.tracks.size()); ++i) if (owner.engine.tracks[size_t(i)]->id == trackId) { track = owner.engine.tracks[size_t(i)].get(); index = i; break; }
    setEnabled(!owner.busy && (trackId.isEmpty() || track));
    if (trackId.isNotEmpty() && !track) return;
    const float gain = track ? track->gain : owner.engine.master.load();
    if (!editing(fader)) fader.setValue(Decibels::gainToDecibels(gain, -60.0f), dontSendNotification);
    if (track) {
        name.setButtonText(track->name); colour = track->colour; selected = index == owner.selectedTrack;
        if (!editing(pan)) pan.setValue(track->pan, dontSendNotification);
        arm.setToggleState(track->armed, dontSendNotification); mute.setToggleState(track->mute, dontSendNotification); solo.setToggleState(track->solo, dontSendNotification);
        arm.setEnabled(!owner.engine.recording && !owner.busy);
        monitor.setToggleState(track->monitor, dontSendNotification); monitor.setEnabled(!track->midi && !owner.busy);
        detail = track->monitor ? String::fromUTF8("入力試聴 / パート消音") : String(index + 1).paddedLeft('0', 2) + (track->midi ? "  MIDI" : String::fromUTF8("  音声")) + " / " + String(track->inserts.size()) + " FX";
    } else { detail = "OUT / " + String(owner.engine.masterInserts.size()) + " FX"; inserts.updateContent(); inserts.repaint(); add.setEnabled(!owner.engine.recording && !owner.busy); more.setEnabled(!owner.engine.recording && !owner.busy); }
    peak = jmax(track ? track->peak.load() : owner.engine.outputPeak.load(), peak * 0.82f);
    repaint();
}
void MixerStrip::resized() {
    const int width = getWidth(), height = getHeight();
    name.setBounds(7, 8, width - 14, 25);
    arm.setBounds(8, 40, 28, 22); mute.setBounds(40, 40, 22, 22); solo.setBounds(66, 40, 22, 22); monitor.setBounds(92, 40, 32, 22);
    pan.setBounds(13, 75, width - 26, 25);
    fader.setBounds(10, 108, 72, height - 135);
    add.setBounds(8, 39, 72, 22); more.setBounds(87, 39, 36, 22); inserts.setBounds(8, 65, width - 16, 32);
}
void MixerStrip::paint(Graphics& g) {
    g.fillAll(Colour(selected ? 0xff26323b : 0xff1a2029));
    g.setColour(colour); g.fillRect(0, 0, getWidth(), 3);
    g.setColour(Colour(0xff34414e)); g.drawRect(getLocalBounds());
    if (trackId.isNotEmpty()) text(g, "L        PAN        R", {8, 64, getWidth() - 16, 17}, 9);
    text(g, peak > 0.00001f ? String(Decibels::gainToDecibels(peak), 1) : "-inf", {80, 96, 45, 16}, 9, peak > 1 ? Colour(0xffff7a85) : Colour(0xff80e3c0));
    Rectangle<float> meter(93, 117, 10, float(jmax(20, getHeight() - 157)));
    g.setColour(Colour(0xff10151b)); g.fillRect(meter);
    float amount = jlimit(0.0f, 1.0f, (Decibels::gainToDecibels(peak, -60.0f) + 60) / 60);
    g.setColour(peak > 1 ? Colour(0xffff7a85) : colour); g.fillRect(meter.withTop(meter.getBottom() - meter.getHeight() * amount));
    for (int db : {0, -12, -24, -48}) { int y = int(meter.getY() + meter.getHeight() * (-db / 60.0f)); text(g, String(db), {105, y - 6, 23, 12}, 8); }
    text(g, detail, {3, getHeight() - 22, getWidth() - 6, 18}, 9);
}
int MixerStrip::getNumRows() { return trackId.isEmpty() ? int(owner.engine.masterInserts.size()) : 0; }
void MixerStrip::paintListBoxItem(int i, Graphics& g, int width, int height, bool selectedRow) {
    if (!isPositiveAndBelow(i, int(owner.engine.masterInserts.size()))) return;
    auto& p = *owner.engine.masterInserts[size_t(i)]; if (selectedRow) { g.setColour(Colour(0xff30463f)); g.fillAll(); }
    g.setColour(Colour(p.bypass ? 0xff8d9cab : 0xffe2e9ee)); g.setFont(10); g.drawText(p.description.name + (p.bypass ? TRANS(" [off]") : ""), 3, 0, width - 6, height, Justification::centredLeft);
}
void MixerStrip::listBoxItemDoubleClicked(int i, const MouseEvent&) { if (isPositiveAndBelow(i, int(owner.engine.masterInserts.size()))) owner.trackInsertAction({}, owner.engine.masterInserts[size_t(i)]->id, 1); }
String MixerStrip::getTooltipForRow(int i) { return isPositiveAndBelow(i, int(owner.engine.masterInserts.size())) ? pluginHelp(owner.engine.masterInserts[size_t(i)]->description) : String(); }
MixerPanel::MixerPanel(Workstation& w) : owner(w), master(w, {}) {
    setComponentID("trackMixer"); addAndMakeVisible(viewport); viewport.setViewedComponent(&channels, false); viewport.setScrollBarsShown(false, true);
    addAndMakeVisible(master); addAndMakeVisible(close); close.setWantsKeyboardFocus(false);
    close.onClick = [this] { owner.toggleMixer(); };
}
void MixerPanel::sync() {
    bool rebuild = strips.size() != owner.engine.tracks.size();
    if (!rebuild) for (size_t i = 0; i < strips.size(); ++i) if (strips[i]->trackId != owner.engine.tracks[i]->id) { rebuild = true; break; }
    if (rebuild) {
        strips.clear(); for (const auto& track : owner.engine.tracks) { auto strip = std::make_unique<MixerStrip>(owner, track->id); channels.addAndMakeVisible(*strip); strips.push_back(std::move(strip)); }
        resized();
    }
    for (auto& strip : strips) strip->sync(); master.sync(); repaint();
}
void MixerPanel::resized() {
    close.setBounds(getWidth() - 66, 4, 58, 23);
    const int height = jmax(0, getHeight() - 35), masterWidth = 132;
    viewport.setBounds(8, 33, jmax(0, getWidth() - masterWidth - 24), height);
    channels.setSize(jmax(viewport.getWidth() - 2, int(strips.size()) * 138), jmax(0, height - 16));
    for (int i = 0; i < int(strips.size()); ++i) strips[size_t(i)]->setBounds(i * 138, 0, 132, channels.getHeight());
    master.setBounds(getWidth() - masterWidth - 8, 33, masterWidth, channels.getHeight());
}
void MixerPanel::paint(Graphics& g) {
    g.fillAll(Colour(0xff11151b)); g.setColour(Colour(0xff34414e)); g.drawHorizontalLine(0, 0, float(getWidth()));
    g.setColour(Colour(0xff80e3c0)); g.setFont(12); g.drawText(TRANS("MIXER"), 13, 3, 74, 26, Justification::centredLeft);
    g.setColour(Colour(0xff8d9cab)); g.setFont(10); g.drawText(TRANS("F3: show / hide    |    Double-click fader: 0 dB"), 92, 3, getWidth() - 170, 26, Justification::centredLeft);
    if (strips.empty()) text(g, TRANS("Add a track to start mixing"), viewport.getBounds(), 14);
}
}
