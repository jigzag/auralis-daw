#include "PianoRoll.h"
#include <cmath>

namespace auralis {
namespace { const Colour bg(0xff11151b), line(0xff28323f), ink(0xffe2e9ee), accent(0xff80e3c0); }
PianoRoll::PianoRoll(Workstation& w, String track, String clip) : owner(&w), trackId(track), clipId(clip) {
    setWantsKeyboardFocus(true); updateSize(); startTimerHz(20);
}
Clip* PianoRoll::resolve() {
    if (!owner) return nullptr;
    for (auto& t : owner->engine.tracks) if (t->id == trackId) for (auto& c : t->clips) if (c.id == clipId && c.midi) return &c;
    return nullptr;
}
void PianoRoll::updateSize() {
    if (!owner) return;
    auto* c = resolve(); // Message-thread-owned metadata; never lock audio for a timer.
    setSize(jmax(980, keyWidth + int((c ? c->length : 16) * pixels) + 160), top + 128 * rowHeight + 20);
}
Rectangle<float> PianoRoll::noteRect(const MidiNote& note, const Clip& c) const {
    return {float(keyWidth + (note.beat - c.offset) * pixels), float(top + (127 - note.pitch) * rowHeight + 1), jmax(1.0f, float(note.length * pixels)), float(rowHeight - 2)};
}
double PianoRoll::getPlayheadX() {
    auto* clip = resolve();
    return clip && owner ? keyWidth + (playheadDragBeat && !owner->engine.recording ? *playheadDragBeat
        : owner->engine.cursor * owner->engine.bpm / 60.0 - clip->start) * pixels : -1;
}
bool PianoRoll::seekPlayhead(float contentX, bool snapToGrid) {
    if (!owner || owner->busy || owner->engine.recording) return false;
    auto* clip = resolve(); if (!clip) return false;
    endPreview();
    double beat = jmax(0.0, (contentX - keyWidth) / pixels);
    if (snapToGrid) beat = snapBeat(beat);
    playheadDragBeat = beat;
    owner->engine.seek((clip->start + beat) * 60.0 / owner->engine.bpm);
    owner->timeline.repaint(); owner->repaint(); repaint(); if (auto* panel = findParentComponentOfClass<PianoRollPanel>()) panel->repaint();
    return true;
}
void PianoRoll::endPlayheadDrag() {
    if (playheadDragBeat && owner && !owner->busy && !owner->engine.recording)
        if (auto* clip = resolve()) owner->engine.seek((clip->start + *playheadDragBeat) * 60.0 / owner->engine.bpm);
    playheadDragBeat.reset(); repaint(); if (auto* panel = findParentComponentOfClass<PianoRollPanel>()) panel->repaint();
}
void PianoRoll::mouseWheelMove(const MouseEvent& event, const MouseWheelDetails& wheel) {
    if (!event.mods.isCommandDown()) { Component::mouseWheelMove(event, wheel); return; }
    if (!owner || owner->busy || selectingRange || editInProgress || !dragOriginals.empty() || draggingPlayhead) return;
    double delta = std::abs(wheel.deltaY) > 0.0001f ? wheel.deltaY : wheel.deltaX;
    if (std::abs(delta) < 0.0001) return;
    auto* view = findParentComponentOfClass<Viewport>();
    const int oldX = view ? view->getViewPositionX() : 0, oldY = view ? view->getViewPositionY() : 0;
    double mouseX = jmax(double(keyWidth), double(event.position.x));
    double beatAtMouse = (mouseX - keyWidth) / pixels;
    double screenX = mouseX - oldX;
    pixels = jlimit(0.125, 624.0, pixels * std::exp(jlimit(-0.7, 0.7, delta * 2.5)));
    updateSize();
    if (view) view->setViewPosition(jmax(0, int(std::round(keyWidth + beatAtMouse * pixels - screenX))), oldY);
    repaint(); if (auto* panel = findParentComponentOfClass<PianoRollPanel>()) panel->repaint();
}
void PianoRoll::paint(Graphics& g) {
    g.fillAll(bg); if (!owner) return;
    auto* c = resolve(); // Drawing reads metadata and atomic transport values only.
    if (!c) { g.setColour(ink); g.drawText(TRANS("This clip is no longer in the project."), getLocalBounds(), Justification::centred); return; }
    for (int pitch = 0; pitch < 128; ++pitch) {
        int y = top + (127 - pitch) * rowHeight; bool black = MidiMessage::isMidiNoteBlack(pitch);
        g.setColour(black ? Colour(0xff141a22) : Colour(0xff1b222c)); g.fillRect(keyWidth, y, getWidth() - keyWidth, rowHeight - 1);
        g.setColour(black ? Colour(0xff303a46) : Colour(0xffb5c1c9)); g.fillRect(0, y, keyWidth - 1, rowHeight - 1);
        if (pitch % 12 == 0) { g.setColour(black ? ink : bg); g.setFont(10); g.drawText(MidiMessage::getMidiNoteName(pitch, true, true, 4), 6, y, 50, rowHeight, Justification::centredLeft); }
    }
    double step = grid > 0 ? grid : 0.25, labelStep = 4;
    while (step * pixels < 8) step *= 2;
    while (labelStep * pixels < 64) labelStep *= 2;
    const auto visible = g.getClipBounds();
    double startBeat = std::floor(jmax(0.0, (visible.getX() - keyWidth) / pixels) / step) * step;
    double endBeat = jmin(c->length + 2, (visible.getRight() - keyWidth) / pixels + step);
    for (double beat = startBeat; beat < endBeat; beat += step) {
        int x = keyWidth + int(beat * pixels);
        bool bar = std::abs(std::fmod(beat, 4.0)) < 0.001;
        g.setColour(bar ? Colour(0xff465566) : line); g.drawVerticalLine(x, top, float(getHeight()));
        if (std::abs(std::fmod(beat, labelStep)) < 0.001) { g.setColour(ink); g.setFont(11); g.drawText(String(int(beat / 4) + 1), x + 5, 0, 60, top, Justification::centredLeft); }
    }
    g.saveState(); g.reduceClipRegion(keyWidth, top, getWidth() - keyWidth, getHeight() - top);
    g.setColour(Colour(0xff070a0e).withAlpha(0.6f)); g.fillRect(keyWidth + int(c->length * pixels), top, getWidth(), getHeight());
    for (int i = 0; i < int(c->notes.size()); ++i) {
        auto& note = c->notes[size_t(i)]; auto r = noteRect(note, *c);
        g.setColour(accent.withAlpha(isNoteSelected(i) ? 0.96f : 0.6f)); g.fillRoundedRectangle(r, 2);
        g.setColour(Colour(0xff386f64)); g.fillRect(r.withHeight(3).withY(r.getBottom() - 3).withWidth(r.getWidth() * note.velocity / 127.0f));
        if (isNoteSelected(i)) { g.setColour(Colours::white); g.drawRoundedRectangle(r, 2, 1); }
    }
    if (selectingRange) { g.setColour(accent.withAlpha(0.12f)); g.fillRect(marquee); g.setColour(accent); g.drawRect(marquee, 1.0f); }
    if (selectedBeatRange && !selectedNotes.empty()) {
        g.setColour(accent.withAlpha(0.65f));
        g.fillRect(float(keyWidth + (selectedBeatRange->getStart() - c->offset) * pixels), float(top), float(selectedBeatRange->getLength() * pixels), 3.0f);
    }
    float playhead = float(getPlayheadX());
    g.setColour(accent.withAlpha(0.85f)); g.drawVerticalLine(int(playhead), top, float(getHeight()));
    g.restoreState();
}
void PianoRoll::endPreview() { if (owner && previewPitch >= 0) owner->engine.keyboard.noteOff(previewChannel, previewPitch, 0); previewPitch = -1; }
void PianoRoll::edited() { if (owner) { owner->engine.midiDiscontinuity = true; owner->changed(); owner->refresh(); } updateSize(); repaint(); }
void PianoRoll::clearSelection() { selectedNotes.clear(); selected = -1; selectedBeatRange.reset(); dragOriginals.clear(); }
void PianoRoll::syncSelection(const Clip& clip) {
    std::erase_if(selectedNotes, [&](int i) { return !isPositiveAndBelow(i, int(clip.notes.size())); });
    if (!selectedNotes.contains(selected)) selected = selectedNotes.empty() ? -1 : *selectedNotes.begin();
    if (selected >= 0) { velocity = clip.notes[size_t(selected)].velocity; channel = clip.notes[size_t(selected)].channel; }
    if (selectedNotes.empty()) selectedBeatRange.reset();
    if (selectionChanged) selectionChanged();
}
void PianoRoll::deleteSelection(Clip& clip) {
    if (selectedNotes.empty()) return;
    owner->rememberEdit(); endPreview();
    for (auto i = selectedNotes.rbegin(); i != selectedNotes.rend(); ++i) clip.notes.erase(clip.notes.begin() + *i);
    clearSelection(); edited();
}
void PianoRoll::duplicateSelection(Clip& clip) {
    if (selectedNotes.empty()) return;
    double start = std::numeric_limits<double>::max(), end = 0;
    std::vector<MidiNote> copies; copies.reserve(selectedNotes.size());
    for (int i : selectedNotes) { const auto& note = clip.notes[size_t(i)]; start = jmin(start, note.beat); end = jmax(end, note.beat + note.length); copies.push_back(note); }
    if (selectedBeatRange) { start = jmin(start, selectedBeatRange->getStart()); end = jmax(end, selectedBeatRange->getEnd()); }
    double step = end - start;
    if (grid > 0) step = jmax(grid, std::ceil(step / grid - 1e-9) * grid);
    else step = jmax(1.0 / 960.0, step);
    owner->rememberEdit(); endPreview(); selectedNotes.clear();
    for (auto note : copies) { note.beat += step; selectedNotes.insert(int(clip.notes.size())); clip.notes.push_back(note); clip.length = jmax(clip.length, note.beat + note.length - clip.offset); }
    selected = *selectedNotes.begin(); selectedBeatRange = Range<double>(start + step, start + step + step);
    clip.length = jmax(clip.length, selectedBeatRange->getEnd() - clip.offset);
    syncSelection(clip); edited();
    if (auto* view = findParentComponentOfClass<Viewport>()) {
        int right = keyWidth + int((selectedBeatRange->getEnd() - clip.offset) * pixels) + 32;
        if (right > view->getViewPositionX() + view->getViewWidth()) view->setViewPosition(jmax(0, right - view->getViewWidth()), view->getViewPositionY());
    }
}
void PianoRoll::mouseDown(const MouseEvent& e) {
    grabKeyboardFocus(); endPreview(); editInProgress = false; selectingRange = false; draggingPlayhead = false; dragOriginals.clear(); if (!owner || owner->engine.recording || owner->busy) return;
    if (e.x >= keyWidth && e.y < top && e.mods.isLeftButtonDown()) { draggingPlayhead = seekPlayhead(e.position.x, e.mods.isShiftDown()); return; }
    const ScopedLock guard(owner->engine.lock); auto* c = resolve(); if (!c) return;
    for (int i = 0; i < int(owner->engine.tracks.size()); ++i) if (owner->engine.tracks[size_t(i)]->id == trackId) owner->selectTrack(i);
    if (e.x < keyWidth && e.y >= top) { previewPitch = pitchAt(float(e.y)); previewChannel = channel; owner->engine.keyboard.noteOn(channel, previewPitch, velocity / 127.0f); return; }
    if (e.x < keyWidth || e.y < top) return;
    syncSelection(*c);
    bool additive = e.mods.isCommandDown() || e.mods.isShiftDown();
    for (int i = int(c->notes.size()) - 1; i >= 0; --i) if (noteRect(c->notes[size_t(i)], *c).contains(e.position)) {
        if (e.mods.isPopupMenu()) { if (!isNoteSelected(i)) { clearSelection(); selectedNotes.insert(i); selected = i; } deleteSelection(*c); return; }
        if (additive && isNoteSelected(i)) { selectedNotes.erase(i); selectedBeatRange.reset(); syncSelection(*c); repaint(); return; }
        if (!additive && !isNoteSelected(i)) clearSelection();
        selectedNotes.insert(i); selected = i;
        if (additive) selectedBeatRange.reset();
        original = c->notes[size_t(i)]; origin = e.position; rangeBeforeDrag = selectedBeatRange;
        for (int index : selectedNotes) dragOriginals.push_back({index, c->notes[size_t(index)]});
        auto bounds = noteRect(original, *c);
        resizing = e.x >= bounds.getRight() - jmin(7.0f, bounds.getWidth() * 0.3f); velocity = original.velocity; channel = original.channel;
        syncSelection(*c);
        previewPitch = original.pitch; previewChannel = original.channel; owner->engine.keyboard.noteOn(previewChannel, previewPitch, velocity / 127.0f); repaint(); return;
    }
    if (e.mods.isPopupMenu()) return;
    if (!additive) clearSelection();
    selectingRange = true; origin = e.position; marquee = Rectangle<float>(origin.x, origin.y, 0, 0);
    selectionBeforeMarquee = selectedNotes; rangeBeforeDrag = selectedBeatRange;
    repaint();
}
void PianoRoll::mouseDoubleClick(const MouseEvent& e) {
    if (!owner || owner->engine.recording || owner->busy || e.x < keyWidth || e.y < top) return;
    const ScopedLock guard(owner->engine.lock); auto* c = resolve(); if (!c) return;
    selectingRange = false;
    for (const auto& note : c->notes) if (noteRect(note, *c).contains(e.position)) return;
    owner->rememberEdit();
    MidiNote note; note.beat = jmax(c->offset, c->offset + snapBeat((e.x - keyWidth) / pixels)); note.pitch = pitchAt(float(e.y)); note.velocity = velocity; note.channel = channel; note.length = grid > 0 ? jmax(grid, 1.0) : 1.0;
    c->notes.push_back(note); clearSelection(); selected = int(c->notes.size()) - 1; selectedNotes.insert(selected); c->length = jmax(c->length, note.beat + note.length - c->offset); syncSelection(*c); edited();
}
void PianoRoll::mouseDrag(const MouseEvent& e) {
    if (!owner || owner->engine.recording) return;
    if (draggingPlayhead) { seekPlayhead(e.position.x, e.mods.isShiftDown()); return; }
    auto* c = resolve(); if (!c) return;
    if (selectingRange) {
        marquee = {jmin(origin.x, e.position.x), jmin(origin.y, e.position.y), std::abs(e.position.x - origin.x), std::abs(e.position.y - origin.y)};
        selectedNotes = selectionBeforeMarquee;
        for (int i = 0; i < int(c->notes.size()); ++i) if (marquee.intersects(noteRect(c->notes[size_t(i)], *c))) selectedNotes.insert(i);
        double first = jmax(c->offset, c->offset + (marquee.getX() - keyWidth) / pixels), last = jmax(first, c->offset + (marquee.getRight() - keyWidth) / pixels);
        if (grid > 0) { first = jmax(c->offset, std::floor(first / grid + 1e-9) * grid); last = std::ceil(last / grid - 1e-9) * grid; }
        if (rangeBeforeDrag) { first = jmin(first, rangeBeforeDrag->getStart()); last = jmax(last, rangeBeforeDrag->getEnd()); }
        selectedBeatRange = Range<double>(first, jmax(first, last)); syncSelection(*c); repaint(); return;
    }
    if (dragOriginals.empty() || e.position.getDistanceFrom(origin) < 2) return;
    const ScopedLock guard(owner->engine.lock);
    if (!editInProgress) { owner->rememberEdit(); editInProgress = true; endPreview(); }
    double delta = (e.position.x - origin.x) / pixels;
    double firstBeat = std::numeric_limits<double>::max(), shortest = firstBeat; int low = 127, high = 0;
    for (const auto& [i, note] : dragOriginals) { firstBeat = jmin(firstBeat, note.beat); shortest = jmin(shortest, note.length); low = jmin(low, note.pitch); high = jmax(high, note.pitch); }
    if (rangeBeforeDrag) firstBeat = jmin(firstBeat, rangeBeforeDrag->getStart());
    double shift = resizing ? jmax((grid > 0 ? grid : 1.0 / 960.0) - shortest, snapBeat(original.length + delta) - original.length)
                            : jmax(c->offset - firstBeat, snapBeat(original.beat + delta) - original.beat);
    int pitchShift = jlimit(-low, 127 - high, -int(std::round((e.position.y - origin.y) / rowHeight)));
    for (const auto& [index, source] : dragOriginals) if (isPositiveAndBelow(index, int(c->notes.size()))) {
        auto& note = c->notes[size_t(index)]; note = source;
        if (resizing) note.length += shift; else { note.beat += shift; note.pitch += pitchShift; }
        c->length = jmax(c->length, note.beat + note.length - c->offset);
    }
    if (rangeBeforeDrag) selectedBeatRange = resizing ? Range<double>(rangeBeforeDrag->getStart(), jmax(rangeBeforeDrag->getStart(), rangeBeforeDrag->getEnd() + shift))
                                                    : Range<double>(rangeBeforeDrag->getStart() + shift, rangeBeforeDrag->getEnd() + shift);
    owner->dirty = true; owner->engine.midiDiscontinuity = true; repaint();
}
void PianoRoll::mouseUp(const MouseEvent&) { if (draggingPlayhead) endPlayheadDrag(); endPreview(); selectingRange = false; draggingPlayhead = false; dragOriginals.clear(); if (editInProgress) { editInProgress = false; edited(); } else repaint(); }
void PianoRoll::setVelocity(int value) {
    velocity = jlimit(1, 127, value); if (!owner || owner->engine.recording) return;
    const ScopedLock guard(owner->engine.lock); auto* c = resolve();
    if (c && !selectedNotes.empty()) { owner->rememberEdit(); for (int i : selectedNotes) if (isPositiveAndBelow(i, int(c->notes.size()))) c->notes[size_t(i)].velocity = velocity; edited(); }
}
void PianoRoll::setChannel(int value) {
    channel = jlimit(1, 16, value); if (!owner || owner->engine.recording) return;
    const ScopedLock guard(owner->engine.lock); auto* c = resolve();
    if (c && !selectedNotes.empty()) { owner->rememberEdit(); for (int i : selectedNotes) if (isPositiveAndBelow(i, int(c->notes.size()))) c->notes[size_t(i)].channel = channel; edited(); }
}
void PianoRoll::quantize() { if (!owner || owner->engine.recording) return; const ScopedLock guard(owner->engine.lock); if (auto* c = resolve()) { owner->rememberEdit(); owner->engine.quantizeClip(*c, grid > 0 ? grid : 0.25); edited(); } }
void PianoRoll::fitClip() {
    if (!owner || owner->busy) return;
    auto* clip = resolve(); auto* view = findParentComponentOfClass<Viewport>();
    if (!clip || !view) return;
    pixels = jlimit(0.125, 624.0, jmax(1, view->getViewWidth() - keyWidth - 24) / jmax(1.0, clip->length));
    updateSize(); view->setViewPosition(0, view->getViewPositionY()); repaint();
    if (auto* panel = findParentComponentOfClass<PianoRollPanel>()) panel->repaint();
}
bool PianoRoll::keyPressed(const KeyPress& key) {
    if (!owner) return false;
    if (key == KeyPress::escapeKey) { endPreview(); owner->engine.stop(); owner->refresh(); return true; }
    if (key == KeyPress::spaceKey) { if (owner->engine.playing) owner->engine.stop(); else owner->engine.play(); return true; }
    if (key.getKeyCode() == 'L' && !key.getModifiers().isCommandDown()) { owner->toggleLoopSelection(); return true; }
    if (key.getKeyCode() == 'F' && !key.getModifiers().isCommandDown()) { fitClip(); return true; }
    if (owner->engine.recording) return true;
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z') { owner->undoEdit(); clearSelection(); updateSize(); repaint(); return true; }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'S') return owner->keyPressed(key);
    const ScopedLock guard(owner->engine.lock); auto* c = resolve(); if (!c) return false;
    syncSelection(*c);
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'A') { clearSelection(); for (int i = 0; i < int(c->notes.size()); ++i) selectedNotes.insert(i); syncSelection(*c); repaint(); return true; }
    if (key == KeyPress::deleteKey || key == KeyPress::backspaceKey) { deleteSelection(*c); return true; }
    if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'D') { duplicateSelection(*c); return true; }
    if (key.getKeyCode() == 'Q') { quantize(); return true; }
    return false;
}
void PianoRoll::timerCallback() {
    if (auto* c = resolve()) {
        if (std::any_of(selectedNotes.begin(), selectedNotes.end(), [&](int i) { return !isPositiveAndBelow(i, int(c->notes.size())); })) syncSelection(*c);
    }
    updateSize(); repaint(); if (auto* panel = findParentComponentOfClass<PianoRollPanel>()) panel->repaint();
}
PianoRollPanel::PianoRollPanel(Workstation& w, String track, String clip) : roll(w, track, clip) {
    viewport.setViewedComponent(&roll, false); addAndMakeVisible(viewport);
    for (auto* c : std::initializer_list<Component*>{&grid, &channel, &velocity, &quantize, &play}) addAndMakeVisible(c);
    grid.addItemList({"1/4", "1/8", "1/16", "1/32", TRANS("1/8 triplet"), TRANS("Free")}, 1); grid.setSelectedId(3);
    grid.onChange = [this] { static const double steps[] = {1, 0.5, 0.25, 0.125, 1.0 / 3.0, 0}; roll.grid = steps[jlimit(0, 5, grid.getSelectedItemIndex())]; roll.repaint(); };
    channel.addItemList({"Ch 1", "Ch 2", "Ch 3", "Ch 4", "Ch 5", "Ch 6", "Ch 7", "Ch 8", "Ch 9", "Ch 10", "Ch 11", "Ch 12", "Ch 13", "Ch 14", "Ch 15", "Ch 16"}, 1); channel.setSelectedId(1);
    channel.onChange = [this] { roll.setChannel(channel.getSelectedId()); };
    velocity.setRange(1, 127, 1); velocity.setValue(100); velocity.setSliderStyle(Slider::LinearHorizontal); velocity.setTextBoxStyle(Slider::TextBoxRight, false, 45, 24);
    velocity.onValueChange = [this] { roll.setVelocity(int(velocity.getValue())); };
    roll.selectionChanged = [this] { velocity.setValue(roll.velocity, dontSendNotification); channel.setSelectedId(roll.channel, dontSendNotification); };
    quantize.onClick = [this] { roll.quantize(); };
    play.onClick = [safe = Component::SafePointer<Workstation>(&w)] { if (safe) { if (safe->engine.playing) safe->engine.stop(); else safe->engine.play(); } };
    setSize(1080, 650); viewport.setViewPosition(0, 28 + (127 - 88) * 16);
}
void PianoRollPanel::resized() {
    play.setBounds(14, 12, 110, 30); grid.setBounds(180, 12, 110, 30); quantize.setBounds(302, 12, 132, 30); channel.setBounds(446, 12, 90, 30); velocity.setBounds(612, 12, 200, 30);
    viewport.setBounds(0, 84, getWidth(), getHeight() - 114);
}
void PianoRollPanel::paint(Graphics& g) {
    g.fillAll(bg); g.setColour(ink); g.setFont(12); g.drawText(TRANS("Grid"), 140, 12, 36, 30, Justification::centredLeft); g.drawText(TRANS("Velocity"), 548, 12, 66, 30, Justification::centredLeft);
    g.setColour(Colour(0xff1b222c)); g.fillRect(0, 56, getWidth(), 28);
    int step = 1, labelStep = 4;
    while (step * roll.pixels < 12) step *= 2;
    while (labelStep * roll.pixels < 64) labelStep *= 2;
    int firstBeat = int(viewport.getViewPositionX() / roll.pixels) / step * step;
    for (int beat = firstBeat; ; beat += step) {
        int x = int(62 + beat * roll.pixels) - viewport.getViewPositionX();
        if (x > getWidth()) break; if (x < 62) continue;
        g.setColour(line); g.drawVerticalLine(x, 56, 84);
        if (beat % labelStep == 0) { g.setColour(ink); g.drawText(String(beat / 4 + 1), x + 5, 56, 60, 28, Justification::centredLeft); }
    }
    int playhead = int(std::round(roll.getPlayheadX())) - viewport.getViewPositionX();
    if (playhead >= 62 && playhead < getWidth() - 15) {
        g.setColour(accent); g.drawVerticalLine(playhead, 56, 84); Path marker;
        marker.addTriangle(float(playhead - 5), 57, float(playhead + 5), 57, float(playhead), 66); g.fillPath(marker);
    }
    g.setColour(ink.withAlpha(0.75f)); g.setFont(11); g.drawText(TRANS("Ctrl+wheel: zoom  |  F: fit clip  |  Ruler: seek  |  Drag empty: select  |  Drag notes: move  |  Delete: remove  |  Ctrl+D: repeat"), 14, getHeight() - 28, getWidth() - 28, 24, Justification::centredLeft);
    double percent = roll.pixels * 100 / 78;
    g.setColour(accent); g.drawText(String(roll.getNumSelectedNotes()) + " selected  /  " + String(percent, percent < 10 ? 1 : 0) + "%", 830, 12, jmax(0, getWidth() - 842), 30, Justification::centredLeft);
}
void PianoRollPanel::mouseDown(const MouseEvent& event) {
    draggingPlayhead = false;
    if (event.x < 62 || event.y < 56 || event.y >= 84 || !event.mods.isLeftButtonDown()) return;
    roll.grabKeyboardFocus();
    draggingPlayhead = roll.seekPlayhead(event.position.x + viewport.getViewPositionX(), event.mods.isShiftDown());
}
void PianoRollPanel::mouseDrag(const MouseEvent& event) {
    if (!draggingPlayhead) return;
    int scroll = viewport.getViewPositionX();
    if (event.x < 62) scroll = jmax(0, scroll - jmax(8, (62 - event.x) / 2));
    else if (event.x > getWidth() - 16) scroll += jmax(8, (event.x - getWidth() + 16) / 2);
    viewport.setViewPosition(scroll, viewport.getViewPositionY());
    roll.seekPlayhead(jlimit(62.0f, float(getWidth() - 16), event.position.x) + viewport.getViewPositionX(), event.mods.isShiftDown());
}
void PianoRollPanel::mouseUp(const MouseEvent&) { if (draggingPlayhead) roll.endPlayheadDrag(); draggingPlayhead = false; }
void PianoRollPanel::mouseWheelMove(const MouseEvent& event, const MouseWheelDetails& wheel) {
    if (draggingPlayhead) return;
    if (event.mods.isCommandDown()) { roll.mouseWheelMove(event.getEventRelativeTo(&roll), wheel); return; }
    Component::mouseWheelMove(event, wheel);
}
PianoRollWindow::PianoRollWindow(Workstation& w, String track, String clip) : DocumentWindow(TRANS("Piano roll"), bg, allButtons), clipId(clip) {
    setLookAndFeel(&w.getLookAndFeel()); setUsingNativeTitleBar(true); setContentOwned(new PianoRollPanel(w, track, clip), true); setResizable(true, false); setResizeLimits(850, 450, 2400, 1600); centreWithSize(1080, 650); setVisible(true);
}
void Workstation::showPianoRoll(int track, int index) {
    if (engine.recording || !isPositiveAndBelow(track, int(engine.tracks.size())) || !isPositiveAndBelow(index, int(engine.tracks[size_t(track)]->clips.size()))) return;
    auto& c = engine.tracks[size_t(track)]->clips[size_t(index)]; if (!c.midi) return;
    selectTrack(track);
    for (auto& w : pianoWindows) if (w->clipId == c.id) { w->setVisible(true); w->toFront(true); return; }
    pianoWindows.push_back(std::make_unique<PianoRollWindow>(*this, engine.tracks[size_t(track)]->id, c.id));
}
Image Workstation::pianoRollPreview() { return pianoWindows.empty() ? Image() : pianoWindows.back()->createComponentSnapshot(pianoWindows.back()->getLocalBounds()); }
void Workstation::exportMidiFile() {
    if (engine.recording) return;
    chooser = std::make_unique<FileChooser>(TRANS("Export standard MIDI file"), File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("Session.mid"), "*.mid");
    chooser->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
        [safe = Component::SafePointer<Workstation>(this)](const FileChooser& c) { if (!safe || c.getResult() == File()) return; try { safe->engine.exportMidi(c.getResult().withFileExtension("mid")); safe->setStatus(TRANS("Exported MIDI file.")); } catch (const std::exception& e) { safe->error(String::fromUTF8(e.what())); } });
}
}
