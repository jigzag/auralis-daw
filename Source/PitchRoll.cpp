#include "PitchRoll.h"
#include <cmath>
#include <cstdlib>

namespace auralis {
namespace {
const Colour bg(0xff11151b), panelColour(0xff1a222b), ink(0xffe2e9ee), mint(0xff80e3c0);
String pitchName(double value) {
    const int note = int(std::round(value)), cents = int(std::round((value - note) * 100));
    return MidiMessage::getMidiNoteName(note, true, true, 4) + " " + (cents >= 0 ? "+" : "") + String(cents) + " cent";
}
}
PitchRollPanel::PitchRollPanel(Workstation& w, String track, String clip, const Clip& before, std::vector<PitchFrame> data)
    : clipId(std::move(clip)), owner(&w), trackId(std::move(track)) {
    setWantsKeyboardFocus(true); addAndMakeVisible(viewport); viewport.setViewedComponent(&grid, false); viewport.setScrollBarsShown(true, true);
    auto button = [&](TextButton& b, const char* name, const String& text, std::function<void()> action) {
        addAndMakeVisible(b); b.setComponentID(name); b.setButtonText(text); b.setWantsKeyboardFocus(false); b.onClick = std::move(action);
    };
    button(play, "pitchPlay", String::fromUTF8("再生"), [this] { apply(true); });
    button(stop, "pitchStop", String::fromUTF8("停止"), [this] { if (owner && !owner->busy) owner->engine.stop(); });
    button(applyButton, "pitchApply", String::fromUTF8("適用"), [this] { apply(); });
    button(undoButton, "pitchUndo", String::fromUTF8("元に戻す"), [this] { undoLocal(); });
    button(discardButton, "pitchDiscard", String::fromUTF8("変更を破棄"), [this] { discard(); });
    button(fitButton, "pitchFit", String::fromUTF8("全体表示"), [this] { fit(); });
    button(reloadButton, "pitchReload", String::fromUTF8("再解析"), [this] { if (owner && !hasPendingEdits()) owner->showPitchRoll(trackId, clipId, true); });
    button(restoreButton, "pitchRestore", String::fromUTF8("音を元へ"), [this] { restoreSelected(); });
    grid.setComponentID("pitchGrid");
    addAndMakeVisible(dragAudition); dragAudition.setComponentID("pitchDragAudition"); dragAudition.setButtonText(String::fromUTF8("ドラッグ試聴"));
    dragAudition.setWantsKeyboardFocus(false); dragAudition.setToggleState(true, dontSendNotification);
    dragAudition.setTooltip(String::fromUTF8("ドラッグ中に選択音をループ試聴します。離すと停止。最終的な補正音は適用して確認できます。"));
    dragAudition.onClick = [this] { if (!dragAudition.getToggleState()) endAudition(true); };
    addAndMakeVisible(curveEdit); curveEdit.setComponentID("pitchCurveEdit"); curveEdit.setButtonText(String::fromUTF8("カーブ編集")); curveEdit.setWantsKeyboardFocus(false);
    curveEdit.setTooltip(String::fromUTF8("緑の音程線を直接ドラッグして部分的に描き直します。音のブロックがない短い線や語尾も編集できます。横に引くと平らにできます。Ctrlで上下を微調整。"));
    curveEdit.onClick = [this] { endAudition(true); grid.setMouseCursor(curveEdit.getToggleState() ? MouseCursor::CrosshairCursor : MouseCursor::UpDownResizeCursor); grid.repaint(); };
    addAndMakeVisible(rangeLoop); rangeLoop.setComponentID("pitchRangeLoop"); rangeLoop.setButtonText(String::fromUTF8("範囲ループ")); rangeLoop.setWantsKeyboardFocus(false);
    rangeLoop.setTooltip(String::fromUTF8("LOOP欄をドラッグして音程調整専用の範囲を指定します。右クリックまたはダブルクリックで解除。プロジェクトのループ範囲は変更しません。"));
    rangeLoop.onClick = [this] { updateLoop(); repaint(); };
    addAndMakeVisible(verticalZoom); verticalZoom.setComponentID("pitchVerticalZoom"); verticalZoom.setRange(0.5, 32, 0.01);
    verticalZoom.setSkewFactorFromMidPoint(4); verticalZoom.setValue(1, dontSendNotification); verticalZoom.setTextValueSuffix(" x");
    verticalZoom.setNumDecimalPlacesToDisplay(2); verticalZoom.setSliderStyle(Slider::LinearHorizontal);
    verticalZoom.setTextBoxStyle(Slider::TextBoxRight, false, 64, 23); verticalZoom.setWantsKeyboardFocus(false);
    verticalZoom.setDoubleClickReturnValue(true, 1);
    verticalZoom.setTooltip(String::fromUTF8("縦方向を0.5〜32倍に拡大。Ctrl+Shift+ホイールでマウス付近を拡大できます。ダブルクリックで1倍。"));
    verticalZoom.onValueChange = [this] { zoomVertical(verticalZoom.getValue()); };
    grid.setTooltip(String::fromUTF8("上下ドラッグ: 半音 / Ctrl+ドラッグ: セント。音の時間と長さは変わりません。"));
    applyButton.setTooltip(String::fromUTF8("編集を音声へ反映します。Ctrl+Enter。適用後もプロジェクトのCtrl+Zで戻せます。"));
    reloadButton.setTooltip(String::fromUTF8("元のパートを解析し直します。未適用の変更は先に適用または破棄してください。"));
    setSize(1100, 720); setAnalysis(before, std::move(data)); startTimerHz(20);
}
PitchRollPanel::~PitchRollPanel() { if (owner) owner->engine.clearPitchLoop(auditionId); endAudition(true); stopTimer(); if (scoopPrompt) { scoopPrompt->setLookAndFeel(nullptr); scoopPrompt->exitModalState(0); } if (numericPrompt) { numericPrompt->setLookAndFeel(nullptr); numericPrompt->exitModalState(0); } viewport.setViewedComponent(nullptr); }
Clip* PitchRollPanel::resolve() {
    if (owner) for (const auto& track : owner->engine.tracks) for (auto& clip : track->clips) if (clip.id == clipId && !clip.midi) { trackId = track->id; return &clip; }
    return nullptr;
}
bool PitchRollPanel::hasPendingEdits() const {
    if (pending && owner) for (const auto& track : owner->engine.tracks) for (const auto& clip : track->clips) if (clip.id == clipId) return true;
    return false;
}
bool PitchRollPanel::targetsTrack(const String& id) { return resolve() && trackId == id; }
void PitchRollPanel::setAnalysis(const Clip& clip, std::vector<PitchFrame> data) {
    endAudition(true); finishSeek(); finishLoop(true);
    if (!loopRange.isEmpty()) {
        const double delta = snapshot.offset - clip.offset;
        loopRange = Range<double>(loopRange.getStart() + delta, loopRange.getEnd() + delta).getIntersectionWith({0, clip.length});
    }
    snapshot = clip; frames = std::move(data); notes = pitchSegments(snapshot, frames); baseline = notes;
    undo.clear(); redo.clear(); selected.clear(); pending = stale = false;
    message = notes.empty() ? String::fromUTF8("音程を検出できませんでした。クリーンな単音の録音で試してください。") : String::fromUTF8("音を選んで上下にドラッグし、適用してください。");
    fit(); timerCallback();
}
void PitchRollPanel::syncCurrent() {
    auto* current = resolve(); if (!current) { stale = true; return; }
    if (current->source == snapshot.source && current->pitchOriginal == snapshot.pitchOriginal && current->offset == snapshot.offset
        && current->length == snapshot.length && current->pitchSettings == snapshot.pitchSettings) { snapshot.start = current->start; stale = false; return; }
    const auto base = [](const Clip& c) { return c.pitchOriginal ? c.pitchOriginal : c.source; };
    if (!pending && base(*current) == base(snapshot) && current->offset == snapshot.offset && current->length == snapshot.length
        && (current->pitchSettings.manual || !current->pitchOriginal || current->pitchSettings == snapshot.pitchSettings)) {
        snapshot = *current; notes = pitchSegments(snapshot, frames); baseline = notes; undo.clear(); redo.clear(); stale = false; selected.clear(); grid.repaint();
    } else { stale = true; message = String::fromUTF8("パートが変更されました。変更を破棄して再解析してください。"); }
}
void PitchRollPanel::updateSize() { grid.setSize(jmax(viewport.getViewWidth(), int(std::ceil(snapshot.length * grid.pixels)) + 8), (PitchRollGrid::highest - PitchRollGrid::lowest + 1) * grid.row); }
void PitchRollPanel::zoomVertical(double multiplier, double viewY) {
    if (!std::isfinite(multiplier) || grid.interacting() || seeking || selectingLoop) { verticalZoom.setValue(double(grid.row) / PitchRollGrid::defaultRow, dontSendNotification); return; }
    const double anchor = viewY < 0 ? viewport.getViewHeight() * 0.5 : jlimit(0.0, double(viewport.getViewHeight()), viewY);
    const double pitchRow = (viewport.getViewPositionY() + anchor) / grid.row;
    const int x = viewport.getViewPositionX();
    grid.row = int(std::round(PitchRollGrid::defaultRow * jlimit(0.5, 32.0, multiplier)));
    updateSize(); viewport.setViewPosition(x, int(std::round(pitchRow * grid.row - anchor)));
    verticalZoom.setValue(double(grid.row) / PitchRollGrid::defaultRow, dontSendNotification); repaint(); grid.repaint();
}
void PitchRollPanel::fit() {
    if (grid.interacting() || seeking || selectingLoop) return;
    grid.row = PitchRollGrid::defaultRow; verticalZoom.setValue(1, dontSendNotification);
    grid.pixels = jlimit(0.25, 1200.0, jmax(1, viewport.getWidth() - 24) / jmax(0.12, snapshot.length)); updateSize();
    double centre = 60; if (!notes.empty()) { centre = 0; for (const auto& n : notes) centre += n.target; centre /= notes.size(); }
    viewport.setViewPosition(0, jmax(0, int((PitchRollGrid::highest - centre + 0.5) * grid.row - viewport.getHeight() / 2))); repaint();
}
void PitchRollPanel::resized() {
    int x = 12;
    for (auto entry : {std::pair<TextButton*, int>{&play, 126}, {&stop, 56}, {&applyButton, 72}, {&undoButton, 80}, {&discardButton, 110}, {&fitButton, 82}, {&reloadButton, 82}, {&restoreButton, 90}}) {
        entry.first->setBounds(x, 12, entry.second, 28); x += entry.second + 8;
    }
    viewport.setBounds(keyWidth, bodyTop, jmax(0, getWidth() - keyWidth), jmax(0, getHeight() - bodyTop - 28)); updateSize();
    dragAudition.setBounds(getWidth() - 143, 43, 135, 23);
    curveEdit.setBounds(getWidth() - 265, 43, 120, 23);
    rangeLoop.setBounds(getWidth() - 112, 12, 108, 28);
    verticalZoom.setBounds(getWidth() - 202, getHeight() - 26, 194, 23);
}
void PitchRollPanel::paint(Graphics& g) {
    g.fillAll(bg); g.setColour(panelColour); g.fillRect(0, 0, getWidth(), bodyTop);
    const Colour state = pending ? Colour(0xffffbd79) : snapshot.pitchOriginal ? mint : Colour(0xffa4b4c2);
    g.setColour(state.withAlpha(0.14f)); g.fillRoundedRectangle(12, 43, 116, 23, 4);
    g.setColour(state); g.setFont(12);
    g.drawText(pending ? String::fromUTF8("未適用の変更") : snapshot.pitchOriginal ? String::fromUTF8("音声に適用済み") : String::fromUTF8("元の録音"), 20, 43, 100, 23, Justification::centredLeft);
    g.setColour(ink); g.drawText(message, 140, 44, jmax(0, getWidth() - 425), 21, Justification::centredLeft);
    if (getWidth() >= 1020) { g.setColour(Colour(0xffa4b4c2)); g.setFont(11); g.drawText("A4=" + String(snapshot.pitchSettings.referenceHz, 1) + " Hz", 782, 12, 114, 28, Justification::centredLeft); }
    const int viewY = viewport.getViewPositionY(), viewX = viewport.getViewPositionX();
    { Graphics::ScopedSaveState save(g); g.reduceClipRegion(0, bodyTop, keyWidth, viewport.getHeight());
      for (int n = PitchRollGrid::lowest; n <= PitchRollGrid::highest; ++n) {
        const int y = bodyTop + (PitchRollGrid::highest - n) * grid.row - viewY;
        if (y + grid.row < bodyTop || y > bodyTop + viewport.getHeight()) continue;
        const bool black = MidiMessage::isMidiNoteBlack(n); g.setColour(black ? Colour(0xff303a46) : Colour(0xffb9c5d0));
        g.fillRect(0, y, keyWidth - 1, grid.row - 1); g.setColour(black ? ink : bg); g.setFont(11);
        g.drawText(MidiMessage::getMidiNoteName(n, true, true, 4), 7, y, keyWidth - 10, grid.row, Justification::centredLeft);
        if (grid.row >= 110) {
            const int step = grid.row >= 440 ? 5 : 10; g.setFont(9);
            for (int cents = -40; cents <= 40; cents += step) if (cents != 0) {
                const int tick = int(std::round(y + grid.row * (0.5 - cents / 100.0)));
                g.drawText((cents > 0 ? "+" : "") + String(cents) + "c", 9, tick - 5, keyWidth - 16, 10, Justification::centredLeft);
            }
        }
      }
    }
    double step = 0.1; while (step * grid.pixels < 60) step *= 2;
    g.setColour(ink); g.setFont(11);
    for (double s = std::floor(viewX / grid.pixels / step) * step; s * grid.pixels < viewX + viewport.getViewWidth(); s += step)
        g.drawText(String(s, step < 1 ? 1 : 0) + "s", keyWidth + int(s * grid.pixels) - viewX + 4, rulerTop, 65, rangeTop - rulerTop, Justification::centredLeft);
    g.setColour(Colour(0xff101c20)); g.fillRect(0, rangeTop, getWidth(), bodyTop - rangeTop);
    g.setColour(rangeLoop.getToggleState() ? mint : Colour(0xff8d9cab)); g.drawText("LOOP", 8, rangeTop, keyWidth - 12, bodyTop - rangeTop, Justification::centredLeft);
    { Graphics::ScopedSaveState save(g); g.reduceClipRegion(keyWidth, rangeTop, getWidth() - keyWidth, bodyTop - rangeTop);
      if (loopRange.getLength() > 0) {
          const float a = float(keyWidth + loopRange.getStart() * grid.pixels - viewX), b = float(keyWidth + loopRange.getEnd() * grid.pixels - viewX);
          const auto colour = rangeLoop.getToggleState() || selectingLoop ? mint : Colour(0xff8297ba);
          const Rectangle<float> r(a, float(rangeTop + 2), b - a, float(bodyTop - rangeTop - 4));
          g.setColour(colour.withAlpha(0.25f)); g.fillRect(r); g.setColour(colour); g.drawRect(r, 1);
          g.fillRect(a, float(rangeTop + 2), 3.0f, float(bodyTop - rangeTop - 4)); g.fillRect(b - 3, float(rangeTop + 2), 3.0f, float(bodyTop - rangeTop - 4));
          if (b - a > 120) g.drawText(String(loopRange.getStart(), 3) + " - " + String(loopRange.getEnd(), 3) + " s", r.reduced(7, 0), Justification::centredLeft);
      } else { g.setColour(Colour(0xff8d9cab)); g.drawText(String::fromUTF8("ここをドラッグして音程調整用のループ範囲を指定"), keyWidth + 8, rangeTop, getWidth() - keyWidth - 16, bodyTop - rangeTop, Justification::centredLeft); }
    }
    const float marker = float(keyWidth + playheadX() - viewX);
    if (marker >= keyWidth && marker < getWidth() - 15) {
        g.setColour(Colour(0xffffe1a3)); g.drawVerticalLine(int(marker), rulerTop, bodyTop);
        Path triangle; triangle.addTriangle(marker - 4, float(rulerTop), marker + 4, float(rulerTop), marker, float(rulerTop + 7)); g.fillPath(triangle);
    }
    g.setColour(Colour(0xff8d9cab)); g.setFont(11);
    g.drawText(curveEdit.getToggleState() ? String::fromUTF8("線をドラッグ / Ctrl: 微調整 / Ctrl+Shift+ホイール: 縦ズーム / Esc: 取消 / 適用で反映") : String::fromUTF8("ドラッグ: 半音 / Ctrl: セント / Ctrl+Shift+ホイール: 縦ズーム / Space: 再生"), 12, getHeight() - 25, getWidth() - 282, 23, Justification::centredLeft);
    g.drawText(String::fromUTF8("縦ズーム"), getWidth() - 266, getHeight() - 25, 62, 23, Justification::centredRight);
}
void PitchRollPanel::timerCallback() {
    if (!isShowing()) suspendPlayback();
    else if (!owner || owner->busy || owner->engine.recording) endAudition(true);
    syncCurrent(); const bool canEdit = owner && !owner->busy && !owner->engine.recording && resolve() && !stale;
    if (!canEdit) endAudition(true);
    applyButton.setEnabled(canEdit && pending); play.setEnabled(canEdit); stop.setEnabled(owner && !owner->busy);
    restoreButton.setEnabled(canEdit && !selected.empty()); discardButton.setEnabled(owner && !owner->busy && pending);
    undoButton.setEnabled(canEdit); reloadButton.setEnabled(owner && !owner->busy && !owner->engine.recording && !pending && resolve());
    grid.setEnabled(canEdit); play.setButtonText(pending ? String::fromUTF8("適用して再生") : String::fromUTF8("再生"));
    applyButton.setColour(TextButton::buttonColourId, pending ? Colour(0xff725136) : Colour(0xff29333e));
    applyButton.setTooltip(pending ? String::fromUTF8("未適用の編集を音声へ反映します。Ctrl+Enter。反映後もプロジェクトのCtrl+Zで戻せます。") : String::fromUTF8("適用待ちの変更はありません。音やカーブを編集すると適用できます。"));
    play.setTooltip(pending ? String::fromUTF8("未適用の編集を音声へ反映してから再生します。Space。") : String::fromUTF8("現在の音声を再生します。Spaceで再生・停止。"));
    rangeLoop.setEnabled(canEdit && loopRange.getLength() >= 0.02 - 1e-9);
    if (rangeLoop.getToggleState() && !selectingLoop && owner) {
        if (!resolve() || stale || owner->engine.recording || !owner->engine.ownsPitchLoop(auditionId)) { rangeLoop.setToggleState(false, dontSendNotification); owner->engine.clearPitchLoop(auditionId); }
        else if (!owner->busy) updateLoop();
    }
    followTransport();
    repaint(); grid.repaint();
}
void PitchRollPanel::followTransport() {
    const int viewX = viewport.getViewPositionX(), width = viewport.getViewWidth();
    const double now = Time::getMillisecondCounterHiRes();
    if (viewX != followViewX || viewport.getHorizontalScrollBar().isMouseButtonDown()) followResumeAt = now + 2000;
    followViewX = viewX;
    if (!owner || !isShowing() || !owner->followPlayhead || owner->busy || stale || owner->engine.exporting
        || (!owner->engine.playing && !owner->engine.recording) || owner->engine.countInRemaining > 0
        || owner->engine.pitchPreviewActive || seeking || selectingLoop || grid.interacting() || now < followResumeAt || width <= 0) return;
    const double local = owner->engine.cursor.load() - snapshot.start;
    if (!std::isfinite(local) || local < 0 || local > snapshot.length) return;
    const double marker = local * grid.pixels;
    if (marker >= viewX && marker < viewX + width * 0.9) return;
    viewport.setViewPosition(jmax(0, int(std::floor(marker - width * 0.2))), viewport.getViewPositionY());
    followViewX = viewport.getViewPositionX();
}
void PitchRollPanel::seek(float x) {
    if (!owner || owner->busy || owner->engine.recording || !resolve()) return;
    endAudition(true); seekPosition = jlimit(0.0, snapshot.length, (x - keyWidth + viewport.getViewPositionX()) / grid.pixels);
    owner->engine.seek(snapshot.start + seekPosition);
}
void PitchRollPanel::mouseDown(const MouseEvent& e) {
    if (e.mods.isPopupMenu()) { mouseDoubleClick(e); return; }
    if (e.mods.isLeftButtonDown() && e.y >= rangeTop && e.y < bodyTop && e.x >= keyWidth) {
        if (!owner || owner->busy || owner->engine.recording || stale || !resolve()) return;
        if (grid.isShowing()) grid.grabKeyboardFocus(); endAudition(true); selectingLoop = true; loopBefore = loopRange;
        loopAnchor = jlimit(0.0, snapshot.length, (e.position.x - keyWidth + viewport.getViewPositionX()) / grid.pixels);
        const double a = keyWidth + loopRange.getStart() * grid.pixels - viewport.getViewPositionX(), b = keyWidth + loopRange.getEnd() * grid.pixels - viewport.getViewPositionX();
        if (loopRange.getLength() >= 0.02 && std::abs(e.position.x - a) < 7) loopAnchor = loopRange.getEnd();
        else if (loopRange.getLength() >= 0.02 && std::abs(e.position.x - b) < 7) loopAnchor = loopRange.getStart();
        dragLoop(e.position.x); return;
    }
    seeking = e.mods.isLeftButtonDown() && e.y >= rulerTop && e.y < rangeTop && e.x >= keyWidth;
    if (seeking) { if (grid.isShowing()) grid.grabKeyboardFocus(); seek(e.position.x); }
}
void PitchRollPanel::mouseDrag(const MouseEvent& e) { if (selectingLoop) dragLoop(e.position.x); else if (seeking) seek(e.position.x); }
void PitchRollPanel::mouseUp(const MouseEvent& e) { if (selectingLoop) { dragLoop(e.position.x); finishLoop(false); timerCallback(); } else if (seeking) { seek(e.position.x); finishSeek(); } }
void PitchRollPanel::finishSeek() { if (seeking && owner && !owner->busy && !owner->engine.recording) owner->engine.seek(snapshot.start + seekPosition); seeking = false; }
double PitchRollPanel::playheadX() const { return (seeking ? seekPosition : owner ? owner->engine.cursor.load() - snapshot.start : 0) * grid.pixels; }
void PitchRollPanel::focusLost(FocusChangeType) { if (!hasKeyboardFocus(true)) { endAudition(true); finishSeek(); finishLoop(true); } }
void PitchRollPanel::suspendPlayback() { endAudition(true); finishSeek(); finishLoop(true); rangeLoop.setToggleState(false, dontSendNotification); if (owner) owner->engine.clearPitchLoop(auditionId); }
void PitchRollPanel::visibilityChanged() { if (!isShowing()) suspendPlayback(); }
void PitchRollPanel::mouseWheelMove(const MouseEvent& e, const MouseWheelDetails& wheel) {
    if (e.mods.isCommandDown() && (e.mods.isShiftDown() || (e.x < keyWidth && e.y >= bodyTop))) {
        zoomVertical(double(grid.row) / PitchRollGrid::defaultRow * std::exp(jlimit(-0.7, 0.7, double(wheel.deltaY) * 2.5)), e.position.y - bodyTop); return;
    }
    Component::mouseWheelMove(e, wheel);
}
void PitchRollPanel::beginAudition(int index, double time) {
    if (!owner || owner->busy || owner->engine.recording || stale || !dragAudition.getToggleState() || !isPositiveAndBelow(index, int(notes.size()))) return;
    auto* current = resolve(); if (!current) return; const auto& note = notes[size_t(index)];
    const auto source = current->pitchOriginal ? current->pitchOriginal : current->source;
    if (owner->engine.startPitchPreview(auditionId, trackId, source, note.start, note.end, note.target - note.original, current->gainDb, time, auditionCurve(note))) auditionNote = index;
}
void PitchRollPanel::updateAudition() {
    if (owner && isPositiveAndBelow(auditionNote, int(notes.size())) && dragAudition.getToggleState()) {
        const auto& note = notes[size_t(auditionNote)];
        if (note.scoop.enabled || !note.curve.empty()) owner->engine.updatePitchPreviewCurve(auditionId, auditionCurve(note));
        else owner->engine.updatePitchPreview(auditionId, note.target - note.original);
    }
}
void PitchRollPanel::endAudition(bool immediate) { if (owner) owner->engine.stopPitchPreview(auditionId, immediate); auditionNote = -1; }
void PitchRollPanel::remember() { undo.push_back(notes); if (undo.size() > 80) undo.erase(undo.begin()); redo.clear(); }
void PitchRollPanel::showSelectedMessage() {
    if (selected.empty()) message = notes.empty() ? String::fromUTF8("音程を検出できませんでした。単音の録音で試してください。")
        : curveEdit.getToggleState() ? String::fromUTF8("緑の線をドラッグして編集。反映には「適用」。") : String::fromUTF8("音を選んで上下にドラッグ。Ctrlで微調整。反映には「適用」。");
    else if (selected.size() == 1 && isPositiveAndBelow(*selected.begin(), int(notes.size()))) {
        const auto& n = notes[size_t(*selected.begin())]; message = pitchName(n.original) + "  ->  " + pitchName(n.target) + "  (" + String(n.start - snapshot.offset, 2) + "s)";
    } else message = String(selected.size()) + String::fromUTF8(" 音を選択");
}
void PitchRollPanel::changed() { pending = notes != baseline; showSelectedMessage(); grid.repaint(); timerCallback(); }
void PitchRollPanel::showCommitRequired() {
    message = String::fromUTF8("先に「適用」または「変更を破棄」を選ぶと、保存・書き出し・終了へ進めます。"); repaint();
}
void PitchRollPanel::discard() { if (!owner || owner->busy) return; notes = baseline; pending = false; undo.clear(); redo.clear(); syncCurrent(); changed(); }
void PitchRollPanel::undoLocal() {
    if (!owner || owner->busy || owner->engine.recording) return;
    if (undo.empty()) { if (!pending) { owner->undoEdit(); syncCurrent(); } }
    else { redo.push_back(notes); notes = std::move(undo.back()); undo.pop_back(); selected.clear(); changed(); }
}
void PitchRollPanel::redoLocal() { if (owner && !owner->busy && !owner->engine.recording && !redo.empty()) { undo.push_back(notes); notes = std::move(redo.back()); redo.pop_back(); selected.clear(); changed(); } }
void PitchRollPanel::moveSelected(double delta) {
    if (!owner || owner->busy || owner->engine.recording || stale || selected.empty() || !std::isfinite(delta)) return;
    bool different = false;
    for (int i : selected) if (isPositiveAndBelow(i, int(notes.size()))) { const auto& n = notes[size_t(i)]; const double target = jlimit(jmax(24.0, n.original - 12), jmin(96.0, n.original + 12), std::round((n.target + delta) * 100) / 100); different |= !n.edited || n.target != target; }
    if (!different) return;
    remember(); for (int i : selected) if (isPositiveAndBelow(i, int(notes.size()))) {
        auto& n = notes[size_t(i)]; n.target = jlimit(jmax(24.0, n.original - 12), jmin(96.0, n.original + 12), std::round((n.target + delta) * 100) / 100); n.edited = true;
        for (auto& point : n.curve) point.note = jlimit(24.0, 96.0, point.note + n.target - undo.back()[size_t(i)].target);
        if (n.scoop.enabled) n.scoop.depthCents = jmin(n.scoop.depthCents, (n.target - 24) * 100);
    } changed();
}
void PitchRollPanel::restoreSelected() {
    if (!owner || owner->busy || owner->engine.recording || stale || selected.empty()) return;
    bool different = false; for (int i : selected) if (isPositiveAndBelow(i, int(notes.size()))) different |= !notes[size_t(i)].edited || notes[size_t(i)].target != notes[size_t(i)].original || notes[size_t(i)].scoop.enabled || !notes[size_t(i)].curve.empty();
    if (!different) return;
    remember(); for (int i : selected) if (isPositiveAndBelow(i, int(notes.size()))) { notes[size_t(i)].target = notes[size_t(i)].original; notes[size_t(i)].edited = true; notes[size_t(i)].scoop = {}; notes[size_t(i)].curve.clear(); } changed();
}
void PitchRollPanel::snapSelected() {
    if (!owner || owner->busy || owner->engine.recording || stale || selected.empty()) return;
    bool different = false; for (int i : selected) if (isPositiveAndBelow(i, int(notes.size()))) different |= !notes[size_t(i)].edited || notes[size_t(i)].target != std::round(notes[size_t(i)].target);
    if (!different) return;
    remember(); for (int i : selected) if (isPositiveAndBelow(i, int(notes.size()))) { auto& n = notes[size_t(i)]; const double delta = std::round(n.target) - n.target; n.target += delta; n.edited = true; for (auto& point : n.curve) point.note = jlimit(24.0, 96.0, point.note + delta); if (n.scoop.enabled) n.scoop.depthCents = jmin(n.scoop.depthCents, (n.target - 24) * 100); } changed();
}
bool PitchRollPanel::splitSelected(double time) {
    if (!owner || owner->busy || owner->engine.recording || stale || selected.size() != 1) return false;
    const int index = *selected.begin(); if (!isPositiveAndBelow(index, int(notes.size()))) return false;
    auto n = notes[size_t(index)]; if (time <= n.start + 0.03 || time >= n.end - 0.03) return false;
    remember(); notes[size_t(index)].end = time; notes[size_t(index)].edited = true;
    if (notes[size_t(index)].scoop.enabled && notes[size_t(index)].scoop.start >= time) notes[size_t(index)].scoop = {};
    n.start = time; n.edited = true;
    notes.insert(notes.begin() + index + 1, n); selected = {index + 1}; changed(); return true;
}
PitchCorrectionSettings PitchRollPanel::settings() const {
    auto result = snapshot.pitchSettings;
    if (!snapshot.pitchOriginal) { result.manual = true; result.amount = 1; result.speedMs = 0; result.maxCents = 1200; }
    result.notes.clear();
    for (auto n : snapshot.pitchSettings.notes) {
        if (n.end <= snapshot.offset || n.start >= snapshot.offset + snapshot.length) result.notes.push_back(n);
        else { if (n.start < snapshot.offset) { auto left = n; left.end = snapshot.offset; if (left.scoop.enabled && left.scoop.start >= left.end) left.scoop = {}; result.notes.push_back(left); } if (n.end > snapshot.offset + snapshot.length) { n.start = snapshot.offset + snapshot.length; result.notes.push_back(n); } }
    }
    for (const auto& n : notes) if (n.edited) result.notes.push_back({n.start, n.end, n.original, n.target, n.scoop, n.curve});
    std::sort(result.notes.begin(), result.notes.end(), [](const auto& a, const auto& b) { return a.start < b.start; }); return result;
}
bool PitchRollPanel::apply(bool playAfter) {
    if (!owner || owner->busy || owner->engine.recording) return false; syncCurrent(); if (stale || !resolve()) return false;
    if (grid.interacting() || selectingLoop) return false;
    endAudition(true); finishSeek();
    const double cursor = owner->engine.cursor.load();
    double time = cursor >= snapshot.start && cursor <= snapshot.start + snapshot.length ? cursor : snapshot.start;
    if (rangeLoop.getToggleState() && (time < snapshot.start + loopRange.getStart() || time >= snapshot.start + loopRange.getEnd())) time = snapshot.start + loopRange.getStart();
    if (!pending) { if (playAfter) { owner->engine.seek(time); owner->engine.play(); } return false; }
    return owner->startPitchCorrection(trackId, clipId, settings(), [safe = Component::SafePointer<PitchRollPanel>(this), playAfter, time] {
        if (!safe) return; if (auto* current = safe->resolve()) {
            safe->snapshot = *current; safe->notes = pitchSegments(*current, safe->frames); safe->baseline = safe->notes;
            safe->pending = safe->stale = false; safe->undo.clear(); safe->redo.clear(); safe->message = String::fromUTF8("編集を適用しました。プロジェクトのCtrl+Zで戻せます。");
            if (playAfter && safe->owner) { safe->owner->engine.seek(time); safe->owner->engine.play(); } safe->timerCallback();
        }
    });
}
void PitchRollPanel::contextMenu(int, double time) {
    PopupMenu menu; menu.addItem(1, String::fromUTF8("音程を数値指定...")); menu.addItem(2, String::fromUTF8("最寄りの半音へ"));
    menu.addItem(3, String::fromUTF8("元の音程へ")); menu.addSeparator(); menu.addItem(4, String::fromUTF8("ここで音を分割"), selected.size() == 1);
    menu.addItem(5, String::fromUTF8("しゃくりを調整..."), selected.size() == 1);
    menu.addItem(6, String::fromUTF8("選択音をまとめる"), selected.size() > 1);
    bool hasCurve = false; for (int i : selected) if (isPositiveAndBelow(i, int(notes.size()))) hasCurve |= !notes[size_t(i)].curve.empty();
    menu.addItem(7, String::fromUTF8("カーブを滑らかに"), hasCurve);
    menu.showMenuAsync(PopupMenu::Options(), [safe = Component::SafePointer<PitchRollPanel>(this), time](int id) {
        if (!safe) return; if (id == 1) MessageManager::callAsync([safe] { if (safe) safe->numericPitch(); });
        else if (id == 2) safe->snapSelected(); else if (id == 3) safe->restoreSelected(); else if (id == 4) safe->splitSelected(time);
        else if (id == 5) MessageManager::callAsync([safe] { if (safe) safe->editScoop(); });
        else if (id == 6) safe->mergeSelected();
        else if (id == 7) safe->smoothSelectedCurves();
    });
}
void PitchRollPanel::numericPitch() {
    if (!owner || owner->busy || owner->engine.recording || stale || selected.size() != 1 || numericPrompt) return;
    const int index = *selected.begin(); if (!isPositiveAndBelow(index, int(notes.size()))) return;
    const auto note = notes[size_t(index)]; auto* dialog = new AlertWindow(String::fromUTF8("音程を指定"), String::fromUTF8("この音の音程を指定します。元の音程から±12半音まで調整できます。"), MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel(&getLookAndFeel()); StringArray names; for (int n = 24; n <= 96; ++n) names.add(MidiMessage::getMidiNoteName(n, true, true, 4));
    dialog->addComboBox("note", names, String::fromUTF8("音名")); dialog->getComboBoxComponent("note")->setSelectedItemIndex(int(std::round(note.target)) - 24, dontSendNotification);
    dialog->addTextEditor("cents", String((note.target - std::round(note.target)) * 100, 1), String::fromUTF8("セント（−50〜＋50）"));
    dialog->addButton(String::fromUTF8("設定"), 1, KeyPress(KeyPress::returnKey)); dialog->addButton(String::fromUTF8("キャンセル"), 0, KeyPress(KeyPress::escapeKey)); numericPrompt = dialog;
    dialog->enterModalState(true, ModalCallbackFunction::create([safe = Component::SafePointer<PitchRollPanel>(this), dialog, index, note](int result) {
        if (!safe) return; safe->numericPrompt = nullptr; if (result != 1 || !safe->owner || safe->owner->busy || safe->owner->engine.recording || safe->stale) return;
        const auto text = dialog->getTextEditorContents("cents").trim(); const auto utf8 = text.toUTF8(); char* end = nullptr; const double cents = std::strtod(utf8.getAddress(), &end);
        const double target = dialog->getComboBoxComponent("note")->getSelectedItemIndex() + 24 + cents / 100;
        if (text.isEmpty() || end == utf8.getAddress() || *end != 0 || !std::isfinite(cents) || std::abs(cents) > 50 || target < 24 || target > 96 || std::abs(target - note.original) > 12 || (note.scoop.enabled && target - note.scoop.depthCents / 100 < 24)) { safe->message = String::fromUTF8("音程とセントの範囲を確認してください。"); safe->repaint(); return; }
        if (!isPositiveAndBelow(index, int(safe->notes.size())) || safe->notes[size_t(index)].start != note.start || safe->notes[size_t(index)].end != note.end) return;
        safe->remember(); auto& n = safe->notes[size_t(index)]; for (auto& point : n.curve) point.note = jlimit(24.0, 96.0, point.note + target - n.target); n.target = target; n.edited = true; safe->changed();
    }), true);
}
PitchRollWindow::PitchRollWindow(Workstation& w, String track, String clip, const Clip& before, std::vector<PitchFrame> frames)
    : DocumentWindow(String::fromUTF8("音程エディター — ") + before.name, bg, DocumentWindow::allButtons), clipId(clip), owner(&w) {
    setLookAndFeel(&w.getLookAndFeel()); setUsingNativeTitleBar(true);
    content = new PitchRollPanel(w, std::move(track), std::move(clip), before, std::move(frames)); setContentOwned(content, true);
    setResizable(true, false); setResizeLimits(880, 480, 1900, 1300); centreWithSize(1100, 720); setVisible(true); content->grabKeyboardFocus();
}
PitchRollWindow::~PitchRollWindow() { if (closePrompt) { closePrompt->setLookAndFeel(nullptr); closePrompt->exitModalState(0); } clearContentComponent(); setLookAndFeel(nullptr); }
void PitchRollWindow::closeButtonPressed() {
    content->endAudition(true);
    if (!owner || owner->busy) return;
    if (!content->hasPendingEdits()) { setVisible(false); return; }
    if (closePrompt) { closePrompt->toFront(true); return; }
    auto* dialog = new AlertWindow(String::fromUTF8("未適用の音程編集"), String::fromUTF8("編集を破棄して閉じますか？ 音声へ反映するには、戻って「適用」を押してください。"), MessageBoxIconType::QuestionIcon, this);
    dialog->setLookAndFeel(&getLookAndFeel()); dialog->addButton(String::fromUTF8("破棄して閉じる"), 1); dialog->addButton(String::fromUTF8("戻る"), 0, KeyPress(KeyPress::escapeKey)); closePrompt = dialog;
    dialog->enterModalState(true, ModalCallbackFunction::create([safe = Component::SafePointer<PitchRollWindow>(this)](int result) {
        if (!safe) return; safe->closePrompt = nullptr; if (result == 1) { safe->content->discard(); safe->setVisible(false); }
    }), true);
}
void PitchRollWindow::activeWindowStatusChanged() { if (!isActiveWindow() && content) content->endAudition(true); }
void PitchRollWindow::visibilityChanged() { DocumentWindow::visibilityChanged(); if (content && !isShowing()) content->suspendPlayback(); }
}
