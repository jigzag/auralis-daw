#include "SpaceEffects.h"
#include "NativeEditorTheme.h"
#include <type_traits>

namespace auralis {
namespace {
template<class Effect> class SpaceGraph final : public Component, public SettableTooltipClient {
public:
    explicit SpaceGraph(Effect& p) : effect(p) { setComponentID("spaceGraph"); setTooltip(String::fromUTF8("設定値から描いたイメージです。実測波形ではありません。")); }
    void paint(Graphics& g) override {
        using namespace nativeUI; g.fillAll(panel); const auto r = getLocalBounds().toFloat().reduced(24, 34); const auto s = effect.settings(); g.setFont(11); g.setColour(dim);
        if constexpr (std::is_same_v<Effect, DelayEffect>) {
            g.drawText(String::fromUTF8("反復イメージ"), 20, 8, 200, 20, Justification::centredLeft);
            g.drawText(String(effect.effectiveTimeMs(), 1) + " ms" + (s.sync ? " / " + String(effect.tempoBpm(), 1) + " BPM" : ""), getWidth() - 330, 8, 310, 20, Justification::centredRight);
            const int repeats = 7; g.setColour(dim); g.drawHorizontalLine(int(r.getBottom()), r.getX(), r.getRight());
            for (int i = 0; i < repeats; ++i) {
                const float x = r.getX() + i * r.getWidth() / (repeats - 1); const float level = i == 0 ? 1 - s.mix * 0.01f : s.mix * 0.01f * float(std::pow(s.feedback * 0.01, i - 1));
                g.setColour(i == 0 ? dim : s.pingPong && i % 2 == 0 ? amber : mint); const float height = level * (r.getHeight() - 18); g.fillRoundedRectangle(x - 3, r.getBottom() - height - 1, 6, jmax(2.0f, height), 2);
                g.setFont(10); g.drawText(String(i * effect.effectiveTimeMs() * 0.001, 2) + "s", int(x) - 28, int(r.getBottom()) + 7, 56, 18, Justification::centred);
                if (s.pingPong && i > 0) g.drawText(i % 2 ? "L" : "R", int(x) - 12, int(r.getY()), 24, 18, Justification::centred);
            }
        } else {
            g.drawText(String::fromUTF8("残響イメージ"), 20, 8, 200, 20, Justification::centredLeft);
            const double tail = effect.getTailLengthSeconds(); g.drawText(String::fromUTF8("推定余韻 ") + String(tail, 1) + " s / PRE " + String(s.preDelayMs, 1) + " ms", getWidth() - 350, 8, 330, 20, Justification::centredRight);
            g.setColour(Colour(0xff2d3946)); for (int i = 0; i <= 3; ++i) g.drawHorizontalLine(int(r.getY() + i * r.getHeight() / 3), r.getX(), r.getRight());
            Path path; const double extent = jmax(0.5, tail), pre = s.preDelayMs * 0.001;
            for (int x = 0; x <= int(r.getWidth()); ++x) { const double time = extent * x / r.getWidth(); const float level = time < pre ? 0 : float(std::exp(-6.9078 * (time - pre) / jmax(0.1, extent - pre))); const float y = r.getBottom() - level * r.getHeight(); if (x == 0) path.startNewSubPath(r.getX(), y); else path.lineTo(r.getX() + x, y); }
            g.setColour(s.bypass ? dim : mint); g.strokePath(path, PathStrokeType(2)); auto fill = path; fill.lineTo(r.getRight(), r.getBottom()); fill.lineTo(r.getX(), r.getBottom()); fill.closeSubPath(); g.setColour(mint.withAlpha(0.1f)); g.fillPath(fill);
            g.setColour(dim); for (int i = 0; i <= 4; ++i) g.drawText(String(extent * i / 4, 1) + "s", int(r.getX() + r.getWidth() * i / 4) - 24, int(r.getBottom()) + 7, 48, 18, Justification::centred);
        }
    }
private: Effect& effect;
};
template<class Effect> class SpaceEditor final : public AudioProcessorEditor, private Timer {
    static constexpr bool isDelay = std::is_same_v<Effect, DelayEffect>;
public:
    explicit SpaceEditor(Effect& processor) : AudioProcessorEditor(processor), effect(processor), graph(processor) {
        nativeUI::style(theme); setLookAndFeel(&theme); addAndMakeVisible(graph);
        addAndMakeVisible(presetLabel); presetLabel.setText(String::fromUTF8("プリセット"), dontSendNotification); addAndMakeVisible(presets); presets.setComponentID("spacePreset");
        presets.addItem(String::fromUTF8("カスタム"), 1);
        if constexpr (isDelay) presets.addItemList({TRANS("Slapback"), TRANS("Tempo echo"), TRANS("Ping-pong dotted")}, 2); else presets.addItemList({TRANS("Small room"), TRANS("Hall"), TRANS("Ambient")}, 2);
        presets.setSelectedId(1, dontSendNotification); presets.onChange = [this] { if (presets.getSelectedId() > 1) { effect.preset(presets.getSelectedId() - 1); syncControls(); } };
        auto add = [&](int i, const char* id, const char* label, double low, double high, double step, const String& suffix) { auto& slider = sliders[size_t(i)]; nativeUI::slider(slider, id, low, high, step, suffix); addAndMakeVisible(slider); labels[size_t(i)].setText(String::fromUTF8(label), dontSendNotification); addAndMakeVisible(labels[size_t(i)]); slider.onValueChange = [this, i] { apply(i); }; };
        if constexpr (isDelay) {
            add(0, "delayTime", "時間", 1, 4000, 0.1, " ms"); add(1, "delayFeedback", "反復量", 0, 90, 0.1, " %"); add(2, "delayHighCut", "ハイカット", 200, 20000, 1, " Hz"); add(3, "delayMix", "ミックス", 0, 100, 0.1, " %"); add(4, "delayOutput", "出力", -24, 12, 0.1, " dB");
            sliders[0].setSkewFactorFromMidPoint(400); sliders[2].setSkewFactorFromMidPoint(4000);
            addAndMakeVisible(sync); sync.setComponentID("delaySync"); sync.setButtonText(String::fromUTF8("テンポ同期")); sync.onClick = [this] { auto s = effect.settings(); s.sync = sync.getToggleState(); effect.setSettings(s); edited(); };
            addAndMakeVisible(division); division.setComponentID("delayDivision"); division.addItemList(DelayEffect::divisions(), 1); division.onChange = [this] { if (syncing) return; auto s = effect.settings(); s.division = division.getSelectedId() - 1; effect.setSettings(s); edited(); };
            addAndMakeVisible(ping); ping.setComponentID("delayPingPong"); ping.setButtonText(String::fromUTF8("左右交互")); ping.onClick = [this] { auto s = effect.settings(); s.pingPong = ping.getToggleState(); effect.setSettings(s); edited(); };
        } else {
            add(0, "reverbSize", "広さ", 0, 100, 0.1, " %"); add(1, "reverbDamping", "高域減衰", 0, 100, 0.1, " %"); add(2, "reverbWidth", "ステレオ幅", 0, 100, 0.1, " %"); add(3, "reverbPreDelay", "プリディレイ", 0, 250, 0.1, " ms");
            add(4, "reverbLowCut", "ローカット", 20, 1000, 1, " Hz"); add(5, "reverbHighCut", "ハイカット", 1000, 20000, 1, " Hz"); add(6, "reverbMix", "ミックス", 0, 100, 0.1, " %"); add(7, "reverbOutput", "出力", -24, 12, 0.1, " dB"); sliders[4].setSkewFactorFromMidPoint(200); sliders[5].setSkewFactorFromMidPoint(6000);
        }
        addAndMakeVisible(bypass); bypass.setComponentID("spaceBypass"); bypass.setButtonText(String::fromUTF8("バイパス")); bypass.onClick = [this] { auto s = effect.settings(); s.bypass = bypass.getToggleState(); effect.setSettings(s); graph.repaint(); };
        addAndMakeVisible(reset); reset.setComponentID("spaceReset"); reset.setButtonText(String::fromUTF8("初期化")); reset.onClick = [this] { effect.restoreDefaults(); presets.setSelectedId(1, dontSendNotification); syncControls(); };
        setResizable(true, false); setResizeLimits(760, 600, 1400, 950); setSize(860, 640); syncControls(); startTimerHz(20);
    }
    ~SpaceEditor() override { stopTimer(); setLookAndFeel(nullptr); }
    void paint(Graphics& g) override { using namespace nativeUI; g.fillAll(background); g.setColour(ink); g.setFont(22); g.drawText(effect.getName(), 22, 14, getWidth() - 310, 32, Justification::centredLeft); g.setColour(dim); g.setFont(11); g.drawText(String::fromUTF8("ミックス 0%: 原音 / 100%: エフェクト音のみ / 録音ファイルは原音のまま"), 22, getHeight() - 32, getWidth() - 44, 22, Justification::centredLeft); }
    void resized() override {
        const int w = getWidth(), h = getHeight(), top = h - 254, cell = (w - 44) / 2;
        reset.setBounds(w - 257, 16, 105, 28); bypass.setBounds(w - 142, 16, 120, 28); presetLabel.setBounds(22, 62, 80, 28); presets.setBounds(105, 62, w - 127, 28); graph.setBounds(20, 106, w - 40, top - 124);
        for (int i = 0; i < (isDelay ? 5 : 8); ++i) { const int x = 22 + (i % 2) * cell, y = top + (i / 2) * 45; labels[size_t(i)].setBounds(x, y, 86, 28); sliders[size_t(i)].setBounds(x + 88, y, cell - 100, 28); }
        if constexpr (isDelay) { sync.setBounds(22, top + 145, 150, 30); division.setBounds(185, top + 145, w / 2 - 205, 28); ping.setBounds(w / 2 + 22, top + 145, w / 2 - 44, 30); }
    }
private:
    void edited() { presets.setSelectedId(1, dontSendNotification); syncControls(); }
    void apply(int i) {
        if (syncing) return; auto s = effect.settings(); const auto v = float(sliders[size_t(i)].getValue());
        if constexpr (isDelay) { switch (i) { case 0: s.timeMs = v; break; case 1: s.feedback = v; break; case 2: s.highCut = v; break; case 3: s.mix = v; break; case 4: s.output = v; break; } }
        else { switch (i) { case 0: s.size = v; break; case 1: s.damping = v; break; case 2: s.width = v; break; case 3: s.preDelayMs = v; break; case 4: s.lowCut = v; break; case 5: s.highCut = v; break; case 6: s.mix = v; break; case 7: s.output = v; break; } }
        effect.setSettings(s); edited();
    }
    void syncControls() {
        syncing = true; const auto s = effect.settings(); std::array<float, 8> values {};
        if constexpr (isDelay) { values = {s.timeMs, s.feedback, s.highCut, s.mix, s.output}; sync.setToggleState(s.sync, dontSendNotification); ping.setToggleState(s.pingPong, dontSendNotification); division.setSelectedId(s.division + 1, dontSendNotification); division.setEnabled(s.sync); sliders[0].setEnabled(!s.sync); }
        else values = {s.size, s.damping, s.width, s.preDelayMs, s.lowCut, s.highCut, s.mix, s.output};
        for (int i = 0; i < (isDelay ? 5 : 8); ++i) if (!nativeUI::editing(sliders[size_t(i)])) sliders[size_t(i)].setValue(values[size_t(i)], dontSendNotification);
        bypass.setToggleState(s.bypass, dontSendNotification); syncing = false; graph.repaint();
    }
    void timerCallback() override { syncControls(); }
    Effect& effect; LookAndFeel_V4 theme; SpaceGraph<Effect> graph; std::array<Slider, 8> sliders; std::array<Label, 8> labels; Label presetLabel; ComboBox presets, division; ToggleButton bypass, sync, ping; TextButton reset; bool syncing = false;
};
}
AudioProcessorEditor* ReverbEffect::createEditor() { return new SpaceEditor<ReverbEffect>(*this); }
AudioProcessorEditor* DelayEffect::createEditor() { return new SpaceEditor<DelayEffect>(*this); }
}
