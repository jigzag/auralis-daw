#include "Interface.h"
#include "PianoRoll.h"
#include "PitchRoll.h"
#include "Mixer.h"
#include "PluginInfo.h"
#include "NativeEffects.h"
#include "ProjectStart.h"
#include "BrandingData.h"
#include "Japanese.h"
#include <cmath>
#include <cstdlib>
#include <numeric>

namespace auralis {
namespace {
const Colour background(0xff11151b), panel(0xff1a2029), border(0xff2b3441), text(0xffe2e9ee), dim(0xff8d9cab), mint(0xff80e3c0);
class ProjectTrustDialog final : public AlertWindow {
public:
    ProjectTrustDialog(Component* owner, const String& details)
        : AlertWindow(String::fromUTF8("プロジェクトの読み込み確認"), String::fromUTF8("以下の外部プラグイン・素材を読み込みます。信頼できるプロジェクトの場合のみ許可してください。許可は今回の読み込みだけに適用します。"), MessageBoxIconType::NoIcon, owner) {
        listing.setComponentID("projectRequirements"); listing.setMultiLine(true, true); listing.setReadOnly(true); listing.setCaretVisible(false); listing.setScrollbarsShown(true); listing.setEscapeAndReturnKeysConsumed(false);
        listing.setFont(14); listing.setColour(TextEditor::backgroundColourId, background); listing.setColour(TextEditor::textColourId, Colour(0xffe2e9ee)); listing.setText(details, false); listing.setCaretPosition(0); listing.setSize(620, 180); addCustomComponent(&listing);
    }
    ~ProjectTrustDialog() override { removeCustomComponent(0); }
private:
    TextEditor listing;
};
String timeText(double t) { return String(int(t) / 60).paddedLeft('0', 2) + ":" + String(int(t) % 60).paddedLeft('0', 2) + "." + String(int(t * 100) % 100).paddedLeft('0', 2); }
void label(Graphics& g, String s, Rectangle<int> r, float size = 12, Colour colour = dim) { g.setColour(colour); g.setFont(size); g.drawText(s, r, Justification::centredLeft); }
double snapTime(const Workstation& w, double t) { double beat = 60.0 / w.engine.bpm.load(); return w.snap ? std::round(t / beat) * beat : t; }
std::vector<int> clipDrawOrder(const Track& track, bool newestFirst = false) {
    std::vector<int> indices(track.clips.size()); std::iota(indices.begin(), indices.end(), 0);
    std::sort(indices.begin(), indices.end(), [&](int a, int b) { return Engine::newerClip(track.clips[size_t(b)], b, track.clips[size_t(a)], a); });
    if (newestFirst) std::reverse(indices.begin(), indices.end()); return indices;
}
}

Timeline::Timeline(Workstation& w) : owner(w) { setMouseCursor(MouseCursor::NormalCursor); setWantsKeyboardFocus(true); }
TimelineRuler::TimelineRuler(Timeline& t, Viewport& v) : timeline(t), viewport(v) {
    setOpaque(true); setComponentID("fixedTimelineRuler");
    selectTool.setComponentID("selectTool"); cutTool.setComponentID("cutTool");
    selectTool.setButtonText(String::fromUTF8("選択 V")); cutTool.setButtonText(String::fromUTF8("カット C"));
    for (auto* button : {&selectTool, &cutTool}) { addAndMakeVisible(button); button->setWantsKeyboardFocus(false); }
    selectTool.setTooltip(TRANS("Select and move parts (V). C toggles select / cut."));
    cutTool.setTooltip(TRANS("Cut parts (C). Click again or press V to select. Shift: free cut."));
    selectTool.onClick = [this] { if (selectTool.isEnabled()) timeline.setCutMode(false); };
    cutTool.onClick = [this] { if (cutTool.isEnabled()) timeline.setCutMode(!timeline.isCutMode()); };
    cutTool.setColour(TextButton::buttonOnColourId, Colour(0xff9b592c)); syncTools(true);
}
void TimelineRuler::resized() {
    selectTool.setBounds(196, 13, 56, 26); cutTool.setBounds(254, 13, 58, 26);
}
void TimelineRuler::syncTools(bool enabled) {
    selectTool.setToggleState(!timeline.isCutMode(), dontSendNotification); cutTool.setToggleState(timeline.isCutMode(), dontSendNotification);
    selectTool.setEnabled(enabled); cutTool.setEnabled(enabled);
}
TrackControlRow::TrackControlRow(Workstation& owner, const String& trackId) : owner(owner), trackId(trackId) {
    setComponentID(trackId);
    struct Entry { TrackControlButton* button; const char* id; String tooltip; TrackControl action; Colour active; };
    for (auto entry : {Entry{&arm, "arm", TRANS("Record arm: enable this track for the next recording"), TrackControl::recordArm, Colour(0xffa63e52)},
                       Entry{&mute, "mute", TRANS("Mute this track"), TrackControl::mute, Colour(0xff9b742c)},
                       Entry{&solo, "solo", TRANS("Solo this track"), TrackControl::solo, Colour(0xff3c788e)},
                       Entry{&monitor, "monitor", TRANS("Input monitor: hear live input instead of this track's clips"), TrackControl::monitor, Colour(0xff337b65)}}) {
        auto& button = *entry.button; addAndMakeVisible(button); button.setComponentID(entry.id);
        button.setClickingTogglesState(true); button.setWantsKeyboardFocus(false); button.setTooltip(entry.tooltip);
        button.setColour(TextButton::buttonColourId, Colour(0xff29333e)); button.setColour(TextButton::buttonOnColourId, entry.active);
        button.setColour(TextButton::textColourOffId, Colour(0xffadb9c4)); button.setColour(TextButton::textColourOnId, Colours::white);
        button.onClick = [safe = Component::SafePointer<Workstation>(&owner), trackId, action = entry.action] { if (safe) safe->toggleTrackControl(trackId, action); };
        button.contextMenu = [safe = Component::SafePointer<Workstation>(&owner), trackId] { if (safe) safe->trackMenu(trackId); };
    }
    for (auto* c : std::initializer_list<Component*>{&name, &input, &gain, &pan, &midiOut, &add, &more, &inserts}) addAndMakeVisible(c);
    name.setComponentID("trackName"); input.setComponentID("trackInput"); gain.setComponentID("trackGain"); pan.setComponentID("trackPan"); inserts.setComponentID("trackInserts");
    name.setSelectAllWhenFocused(true);
    addAndMakeVisible(fold); fold.setComponentID("trackFold"); fold.setWantsKeyboardFocus(false);
    fold.onClick = [this] { this->owner.toggleTrackExpanded(this->trackId); };
    fold.contextMenu = [this] { this->owner.trackMenu(this->trackId); };
    auto commit = [this] { this->owner.setTrackDetails(this->trackId, name.getText(), jmax(0, input.getSelectedId() - 1), midiOut.getToggleState()); };
    name.onFocusLost = commit; name.onReturnKey = [this, commit] { commit(); this->owner.grabKeyboardFocus(); };
    input.onChange = commit; midiOut.onClick = commit;
    for (auto* slider : {&gain, &pan}) { slider->setSliderStyle(Slider::LinearHorizontal); slider->setTextBoxStyle(Slider::TextBoxRight, false, 49, 21); slider->setWantsKeyboardFocus(false); }
    gain.setRange(-60, 6, 0.1); gain.setTextValueSuffix(" dB"); gain.setDoubleClickReturnValue(true, 0);
    pan.setRange(-1, 1, 0.01); pan.setDoubleClickReturnValue(true, 0);
    gain.onValueChange = [this] { this->owner.setMixerValue(this->trackId, gain.getValue()); };
    pan.onValueChange = [this] { this->owner.setMixerValue(this->trackId, pan.getValue(), true); };
    add.onClick = [this] { this->owner.trackInsertMenu(this->trackId, {}, true); };
    more.onClick = [this] { int i = inserts.getSelectedRow(); for (auto& t : this->owner.engine.tracks) if (t->id == this->trackId) this->owner.trackInsertMenu(this->trackId, isPositiveAndBelow(i, int(t->inserts.size())) ? t->inserts[size_t(i)]->id : String()); };
    inserts.setRowHeight(21); inserts.setColour(ListBox::backgroundColourId, Colour(0xff11151b));
}
void TrackControlRow::resized() {
    input.setVisible(getHeight() >= 100); midiOut.setVisible(getHeight() >= 100 && input.getNumItems() == 17);
    gain.setVisible(getHeight() >= 140); pan.setVisible(getHeight() >= 140);
    add.setVisible(getHeight() >= 170); more.setVisible(getHeight() >= 170); inserts.setVisible(getHeight() >= 210);
    name.setBounds(42, 8, 228, 25); fold.setBounds(278, 8, 28, 25);
    const int controlsY = getHeight() < 90 ? 36 : 40, controlsH = getHeight() < 90 ? 16 : 23;
    arm.setBounds(14, controlsY, 67, controlsH); mute.setBounds(89, controlsY, 67, controlsH); solo.setBounds(164, controlsY, 67, controlsH); monitor.setBounds(239, controlsY, 67, controlsH);
    input.setBounds(14, 69, midiOut.isVisible() ? 188 : 292, 25); midiOut.setBounds(205, 69, 101, 25);
    gain.setBounds(10, 112, 181, 24); pan.setBounds(198, 112, 112, 24);
    add.setBounds(214, 142, 54, 23); more.setBounds(276, 142, 30, 23); inserts.setBounds(14, 171, 292, 41);
}
void TrackControlRow::paint(Graphics& g) {
    if (getHeight() >= 140) { label(g, TRANS("VOLUME"), {14, 97, 160, 14}, 9); label(g, TRANS("PAN"), {202, 97, 104, 14}, 9); }
    if (getHeight() >= 170) label(g, TRANS("INSERTS"), {14, 142, 170, 23}, 10, mint);
    g.setColour(dim.withAlpha(0.7f)); g.fillRect(135, getHeight() - 3, 50, 1);
}
int Timeline::trackHeight(int i) const { return isPositiveAndBelow(i, int(owner.engine.tracks.size())) ? owner.engine.tracks[size_t(i)]->height : row; }
int Timeline::trackTop(int i) const { int y = ruler; for (int k = 0; k < jmin(i, int(owner.engine.tracks.size())); ++k) y += trackHeight(k); return y; }
int Timeline::totalHeight() const { return trackTop(int(owner.engine.tracks.size())); }
int Timeline::trackAt(float y) const { if (y < ruler) return -1; for (int i = 0; i < int(owner.engine.tracks.size()); ++i) if (y < trackTop(i) + trackHeight(i)) return i; return -1; }
void TrackControlRow::sync(const Track& t, bool recording, bool busy) {
    fold.setButtonText(t.height <= 64 ? "+" : "-"); fold.setEnabled(!recording && !busy);
    fold.setTooltip(t.height <= 64 ? TRANS("Expand track to its previous height") : TRANS("Collapse track to compact height"));
    if (!name.hasKeyboardFocus(true)) name.setText(t.name, false);
    if (input.getNumItems() != (t.midi ? 17 : 33)) { input.clear(dontSendNotification); input.addItem(t.midi ? TRANS("All MIDI channels") : TRANS("Stereo inputs 1 + 2"), 1);
        for (int i = 1; i <= (t.midi ? 16 : 32); ++i) input.addItem((t.midi ? TRANS("MIDI channel ") : TRANS("Mono input ")) + String(i), i + 1); }
    input.setSelectedId((t.midi ? t.midiChannel : t.input) + 1, dontSendNotification); input.setEnabled(!recording && !busy);
    midiOut.setVisible(t.midi); midiOut.setToggleState(t.sendMidi, dontSendNotification); midiOut.setEnabled(!busy);
    auto* focused = Component::getCurrentlyFocusedComponent();
    if (!gain.isMouseButtonDown() && !(focused && gain.isParentOf(focused))) gain.setValue(Decibels::gainToDecibels(t.gain, -60.0f), dontSendNotification);
    if (!pan.isMouseButtonDown() && !(focused && pan.isParentOf(focused))) pan.setValue(t.pan, dontSendNotification);
    arm.setToggleState(t.armed, dontSendNotification); mute.setToggleState(t.mute, dontSendNotification); solo.setToggleState(t.solo, dontSendNotification); monitor.setToggleState(t.monitor, dontSendNotification);
    arm.setEnabled(!recording && !busy); mute.setEnabled(!busy); solo.setEnabled(!busy); monitor.setEnabled(!t.midi && !busy);
    add.setEnabled(!recording && !busy); more.setEnabled(!recording && !busy); gain.setEnabled(!busy); pan.setEnabled(!busy); name.setEnabled(!busy);
    inserts.updateContent(); inserts.repaint(); resized();
}
int TrackControlRow::getNumRows() { for (auto& t : owner.engine.tracks) if (t->id == trackId) return int(t->inserts.size()); return 0; }
void TrackControlRow::paintListBoxItem(int i, Graphics& g, int width, int height, bool selected) {
    for (auto& t : owner.engine.tracks) if (t->id == trackId && isPositiveAndBelow(i, int(t->inserts.size()))) {
        auto& p = *t->inserts[size_t(i)]; if (selected) { g.setColour(Colour(0xff30463f)); g.fillAll(); }
        label(g, "[" + describePlugin(p.description).kind + "] " + p.description.name + (p.bypass ? TRANS("  [off]") : ""), {6, 0, width - 12, height}, 11, p.bypass ? dim : text); break;
    }
}
void TrackControlRow::listBoxItemDoubleClicked(int i, const MouseEvent&) { for (auto& t : owner.engine.tracks) if (t->id == trackId && isPositiveAndBelow(i, int(t->inserts.size()))) { owner.trackInsertAction(trackId, t->inserts[size_t(i)]->id, 1); break; } }
void TrackControlRow::listBoxItemClicked(int i, const MouseEvent& e) { if (e.mods.isPopupMenu()) for (auto& t : owner.engine.tracks) if (t->id == trackId && isPositiveAndBelow(i, int(t->inserts.size()))) { owner.trackInsertMenu(trackId, t->inserts[size_t(i)]->id); break; } }
void Timeline::syncTrackControls() {
    bool rebuild = trackControls.size() != owner.engine.tracks.size();
    if (!rebuild) for (size_t i = 0; i < trackControls.size(); ++i) if (trackControls[i]->getComponentID() != owner.engine.tracks[i]->id) { rebuild = true; break; }
    if (rebuild) {
        trackControls.clear();
        for (auto& track : owner.engine.tracks) {
            auto controls = std::make_unique<TrackControlRow>(owner, track->id); addAndMakeVisible(*controls); trackControls.push_back(std::move(controls));
        }
    }
    for (size_t i = 0; i < trackControls.size(); ++i) {
        trackControls[i]->setBounds(0, trackTop(int(i)), header, trackHeight(int(i)) - 1);
        trackControls[i]->sync(*owner.engine.tracks[i], owner.engine.recording, owner.busy);
    }
}
Rectangle<float> Timeline::gainHandleBounds(const Clip& clip, int track) const {
    float left = jmax(float(header), xAt(clip.startTime(owner.engine.bpm)));
    float right = jmin(float(getWidth()), xAt(clip.endTime(owner.engine.bpm)));
    if (right - left < 100) return {};
    return {right - 74, float(trackTop(track) + 11), 66, 17};
}
Timeline::EdgeHit Timeline::trimEdgeAt(Point<float> point) const {
    if (point.x < header || point.y < ruler) return {};
    int track = trackAt(point.y);
    if (!isPositiveAndBelow(track, int(owner.engine.tracks.size()))) return {};
    float top = float(trackTop(track) + 8);
    if (point.y < top || point.y > top + trackHeight(track) - 16) return {};
    const auto& clips = owner.engine.tracks[size_t(track)]->clips;
    // A selected part keeps its own handles even when a newer part covers its edge.
    if (track == owner.selectedTrack && isPositiveAndBelow(selectedClip, int(clips.size()))) {
        const auto& selected = clips[size_t(selectedClip)];
        float left = xAt(selected.startTime(owner.engine.bpm)), right = xAt(selected.endTime(owner.engine.bpm));
        float radius = jmin(8.0f, jmax(2.0f, (right - left) * 0.2f));
        float dl = std::abs(point.x - left), dr = std::abs(point.x - right);
        if (jmin(dl, dr) <= radius) return {track, selectedClip, dl < dr ? 1 : 2};
    }
    auto order = clipDrawOrder(*owner.engine.tracks[size_t(track)], true); int topClip = -1;
    for (int i : order) if (point.x >= xAt(clips[size_t(i)].startTime(owner.engine.bpm)) && point.x < xAt(clips[size_t(i)].endTime(owner.engine.bpm))) { topClip = i; break; }
    EdgeHit hit; float best = 100;
    for (int i : order) {
        if (topClip >= 0 && Engine::newerClip(clips[size_t(topClip)], topClip, clips[size_t(i)], i)) continue;
        float left = xAt(clips[size_t(i)].startTime(owner.engine.bpm)), right = xAt(clips[size_t(i)].endTime(owner.engine.bpm));
        float radius = jmin(8.0f, jmax(2.0f, (right - left) * 0.2f));
        float distanceLeft = std::abs(point.x - left), distanceRight = std::abs(point.x - right);
        float distance = jmin(distanceLeft, distanceRight);
        float score = distance + (point.x >= left && point.x <= right ? 0.0f : 0.5f);
        if (distance <= radius && score < best) { hit = {track, i, distanceLeft < distanceRight ? 1 : 2}; best = score; }
    }
    return hit;
}
void Timeline::beginClipDrag(int track, int clip, int mode, const MouseEvent& e) {
    owner.selectTrack(track); selectedClip = clip;
    const ScopedLock guard(owner.engine.lock); auto& clips = owner.engine.tracks[size_t(track)]->clips;
    if (e.mods.isAltDown()) {
        owner.rememberEdit(); dragRemembered = true; Clip duplicate = clips[size_t(clip)]; duplicate.id = Uuid().toString();
        owner.engine.stampNewClip(duplicate);
        clips.push_back(duplicate); selectedClip = int(clips.size()) - 1; owner.changed();
    }
    original = clips[size_t(selectedClip)]; dragTrack = track; dragClip = selectedClip;
    mouseStart = timeAt(e.position.x); clipDragOrigin = e.position;
    if (mode == 0) {
        bool onGain = !original.midi && gainHandleBounds(original, track).contains(e.position);
        mode = onGain ? 4 : !original.midi && e.y >= trackTop(track) + 32 ? 0 : 3;
    }
    dragMode = mode;
    if (mode == 1 || mode == 2) {
        setMouseCursor(MouseCursor::LeftRightResizeCursor);
        owner.setStatus(TRANS("Drag edge to trim. Shift: disable snap. Ctrl+Z: undo."));
    }
    repaint();
}
void Timeline::paint(Graphics& g) {
    g.fillAll(background);
    // Session metadata is edited only on the message thread. The callback reads it
    // and writes atomic meters, so painting must not take the callback's edit lock.
    int count = g.getClipBounds().getBottom() <= ruler ? 0 : int(owner.engine.tracks.size());
    for (int i = 0; i < count; ++i) {
        auto& t = *owner.engine.tracks[size_t(i)]; int y = trackTop(i);
        g.setColour(i == owner.selectedTrack ? Colour(0xff26323b) : panel); g.fillRect(0, y, header, trackHeight(i) - 1);
        g.setColour(t.colour); g.fillRect(0, y, 3, trackHeight(i) - 1);
        label(g, String(i + 1).paddedLeft('0', 2), {13, y + 10, 24, 24}, 12, t.colour);
        
        
        g.setColour(border); g.fillRoundedRectangle(14, float(y + trackHeight(i) - 7), float(header - 28), 4, 2);
        g.setColour(t.peak > 1 ? Colour(0xffff7a85) : t.colour); g.fillRoundedRectangle(14, float(y + trackHeight(i) - 7), (header - 28) * jlimit(0.0f, 1.0f, t.peak.load()), 4, 2);
        g.setColour(border); g.drawHorizontalLine(y + trackHeight(i) - 1, 0, float(getWidth()));
    }
    g.saveState(); g.reduceClipRegion(header, 0, getWidth() - header, getHeight());
    g.setColour(Colour(hoverRulerLane == 0 ? 0xff294238 : 0xff20342e)); g.fillRect(header, 0, getWidth() - header, seekLaneHeight);
    g.setColour(Colour(hoverRulerLane == 1 ? 0xff263e57 : 0xff1b2c40)); g.fillRect(header, seekLaneHeight, getWidth() - header, ruler - seekLaneHeight);
    g.setColour(mint.withAlpha(0.35f)); g.drawHorizontalLine(seekLaneHeight - 1, float(header), float(getWidth()));
    g.setColour(Colour(0xff70bcff).withAlpha(0.5f)); g.drawHorizontalLine(ruler - 1, float(header), float(getWidth()));
    if (!owner.hasTimeSelection) label(g, TRANS("Drag here to select a range"), {header + 10, seekLaneHeight + 2, 250, ruler - seekLaneHeight - 4}, 11, Colour(0xff92b5d6));
    double beat = 60 / owner.engine.bpm.load();
    int step = zoom * beat < 12 ? 4 : 1, labelStep = 4;
    while (step * zoom * beat < 12) step *= 2;
    while (labelStep * zoom * beat < 64) labelStep *= 2;
    for (int n = int(scroll / beat) / step * step; n * beat < scroll + getWidth() / zoom; n += step) {
        int x = int(xAt(n * beat));
        g.setColour(n % 4 == 0 ? Colour(0xff303a46) : Colour(0xff202833)); g.drawVerticalLine(x, ruler, float(getHeight()));
        if (n % labelStep == 0) label(g, String(n / 4 + 1), {x + 5, 0, 60, seekLaneHeight}, 11, mint);
    }
    if (owner.engine.loop) {
        g.setColour(mint.withAlpha(0.08f));
        g.fillRect(xAt(owner.engine.loopStart), float(ruler), float((owner.engine.loopEnd - owner.engine.loopStart) * zoom), float(getHeight() - ruler));
        g.setColour(mint.withAlpha(0.7f)); g.fillRect(xAt(owner.engine.loopStart), float(ruler - 5), float((owner.engine.loopEnd - owner.engine.loopStart) * zoom), 4.0f);
    }
    if (owner.engine.recordRange) {
        float left = xAt(owner.engine.recordFrom), width = float((owner.engine.recordTo - owner.engine.recordFrom) * zoom);
        g.setColour(Colour(0xffff7a85).withAlpha(0.07f)); g.fillRect(left, float(ruler), width, float(getHeight() - ruler));
        g.setColour(Colour(0xffff7a85)); g.fillRect(left, float(seekLaneHeight + 1), width, 3.0f);
    }
    if (owner.hasTimeSelection) {
        float left = xAt(owner.timeSelection.getStart()), width = float(owner.timeSelection.getLength() * zoom);
        g.setColour(Colour(0xff70bcff).withAlpha(0.13f)); g.fillRect(left, float(ruler), width, float(getHeight() - ruler));
        g.setColour(Colour(0xff70bcff).withAlpha(0.45f)); g.fillRect(left, float(seekLaneHeight + 4), width, float(ruler - seekLaneHeight - 9));
        g.setColour(Colour(0xff70bcff));
        g.fillRect(left, float(seekLaneHeight + 4), 3.0f, float(ruler - seekLaneHeight - 9));
        g.fillRect(left + width - 3, float(seekLaneHeight + 4), 3.0f, float(ruler - seekLaneHeight - 9));
        g.drawVerticalLine(int(left), float(seekLaneHeight), float(getHeight())); g.drawVerticalLine(int(left + width), float(seekLaneHeight), float(getHeight()));
        label(g, TRANS("RANGE  ") + String(owner.timeSelection.getStart(), 2) + " - " + String(owner.timeSelection.getEnd(), 2) + " s", {int(left) + 8, seekLaneHeight + 3, jmax(0, int(width) - 16), ruler - seekLaneHeight - 7}, 10, Colours::white);
    }
    for (int i = 0; i < count; ++i) {
        auto& track = *owner.engine.tracks[size_t(i)];
        for (int k : clipDrawOrder(track)) {
            auto& c = track.clips[size_t(k)];
            Rectangle<float> r(xAt(c.startTime(owner.engine.bpm)), float(trackTop(i) + 8), float(c.lengthTime(owner.engine.bpm) * zoom), float(trackHeight(i) - 16));
            if (r.getRight() < header || r.getX() > getWidth()) continue;
            g.setColour(background); g.fillRoundedRectangle(r, 4);
            g.setColour(track.colour.withAlpha(0.18f)); g.fillRoundedRectangle(r, 4);
            g.setColour(track.colour.withAlpha(0.3f)); g.fillRect(r.withHeight(22));
            const bool showGain = !c.midi && ((i == owner.selectedTrack && k == selectedClip) || std::abs(c.gainDb) > 0.0001f);
            auto gainHandle = showGain ? gainHandleBounds(c, i) : Rectangle<float>();
            label(g, c.name + (c.pitchOriginal ? " [Pitch]" : ""), r.withHeight(22).withTrimmedRight(gainHandle.isEmpty() ? 0 : 76).reduced(8, 0).toNearestInt(), 11, text);
            auto wave = r.withTrimmedTop(28).reduced(0, 8);
            if (c.midi) {
                g.setColour(track.colour);
                for (const auto& note : c.notes) {
                    double on = jmax(0.0, note.beat - c.offset), off = jmin(c.length, note.beat + note.length - c.offset);
                    if (off <= on) continue;
                    g.fillRoundedRectangle(Rectangle<float>(r.getX() + float(on * c.scale(owner.engine.bpm) * zoom), wave.getY() + (127 - note.pitch) * wave.getHeight() / 128, jmax(2.0f, float((off - on) * c.scale(owner.engine.bpm) * zoom)), 3.0f), 1);
                }
            } else {
            auto& audio = c.source->samples;
            int left = jmax(header, int(r.getX())), right = jmin(getWidth(), int(r.getRight()));
            const float displayGain = Decibels::decibelsToGain(c.gainDb);
            g.setColour(track.colour.withAlpha(0.85f));
            for (int x = left; x < right; x += 2) {
                double local = (x - r.getX()) / zoom;
                int a = jlimit(0, audio.getNumSamples() - 1, int((c.offset + local) * c.source->rate));
                int len = jmin(audio.getNumSamples() - a, jmax(1, int(c.source->rate * 2 / zoom)));
                float peak = c.source->displayPeak(a, len);
                float h = jmin(1.0f, peak * displayGain) * wave.getHeight() / 2;
                g.drawVerticalLine(x, wave.getCentreY() - h, wave.getCentreY() + h);
            }
            }
            g.setColour(i == owner.selectedTrack && k == selectedClip ? Colours::white : track.colour.withAlpha(0.5f)); g.drawRoundedRectangle(r, 4, 1);
            if (!c.midi) { g.setColour(track.colour); g.drawLine(r.getX(), r.getBottom(), r.getX() + float(c.fadeIn * zoom), r.getY() + 25, 1);
            g.drawLine(r.getRight() - float(c.fadeOut * zoom), r.getY() + 25, r.getRight(), r.getBottom(), 1);
            }
            if (!gainHandle.isEmpty()) {
                g.setColour(background.withAlpha(0.85f)); g.fillRoundedRectangle(gainHandle, 3);
                label(g, (c.gainDb > 0 ? "+" : "") + String(c.gainDb, 1) + " dB", gainHandle.toNearestInt(), 10, mint);
            }
        }
        // Draw selection and trim handles after all layers so covered handles stay visible.
        for (int k = 0; k < int(track.clips.size()); ++k) {
            auto& c = track.clips[size_t(k)];
            Rectangle<float> r(xAt(c.startTime(owner.engine.bpm)), float(trackTop(i) + 8), float(c.lengthTime(owner.engine.bpm) * zoom), float(trackHeight(i) - 16));
            if (i == owner.selectedTrack && k == selectedClip) {
                g.setColour(Colours::white.withAlpha(0.85f)); g.drawRoundedRectangle(r, 4, 1.3f);
            }
            int edge = dragTrack == i && dragClip == k && (dragMode == 1 || dragMode == 2) ? dragMode
                : hoverTrimTrack == i && hoverTrimClip == c.id ? hoverTrimSide : 0;
            if (edge != 0 && !cutMode && !owner.engine.recording) {
                float x = edge == 1 ? r.getX() + 1 : r.getRight() - 3;
                g.setColour(mint); g.fillRoundedRectangle(x, r.getY() + 3, 2, r.getHeight() - 6, 1);
                g.fillRoundedRectangle(edge == 1 ? x + 3 : x - 3, r.getCentreY() - 10, 2, 20, 1);
            }
        }
        if (owner.engine.recording && owner.engine.countInRemaining == 0 && owner.engine.cursor >= track.recordStart && (track.recorder || track.recordingMidi)) {
            const auto frames = track.recordingWaveform.samples.load(std::memory_order_acquire);
            const double length = track.midi ? owner.engine.cursor - track.recordStart : frames / owner.engine.rate.load();
            Rectangle<float> r(xAt(track.recordStart), float(trackTop(i) + 8), float(length * zoom), float(trackHeight(i) - 16));
            const Colour recordingColour(0xffff7a85);
            g.setColour(background); g.fillRoundedRectangle(r, 4);
            g.setColour(recordingColour.withAlpha(0.25f)); g.fillRoundedRectangle(r, 4);
            g.setColour(recordingColour.withAlpha(0.4f)); g.fillRect(r.withHeight(22));
            label(g, TRANS("Recording"), r.withHeight(22).reduced(8, 0).toNearestInt(), 11, text);
            if (!track.midi && frames > 0) {
                const auto wave = r.withTrimmedTop(28).reduced(0, 8);
                const int left = jmax(header, int(std::floor(r.getX()))), right = jmin(getWidth(), int(std::ceil(r.getRight())));
                const double framesPerPixel = owner.engine.rate.load() / zoom;
                g.setColour(recordingColour);
                for (int x = left; x < right; x += 2) {
                    const int64 first = int64(jmax(0.0, (x - r.getX()) * framesPerPixel));
                    const int64 last = jmin(frames, int64(std::ceil((x + 2 - r.getX()) * framesPerPixel)));
                    const float height = jmin(1.0f, track.recordingWaveform.peak(first, last)) * wave.getHeight() / 2;
                    g.drawVerticalLine(x, wave.getCentreY() - jmax(0.5f, height), wave.getCentreY() + jmax(0.5f, height));
                }
            }
            g.setColour(recordingColour); g.drawRoundedRectangle(r, 4, 1);
        }
    }
    if (cutMode && cutHover.x >= header && cutHover.y >= ruler && !owner.engine.recording) {
        int track = trackAt(cutHover.y);
        double seconds = cutFree ? timeAt(cutHover.x) : snapTime(owner, timeAt(cutHover.x));
        if (isPositiveAndBelow(track, count)) {
            const auto& clips = owner.engine.tracks[size_t(track)]->clips;
            for (int i : clipDrawOrder(*owner.engine.tracks[size_t(track)], true)) {
                if (timeAt(cutHover.x) < clips[size_t(i)].startTime(owner.engine.bpm) || timeAt(cutHover.x) >= clips[size_t(i)].endTime(owner.engine.bpm)) continue;
                if (owner.engine.canSplitClip(track, i, seconds)) {
                    float x = xAt(seconds), y = float(trackTop(track) + 8);
                    g.setColour(Colour(0xffffb477)); g.drawLine(x, y, x, y + trackHeight(track) - 16, 2);
                }
                break;
            }
        }
    }
    float x = xAt(draggingPlayhead && !owner.engine.recording ? dragPosition : owner.engine.cursor.load()); g.setColour(mint); g.drawVerticalLine(int(x), 0, float(getHeight()));
    Path marker; marker.addTriangle(x - 5, 0, x + 5, 0, x, 8); g.fillPath(marker);
    g.restoreState();
    if (headerDrag >= 0 && !resizingTrack && headerDrop >= 0) { g.setColour(mint); g.fillRect(0, trackTop(headerDrop) + (headerDrop > headerDrag ? trackHeight(headerDrop) : 0), getWidth(), 3); }
    if (dropTrack >= 0 && dragMode == 3) { g.setColour(mint); g.drawRect(header, trackTop(dropTrack), getWidth() - header, trackHeight(dropTrack), 2); }
    g.setColour(panel); g.fillRect(0, 0, header, ruler);
    label(g, TRANS("PLAYHEAD  /  drag to move"), {14, 0, 180, seekLaneHeight}, 11, mint);
    label(g, TRANS("RANGE  /  drag to select"), {14, seekLaneHeight, 180, ruler - seekLaneHeight}, 11, Colour(0xff70bcff));
    g.setColour(border); g.drawHorizontalLine(seekLaneHeight - 1, 0, float(header));
    if (cutMode) label(g, TRANS("CUT / V: select"), {header + 10, ruler + 2, 120, 14}, 10, Colour(0xffffb477));
    if (count == 0) label(g, TRANS("Drop audio here to start"), {header + 45, ruler + 50, 380, 45}, 22, dim);
}
void Timeline::setCutMode(bool enabled) {
    if (enabled && (owner.busy || owner.engine.recording)) return;
    cutMode = enabled; cutHover = {-1, -1};
    owner.rulerBar.syncTools(!owner.busy && !owner.engine.recording);
    if (isShowing()) grabKeyboardFocus();
    setMouseCursor(cutMode ? MouseCursor::CrosshairCursor : MouseCursor::NormalCursor);
    owner.setStatus(cutMode ? TRANS("Cut tool: click to split. C / V: select. Shift: free cut. Ctrl+Z: undo.") : TRANS("Select tool. C: toggle cut. Ctrl+Z: undo.")); repaint();
}
void Timeline::mouseMove(const MouseEvent& e) {
    if (e.x < header && e.y >= ruler && !owner.engine.recording && !owner.busy) {
        int t = trackAt(e.position.y);
        bool edge = t >= 0 && e.y >= trackTop(t) + trackHeight(t) - 6;
        setMouseCursor(edge ? MouseCursor::UpDownResizeCursor : MouseCursor::DraggingHandCursor);
        setTooltip(edge ? TRANS("Drag the bottom edge to resize this track") : TRANS("Drag the track number or empty header area to reorder tracks")); return;
    }
    const int lane = e.x >= header && e.y >= 0 && e.y < ruler ? (e.y < seekLaneHeight ? 0 : 1) : -1;
    if (lane != hoverRulerLane) { hoverRulerLane = lane; repaint(); }
    if (lane >= 0) {
        hoverTrimClip.clear(); hoverTrimTrack = -1; hoverTrimSide = 0; cutHover = {-1, -1};
        setTooltip(lane == 0 ? TRANS("Drag to move the playhead. Shift: snap to beats.") : TRANS("Drag to select a time range. L: repeat. P: punch in. Export uses this range."));
        setMouseCursor(owner.busy || owner.engine.recording ? MouseCursor::NormalCursor : lane == 0 ? MouseCursor::DraggingHandCursor : MouseCursor::CrosshairCursor);
        repaint(); return;
    }
    setTooltip(TRANS("Select or drag a part. Drag its edge to trim. Shift-drag: select a time range."));
    if (cutMode) { cutHover = e.position; cutFree = e.mods.isShiftDown(); setMouseCursor(MouseCursor::CrosshairCursor); repaint(); return; }
    EdgeHit hit = owner.busy || owner.engine.recording ? EdgeHit() : trimEdgeAt(e.position);
    String id = hit.side != 0 ? owner.engine.tracks[size_t(hit.track)]->clips[size_t(hit.clip)].id : String();
    if (hoverTrimClip != id || hoverTrimTrack != hit.track || hoverTrimSide != hit.side) {
        hoverTrimClip = id; hoverTrimTrack = hit.track; hoverTrimSide = hit.side; repaint();
    }
    setMouseCursor(hit.side != 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor);
}
void Timeline::mouseExit(const MouseEvent&) {
    hoverRulerLane = -1; cutHover = {-1, -1}; hoverTrimClip.clear(); hoverTrimTrack = -1; hoverTrimSide = 0;
    if (dragTrack < 0) setMouseCursor(cutMode ? MouseCursor::CrosshairCursor : MouseCursor::NormalCursor); repaint();
}
void Timeline::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) {
    if (!e.mods.isCommandDown()) { Component::mouseWheelMove(e, wheel); return; }
    if (owner.busy || draggingPlayhead || dragTrack >= 0) return;
    double delta = std::abs(wheel.deltaY) > 0.0001f ? wheel.deltaY : wheel.deltaX;
    if (std::abs(delta) > 0.0001) owner.setTimelineZoom(zoom * std::exp(jlimit(-0.7, 0.7, delta * 2.5)), e.position.x);
}
void Timeline::mouseDown(const MouseEvent& e) {
    // Own editing shortcuts instead of leaving focus in a track-name editor.
    if (!owner.busy && isShowing()) grabKeyboardFocus();
    draggingPlayhead = false; selectingTime = false; dragTrack = -1; dragRemembered = false;
    if (owner.busy || owner.engine.recording) return;
    headerDrag = -1; headerDrop = -1; dropTrack = -1;
    if (e.mods.isPopupMenu() && e.y >= seekLaneHeight && e.y < ruler) {
        if (owner.hasTimeSelection) {
            owner.hasTimeSelection = false;
            owner.setStatus(String::fromUTF8("時間範囲の選択を解除しました。")); owner.refresh();
        }
        return;
    }
    if (e.x < header && e.y >= ruler && e.mods.isLeftButtonDown()) {
        int t = trackAt(e.position.y); if (t >= 0) {
            headerDrag = t; headerDrop = t; headerStartY = e.position.y; resizeStartHeight = trackHeight(t);
            resizingTrack = e.y >= trackTop(t) + trackHeight(t) - 6; owner.selectTrack(t); return;
        }
    }
    if (!cutMode && e.mods.isLeftButtonDown()) {
        auto edge = trimEdgeAt(e.position);
        if (edge.side != 0) { beginClipDrag(edge.track, edge.clip, edge.side, e); return; }
    }
    if (e.x >= header && e.mods.isLeftButtonDown()
        && ((e.y >= seekLaneHeight && e.y < ruler) || (e.y >= ruler && e.mods.isShiftDown() && !cutMode))) {
        selectingTime = true; rangeAnchor = e.mods.isShiftDown() ? timeAt(e.position.x) : snapTime(owner, timeAt(e.position.x));
        return;
    }
    int track = trackAt(float(e.y));
    if (e.y < ruler) {
        if (e.x >= header && e.mods.isLeftButtonDown()) {
            draggingPlayhead = true; dragPosition = timeAt(e.position.x);
            if (e.mods.isShiftDown()) dragPosition = snapTime(owner, dragPosition);
            owner.engine.seek(dragPosition); repaint(); owner.repaint();
        }
        return;
    }
    if (!isPositiveAndBelow(track, int(owner.engine.tracks.size()))) { selectedClip = -1; return; }
    owner.selectTrack(track); selectedClip = -1;
    if (e.x < header) { if (e.mods.isPopupMenu()) owner.trackMenu(owner.engine.tracks[size_t(track)]->id); return; }
    auto& clips = owner.engine.tracks[size_t(track)]->clips;
    for (int k : clipDrawOrder(*owner.engine.tracks[size_t(track)], true)) {
        auto& c = clips[size_t(k)];
        if (timeAt(float(e.x)) >= c.startTime(owner.engine.bpm) && timeAt(float(e.x)) <= c.endTime(owner.engine.bpm)) {
            selectedClip = k;
            if (e.mods.isPopupMenu()) { owner.clipMenu(track, k, e.mods.isShiftDown() ? timeAt(e.position.x) : snapTime(owner, timeAt(e.position.x))); repaint(); return; }
            if (cutMode && e.mods.isLeftButtonDown()) {
                owner.cutClipAt(track, k, e.mods.isShiftDown() ? timeAt(e.position.x) : snapTime(owner, timeAt(e.position.x))); return;
            }
            beginClipDrag(track, k, 0, e); return;
        }
    }
    if (e.mods.isPopupMenu()) { owner.trackMenu(owner.engine.tracks[size_t(track)]->id); return; }
    owner.engine.seek(snapTime(owner, timeAt(float(e.x)))); repaint();
}
void Timeline::mouseDrag(const MouseEvent& e) {
    if (headerDrag >= 0) {
        if (owner.busy || owner.engine.recording) return;
        if (resizingTrack) owner.setTrackHeight(owner.engine.tracks[size_t(headerDrag)]->id, resizeStartHeight + int(e.position.y - headerStartY));
        else { int t = trackAt(e.position.y); headerDrop = t >= 0 ? t : e.y < ruler ? 0 : int(owner.engine.tracks.size()) - 1; repaint(); }
        return;
    }
    if (selectingTime) {
        if (owner.busy || owner.engine.recording) return;
        double end = e.mods.isShiftDown() ? timeAt(e.position.x) : snapTime(owner, timeAt(e.position.x));
        owner.setTimeSelection(jmin(rangeAnchor, end), jmax(rangeAnchor, end)); return;
    }
    if (draggingPlayhead) {
        if (owner.busy || owner.engine.recording) return;
        dragPosition = timeAt(e.position.x);
        if (e.mods.isShiftDown()) dragPosition = snapTime(owner, dragPosition);
        owner.engine.seek(dragPosition); repaint(); owner.repaint(); return;
    }
    if (dragTrack < 0 || owner.engine.recording || owner.busy) return;
    const ScopedLock guard(owner.engine.lock);
    if (!isPositiveAndBelow(dragTrack, int(owner.engine.tracks.size())) || !isPositiveAndBelow(dragClip, int(owner.engine.tracks[size_t(dragTrack)]->clips.size()))) { dragTrack = -1; return; }
    auto& c = owner.engine.tracks[size_t(dragTrack)]->clips[size_t(dragClip)];
    const auto movement = e.position - clipDragOrigin;
    int target = trackAt(e.position.y);
    if ((dragMode == 0 || dragMode == 4) && target >= 0 && target != dragTrack ) { dragMode = 3; c.gainDb = original.gainDb; }
    dropTrack = dragMode == 3 && target >= 0 && owner.engine.tracks[size_t(target)]->midi == original.midi ? target : -1;
    if (dragMode == 0) {
        if (std::abs(movement.y) >= 4 && std::abs(movement.y) > std::abs(movement.x)) dragMode = 4;
        else if (std::abs(movement.x) >= 4) dragMode = 3;
        else return;
    }
    if (dragMode == 4) {
        float db = float(std::round(jlimit(-60.0, 60.0, original.gainDb - movement.y * (e.mods.isCtrlDown() ? 0.05 : 0.25)) * 10) / 10);
        if (std::abs(db - c.gainDb) < 0.0001f) return;
        if (!dragRemembered) { owner.rememberEdit(); dragRemembered = true; }
        c.gainDb = db; owner.changed(); repaint(); owner.setStatus("Clip volume " + String(db, 1) + " dB. Ctrl: fine adjustment. Ctrl+Z: undo."); return;
    }
    const double scale = original.scale(owner.engine.bpm);
    double delta = (timeAt(e.position.x) - mouseStart) / scale;
    auto snapUnits = [&](double units) { return e.mods.isShiftDown() ? units : snapTime(owner, units * scale) / scale; };
    const double minimum = jmin(original.length, original.midi ? 1.0 / 960 : 1.0 / original.source->rate);
    double start = original.start, offset = original.offset, length = original.length;
    if (dragMode == 1) {
        start = jlimit(jmax(0.0, original.start - original.offset), original.start + original.length - minimum, snapUnits(original.start + delta));
        offset = original.offset + start - original.start; length = original.length - (start - original.start);
    } else if (dragMode == 2) {
        double maximum = original.midi ? jmax(4096.0, original.length) : original.source->samples.getNumSamples() / original.source->rate - original.offset;
        length = jlimit(minimum, jmax(minimum, maximum), snapUnits(original.start + original.length + delta) - original.start);
    } else start = jmax(0.0, snapUnits(original.start + delta));
    if (std::abs(c.start - start) < 1e-10 && std::abs(c.offset - offset) < 1e-10 && std::abs(c.length - length) < 1e-10) return;
    if (!dragRemembered) { owner.rememberEdit(); dragRemembered = true; }
    c.start = start; c.offset = offset; c.length = length;
    owner.engine.midiDiscontinuity = true; owner.changed(); repaint();
    if (dragMode == 1 || dragMode == 2) owner.setStatus("Trim: " + String(c.startTime(owner.engine.bpm), 3) + " - " + String(c.endTime(owner.engine.bpm), 3) + " s. Shift: disable snap.");
}
void Timeline::mouseUp(const MouseEvent& e) {
    if (headerDrag >= 0) {
        const int from = headerDrag, to = headerDrop; const bool resize = resizingTrack;
        headerDrag = -1; headerDrop = -1; resizingTrack = false;
        if (!resize && std::abs(e.position.y - headerStartY) >= 4 && to >= 0) owner.moveTrack(owner.engine.tracks[size_t(from)]->id, to);
        owner.refresh(); return;
    }
    if (selectingTime) { selectingTime = false; repaint(); return; }
    if (draggingPlayhead) {
        if (!owner.busy && !owner.engine.recording) owner.engine.seek(dragPosition);
        draggingPlayhead = false; repaint(); owner.repaint(); return;
    }
    if (dragTrack >= 0 && dragMode == 3 && !owner.busy && !owner.engine.recording) {
        int destination = trackAt(float(e.y));
        if (isPositiveAndBelow(destination, int(owner.engine.tracks.size())) && destination != dragTrack
            && owner.engine.tracks[size_t(destination)]->midi == owner.engine.tracks[size_t(dragTrack)]->midi) {
            const ScopedLock guard(owner.engine.lock);
            if (!dragRemembered) { owner.rememberEdit(); dragRemembered = true; }
            auto& clips = owner.engine.tracks[size_t(dragTrack)]->clips;
            owner.engine.tracks[size_t(destination)]->clips.push_back(clips[size_t(dragClip)]); clips.erase(clips.begin() + dragClip);
            owner.selectTrack(destination); selectedClip = int(owner.engine.tracks[size_t(destination)]->clips.size()) - 1;
            owner.changed();
        }
    }
    dragTrack = -1; dropTrack = -1; owner.refresh(); mouseMove(e);
}
void Timeline::mouseDoubleClick(const MouseEvent& e) {
    dragTrack = -1;
    if (cutMode) return;
    if (e.y < ruler || owner.engine.recording || owner.busy) return;
    int track = trackAt(float(e.y));
    if (!isPositiveAndBelow(track, int(owner.engine.tracks.size()))) return;
    owner.selectTrack(track);
    if (e.x < header) return;
    for (int i : clipDrawOrder(*owner.engine.tracks[size_t(track)], true)) {
        auto& c = owner.engine.tracks[size_t(track)]->clips[size_t(i)];
        if (timeAt(float(e.x)) >= c.startTime(owner.engine.bpm) && timeAt(float(e.x)) <= c.endTime(owner.engine.bpm)) {
            selectedClip = i; if (c.midi) owner.showPianoRoll(track, i); else owner.showPitchRoll(owner.engine.tracks[size_t(track)]->id, c.id); return;
        }
    }
    if (!owner.engine.tracks[size_t(track)]->midi) return;
    owner.rememberEdit(); owner.engine.addMidiClip(track, snapTime(owner, timeAt(float(e.x)))); selectedClip = int(owner.engine.tracks[size_t(track)]->clips.size()) - 1;
    owner.changed(); owner.refresh(); owner.showPianoRoll(track, selectedClip);
}
void Timeline::filesDropped(const StringArray& paths, int x, int y) { owner.importFiles(paths, x < header ? -1 : trackAt(float(y)), x < header ? 0 : timeAt(float(x))); }

PluginWindow::PluginWindow(AudioPluginInstance& plugin) : DocumentWindow(plugin.getName(), panel, DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    auto* editor = plugin.createEditorIfNeeded();
    if (!editor) editor = new GenericAudioProcessorEditor(plugin);
    setContentOwned(editor, true); setResizable(editor->isResizable(), false);
    if (isNativeEffect(plugin.getPluginDescription())) if (auto* limits = editor->getConstrainer()) {
        const int frameW = getWidth() - editor->getWidth(), frameH = getHeight() - editor->getHeight();
        setResizeLimits(limits->getMinimumWidth() + frameW, limits->getMinimumHeight() + frameH, limits->getMaximumWidth() + frameW, limits->getMaximumHeight() + frameH);
    }
    centreWithSize(getWidth(), getHeight()); setVisible(true);
}
File Workstation::dataDirectory() const {
    auto dir = File::getSpecialLocation(File::currentExecutableFile).getParentDirectory().getChildFile("Data"); dir.createDirectory(); return dir;
}
void Workstation::loadPluginRegistry(const File& legacyReleaseDirectory) {
    Array<File> sources;
    if (legacyReleaseDirectory != File()) {
        sources.add(legacyReleaseDirectory.getChildFile("Data/plugins.xml"));
        for (auto directory : legacyReleaseDirectory.getParentDirectory().findChildFiles(File::findDirectories, false, "Auralis*"))
            sources.addIfNotAlreadyThere(directory.getChildFile("Data/plugins.xml"));
        std::sort(sources.begin(), sources.end(), [](const File& a, const File& b) { return a.getLastModificationTime() < b.getLastModificationTime(); });
    }
    bool imported = false;
    auto merge = [this](const File& file) {
        if (auto xml = parseXML(file); xml && xml->hasTagName("KNOWNPLUGINS")) {
            KnownPluginList list; list.recreateFromXml(*xml);
            for (const auto& type : list.getTypes()) knownPlugins.addType(type);
            return !list.getTypes().isEmpty();
        }
        return false;
    };
    for (const auto& source : sources) imported |= merge(source);
    if (pluginRegistryFile != File()) merge(pluginRegistryFile);
    if (imported && !savePluginRegistry()) status = "Restored plugin list, but could not save shared settings. Check folder permissions.";
}
bool Workstation::savePluginRegistry() {
    if (pluginRegistryFile == File() || pluginRegistryFile.getParentDirectory().createDirectory().failed()) return false;
    // Merge the current file so another instance's scan is not silently discarded.
    KnownPluginList merged;
    if (auto xml = parseXML(pluginRegistryFile); xml && xml->hasTagName("KNOWNPLUGINS")) merged.recreateFromXml(*xml);
    for (const auto& type : knownPlugins.getTypes()) merged.addType(type);
    auto xml = merged.createXml(); if (!xml) return false;
    TemporaryFile temporary(pluginRegistryFile);
    if (!xml->writeTo(temporary.getFile()) || !temporary.overwriteTargetFileWithTemporary()) return false;
    knownPlugins.recreateFromXml(*xml); return true;
}
Workstation::Workstation(bool connectDevices, const File& pluginRegistry) : timeline(*this), pluginList(TRANS("Inserts"), nullptr), pluginModel(*this) {
    int logoBytes = 0;
    const auto* logoData = AuralisBranding::getNamedResource(AuralisBranding::namedResourceList[0], logoBytes);
    brandLogo = ImageFileFormat::loadFrom(logoData, size_t(logoBytes));
    theme.setColour(ResizableWindow::backgroundColourId, background);
    theme.setColour(TextButton::buttonColourId, Colour(0xff29333e)); theme.setColour(TextButton::textColourOffId, text);
    theme.setColour(TextButton::buttonOnColourId, Colour(0xff3b6359));
    theme.setColour(ToggleButton::textColourId, text); theme.setColour(ToggleButton::tickColourId, mint);
    theme.setColour(Slider::thumbColourId, mint); theme.setColour(Slider::trackColourId, mint);
    theme.setColour(TextEditor::backgroundColourId, background); theme.setColour(TextEditor::textColourId, text);
    theme.setColour(TextEditor::outlineColourId, border); theme.setColour(ComboBox::backgroundColourId, background);
    theme.setColour(ListBox::backgroundColourId, background); theme.setColour(PopupMenu::backgroundColourId, panel);
    theme.setColour(PopupMenu::textColourId, text); setLookAndFeel(&theme);
    setWantsKeyboardFocus(true);
    buildButton(fileButton, [this] { projectMenu(); });
    buildButton(importButton, [this] {
        if (engine.recording) return;
        chooser = std::make_unique<FileChooser>(TRANS("Import audio or MIDI"), File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.mid;*.midi");
        chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems,
            [safe = Component::SafePointer<Workstation>(this)](const FileChooser& c) { if (!safe) return; StringArray paths; for (auto& f : c.getResults()) paths.add(f.getFullPathName()); safe->importFiles(paths); });
    });
    buildButton(addButton, [this] {
        if (engine.recording) return;
        PopupMenu menu; menu.addItem(1, TRANS("Audio track")); menu.addItem(2, TRANS("MIDI / Instrument track"));
        menu.addSeparator(); menu.addItem(3, TRANS("MIDI clip at playhead"));
        bool canOpenPiano = isPositiveAndBelow(selectedTrack, int(engine.tracks.size())) && engine.tracks[size_t(selectedTrack)]->midi && !engine.tracks[size_t(selectedTrack)]->clips.empty();
        menu.addItem(4, TRANS("Open piano roll"), canOpenPiano);
        menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&addButton), [safe = Component::SafePointer<Workstation>(this)](int result) {
            if (!safe || result == 0 || safe->engine.recording) return;
            if (result == 3) { safe->newMidiButton.onClick(); return; }
            if (result == 4) { safe->pianoButton.onClick(); return; }
            safe->engine.addTrack({}, result == 2); safe->selectTrack(int(safe->engine.tracks.size()) - 1); safe->undo.clear(); safe->timeline.selectedClip = -1; safe->changed(); safe->refresh();
        });
    });
    buildButton(newMidiButton, [this] { if (engine.recording) return; if (!isPositiveAndBelow(selectedTrack, int(engine.tracks.size())) || !engine.tracks[size_t(selectedTrack)]->midi) { engine.addTrack({}, true); selectTrack(int(engine.tracks.size()) - 1); undo.clear(); }
        rememberEdit(); engine.addMidiClip(selectedTrack, snapTime(*this, engine.cursor)); timeline.selectedClip = int(engine.tracks[size_t(selectedTrack)]->clips.size()) - 1; changed(); refresh(); showPianoRoll(selectedTrack, timeline.selectedClip); });
    buildButton(pianoButton, [this] { if (isPositiveAndBelow(selectedTrack, int(engine.tracks.size())) && engine.tracks[size_t(selectedTrack)]->midi) { int i = timeline.selectedClip < 0 ? 0 : timeline.selectedClip; if (isPositiveAndBelow(i, int(engine.tracks[size_t(selectedTrack)]->clips.size()))) showPianoRoll(selectedTrack, i); } });
    buildButton(settingsButton, [this] { audioSettings(); });
    buildButton(panicButton, [this] { try { bool wasRecording = engine.recording; engine.stop(); if (wasRecording) changed(); refresh(); setStatus(TRANS("All notes stopped.")); } catch (const std::exception& e) { error(String::fromUTF8(e.what())); } });
    panicButton.setTooltip(TRANS("Stop playback / recording and silence all MIDI notes (Esc)"));
    buildButton(metronomeButton, [this] { engine.metronome = !engine.metronome; changed(); refresh(); });
    metronomeButton.setComponentID("metronome");
    metronomeButton.setTooltip(TRANS("BPM click during playback and recording; accent every 4 beats. Excluded from recordings and WAV export."));
    buildButton(recordRangeButton, [this] { if (engine.recording) return; engine.recordRange = !engine.recordRange; changed(); refresh(); });
    recordRangeButton.setButtonText(TRANS("Punch in")); recordRangeButton.setComponentID("recordRange");
    recordRangeButton.setTooltip(TRANS("Record a new take only between In and Out (seconds). Existing clips are preserved."));
    addAndMakeVisible(countIn); countIn.setComponentID("countIn");
    countIn.addItemList({TRANS("Off"), TRANS("1 bar"), TRANS("2 bars")}, 1); countIn.setSelectedId(1, dontSendNotification);
    countIn.onChange = [this] { if (!engine.recording) { engine.countInBars = countIn.getSelectedId() - 1; changed(); } };
    countIn.setTooltip(TRANS("Count 4 or 8 beats before recording; the count is not recorded."));
    addAndMakeVisible(recordFrom); addAndMakeVisible(recordTo);
    recordFrom.setComponentID("recordFrom"); recordTo.setComponentID("recordTo");
    recordFrom.setText("0"); recordTo.setText("8");
    auto applyRange = [this] {
        if (engine.recording) return;
        double from = engine.recordFrom, to = engine.recordTo;
        if (recordFrom.getText().getDoubleValue() != String(from, 3).getDoubleValue()) from = jmax(0.0, recordFrom.getText().getDoubleValue());
        if (recordTo.getText().getDoubleValue() != String(to, 3).getDoubleValue()) to = recordTo.getText().getDoubleValue();
        to = jmax(from + 0.01, to);
        if (from != engine.recordFrom || to != engine.recordTo) { engine.recordFrom = from; engine.recordTo = to; changed(); }
        refresh();
        if (hasTimeSelection) timeSelection = {engine.recordFrom.load(), engine.recordTo.load()};
    };
    for (auto* field : {&recordFrom, &recordTo}) {
        field->setSelectAllWhenFocused(true); field->onFocusLost = applyRange;
        field->onReturnKey = [this] { grabKeyboardFocus(); };
    }
    buildButton(playButton, [this] { if (engine.playing) engine.stop(); else engine.play(); refresh(); });
    buildButton(stopButton, [this] { try { bool wasRecording = engine.recording; engine.stop(); if (wasRecording) changed(); refresh(); } catch (const std::exception& e) { error(String::fromUTF8(e.what())); } });
    buildButton(recordButton, [this] { try { if (engine.recording) engine.finishRecording(); else engine.beginRecording(); changed(); refresh(); } catch (const std::exception& e) { error(String::fromUTF8(e.what())); } });
    buildButton(homeButton, [this] { if (!engine.recording) engine.seek(0); });
    buildButton(exportButton, [this] { exportAudio(); });
    buildButton(loopButton, [this] { toggleLoopSelection(); });
    loopButton.setTooltip(TRANS("Repeat the selected time range (L). Options sets manual loop points."));
    buildButton(snapButton, [this] { snap = !snap; snapButton.setToggleState(snap, dontSendNotification); }); snapButton.setToggleState(true, dontSendNotification);
    buildButton(undoButton, [this] { undoEdit(); });
    buildButton(scanButton, [this] { scanPlugins(); });
    buildButton(loadButton, [this] { insertPlugin(); });
    buildButton(editorButton, [this] { showEditor(); });
    buildButton(bypassButton, [this] {
        const ScopedLock guard(engine.lock); int i = pluginList.getSelectedRow();
        if (isPositiveAndBelow(selectedTrack, int(engine.tracks.size())) && isPositiveAndBelow(i, int(engine.tracks[size_t(selectedTrack)]->inserts.size()))) {
            auto& p = engine.tracks[size_t(selectedTrack)]->inserts[size_t(i)]; p->bypass = !p->bypass; engine.midiDiscontinuity = true; changed(); pluginList.repaint();
        }
    });
    buildButton(removeButton, [this] {
        if (engine.recording) return;
        int i = pluginList.getSelectedRow();
        if (isPositiveAndBelow(selectedTrack, int(engine.tracks.size())) && isPositiveAndBelow(i, int(engine.tracks[size_t(selectedTrack)]->inserts.size()))) {
            pluginWindows.clear(); engine.removePlugin(selectedTrack, i); changed(); refresh();
        }
    });
    buildButton(transportOptions, [this] { transportExpanded = !transportExpanded; resized(); });
    buildButton(mixerButton, [this] { toggleMixer(); });
    mixerButton.setComponentID("mixerToggle"); mixerButton.setTooltip(TRANS("Show or hide the track mixer (F3)"));
    transportOptions.setComponentID("transportOptions");
    transportOptions.setTooltip(TRANS("Show count-in, recording range and loop points"));
    buildButton(pluginActions, [this] { pluginMenu(); });
    pluginActions.setTooltip(TRANS("Open, bypass or remove selected insert; scan VST3 plugins"));
    loadButton.setButtonText(TRANS("+ Add"));
    for (auto* button : {&importButton, &exportButton, &undoButton, &newMidiButton, &pianoButton, &panicButton,
                         &scanButton, &editorButton, &bypassButton, &removeButton}) button->setVisible(false);
    for (auto* t : {&monitor, &hardwareMidi}) { addAndMakeVisible(t); t->onClick = [this] { applyControls(); }; }
    for (auto* e : {&trackName, &tempo, &loopFrom, &loopTo}) { addAndMakeVisible(e); e->setSelectAllWhenFocused(true); }
    trackName.onReturnKey = [this] { applyControls(); grabKeyboardFocus(); }; trackName.onFocusLost = [this] { applyControls(); };
    tempo.setText("120"); loopFrom.setText("0"); loopTo.setText("8");
    tempo.onFocusLost = [this] { if (engine.recording) return; double value = jlimit(20.0, 300.0, tempo.getText().getDoubleValue());
        if (value != String(engine.bpm.load(), 1).getDoubleValue() && value != engine.bpm) { engine.setTempo(value); changed(); }
        tempo.setText(String(engine.bpm.load(), 1)); };
    tempo.onReturnKey = [this] { grabKeyboardFocus(); };
    auto setLoop = [this] { if (!engine.recording) { const ScopedLock guard(engine.lock);
        double from = engine.loopStart, to = engine.loopEnd;
        if (loopFrom.getText().getDoubleValue() != String(from, 2).getDoubleValue()) from = jmax(0.0, loopFrom.getText().getDoubleValue());
        if (loopTo.getText().getDoubleValue() != String(to, 2).getDoubleValue()) to = loopTo.getText().getDoubleValue();
        to = jmax(from + 0.1, to);
        if (from != engine.loopStart || to != engine.loopEnd) { engine.loopStart = from; engine.loopEnd = to; changed(); }
    } };
    loopFrom.onFocusLost = setLoop; loopTo.onFocusLost = setLoop;
    for (auto* s : {&volume, &pan, &masterVolume, &zoom}) { addAndMakeVisible(s); s->setSliderStyle(Slider::LinearHorizontal); s->setTextBoxStyle(Slider::TextBoxRight, false, 52, 22); }
    volume.setRange(-60, 6, 0.1); volume.setTextValueSuffix(" dB"); volume.onValueChange = [this] { applyControls(); };
    volume.setComponentID("trackGain");
    pan.setRange(-1, 1, 0.01); pan.onValueChange = [this] { applyControls(); };
    masterVolume.setRange(-60, 6, 0.1); masterVolume.setValue(Decibels::gainToDecibels(engine.master.load()), dontSendNotification); masterVolume.setTextValueSuffix(" dB");
    masterVolume.onValueChange = [this] { setMixerValue({}, masterVolume.getValue()); };
    zoom.setRange(0.25, 300, 0.01); zoom.setSkewFactorFromMidPoint(12); zoom.setValue(timeline.zoom);
    zoom.onValueChange = [this] { setTimelineZoom(zoom.getValue()); };
    zoom.setDoubleClickReturnValue(true, 70); zoom.setTooltip(TRANS("Zoom: Ctrl+wheel on timeline. F fits the whole project."));
    zoom.setComponentID("timelineZoom");
    input.addItem(TRANS("Stereo inputs 1 + 2"), 1); for (int i = 1; i <= 32; ++i) input.addItem(TRANS("Mono input ") + String(i), i + 1);
    input.onChange = [this] { applyControls(); }; addAndMakeVisible(input); pluginList.setModel(&pluginModel); addAndMakeVisible(pluginList);
    viewport.setViewedComponent(&timeline, false); viewport.setScrollBarsShown(true, false); addAndMakeVisible(viewport);
    addAndMakeVisible(horizontal);
    // Scrollbar listener via a tiny timer-polled range; mouse wheel also works on the bar.
    keys = std::make_unique<MidiKeyboardComponent>(engine.keyboard, MidiKeyboardComponent::horizontalKeyboard);
    keys->setAvailableRange(36, 96); keys->setKeyWidth(19); addAndMakeVisible(*keys);
    pluginRegistryFile = pluginRegistry != File() ? pluginRegistry : connectDevices
        ? File::getSpecialLocation(File::userApplicationDataDirectory).getChildFile("Auralis/plugins.xml") : File();
    loadPluginRegistry(connectDevices ? File::getSpecialLocation(File::currentExecutableFile).getParentDirectory() : File());
    recentProjectsFile = pluginRegistryFile != File() ? pluginRegistryFile.getSiblingFile("recent-projects.xml") : File(); loadRecentProjects();
    if (connectDevices) { engine.deviceStateFile = dataDirectory().getChildFile("audio-device.xml"); engine.initialise(); }
    engine.addTrack("Audio 1");
    if (engine.initialisationError.isNotEmpty()) status = engine.initialisationError;
    mixer = std::make_unique<MixerPanel>(*this); addChildComponent(*mixer);
    addAndMakeVisible(rulerBar);
    projectStart = std::make_unique<ProjectStartPanel>(*this); addChildComponent(*projectStart);
    setSize(1400, 880); refresh(); startTimerHz(25);
}
Workstation::~Workstation() {
    if (projectTrustPrompt) { projectTrustPrompt->setLookAndFeel(nullptr); projectTrustPrompt->exitModalState(0); }
    if (pitchPrompt) { pitchPrompt->setLookAndFeel(nullptr); pitchPrompt->exitModalState(0); }
    if (normalizePrompt) { normalizePrompt->setLookAndFeel(nullptr); normalizePrompt->exitModalState(0); }
    if (clipGainPrompt) { clipGainPrompt->setLookAndFeel(nullptr); clipGainPrompt->exitModalState(0); }
    if (trackRemovalPrompt) { trackRemovalPrompt->setLookAndFeel(nullptr); trackRemovalPrompt->exitModalState(0); }
    if (savePrompt) { savePrompt->setLookAndFeel(nullptr); savePrompt->exitModalState(0); }
    stopTimer(); if (worker.joinable()) worker.join(); pitchWindows.clear(); pianoWindows.clear(); pluginWindows.clear(); projectStart.reset(); mixer.reset(); pluginList.setModel(nullptr);
    try { engine.stop(); } catch (...) {}
    setLookAndFeel(nullptr);
}
void Workstation::buildButton(TextButton& b, std::function<void()> fn) { addAndMakeVisible(b); b.onClick = std::move(fn); buttons.push_back(&b); }
void Workstation::paint(Graphics& g) {
    g.fillAll(background);
    if (brandLogo.isValid()) {
        g.setImageResamplingQuality(Graphics::highResamplingQuality);
        g.drawImage(brandLogo, 20, 12, 166, 27, 250, 270, 1500, 230);
    } else label(g, "AURALIS", {22, 9, 156, 32}, 23, text);
    label(g, "AUDIO + MIDI  /  " + String(ProjectInfo::versionString) + "  /  ASIO", {24, 40, 166, 18}, 10, dim);
    const int contentTop = transportExpanded ? 190 : 130;
    g.setColour(panel); g.fillRect(0, 62, getWidth(), 62); 
    String title = projectFile == File() ? TRANS("Untitled session") : projectFile.getFileNameWithoutExtension();
    label(g, title + (dirty ? " *" : ""), {548, 14, getWidth() - 713, 25}, 14, text);
    label(g, timeText(engine.cursor), {320, 72, 154, 35}, 26, mint);
    label(g, "BPM", {488, 76, 34, 28}, 10);
    String transportInfo;
    if (engine.countInRemaining > 0) transportInfo = TRANS("Count ") + String(int(std::ceil(engine.countInRemaining * engine.bpm / (60.0 * engine.rate))));
    else if (engine.recording && engine.recordRange && engine.cursor < engine.recordFrom) transportInfo = TRANS("Pre-roll");
    else if (engine.countInBars > 0) transportInfo = TRANS("Count-in ") + String(engine.countInBars.load()) + TRANS(" bar") + (engine.countInBars > 1 ? "s" : "");
    if (hasTimeSelection) transportInfo += (transportInfo.isEmpty() ? "" : "   /   ") + String(TRANS("Selection ")) + String(timeSelection.getStart(), 2) + " - " + String(timeSelection.getEnd(), 2) + " s";
    if (engine.recordRange) transportInfo += (transportInfo.isEmpty() ? "" : "   /   ") + String(TRANS("Record ")) + String(engine.recordFrom.load(), 2) + " - " + String(engine.recordTo.load(), 2) + " s";
    if (engine.loop) transportInfo += (transportInfo.isEmpty() ? "" : "   /   ") + String(TRANS("Loop ")) + String(engine.loopStart.load(), 2) + " - " + String(engine.loopEnd.load(), 2) + " s";
    label(g, transportInfo, {320, 106, 600, 16}, 10, engine.countInRemaining > 0 ? mint : dim);
    if (transportExpanded) {
        g.setColour(panel); g.fillRoundedRectangle(16, 130, float(getWidth() - 32), 54, 5);
        label(g, TRANS("COUNT IN"), {24, 130, 116, 20}, 10, dim);
        label(g, TRANS("RECORDING"), {162, 130, 110, 20}, 10, dim);
        label(g, TRANS("IN / seconds"), {284, 130, 82, 20}, 10, dim); label(g, TRANS("OUT / seconds"), {376, 130, 90, 20}, 10, dim);
        label(g, TRANS("LOOP IN / seconds"), {512, 130, 106, 20}, 10, dim); label(g, TRANS("LOOP OUT / seconds"), {628, 130, 118, 20}, 10, dim);
        label(g, TRANS("Record range adds a new take between In and Out."), {780, 149, getWidth() - 810, 28}, 11, dim);
    }
    label(g, TRANS("LIVE MIDI"), {14, getHeight() - 69, 76, 35}, 10);
    label(g, TRANS("ZOOM"), {getWidth() - 300, getHeight() - 69, 50, 35}, 10);
    label(g, TRANS("MASTER"), {getWidth() - 285, 76, 65, 28}, 10);
    g.setColour(border); g.fillRect(getWidth() - 285, 112, 268, 3);
    g.setColour(engine.outputPeak > 1 ? Colour(0xffff7a85) : mint); g.fillRect(float(getWidth() - 285), 112.0f, 268 * jlimit(0.0f, 1.0f, engine.outputPeak.load()), 3.0f);
    g.setColour(panel); g.fillRect(0, getHeight() - 27, getWidth(), 27);
    auto device = engine.devices.getCurrentAudioDevice();
    String info = String(engine.rate.load() / 1000, 1) + " kHz  /  " + String(engine.block) + TRANS(" samples");
    if (device) info += " / " + device->getTypeName();
    info += " / DSP " + String(engine.devices.getCpuUsage() * 100, 1) + "% / XRuns " + String(engine.audioXRuns()) + " / Skips " + String(engine.lockMisses.load());
    label(g, japaneseMessage(status), {15, getHeight() - 27, getWidth() - 640, 27}, 11, (engine.dropped > 0 || engine.lockMisses > 0) ? Colour(0xffff7a85) : dim);
    label(g, info, {getWidth() - 610, getHeight() - 27, 600, 27}, 10);
    if (busy) { g.setColour(Colours::black.withAlpha(0.55f)); g.fillAll(); label(g, japaneseMessage(status), {getWidth() / 2 - 280, getHeight() / 2 - 30, 560, 60}, 20, mint); }
}
void Workstation::resized() {
    fileButton.setBounds(214, 16, 100, 31); addButton.setBounds(326, 16, 96, 31);
    mixerButton.setBounds(434, 16, 94, 31);
    settingsButton.setBounds(getWidth() - 132, 16, 114, 31);
    homeButton.setBounds(20, 76, 34, 32); playButton.setBounds(62, 76, 68, 32);
    stopButton.setBounds(138, 76, 68, 32); recordButton.setBounds(214, 76, 84, 32);
    tempo.setBounds(526, 76, 58, 30); metronomeButton.setBounds(600, 76, 92, 30);
    loopButton.setBounds(700, 76, 54, 30); snapButton.setBounds(762, 76, 54, 30);
    transportOptions.setBounds(824, 76, 90, 30);
    transportOptions.setToggleState(transportExpanded, dontSendNotification);
    for (auto* field : std::initializer_list<Component*>{&countIn, &recordRangeButton, &recordFrom, &recordTo, &loopFrom, &loopTo}) field->setVisible(transportExpanded);
    countIn.setBounds(24, 150, 116, 26); recordRangeButton.setBounds(162, 150, 110, 26);
    recordFrom.setBounds(284, 150, 82, 26); recordTo.setBounds(376, 150, 90, 26);
    loopFrom.setBounds(512, 150, 106, 26); loopTo.setBounds(628, 150, 118, 26);
    masterVolume.setBounds(getWidth() - 221, 76, 207, 29);
    const int contentTop = transportExpanded ? 190 : 130;
    const int mixerHeight = mixerVisible ? 310 : 0;
    viewport.setBounds(0, contentTop, getWidth(), getHeight() - contentTop - 94 - mixerHeight);
    rulerBar.setBounds(0, contentTop, viewport.getWidth() - 15, Timeline::ruler);
    horizontal.setBounds(Timeline::header, getHeight() - 90 - mixerHeight, getWidth() - Timeline::header, 15);
    if (mixer) mixer->setBounds(0, getHeight() - 72 - 310, getWidth(), 310);
    for (auto* control : std::initializer_list<Component*>{&trackName, &input, &monitor, &hardwareMidi, &volume, &pan, &loadButton, &pluginActions, &pluginList}) control->setVisible(false);
    keys->setBounds(94, getHeight() - 69, 400, 37); zoom.setBounds(getWidth() - 246, getHeight() - 66, 230, 28);
    if (projectStart) projectStart->setBounds(getLocalBounds());
    refresh();
}
void Workstation::setTimeSelection(double start, double end) {
    if (busy || engine.recording || !std::isfinite(start) || !std::isfinite(end)) return;
    start = jmax(0.0, start); end = jmax(start, end);
    hasTimeSelection = end - start > 1e-6; timeSelection = {start, end};
    if (hasTimeSelection) {
        const ScopedLock guard(engine.lock); engine.recordFrom = start; engine.recordTo = end;
        if (engine.loop) {
            engine.loopStart = start; engine.loopEnd = jmax(start + 1.0 / engine.rate, end);
            if (engine.cursor < engine.loopStart || engine.cursor >= engine.loopEnd) engine.seek(engine.loopStart);
        }
        if (engine.recordRange || engine.loop) changed();
    }
    refresh();
}
void Workstation::togglePunchSelection() {
    if (busy || engine.recording) return;
    if (!engine.recordRange && hasTimeSelection) { engine.recordFrom = timeSelection.getStart(); engine.recordTo = timeSelection.getEnd(); }
    engine.recordRange = !engine.recordRange; changed(); refresh();
}
void Workstation::toggleLoopSelection() {
    if (busy || engine.recording) return;
    const ScopedLock guard(engine.lock);
    if (engine.loop) engine.loop = false;
    else {
        if (hasTimeSelection) { engine.loopStart = timeSelection.getStart(); engine.loopEnd = jmax(engine.loopStart + 1.0 / engine.rate, timeSelection.getEnd()); }
        engine.loop = true;
        if (engine.cursor < engine.loopStart || engine.cursor >= engine.loopEnd) engine.seek(engine.loopStart);
    }
    changed(); refresh();
}
void Workstation::setTimelineZoom(double pixelsPerSecond, float anchorX) {
    const float anchor = jmax(float(Timeline::header), anchorX);
    const double time = timeline.timeAt(anchor);
    timeline.zoom = jlimit(0.25, 300.0, pixelsPerSecond);
    timeline.scroll = jmax(0.0, time - (anchor - Timeline::header) / timeline.zoom);
    zoom.setValue(timeline.zoom, dontSendNotification); refresh();
}
void Workstation::fitProject() {
    double extent = jmax(1.0, engine.duration());
    if (engine.recordRange) extent = jmax(extent, engine.recordTo.load());
    if (engine.loop) extent = jmax(extent, engine.loopEnd.load());
    timeline.scroll = 0;
    setTimelineZoom(jmax(1, timeline.getWidth() - Timeline::header - 24) / (extent * 1.03));
}
void Workstation::refresh() {
    rulerBar.syncTools(!busy && !engine.recording);
    rulerBar.repaint();
    if (!masterVolume.isMouseButtonDown() && !masterVolume.hasKeyboardFocus(true))
        masterVolume.setValue(Decibels::gainToDecibels(engine.master.load(), -60.0f), dontSendNotification);
    int width = viewport.getWidth() - 15;
    timeline.setSize(width, jmax(viewport.getHeight() - 2, timeline.totalHeight() + 110));
    timeline.syncTrackControls();
    double visible = jmax(1.0, (width - Timeline::header) / timeline.zoom);
    horizontal.setRangeLimits(0, jmax(120.0, engine.duration() + 30.0, timeline.scroll + visible));
    horizontal.setCurrentRange(timeline.scroll, visible, dontSendNotification);
    playButton.setToggleState(engine.playing, dontSendNotification); recordButton.setToggleState(engine.recording, dontSendNotification);
    metronomeButton.setToggleState(engine.metronome, dontSendNotification);
    recordRangeButton.setToggleState(engine.recordRange, dontSendNotification);
    countIn.setSelectedId(engine.countInBars + 1, dontSendNotification);
    if (!recordFrom.hasKeyboardFocus(true)) recordFrom.setText(String(engine.recordFrom.load(), 3), false);
    if (!recordTo.hasKeyboardFocus(true)) recordTo.setText(String(engine.recordTo.load(), 3), false);
    recordButton.setColour(TextButton::buttonColourId, Colour(0xff61353e)); loopButton.setToggleState(engine.loop, dontSendNotification);
    if (!tempo.hasKeyboardFocus(true)) tempo.setText(String(engine.bpm.load(), 1), false);
    if (!loopFrom.hasKeyboardFocus(true)) loopFrom.setText(String(engine.loopStart.load(), 2), false);
    if (!loopTo.hasKeyboardFocus(true)) loopTo.setText(String(engine.loopEnd.load(), 2), false);
    updateTrack(); if (mixer && mixerVisible) mixer->sync(); timeline.repaint(); repaint();
}
void Workstation::timerCallback() {
    rulerBar.syncTools(!busy && !engine.recording);
    rulerBar.repaint();
    if (busy) { repaint(); return; }
    if (engine.recordingComplete) { try { engine.finishRecording(); changed(); refresh(); status = TRANS("Recording range complete. New take added; existing clips preserved."); } catch (const std::exception& e) { error(String::fromUTF8(e.what())); } }
    if (engine.deviceInterrupted && engine.recording) { try { engine.finishRecording(); engine.deviceInterrupted = false; changed(); refresh(); status = TRANS("Audio device stopped; the take was finalized."); } catch (const std::exception& e) { error(String::fromUTF8(e.what())); } }
    bool canEdit = !engine.recording;
    countIn.setEnabled(canEdit); recordRangeButton.setEnabled(canEdit); recordFrom.setEnabled(canEdit); recordTo.setEnabled(canEdit);
    tempo.setEnabled(canEdit); input.setEnabled(canEdit && !engine.tracks.empty()); newMidiButton.setEnabled(canEdit); pianoButton.setEnabled(canEdit);
    timeline.syncTrackControls();
    followTransport();
    if (mixer && mixerVisible) mixer->sync();
    playButton.setToggleState(engine.playing, dontSendNotification); recordButton.setToggleState(engine.recording, dontSendNotification);
    if (engine.lockMisses > 0) status = "Audio skipped " + String(engine.lockMisses.load()) + " blocks. See PROJECT > Audio diagnostics.";
    else if (engine.dropped > 0) status = "Recording dropped " + String(engine.dropped.load()) + " samples/events. See Audio diagnostics.";
    else if (engine.outputPeak > 1) status = TRANS("Master clipping. Reduce track or master volume before export.");
    timeline.repaint(); repaint();
}
void Workstation::changed() { dirty = true; loopButton.setToggleState(engine.loop, dontSendNotification); }
void Workstation::followTransport() {
    const double now = Time::getMillisecondCounterHiRes();
    const double requested = horizontal.getCurrentRangeStart();
    if (std::abs(requested - timeline.scroll) > 1e-7 || horizontal.isMouseButtonDown()) followResumeAt = now + 2000;
    timeline.scroll = requested;
    if (!followPlayhead || busy || engine.exporting || (!engine.playing && !engine.recording) || engine.countInRemaining > 0
        || timeline.isInteracting() || now < followResumeAt || (projectStart && projectStart->isVisible())) return;
    const double visible = (timeline.getWidth() - Timeline::header) / timeline.zoom, position = engine.cursor.load();
    if (!std::isfinite(visible) || visible <= 0 || !std::isfinite(position)) return;
    if (position >= timeline.scroll && position < timeline.scroll + visible * 0.9) return;
    const double start = jmax(0.0, position - visible * 0.2);
    horizontal.setRangeLimits(0, jmax(horizontal.getMaximumRangeLimit(), start + visible, engine.duration() + 30));
    horizontal.setCurrentRange(start, visible, dontSendNotification);
    timeline.scroll = horizontal.getCurrentRangeStart(); timeline.repaint();
}
void Workstation::selectTrack(int index) { selectedTrack = jlimit(0, jmax(0, int(engine.tracks.size()) - 1), index); engine.selectMidiTrack(selectedTrack); updateTrack(); if (mixer && mixerVisible) mixer->sync(); timeline.repaint(); }
void Workstation::toggleMixer() {
    if (busy || !mixer) return;
    mixerVisible = !mixerVisible; mixer->setVisible(mixerVisible); mixerButton.setToggleState(mixerVisible, dontSendNotification);
    resized(); refresh();
}
bool Workstation::setMixerValue(const String& trackId, double value, bool isPan) {
    if (busy || !std::isfinite(value) || value < (isPan ? -1 : -60) || value > (isPan ? 1 : 6) || (trackId.isEmpty() && isPan)) return false;
    bool modified = false;
    {
        const ScopedLock guard(engine.lock);
        if (trackId.isEmpty()) {
            const float next = Decibels::decibelsToGain(float(value), -60.0f);
            modified = std::abs(engine.master.load() - next) > 1e-7f; if (modified) engine.master = next;
        } else {
            Track* track = nullptr; for (const auto& candidate : engine.tracks) if (candidate->id == trackId) { track = candidate.get(); break; }
            if (!track) return false;
            float& target = isPan ? track->pan : track->gain;
            const float next = isPan ? float(value) : Decibels::decibelsToGain(float(value), -60.0f);
            modified = std::abs(target - next) > 1e-7f; if (modified) target = next;
        }
    }
    if (modified) changed();
    masterVolume.setValue(Decibels::gainToDecibels(engine.master.load(), -60.0f), dontSendNotification);
    updateTrack(); timeline.syncTrackControls(); if (mixer && mixerVisible) mixer->sync(); repaint(); return true;
}
void Workstation::toggleTrackControl(const String& trackId, TrackControl control) {
    if (busy) return;
    bool updated = false;
    {
        const ScopedLock guard(engine.lock);
        for (auto& track : engine.tracks) if (track->id == trackId) {
            if (control == TrackControl::recordArm) { if (!engine.recording) { track->armed = !track->armed; updated = true; } }
            else if (control == TrackControl::monitor) { if (!track->midi) { track->monitor = !track->monitor; updated = true; } }
            else { if (control == TrackControl::mute) track->mute = !track->mute; else track->solo = !track->solo; engine.midiDiscontinuity = true; updated = true; }
            break;
        }
    }
    if (updated) changed();
    if (updated && control == TrackControl::monitor) updateTrack();
    if (mixer && mixerVisible) mixer->sync();
    timeline.syncTrackControls(); timeline.repaint(); repaint();
}
void Workstation::trackMenu(const String& trackId) {
    if (busy || engine.recording) return;
    bool exists = false; for (auto& track : engine.tracks) exists |= track->id == trackId;
    if (!exists) return;
    trackMenuTarget = trackId;
    PopupMenu menu; menu.addItem(1, TRANS("Delete track..."));
    menu.showMenuAsync(PopupMenu::Options(), [safe = Component::SafePointer<Workstation>(this), trackId](int result) {
        if (safe && result == 1) MessageManager::callAsync([safe, trackId] { if (safe) safe->confirmRemoveTrack(trackId); });
    });
}
void Workstation::confirmRemoveTrack(const String& trackId) {
    if (busy || engine.recording) return;
    if (trackRemovalPrompt) { trackRemovalPrompt->toFront(true); return; }
    Track* target = nullptr; for (auto& track : engine.tracks) if (track->id == trackId) target = track.get();
    if (!target) return;
    auto* prompt = new AlertWindow(TRANS("Delete track?"), "Delete \"" + target->name + "\" and all of its clips and plugins?", MessageBoxIconType::QuestionIcon, this);
    prompt->setComponentID("deleteTrackPrompt"); prompt->setLookAndFeel(&theme);
    prompt->addButton(TRANS("Delete track"), 1); prompt->addButton(TRANS("Cancel"), 0, KeyPress(KeyPress::escapeKey), KeyPress(KeyPress::returnKey));
    trackRemovalPrompt = prompt;
    prompt->enterModalState(true, ModalCallbackFunction::create([safe = Component::SafePointer<Workstation>(this), trackId](int result) {
        if (!safe) return; safe->trackRemovalPrompt = nullptr;
        if (result == 1) MessageManager::callAsync([safe, trackId] { if (safe) safe->removeTrackById(trackId); });
    }), true);
}
bool Workstation::removeTrackById(String trackId) {
    if (busy || engine.recording) return false;
    const ScopedLock guard(engine.lock);
    int index = -1; for (int i = 0; i < int(engine.tracks.size()); ++i) if (engine.tracks[size_t(i)]->id == trackId) { index = i; break; }
    if (index < 0) return false;
    String previous = isPositiveAndBelow(selectedTrack, int(engine.tracks.size())) ? engine.tracks[size_t(selectedTrack)]->id : String();
    auto& clips = engine.tracks[size_t(index)]->clips;
    std::erase_if(pianoWindows, [&](auto& window) { return std::any_of(clips.begin(), clips.end(), [&](auto& clip) { return clip.id == window->clipId; }); });
    std::erase_if(pitchWindows, [&](auto& window) { return window->panel().targetsTrack(trackId); });
    pluginWindows.clear();
    engine.tracks.erase(engine.tracks.begin() + index); engine.midiDiscontinuity = true; undo.clear();
    int next = jlimit(0, jmax(0, int(engine.tracks.size()) - 1), selectedTrack);
    for (int i = 0; i < int(engine.tracks.size()); ++i) if (engine.tracks[size_t(i)]->id == previous) next = i;
    if (previous == trackId) { timeline.selectedClip = -1; engine.selectMidiTrack(-1); }
    selectTrack(next); changed(); refresh(); setStatus(TRANS("Track deleted.")); return true;
}
void Workstation::updateTrack() {
    bool hasTrack = isPositiveAndBelow(selectedTrack, int(engine.tracks.size()));
    for (auto* component : std::initializer_list<Component*>{&trackName, &volume, &pan, &monitor, &hardwareMidi, &loadButton, &pluginActions, keys.get()}) component->setEnabled(hasTrack);
    input.setEnabled(hasTrack && !engine.recording);
    if (!hasTrack) { trackName.setText({}, false); pluginList.updateContent(); pluginList.repaint(); return; }
    auto& t = *engine.tracks[size_t(selectedTrack)];
    trackName.setText(t.name, false); monitor.setToggleState(t.monitor, dontSendNotification);
    volume.setValue(Decibels::gainToDecibels(t.gain, -60.0f), dontSendNotification); pan.setValue(t.pan, dontSendNotification);
    if (input.getNumItems() != (t.midi ? 17 : 33)) {
        input.clear(dontSendNotification);
        if (t.midi) { input.addItem(TRANS("All MIDI channels"), 1); for (int i = 1; i <= 16; ++i) input.addItem(TRANS("MIDI channel ") + String(i), i + 1); }
        else { input.addItem(TRANS("Stereo inputs 1 + 2"), 1); for (int i = 1; i <= 32; ++i) input.addItem(TRANS("Mono input ") + String(i), i + 1); }
    }
    input.setSelectedId((t.midi ? t.midiChannel : t.input) + 1, dontSendNotification); monitor.setVisible(false); hardwareMidi.setVisible(false); hardwareMidi.setToggleState(t.sendMidi, dontSendNotification); pluginList.updateContent(); pluginList.repaint();
}
void Workstation::applyControls() {
    if (!isPositiveAndBelow(selectedTrack, int(engine.tracks.size()))) return;
    const ScopedLock guard(engine.lock); auto& t = *engine.tracks[size_t(selectedTrack)]; bool modified = false;
    auto assign = [&](auto& value, auto next) { if (value != next) { value = next; modified = true; } };
    if (t.sendMidi != hardwareMidi.getToggleState() || (t.midi && t.midiChannel != jmax(0, input.getSelectedId() - 1))) engine.midiDiscontinuity = true;
    assign(t.name, trackName.getText()); assign(t.sendMidi, hardwareMidi.getToggleState());
    if (!engine.recording) { if (t.midi) assign(t.midiChannel, jmax(0, input.getSelectedId() - 1)); else assign(t.input, jmax(0, input.getSelectedId() - 1)); }
    assign(t.monitor, monitor.getToggleState());
    double displayedGain = jlimit(-60.0, 6.0, std::round(double(Decibels::gainToDecibels(t.gain, -60.0f)) * 10) / 10);
    if (std::abs(volume.getValue() - displayedGain) > 0.0001) assign(t.gain, Decibels::decibelsToGain(float(volume.getValue()), -60.0f));
    if (std::abs(pan.getValue() - std::round(double(t.pan) * 100) / 100) > 0.0001) assign(t.pan, float(pan.getValue()));
    if (modified) changed();
    if (mixer && mixerVisible) mixer->sync();
}
void Workstation::setStatus(const String& message) { status = message; repaint(); }
void Workstation::error(const String& message) { status = message; AlertWindow::showMessageBoxAsync(MessageBoxIconType::NoIcon, "Auralis", japaneseMessage(message)); repaint(); }
void Workstation::rememberEdit() {
    const ScopedLock guard(engine.lock); std::vector<std::vector<Clip>> state;
    for (auto& t : engine.tracks) state.push_back(t->clips); undo.push_back(std::move(state)); if (undo.size() > 40) undo.erase(undo.begin());
}
void Workstation::undoEdit() {
    if (undo.empty() || engine.recording) return;
    const ScopedLock guard(engine.lock);
    if (undo.back().size() == engine.tracks.size()) for (size_t i = 0; i < engine.tracks.size(); ++i) engine.tracks[i]->clips = undo.back()[i];
    undo.pop_back(); engine.midiDiscontinuity = true; timeline.selectedClip = -1; changed(); refresh();
}
bool Workstation::cutClipAt(int track, int clip, double seconds) {
    if (busy || engine.recording) return false;
    const ScopedLock guard(engine.lock);
    if (!engine.canSplitClip(track, clip, seconds)) return false;
    rememberEdit(); engine.splitClip(track, clip, seconds);
    selectTrack(track); timeline.selectedClip = int(engine.tracks[size_t(track)]->clips.size()) - 1;
    changed(); refresh(); setStatus("Cut at " + String(seconds, 3) + " s. Ctrl+Z to undo."); return true;
}
bool Workstation::setClipGain(const String& trackId, const String& clipId, double db) {
    if (busy || engine.recording || !std::isfinite(db) || db < -60 || db > 60) return false;
    const ScopedLock guard(engine.lock);
    for (auto& track : engine.tracks) if (track->id == trackId) for (auto& clip : track->clips) if (clip.id == clipId && !clip.midi) {
        float gain = float(std::round(db * 10) / 10);
        if (std::abs(clip.gainDb - gain) < 0.0001f) return false;
        rememberEdit(); clip.gainDb = gain; changed(); refresh(); setStatus("Clip volume " + String(gain, 1) + " dB."); return true;
    }
    return false;
}
void Workstation::showClipGainDialog(const String& trackId, const String& clipId) {
    if (busy || engine.recording) return;
    if (clipGainPrompt) { clipGainPrompt->toFront(true); return; }
    Clip* target = nullptr;
    for (auto& track : engine.tracks) if (track->id == trackId) for (auto& clip : track->clips) if (clip.id == clipId && !clip.midi) target = &clip;
    if (!target) return;
    auto* prompt = new AlertWindow(TRANS("Clip volume"), "Volume for \"" + target->name + "\". Range: -60 to +60 dB. 0 dB keeps the original level.", MessageBoxIconType::NoIcon, this);
    prompt->setLookAndFeel(&theme); prompt->setComponentID("clipGainPrompt");
    prompt->addTextEditor("gain", String(target->gainDb, 1), TRANS("Volume (dB)"));
    prompt->addButton(TRANS("Apply"), 1, KeyPress(KeyPress::returnKey)); prompt->addButton(TRANS("Cancel"), 0, KeyPress(KeyPress::escapeKey));
    clipGainPrompt = prompt;
    prompt->enterModalState(true, ModalCallbackFunction::create([safe = Component::SafePointer<Workstation>(this), prompt, trackId, clipId](int result) {
        if (!safe) return; safe->clipGainPrompt = nullptr;
        if (result != 1) return;
        const String value = prompt->getTextEditorContents("gain").trim();
        const auto utf8 = value.toUTF8(); char* end = nullptr; double db = std::strtod(utf8.getAddress(), &end);
        if (value.isEmpty() || end == utf8.getAddress() || *end != 0 || !std::isfinite(db) || db < -60 || db > 60) {
            MessageManager::callAsync([safe] { if (safe) safe->error(TRANS("Enter a clip volume between -60 and +60 dB.")); }); return;
        }
        MessageManager::callAsync([safe, trackId, clipId, db] { if (safe) safe->setClipGain(trackId, clipId, db); });
    }), true);
}
bool Workstation::selectClipById(const String& trackId, const String& clipId) {
    if (busy || engine.recording) return false;
    for (int i = 0; i < int(engine.tracks.size()); ++i) if (engine.tracks[size_t(i)]->id == trackId) {
        auto& clips = engine.tracks[size_t(i)]->clips;
        for (int k = 0; k < int(clips.size()); ++k) if (clips[size_t(k)].id == clipId) {
            selectTrack(i); timeline.selectedClip = k; refresh(); return true;
        }
    }
    return false;
}
void Workstation::clipMenu(int track, int clip, double clickSeconds) {
    if (!isPositiveAndBelow(track, int(engine.tracks.size())) || !isPositiveAndBelow(clip, int(engine.tracks[size_t(track)]->clips.size()))) return;
    auto trackId = engine.tracks[size_t(track)]->id, clipId = engine.tracks[size_t(track)]->clips[size_t(clip)].id;
    StringArray layerIds; PopupMenu layers;
    const auto& target = engine.tracks[size_t(track)]->clips[size_t(clip)];
    for (int k : clipDrawOrder(*engine.tracks[size_t(track)], true)) {
        const auto& candidate = engine.tracks[size_t(track)]->clips[size_t(k)];
        if (candidate.startTime(engine.bpm) >= target.endTime(engine.bpm) || candidate.endTime(engine.bpm) <= target.startTime(engine.bpm)) continue;
        int id = 1000 + layerIds.size(); layerIds.add(candidate.id);
        layers.addItem(id, String(k + 1) + ": " + candidate.name + "  (" + String(candidate.startTime(engine.bpm), 2) + " - " + String(candidate.endTime(engine.bpm), 2) + " s)", true, k == clip);
    }
    PopupMenu menu; menu.addItem(8, TRANS("Cut here"), engine.canSplitClip(track, clip, clickSeconds));
    menu.addItem(1, TRANS("Cut at playhead    S"), engine.canSplitClip(track, clip, engine.cursor));
    menu.addSeparator(); menu.addItem(2, TRANS("Duplicate")); menu.addItem(3, TRANS("Delete clip    Delete"));
    if (layerIds.size() > 1) { menu.addSeparator(); menu.addSubMenu(TRANS("Select overlapping part"), layers); }
    bool midi = engine.tracks[size_t(track)]->clips[size_t(clip)].midi;
    if (!midi) {
        menu.addSeparator(); menu.addItem(9, TRANS("Clip volume..."));
        menu.addItem(11, String::fromUTF8("ノーマライズ..."));
        menu.addItem(14, String::fromUTF8("音程エディター..."));
        menu.addItem(12, String::fromUTF8("音程補正..."));
        if (target.pitchOriginal) menu.addItem(13, String::fromUTF8("音程補正を解除"));
        menu.addItem(10, TRANS("Reset volume to 0 dB"), std::abs(engine.tracks[size_t(track)]->clips[size_t(clip)].gainDb) > 0.0001f);
    }
    if (midi) menu.addItem(7, TRANS("Open piano roll"));
    else { menu.addItem(4, TRANS("Fade in/out: 10 ms")); menu.addItem(5, TRANS("Fade in/out: 100 ms")); menu.addItem(6, TRANS("Fade in/out: 1 second")); }
    menu.showMenuAsync(PopupMenu::Options(), [safe = Component::SafePointer<Workstation>(this), trackId, clipId, clickSeconds, layerIds](int id) {
        if (!safe || id == 0 || safe->busy || safe->engine.recording) return;
        int track = -1, clip = -1;
        for (int i = 0; i < int(safe->engine.tracks.size()); ++i) if (safe->engine.tracks[size_t(i)]->id == trackId) {
            track = i;
            auto& clips = safe->engine.tracks[size_t(i)]->clips;
            for (int k = 0; k < int(clips.size()); ++k) if (clips[size_t(k)].id == clipId) { clip = k; break; }
            break;
        }
        if (track < 0 || clip < 0) return;
        if (id >= 1000 && isPositiveAndBelow(id - 1000, layerIds.size())) { safe->selectClipById(trackId, layerIds[id - 1000]); return; }
        if (id == 7) { safe->showPianoRoll(track, clip); return; }
        if (id == 9) { MessageManager::callAsync([safe, trackId, clipId] { if (safe) safe->showClipGainDialog(trackId, clipId); }); return; }
        if (id == 10) { safe->setClipGain(trackId, clipId, 0); return; }
        if (id == 11) { MessageManager::callAsync([safe, trackId, clipId] { if (safe) safe->showNormalizeDialog(trackId, clipId); }); return; }
        if (id == 12) { MessageManager::callAsync([safe, trackId, clipId] { if (safe) safe->showPitchCorrectionDialog(trackId, clipId); }); return; }
        if (id == 13) { safe->resetPitchCorrection(trackId, clipId); return; }
        if (id == 14) { MessageManager::callAsync([safe, trackId, clipId] { if (safe) safe->showPitchRoll(trackId, clipId); }); return; }
        if (id == 1 || id == 8) { safe->cutClipAt(track, clip, id == 8 ? clickSeconds : safe->engine.cursor.load()); return; }
        safe->rememberEdit(); const ScopedLock guard(safe->engine.lock);
        auto& clips = safe->engine.tracks[size_t(track)]->clips; auto& c = clips[size_t(clip)];
        if (id == 2) { Clip second = c; second.id = Uuid().toString(); second.start += c.length; safe->engine.stampNewClip(second); clips.push_back(second); }
        else if (id == 3) clips.erase(clips.begin() + clip);
        else c.fadeIn = c.fadeOut = jmin(c.length / 2, id == 4 ? 0.01 : id == 5 ? 0.1 : 1.0);
        safe->engine.midiDiscontinuity = true; safe->timeline.selectedClip = -1; safe->changed(); safe->refresh();
    });
}
void Workstation::importFiles(const StringArray& files, int track, double start) {
    if (engine.recording || busy || files.isEmpty()) return;
    rememberEdit();
    try { for (auto& f : files) { if (File(f).hasFileExtension("mid;midi")) engine.importMidi(File(f), start < 0 ? engine.cursor.load() : start);
        else engine.importAudio(File(f), files.size() == 1 ? track : -1, start < 0 ? engine.cursor.load() : start); } changed(); status = TRANS("Imported ") + String(files.size()) + TRANS(" file(s)."); refresh(); }
    catch (const std::exception& e) { error(String::fromUTF8(e.what())); refresh(); }
}
bool Workstation::keyPressed(const KeyPress& key) {
    if (busy) return true;
    if (projectStart && projectStart->isVisible()) { if (key == KeyPress::escapeKey) showProjectStart(false); return true; }
    if (dynamic_cast<TextEditor*>(Component::getCurrentlyFocusedComponent())) return false;
    if (key == KeyPress::F3Key) { toggleMixer(); return true; }
    if (key == KeyPress::escapeKey) { hasTimeSelection = false; timeline.setCutMode(false); panicButton.onClick(); return true; }
    if (key == KeyPress::spaceKey) { playButton.triggerClick(); return true; }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S') { save(false); return true; }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z') { undoEdit(); return true; }
    if (key.getKeyCode() == 'R') { recordButton.triggerClick(); return true; }
    if (key == KeyPress::homeKey) { homeButton.triggerClick(); return true; }
    if (key.getKeyCode() == 'F' && !key.getModifiers().isCommandDown()) { fitProject(); return true; }
    if (key.getKeyCode() == 'C' && !key.getModifiers().isCommandDown() && !engine.recording) { timeline.setCutMode(!timeline.isCutMode()); return true; }
    if (key.getKeyCode() == 'V' && !key.getModifiers().isCommandDown()) { timeline.setCutMode(false); return true; }
    if (key.getKeyCode() == 'P' && !key.getModifiers().isCommandDown()) { togglePunchSelection(); return true; }
    if (key.getKeyCode() == 'L' && !key.getModifiers().isCommandDown()) { toggleLoopSelection(); return true; }
    if (key.getKeyCode() == 'S' && !key.getModifiers().isCommandDown()) { cutClipAt(selectedTrack, timeline.selectedClip, engine.cursor); return true; }
    if (key == KeyPress::deleteKey && timeline.selectedClip >= 0 && !engine.recording) {
        rememberEdit(); const ScopedLock guard(engine.lock); auto& clips = engine.tracks[size_t(selectedTrack)]->clips;
        if (!isPositiveAndBelow(timeline.selectedClip, int(clips.size()))) return true;
        if (key == KeyPress::deleteKey) { clips.erase(clips.begin() + timeline.selectedClip); timeline.selectedClip = -1; }
        engine.midiDiscontinuity = true; changed(); refresh(); return true;
    }
    return false;
}
MemoryBlock Workstation::pluginStateSnapshot() {
    const ScopedLock guard(engine.lock); MemoryOutputStream stream;
    for (auto& track : engine.tracks) for (auto& plugin : track->inserts) {
        MemoryBlock state; plugin->instance->getStateInformation(state);
        stream.writeString(track->id); stream.writeString(plugin->description.createIdentifierString());
        stream.writeBool(plugin->bypass); stream.writeInt64(int64(state.getSize())); stream.write(state.getData(), state.getSize());
    }
    for (const auto& plugin : engine.masterInserts) {
        MemoryBlock state; plugin->instance->getStateInformation(state); stream.writeString("MASTER"); stream.writeString(plugin->description.createIdentifierString());
        stream.writeBool(plugin->bypass); stream.writeInt64(int64(state.getSize())); stream.write(state.getData(), state.getSize());
    }
    return stream.getMemoryBlock();
}
void Workstation::markProjectSaved() { savedPluginState = pluginStateSnapshot(); dirty = false; }
void Workstation::requestClose(std::function<void()> action) {
    if (busy) { error(TRANS("Wait for the current export or plugin scan to finish.")); return; }
    if (blockForPitchEdits()) return;
    if (engine.recording) { error(TRANS("Stop recording before closing the project.")); return; }
    if (savePrompt) { savePrompt->toFront(true); return; }
    if (!dirty && pluginStateSnapshot() == savedPluginState) { action(); return; }
    auto* prompt = new AlertWindow(TRANS("Save project?"), TRANS("This project has unsaved changes. Save them before continuing?"), MessageBoxIconType::QuestionIcon, this);
    prompt->setComponentID("saveProjectPrompt"); prompt->setLookAndFeel(&theme);
    prompt->addButton(TRANS("Save"), 1, KeyPress(KeyPress::returnKey)); prompt->addButton(TRANS("Discard changes"), 2); prompt->addButton(TRANS("Cancel"), 0, KeyPress(KeyPress::escapeKey));
    savePrompt = prompt;
    prompt->enterModalState(true, ModalCallbackFunction::create([safe = Component::SafePointer<Workstation>(this), action](int result) {
        if (!safe) return; safe->savePrompt = nullptr;
        // Let the confirmation window and its modal state be destroyed before opening another dialog.
        MessageManager::callAsync([safe, action, result] {
            if (!safe) return;
            if (result == 1) safe->save(false, action); else if (result == 2) action();
        });
    }), true);
}
void Workstation::projectMenu() {
    PopupMenu menu; menu.addItem(1, TRANS("New project")); menu.addItem(2, TRANS("Open project...")); menu.addItem(3, TRANS("Save project    Ctrl+S")); menu.addItem(4, TRANS("Save project as..."));
    const auto recentFiles = recentProjects; PopupMenu recentMenu;
    for (int i = 0; i < int(recentFiles.size()); ++i) recentMenu.addItem(2000 + i, recentFiles[size_t(i)].getFileName() + "  (" + recentFiles[size_t(i)].getParentDirectory().getFullPathName() + ")", !engine.recording && recentFiles[size_t(i)].existsAsFile());
    if (!recentFiles.empty()) menu.addSubMenu(TRANS("Recent projects"), recentMenu);
    menu.addItem(21, TRANS("Start screen..."));
    menu.addSeparator(); menu.addItem(5, TRANS("Remove selected track")); menu.addItem(6, TRANS("Open recordings folder"));
    menu.addSeparator(); menu.addItem(9, TRANS("Import audio / MIDI..."));
    menu.addItem(10, TRANS("Export audio (WAV / MP3)...")); menu.addItem(7, TRANS("Export MIDI file..."));
    menu.addSeparator(); menu.addItem(11, TRANS("Undo edit    Ctrl+Z"), !engine.recording && !undo.empty());
    menu.addItem(13, TRANS("Fit project to window    F"));
    menu.addItem(20, TRANS("Mixer    F3"), true, mixerVisible);
    PopupMenu tools; tools.addItem(14, TRANS("Select tool    V"), true, !timeline.isCutMode());
    tools.addItem(15, TRANS("Cut tool    C"), !engine.recording, timeline.isCutMode());
    tools.addItem(16, TRANS("Cut selected clip at playhead    S"), engine.canSplitClip(selectedTrack, timeline.selectedClip, engine.cursor));
    tools.addSeparator(); tools.addItem(17, TRANS("Punch in selected range    P"), !engine.recording, engine.recordRange);
    tools.addItem(18, TRANS("Clear time selection"), hasTimeSelection);
    tools.addItem(19, TRANS("Repeat selected range    L"), !engine.recording, engine.loop);
    tools.addItem(22, TRANS("Follow playhead"), true, followPlayhead);
    menu.addSubMenu(TRANS("Tools"), tools);
    menu.addSeparator(); menu.addItem(8, TRANS("Audio diagnostics...")); menu.addItem(12, TRANS("Stop all notes    Esc"));
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&fileButton), [safe = Component::SafePointer<Workstation>(this), recentFiles](int result) {
        if (!safe) return;
        if (result >= 2000 && isPositiveAndBelow(result - 2000, int(recentFiles.size()))) { safe->openSelectedProject(recentFiles[size_t(result - 2000)]); return; }
        if (result == 21) { safe->showProjectStart(); return; }
        if (result == 22) { safe->followPlayhead = !safe->followPlayhead; safe->followResumeAt = 0; return; }
        if (result == 8) { safe->audioDiagnostics(); return; }
        if (result == 12) { safe->panicButton.onClick(); return; }
        if (result == 13) { safe->fitProject(); return; }
        if (result == 20) { safe->toggleMixer(); return; }
        if (result == 14) { safe->timeline.setCutMode(false); return; }
        if (safe->engine.recording) return;
        if (result == 15) { safe->timeline.setCutMode(true); return; }
        if (result == 16) { safe->cutClipAt(safe->selectedTrack, safe->timeline.selectedClip, safe->engine.cursor); return; }
        if (result == 17) { safe->togglePunchSelection(); return; }
        if (result == 18) { safe->hasTimeSelection = false; safe->refresh(); return; }
        if (result == 19) { safe->toggleLoopSelection(); return; }
        if (result == 9) { safe->importButton.onClick(); return; }
        if (result == 10) { safe->exportAudio(); return; }
        if (result == 11) { safe->undoEdit(); return; }
        if (result == 1) safe->newProject();
        else if (result == 2) safe->open();
        else if (result == 3 || result == 4) safe->save(result == 4);
        else if (result == 5 && isPositiveAndBelow(safe->selectedTrack, int(safe->engine.tracks.size()))) {
            safe->confirmRemoveTrack(safe->engine.tracks[size_t(safe->selectedTrack)]->id);
        } else if (result == 6) { safe->engine.recordingDirectory.createDirectory(); safe->engine.recordingDirectory.startAsProcess(); }
        else if (result == 7) safe->exportMidiFile();
    });
}
void Workstation::pluginMenu() {
    const int row = pluginList.getSelectedRow();
    const bool selected = isPositiveAndBelow(selectedTrack, int(engine.tracks.size()))
        && isPositiveAndBelow(row, int(engine.tracks[size_t(selectedTrack)]->inserts.size()));
    const bool bypassed = selected && engine.tracks[size_t(selectedTrack)]->inserts[size_t(row)]->bypass;
    PopupMenu menu; menu.addItem(1, TRANS("Open editor    Double-click"), selected);
    menu.addItem(2, TRANS("Bypass"), selected, bypassed); menu.addItem(3, TRANS("Remove insert"), selected && !engine.recording);
    menu.addSeparator(); menu.addItem(4, TRANS("Scan VST3 folder..."), !engine.recording);
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&pluginActions), [safe = Component::SafePointer<Workstation>(this)](int id) {
        if (!safe || safe->busy) return;
        if (id == 1) safe->showEditor();
        else if (id == 2) safe->bypassButton.onClick();
        else if (id == 3) safe->removeButton.onClick();
        else if (id == 4) safe->scanButton.onClick();
    });
}
void Workstation::saveToFile(const File& file, std::function<void()> after) {
    if (busy || engine.recording) return;
    if (blockForPitchEdits()) return;
    try {
        engine.stop(); engine.saveProject(file); projectFile = file; markProjectSaved();
        const bool historySaved = rememberProject(file);
        setStatus(TRANS("Saved ") + projectFile.getFileName() + (historySaved ? "" : " / Recent history could not be saved.")); if (after) after();
    } catch (const std::exception& e) { error(String::fromUTF8(e.what())); }
}
void Workstation::save(bool saveAs, std::function<void()> after) {
    if (busy || engine.recording) return;
    if (blockForPitchEdits()) return;
    if (saveAs || projectFile == File()) {
        chooser = std::make_unique<FileChooser>(TRANS("Save Auralis project"), projectFile == File() ? File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("Untitled.auralis") : projectFile, "*.auralis");
        chooser->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
            [safe = Component::SafePointer<Workstation>(this), after](const FileChooser& c) {
                const File file = c.getResult(); if (!safe || file == File()) return;
                MessageManager::callAsync([safe, file, after] { if (safe) safe->saveToFile(file.withFileExtension("auralis"), after); });
            }); return;
    }
    saveToFile(projectFile, after);
}
void Workstation::loadProjectFile(const File& file) {
    if (busy || engine.recording) return;
    if (projectTrustPrompt) { projectTrustPrompt->toFront(true); return; }
    try {
        const auto document = ProjectDocument::read(file);
        if (document->pluginsToApprove().isEmpty() && document->mediaToApprove().isEmpty()) { installProject(*document, {}); return; }
        String details;
        if (!document->pluginsToApprove().isEmpty()) details += String::fromUTF8("外部VST（PC上のプログラムとして実行されます）\n") + document->pluginsToApprove().joinIntoString("\n\n") + "\n\n";
        if (!document->mediaToApprove().isEmpty()) details += String::fromUTF8("プロジェクトフォルダ外の素材\n") + document->mediaToApprove().joinIntoString("\n");
        auto* prompt = new ProjectTrustDialog(this, details);
        prompt->setComponentID("projectTrustPrompt"); prompt->setLookAndFeel(&theme);
        prompt->addButton(String::fromUTF8("許可して開く"), 1); prompt->addButton(String::fromUTF8("キャンセル"), 0, KeyPress(KeyPress::escapeKey)); projectTrustPrompt = prompt;
        prompt->enterModalState(true, ModalCallbackFunction::create([safe = Component::SafePointer<Workstation>(this), document](int choice) {
            if (!safe) return; safe->projectTrustPrompt = nullptr;
            MessageManager::callAsync([safe, document, choice] { if (safe && choice == 1 && !safe->busy && !safe->engine.recording) safe->installProject(*document, {true, true}); });
        }), true);
    } catch (const std::exception& e) { error(String::fromUTF8(e.what())); }
}
void Workstation::installProject(const ProjectDocument& document, ProjectLoadApproval approval) {
    if (busy || engine.recording) return;
    try {
        pitchWindows.clear(); pianoWindows.clear(); pluginWindows.clear(); engine.loadProject(document, approval); hasTimeSelection = false; projectFile = document.file();
        undo.clear(); selectTrack(0); timeline.selectedClip = -1; timeline.setCutMode(false);
        tempo.setText(String(engine.bpm.load(), 1)); loopFrom.setText(String(engine.loopStart, 2)); loopTo.setText(String(engine.loopEnd, 2));
        masterVolume.setValue(Decibels::gainToDecibels(engine.master.load()), dontSendNotification);
        refresh(); markProjectSaved(); showProjectStart(false);
        const bool historySaved = rememberProject(document.file());
        setStatus(TRANS("Opened ") + document.file().getFileName() + (historySaved ? "" : " / Recent history could not be saved."));
    } catch (const std::exception& e) { error(String::fromUTF8(e.what())); }
}
void Workstation::openSelectedProject(const File& file) {
    if (file == File()) return;
    requestClose([safe = Component::SafePointer<Workstation>(this), file] { if (safe) safe->loadProjectFile(file); });
}
void Workstation::open() {
    if (busy || engine.recording) return;
    chooser = std::make_unique<FileChooser>(TRANS("Open Auralis project"), projectFile, "*.auralis");
    chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
        [safe = Component::SafePointer<Workstation>(this)](const FileChooser& c) {
            const File file = c.getResult(); if (!safe || file == File()) return;
            // Choose a project first; cancelling this picker never prompts or changes the current project.
            MessageManager::callAsync([safe, file] { if (safe) safe->openSelectedProject(file); });
        });
}
void Workstation::runBackground(std::function<String()> work, std::function<void()> done) {
    if (worker.joinable()) worker.join(); busy = true; setEnabled(false);
    for (auto& w : pianoWindows) w->setEnabled(false); for (auto& w : pitchWindows) w->setEnabled(false); for (auto& w : pluginWindows) w->setEnabled(false); repaint();
    worker = std::thread([safe = Component::SafePointer<Workstation>(this), work, done] {
        String message; bool ok = true; try { message = work(); } catch (const std::exception& e) { message = String::fromUTF8(e.what()); ok = false; }
        MessageManager::callAsync([safe, message, ok, done] { if (!safe) return; safe->busy = false; safe->setEnabled(true); safe->status = message;
            for (auto& w : safe->pianoWindows) w->setEnabled(true); for (auto& w : safe->pitchWindows) w->setEnabled(true); for (auto& w : safe->pluginWindows) w->setEnabled(true);
            if (ok && done) done(); if (!ok) safe->error(message); safe->refresh(); });
    });
}
Range<double> Workstation::defaultExportRange() const {
    return hasTimeSelection ? timeSelection : Range<double>(0, jmax(1.0, engine.duration()) + jmax(2.0, engine.effectTailSeconds()));
}
void Workstation::exportAudio() {
    if (engine.recording) { error(TRANS("Stop recording before exporting.")); return; }
    if (blockForPitchEdits()) return;
    const auto bounds = defaultExportRange(); const double startDefault = bounds.getStart(), endDefault = bounds.getEnd();
    auto* dialog = new AlertWindow(TRANS("Export audio"), hasTimeSelection ? TRANS("Export the selected time range as stereo WAV or MP3.") : TRANS("Stereo WAV or MP3. Default End includes estimated effect tails; adjust as needed."), MessageBoxIconType::NoIcon);
    dialog->addTextEditor("start", String(startDefault, 6), TRANS("Start (seconds)")); dialog->addTextEditor("end", String(endDefault, 6), TRANS("End (seconds)"));
    dialog->addComboBox("bits", {"WAV / 24-bit PCM", "WAV / 16-bit PCM", "WAV / 32-bit float", "MP3 / 320 kbps", "MP3 / 256 kbps", "MP3 / 192 kbps", "MP3 / 128 kbps"}, TRANS("Format"));
    dialog->addButton(TRANS("Choose destination"), 1); dialog->addButton(TRANS("Cancel"), 0);
    dialog->enterModalState(true, ModalCallbackFunction::create([safe = Component::SafePointer<Workstation>(this), dialog](int result) {
        if (!safe || result != 1) return;
        double start = dialog->getTextEditorContents("start").getDoubleValue(), end = dialog->getTextEditorContents("end").getDoubleValue();
        int choice = dialog->getComboBoxComponent("bits")->getSelectedItemIndex();
        int bits = choice == 1 ? 16 : choice == 2 ? 32 : 24;
        const int rates[] {320, 256, 192, 128}; int kbps = choice >= 3 && choice <= 6 ? rates[choice - 3] : 0;
        if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end <= start || end - start >= 86400) { safe->error(TRANS("Invalid export range.")); return; }
        String extension = kbps ? "mp3" : "wav";
        safe->chooser = std::make_unique<FileChooser>("Export stereo " + extension.toUpperCase(), File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("Mix." + extension), "*." + extension);
        safe->chooser->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
            [safe, start, end, bits, kbps, extension](const FileChooser& c) { if (!safe || c.getResult() == File()) return; auto file = c.getResult().withFileExtension(extension);
                safe->engine.stop(); safe->setStatus(TRANS("Rendering stereo ") + extension.toUpperCase() + "..."); safe->runBackground([safe, file, start, end, bits, kbps] {
                    if (kbps) safe->engine.exportMp3(file, start, end, kbps); else safe->engine.exportWav(file, start, end, bits);
                    return TRANS("Exported ") + file.getFileName() + (safe->engine.clipped > 0 ? " — clipping detected; reduce master volume." : ""); });
            });
    }), true);
}
void Workstation::audioSettings() {
    if (engine.recording) { error(TRANS("Stop recording before changing audio devices.")); return; }
    engine.stop();
    auto* settings = new AudioDeviceSelectorComponent(engine.devices, 0, 32, 0, 2, true, true, true, false);
    settings->setSize(640, 530); DialogWindow::LaunchOptions options; options.dialogTitle = TRANS("Audio and MIDI devices");
    options.dialogBackgroundColour = panel; options.content.setOwned(settings); options.useNativeTitleBar = true; options.resizable = false; options.launchAsync();
}
void Workstation::audioDiagnostics() {
    auto report = engine.audioDiagnostics();
    auto* dialog = new AlertWindow(TRANS("Audio diagnostics"), report, MessageBoxIconType::InfoIcon);
    dialog->addButton(TRANS("Save report"), 1); dialog->addButton(TRANS("Reset counters"), 2); dialog->addButton(TRANS("Close"), 0);
    dialog->enterModalState(true, ModalCallbackFunction::create([safe = Component::SafePointer<Workstation>(this), report](int result) {
        if (!safe) return;
        if (result == 2) { safe->engine.resetAudioDiagnostics(); safe->setStatus(TRANS("Audio counters reset.")); return; }
        if (result != 1) return;
        safe->chooser = std::make_unique<FileChooser>(TRANS("Save audio diagnostics"), File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("Auralis-audio-diagnostics.txt"), "*.txt");
        safe->chooser->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
            [safe, report](const FileChooser& c) { if (!safe || c.getResult() == File()) return; if (!c.getResult().replaceWithText(report)) safe->error(TRANS("Cannot save the report.")); });
    }), true);
}
void Workstation::scanPlugins() {
    if (engine.recording) return;
    chooser = std::make_unique<FileChooser>(TRANS("Select a VST3 plugin or a folder to scan"), File("C:/Program Files/Common Files/VST3"), "*");
    chooser->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectDirectories,
        [safe = Component::SafePointer<Workstation>(this)](const FileChooser& c) {
            if (!safe || c.getResult() == File()) return;
            auto root = c.getResult(); safe->engine.stop(); safe->setStatus(TRANS("Scanning plugins in an isolated process..."));
            auto results = std::make_shared<Array<PluginDescription>>();
            safe->runBackground([safe, root, results] {
                Array<File> candidates;
                if (root.hasFileExtension("vst3")) candidates.add(root);
                else { auto found = root.findChildFiles(File::findFilesAndDirectories, true, "*.vst3"); for (auto& f : found) {
                    bool insideBundle = false; auto parent = f.getParentDirectory();
                    while (parent != root && parent != parent.getParentDirectory()) { if (parent.hasFileExtension("vst3")) { insideBundle = true; break; } parent = parent.getParentDirectory(); }
                    if (!insideBundle) candidates.add(f);
                } }
                int failed = 0;
                for (auto& file : candidates) {
                    auto output = safe->dataDirectory().getChildFile("scan-" + Uuid().toString() + ".xml");
                    StringArray arguments; arguments.add(File::getSpecialLocation(File::currentExecutableFile).getFullPathName());
                    arguments.add("--scan-plugin"); arguments.add(file.getFullPathName()); arguments.add("--scan-output"); arguments.add(output.getFullPathName());
                    ChildProcess child;
                    if (!child.start(arguments, 0)) { failed++; continue; }
                    if (!child.waitForProcessToFinish(45000)) { child.kill(); failed++; }
                    if (auto xml = parseXML(output)) { KnownPluginList list; list.recreateFromXml(*xml); for (auto& desc : list.getTypes()) results->add(desc); }
                    else failed++;
                    output.deleteFile();
                }
                return TRANS("Scan finished: ") + String(results->size()) + TRANS(" plugin(s), ") + String(failed) + TRANS(" skipped/failed.");
            }, [safe, results] { if (!safe) return; for (auto& desc : *results) safe->knownPlugins.addType(desc);
                if (!safe->savePluginRegistry()) safe->error("The scan completed, but the plugin list could not be saved. Check that the Auralis settings folder is writable.");
            });
        });
}
void Workstation::insertPlugin() {
    if (isPositiveAndBelow(selectedTrack, int(engine.tracks.size()))) trackInsertMenu(engine.tracks[size_t(selectedTrack)]->id, {}, true);
}
void Workstation::showEditor() {
    int i = pluginList.getSelectedRow();
    if (!isPositiveAndBelow(selectedTrack, int(engine.tracks.size())) || !isPositiveAndBelow(i, int(engine.tracks[size_t(selectedTrack)]->inserts.size()))) return;
    auto& p = *engine.tracks[size_t(selectedTrack)]->inserts[size_t(i)]->instance;
    for (auto& w : pluginWindows) if (w->getContentComponent() == p.getActiveEditor()) { w->setVisible(true); w->toFront(true); return; }
    pluginWindows.push_back(std::make_unique<PluginWindow>(p));
}
int Workstation::PluginModel::getNumRows() { return isPositiveAndBelow(owner.selectedTrack, int(owner.engine.tracks.size())) ? int(owner.engine.tracks[size_t(owner.selectedTrack)]->inserts.size()) : 0; }
void Workstation::PluginModel::paintListBoxItem(int rowIndex, Graphics& g, int width, int height, bool selected) {
    if (rowIndex >= getNumRows()) return;
    auto& p = *owner.engine.tracks[size_t(owner.selectedTrack)]->inserts[size_t(rowIndex)];
    if (selected) { g.setColour(Colour(0xff30463f)); g.fillAll(); }
    label(g, String(rowIndex + 1) + "   " + p.description.name + (p.bypass ? TRANS("  [off]") : ""), {9, 0, width - 18, height}, 12, p.bypass ? dim : text);
}
void Workstation::PluginModel::listBoxItemDoubleClicked(int, const MouseEvent&) { owner.showEditor(); }
}

namespace auralis {
String TrackControlRow::getTooltipForRow(int i) {
    for (const auto& t : owner.engine.tracks) if (t->id == trackId && isPositiveAndBelow(i, int(t->inserts.size()))) return pluginHelp(t->inserts[size_t(i)]->description);
    return {};
}
}
