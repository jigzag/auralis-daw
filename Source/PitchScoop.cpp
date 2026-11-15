#include "PitchRoll.h"
#include <cmath>

namespace auralis {
namespace {
class ScoopControls : public Component, public Timer {
public:
    ScoopControls(PitchScoop value, double length, double maximumDepth, std::function<void(bool, PitchScoop)> audition)
        : initial(value), onAudition(std::move(audition)) {
        auto slider = [&](Slider& s, Label& label, const char* id, const String& title, double lo, double hi, double step, double current, const String& suffix) {
            addAndMakeVisible(s); addAndMakeVisible(label); label.setText(title, dontSendNotification);
            s.setComponentID(id); s.setRange(lo, hi, step); s.setValue(current, dontSendNotification); s.setTextValueSuffix(suffix);
            s.setSliderStyle(Slider::LinearHorizontal); s.setTextBoxStyle(Slider::TextBoxRight, false, 85, 25); s.onValueChange = [this] { changed(); };
        };
        slider(depth, depthLabel, "scoopDepth", String::fromUTF8("深さ"), 0, jmax(1.0, maximumDepth), 1, value.depthCents, " cent");
        slider(duration, durationLabel, "scoopDuration", String::fromUTF8("長さ"), 20, jmax(21.0, jmin(2000.0, length)), 1, value.durationMs, " ms");
        slider(curve, curveLabel, "scoopCurve", String::fromUTF8("立ち上がり"), 0.25, 4, 0.05, value.curve, " x");
        curve.setTooltip(String::fromUTF8("1は均等。大きいほど早く目標音程に到達し、小さいほどゆっくり上がります。"));
        addAndMakeVisible(enabled); enabled.setComponentID("scoopEnabled"); enabled.setButtonText(String::fromUTF8("しゃくりを調整（OFFで元のまま）"));
        enabled.setToggleState(value.enabled, dontSendNotification); enabled.onClick = [this] { changed(); };
        addAndMakeVisible(preview); preview.setComponentID("scoopPreview"); preview.setClickingTogglesState(true);
        preview.onClick = [this] { changed(); }; changed(); setSize(520, 275); startTimerHz(10);
    }
    ~ScoopControls() override { stopTimer(); }
    PitchScoop value() const { auto result = initial; result.enabled = enabled.getToggleState(); result.depthCents = depth.getValue(); result.durationMs = duration.getValue(); result.curve = curve.getValue(); return result; }
    void resized() override {
        enabled.setBounds(0, 0, 355, 26); preview.setBounds(getWidth() - 145, 0, 145, 26);
        int y = 36; for (auto pair : {std::pair<Label*, Slider*>{&depthLabel, &depth}, {&durationLabel, &duration}, {&curveLabel, &curve}}) {
            pair.first->setBounds(0, y, 88, 30); pair.second->setBounds(88, y, getWidth() - 88, 30); y += 36;
        }
    }
    void paint(Graphics& g) override {
        const auto v = value(); const Rectangle<float> graph(12, 156, float(getWidth() - 24), 83);
        g.setColour(Colour(0xff11151b)); g.fillRoundedRectangle(graph, 4);
        g.setColour(Colour(0xff8297ba)); g.drawHorizontalLine(int(graph.getY() + 12), graph.getX() + 8, graph.getRight() - 8);
        if (v.enabled) {
            Path path; for (int i = 0; i <= 100; ++i) {
                const float x = graph.getX() + 8 + (graph.getWidth() - 16) * i / 100;
                const float y = graph.getY() + 12 + float(58 * v.depthCents / jmax(100.0, v.depthCents) * std::pow(1 - i / 100.0, v.curve));
                if (i == 0) path.startNewSubPath(x, y); else path.lineTo(x, y);
            }
            g.setColour(Colour(0xff80e3c0)); g.strokePath(path, PathStrokeType(2));
        }
        g.setColour(Colour(0xffb9c5d0)); g.setFont(12);
        g.drawText(String::fromUTF8("深さ0で出だしを平らに。設定後はエディターの「適用」で反映。"), 0, 245, getWidth(), 24, Justification::centredLeft);
    }
    void timerCallback() override {
        if (preview.getToggleState() && (!isShowing() || !Process::isForegroundProcess() || !hasKeyboardFocus(true))) {
            preview.setToggleState(false, dontSendNotification); changed();
        }
    }
private:
    PitchScoop initial;
    std::function<void(bool, PitchScoop)> onAudition;
    Slider depth, duration, curve;
    Label depthLabel, durationLabel, curveLabel;
    ToggleButton enabled;
    TextButton preview;
    void changed() {
        depth.setEnabled(enabled.getToggleState()); duration.setEnabled(enabled.getToggleState()); curve.setEnabled(enabled.getToggleState());
        preview.setButtonText(preview.getToggleState() ? String::fromUTF8("試聴を停止") : String::fromUTF8("ループ試聴"));
        onAudition(preview.getToggleState(), value()); repaint();
    }
};
}

std::shared_ptr<const PitchPreviewCurve> PitchRollPanel::auditionCurve(const PitchSegment& n) const {
    if (!n.scoop.enabled && n.curve.empty()) return {};
    auto curve = std::make_shared<PitchPreviewCurve>();
    for (const auto& frame : frames) {
        const double time = snapshot.offset + frame.seconds;
        if (time >= n.start && time < n.end && frame.voiced) curve->push_back({time, pitchEditShift(frame.note, time, n)});
    }
    if (curve->empty()) curve->push_back({n.start, n.target - n.original});
    return curve;
}

bool PitchRollPanel::mergeSelected() {
    if (!owner || owner->busy || owner->engine.recording || stale || selected.size() < 2 || grid.interacting()) return false;
    const int first = *selected.begin(), last = *selected.rbegin();
    if (first < 0 || last >= int(notes.size()) || last - first + 1 != int(selected.size())) return false;
    int longest = first;
    for (int i = first + 1; i <= last; ++i) {
        if (notes[size_t(i)].start - notes[size_t(i - 1)].end > 0.08) { message = String::fromUTF8("離れた音はまとめられません。隣り合う音を選んでください。"); repaint(); return false; }
        if (notes[size_t(i)].end - notes[size_t(i)].start > notes[size_t(longest)].end - notes[size_t(longest)].start) longest = i;
    }
    PitchSegment merged; merged.start = notes[size_t(first)].start; merged.end = notes[size_t(last)].end; merged.edited = true;
    std::vector<double> measured;
    for (const auto& frame : frames) if (frame.voiced && snapshot.offset + frame.seconds >= merged.start && snapshot.offset + frame.seconds < merged.end) measured.push_back(frame.note);
    if (measured.empty()) return false;
    std::sort(measured.begin(), measured.end()); merged.original = measured[measured.size() / 2];
    merged.target = jlimit(24.0, 96.0, merged.original + notes[size_t(longest)].target - notes[size_t(longest)].original);
    bool shaped = false; for (int i = first; i <= last; ++i) shaped |= notes[size_t(i)].scoop.enabled || !notes[size_t(i)].curve.empty();
    if (shaped) for (const auto& frame : frames) {
        const double time = snapshot.offset + frame.seconds; if (!frame.voiced) continue;
        for (int i = first; i <= last; ++i) if (time >= notes[size_t(i)].start && time < notes[size_t(i)].end) {
            merged.curve.push_back({time, jlimit(24.0, 96.0, frame.note + (notes[size_t(i)].edited ? pitchEditShift(frame.note, time, notes[size_t(i)]) : 0))}); break;
        }
    }
    endAudition(true); remember(); notes.erase(notes.begin() + first, notes.begin() + last + 1); notes.insert(notes.begin() + first, merged); selected = {first}; changed(); return true;
}

void PitchRollPanel::editScoop() {
    if (!owner || owner->busy || owner->engine.recording || stale || selected.size() != 1 || scoopPrompt || grid.interacting()) return;
    const int index = *selected.begin(); if (!isPositiveAndBelow(index, int(notes.size()))) return;
    endAudition(true); const auto before = notes[size_t(index)]; auto initial = before.scoop;
    if (!initial.enabled) {
        initial.enabled = true; initial.start = before.start; initial.depthCents = 0;
        double peak = 0; for (const auto& frame : frames) if (frame.voiced && snapshot.offset + frame.seconds >= before.start && snapshot.offset + frame.seconds < before.end) peak = jmax(peak, frame.level);
        // Pitch windows can detect a voice before its audible attack. Anchor the bend to actual note energy.
        for (const auto& frame : frames) if (frame.voiced && frame.level >= jmax(0.003, peak * 0.1) && snapshot.offset + frame.seconds >= before.start && snapshot.offset + frame.seconds < before.end) { initial.start = snapshot.offset + frame.seconds; break; }
        initial.durationMs = jlimit(20.0, 150.0, (before.end - initial.start) * 1000);
        for (const auto& frame : frames) if (frame.voiced && snapshot.offset + frame.seconds >= initial.start && snapshot.offset + frame.seconds < initial.start + 0.05)
            initial.depthCents = jmax(initial.depthCents, jlimit(0.0, 1200.0, (before.original - frame.note) * 100));
    }
    auto* dialog = new AlertWindow(String::fromUTF8("しゃくりを調整"), before.curve.empty() ? String::fromUTF8("音の出だしだけを調整します。深さ・長さ・立ち上がりを変えてループ試聴できます。") : String::fromUTF8("しゃくりを設定すると、この音の手描きカーブを置き換えます。キャンセルで元のカーブを保ちます。"), MessageBoxIconType::NoIcon, this);
    dialog->setLookAndFeel(&getLookAndFeel()); scoopPrompt = dialog;
    const auto safe = Component::SafePointer<PitchRollPanel>(this);
    auto controls = std::make_shared<ScoopControls>(initial, (before.end - initial.start) * 1000, jmin(1200.0, (before.target - 24) * 100), [safe, index, before](bool playing, PitchScoop scoop) {
        if (!safe) return;
        if (!playing || !safe->owner || safe->owner->busy || safe->owner->engine.recording || safe->stale || !safe->resolve()) { safe->endAudition(true); return; }
        auto candidate = before; candidate.scoop = scoop; candidate.curve.clear(); auto& e = safe->owner->engine;
        if (safe->auditionNote == index && e.pitchPreviewActive) {
            if (scoop.enabled) e.updatePitchPreviewCurve(safe->auditionId, safe->auditionCurve(candidate)); else e.updatePitchPreview(safe->auditionId, before.target - before.original);
        } else {
            const auto* clip = safe->resolve(); const auto source = clip->pitchOriginal ? clip->pitchOriginal : clip->source;
            if (e.startPitchPreview(safe->auditionId, safe->trackId, source, before.start, before.end, before.target - before.original, clip->gainDb, before.start, safe->auditionCurve(candidate))) safe->auditionNote = index;
        }
    });
    dialog->addCustomComponent(controls.get()); dialog->addButton(String::fromUTF8("設定"), 1, KeyPress(KeyPress::returnKey));
    dialog->addButton(String::fromUTF8("キャンセル"), 0, KeyPress(KeyPress::escapeKey));
    dialog->enterModalState(true, ModalCallbackFunction::create([safe, controls, index, before](int result) {
        if (!safe) return; safe->endAudition(true); safe->scoopPrompt = nullptr;
        if (result != 1 || !safe->owner || safe->owner->busy || safe->owner->engine.recording || safe->stale || !safe->resolve()
            || !isPositiveAndBelow(index, int(safe->notes.size())) || safe->notes[size_t(index)] != before) return;
        auto candidate = before; candidate.scoop = controls->value(); candidate.curve.clear();
        if (!candidate.scoop.enabled) candidate.scoop = {};
        if (candidate.scoop == before.scoop && before.curve.empty()) return;
        PitchCorrectionSettings validation; validation.notes = {{candidate.start, candidate.end, candidate.original, candidate.target, candidate.scoop}};
        if (!validation.valid()) { safe->message = String::fromUTF8("しゃくりの深さ・長さの範囲を確認してください。"); safe->repaint(); return; }
        candidate.edited = true; safe->remember(); safe->notes[size_t(index)] = candidate; safe->changed();
    }), true);
}
}
