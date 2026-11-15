#include "Compressor.h"
#include "NativeEditorTheme.h"

namespace auralis {
namespace {
class CompressionGraph final : public Component {
public:
    explicit CompressionGraph(Compressor& processor) : compressor(processor) { setComponentID("compressorGraph"); }
    void paint(Graphics& g) override {
        using namespace nativeUI; g.fillAll(panel); const auto r = getLocalBounds().toFloat().reduced(38, 24);
        const auto xAt = [&](double db) { return r.getX() + float(db + 60) / 60 * r.getWidth(); };
        const auto yAt = [&](double db) { return r.getBottom() - float(db + 60) / 60 * r.getHeight(); };
        g.setFont(10);
        for (int db = -60; db <= 0; db += 12) { const auto x = xAt(db), y = yAt(db); g.setColour(Colour(0xff2d3946)); g.drawHorizontalLine(int(y), r.getX(), r.getRight()); g.drawVerticalLine(int(x), r.getY(), r.getBottom()); g.setColour(dim); g.drawText(String(db), 0, int(y) - 7, 31, 14, Justification::centredRight); g.drawText(String(db), int(x) - 18, int(r.getBottom()) + 4, 36, 14, Justification::centred); }
        g.setColour(dim.withAlpha(0.4f)); g.drawLine(r.getX(), r.getBottom(), r.getRight(), r.getY(), 1);
        { Graphics::ScopedSaveState save(g); g.reduceClipRegion(r.toNearestInt()); Path p;
          for (int i = 0; i <= 240; ++i) { const double input = -60 + i * 0.25; if (i == 0) p.startNewSubPath(xAt(input), yAt(compressor.transferDb(input))); else p.lineTo(xAt(input), yAt(compressor.transferDb(input))); }
          g.setColour(compressor.settings().bypass ? dim : mint); g.strokePath(p, PathStrokeType(2.5f));
          const double input = jlimit(-60.0, 0.0, double(compressor.inputDb())); g.setColour(amber); g.fillEllipse(xAt(input) - 4, yAt(compressor.transferDb(input)) - 4, 8, 8);
        }
    }
private: Compressor& compressor;
};
class CompressorEditor final : public AudioProcessorEditor, private Timer {
public:
    explicit CompressorEditor(Compressor& processor) : AudioProcessorEditor(processor), compressor(processor), graph(processor) {
        nativeUI::style(theme); setLookAndFeel(&theme); addAndMakeVisible(graph);
        const std::array<const char*, 7> names {"しきい値", "比率", "アタック", "リリース", "ニー", "補正音量", "ミックス"};
        const std::array<const char*, 7> ids {"compThreshold", "compRatio", "compAttack", "compRelease", "compKnee", "compMakeup", "compMix"};
        const std::array<double, 7> lows {-60, 1, 0.1, 10, 0, -12, 0}, highs {0, 20, 200, 2000, 24, 24, 100};
        const StringArray suffixes {" dB", ":1", " ms", " ms", " dB", " dB", " %"};
        for (size_t i = 0; i < sliders.size(); ++i) {
            auto& s = sliders[i]; nativeUI::slider(s, ids[i], lows[i], highs[i], i == 6 ? 1 : 0.1, suffixes[int(i)]); addAndMakeVisible(s);
            labels[i].setText(String::fromUTF8(names[i]), dontSendNotification); addAndMakeVisible(labels[i]);
            s.onValueChange = [this, i] { if (syncing) return; auto settings = compressor.settings(); const auto value = float(sliders[i].getValue());
                switch (i) { case 0: settings.threshold = value; break; case 1: settings.ratio = value; break; case 2: settings.attackMs = value; break; case 3: settings.releaseMs = value; break; case 4: settings.knee = value; break; case 5: settings.makeup = value; break; case 6: settings.mix = value; break; }
                compressor.setSettings(settings); graph.repaint(); };
        }
        sliders[2].setSkewFactorFromMidPoint(10); sliders[3].setSkewFactorFromMidPoint(120);
        sliders[4].setTooltip(String::fromUTF8("しきい値の前後で圧縮をなめらかに始めます。0 dB はハードニー。"));
        sliders[6].setTooltip(String::fromUTF8("100%: 圧縮音 / 0%: 原音。中間値で原音と混ぜられます。"));
        bypass.setComponentID("compBypass"); bypass.setButtonText(String::fromUTF8("バイパス")); addAndMakeVisible(bypass);
        bypass.onClick = [this] { auto s = compressor.settings(); s.bypass = bypass.getToggleState(); compressor.setSettings(s); graph.repaint(); };
        reset.setComponentID("compReset"); reset.setButtonText(String::fromUTF8("初期化")); addAndMakeVisible(reset); reset.onClick = [this] { compressor.restoreDefaults(); sync(); };
        setResizable(true, false); setResizeLimits(720, 560, 1400, 950); setSize(860, 650); sync(); startTimerHz(20);
    }
    ~CompressorEditor() override { stopTimer(); setLookAndFeel(nullptr); }
    void paint(Graphics& g) override {
        using namespace nativeUI; const int w = getWidth(), h = getHeight(); g.fillAll(background); g.setColour(ink); g.setFont(22); g.drawText("Auralis Compressor", 22, 14, w - 310, 32, Justification::centredLeft);
        const int x = w * 2 / 3 + 18, meterWidth = w - x - 26, top = 82;
        g.setColour(dim); g.setFont(12); g.drawText(String::fromUTF8("圧縮量 / GAIN REDUCTION"), x, top, meterWidth, 20, Justification::centredLeft);
        g.setColour(mint); g.setFont(34); g.drawText(String::fromUTF8("−") + String(displayReduction, 1) + " dB", x, top + 28, meterWidth, 44, Justification::centredLeft);
        g.setColour(panel); g.fillRoundedRectangle(float(x), float(top + 88), float(meterWidth), 12, 3);
        g.setColour(mint); g.fillRoundedRectangle(float(x), float(top + 88), meterWidth * jlimit(0.0f, 1.0f, displayReduction / 30), 12, 3);
        g.setColour(dim); g.setFont(11); g.drawText("0", x, top + 103, 30, 18, Justification::centredLeft); g.drawText("30 dB", x + meterWidth - 50, top + 103, 50, 18, Justification::centredRight);
        for (int i = 0; i < 2; ++i) {
            const float db = i == 0 ? compressor.inputDb() : compressor.outputDb(); const int y = top + 145 + i * 43;
            g.setColour(dim); g.drawText((i == 0 ? "IN   " : "OUT ") + String(db, 1) + " dBFS", x, y, meterWidth, 18, Justification::centredLeft);
            g.setColour(panel); g.fillRect(x, y + 23, meterWidth, 5); g.setColour(db > 0 ? Colour(0xffff807e) : mint.withAlpha(0.65f)); g.fillRect(x, y + 23, int(meterWidth * jlimit(0.0f, 1.0f, (db + 60) / 60)), 5);
        }
        g.setColour(dim); g.setFont(11); g.drawText(String::fromUTF8("ステレオリンク / 先読みなし / 録音ファイルは原音のまま"), 22, h - 32, w - 44, 22, Justification::centredLeft);
    }
    void resized() override {
        const int w = getWidth(), h = getHeight(), top = h - 260, cell = (w - 44) / 2;
        reset.setBounds(w - 257, 16, 105, 28); bypass.setBounds(w - 142, 16, 120, 28); graph.setBounds(20, 64, w * 2 / 3 - 30, top - 80);
        for (int i = 0; i < 7; ++i) { const int x = 22 + (i % 2) * cell, y = top + (i / 2) * 51; labels[size_t(i)].setBounds(x, y, 75, 28); sliders[size_t(i)].setBounds(x + 78, y, cell - 90, 28); }
    }
private:
    void sync() {
        syncing = true; const auto s = compressor.settings(); const std::array<float, 7> values {s.threshold, s.ratio, s.attackMs, s.releaseMs, s.knee, s.makeup, s.mix};
        for (size_t i = 0; i < sliders.size(); ++i) if (!nativeUI::editing(sliders[i])) sliders[i].setValue(values[i], dontSendNotification);
        bypass.setToggleState(s.bypass, dontSendNotification); syncing = false; graph.repaint(); repaint();
    }
    void timerCallback() override { displayReduction = jmax(compressor.reductionDb(), displayReduction - 1.5f); sync(); }
    Compressor& compressor; LookAndFeel_V4 theme; CompressionGraph graph; std::array<Slider, 7> sliders; std::array<Label, 7> labels; ToggleButton bypass; TextButton reset; bool syncing = false; float displayReduction = 0;
};
}
AudioProcessorEditor* Compressor::createEditor() { return new CompressorEditor(*this); }
}
