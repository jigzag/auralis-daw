#include "Equalizer.h"
#include <cmath>

namespace auralis {
namespace {
const Colour bg(0xff11151b), ink(0xffe2e9ee), dim(0xff8d9cab);
const std::array<Colour, 4> colours {Colour(0xff80e3c0), Colour(0xffffbd79), Colour(0xff80b8f5), Colour(0xffc5a0f4)};
const StringArray types {"Bell", "Low shelf", "High shelf", "Low cut", "High cut"};

class EqGraph : public Component, public SettableTooltipClient {
public:
    EqGraph(Equalizer& processor, int& band, std::function<void()> changed) : eq(processor), selected(band), onChange(std::move(changed)) {
        setComponentID("eqGraph"); setMouseCursor(MouseCursor::DraggingHandCursor);
        setWantsKeyboardFocus(true);
        setTooltip(String::fromUTF8("点をドラッグ: 左右で周波数・上下で増減。ホイール: Q。Ctrl: 細かく。ダブルクリック: 0 dB。"));
    }
    Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedLeft(46).withTrimmedRight(16).withTrimmedTop(18).withTrimmedBottom(24); }
    double maximum() const { return jmin(20000.0, eq.currentRate() * 0.49); }
    float xAt(double frequency) const { const auto r = plot(); return r.getX() + float(std::log(jlimit(20.0, maximum(), frequency) / 20) / std::log(maximum() / 20)) * r.getWidth(); }
    float yAt(double db) const { const auto r = plot(); return r.getCentreY() - float(jlimit(-24.0, 24.0, db)) / 48 * r.getHeight(); }
    Point<float> point(int i) const { const auto b = eq.band(i); return {xAt(b.frequency), yAt(b.type < 3 ? b.gain : 0)}; }
    void paint(Graphics& g) override {
        g.fillAll(bg); const auto r = plot(); g.setFont(11);
        for (int db = -24; db <= 24; db += 6) { const float y = yAt(db); g.setColour(db == 0 ? Colour(0xff526275) : Colour(0xff26313d)); g.drawHorizontalLine(int(y), r.getX(), r.getRight()); g.setColour(dim); g.drawText(String(db) + " dB", 0, int(y) - 7, 39, 14, Justification::centredRight); }
        for (double hz : {20., 50., 100., 200., 500., 1000., 2000., 5000., 10000., 20000.}) if (hz <= maximum()) {
            const float x = xAt(hz); g.setColour(Colour(0xff26313d)); g.drawVerticalLine(int(x), r.getY(), r.getBottom());
            g.setColour(dim); g.drawText(hz >= 1000 ? String(hz / 1000, 0) + "k" : String(hz, 0), int(x) - 18, int(r.getBottom()) + 5, 36, 16, Justification::centred);
        }
        { Graphics::ScopedSaveState save(g); g.reduceClipRegion(r.toNearestInt());
          auto path = [&](int band) { Path p; for (int i = 0; i <= int(r.getWidth()); i += 2) { const double hz = 20 * std::pow(maximum() / 20, i / double(r.getWidth())); const float x = r.getX() + i, y = yAt(eq.bypassed() && band < 0 ? 0 : eq.responseDb(hz, band)); if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y); } return p; };
          for (int i = 0; i < 4; ++i) { g.setColour(colours[size_t(i)].withAlpha(0.25f)); g.strokePath(path(i), PathStrokeType(1)); }
          const auto total = path(-1); auto fill = total; fill.lineTo(r.getRight(), yAt(0)); fill.lineTo(r.getX(), yAt(0)); fill.closeSubPath();
          g.setColour(colours[0].withAlpha(eq.bypassed() ? 0.02f : 0.09f)); g.fillPath(fill);
          g.setColour(eq.bypassed() ? dim : ink); g.strokePath(total, PathStrokeType(2));
        }
        for (int i = 0; i < 4; ++i) {
            const auto b = eq.band(i); const auto p = point(i); const float radius = selected == i ? 9.0f : 7.0f;
            g.setColour(colours[size_t(i)].withAlpha(b.enabled ? 1.0f : 0.35f)); g.fillEllipse(p.x - radius, p.y - radius, radius * 2, radius * 2);
            g.setColour(bg); g.setFont(11); g.drawText(String(i + 1), int(p.x - radius), int(p.y - radius), int(radius * 2), int(radius * 2), Justification::centred);
        }
    }
    void mouseDown(const MouseEvent& e) override {
        dragging = false; if (!e.mods.isLeftButtonDown()) return; if (isShowing()) grabKeyboardFocus();
        int found = -1; float distance = 18;
        for (int i = 0; i < 4; ++i) if (point(i).getDistanceFrom(e.position) < distance) { distance = point(i).getDistanceFrom(e.position); found = i; }
        if (found < 0) return; selected = found; dragging = true; origin = e.position; before = eq.band(selected); onChange(); repaint();
    }
    void mouseDrag(const MouseEvent& e) override {
        if (!dragging) return; const auto r = plot(); auto b = before;
        const double fine = e.mods.isCtrlDown() ? 0.1 : 1;
        b.frequency = float(jlimit(20.0, maximum(), double(before.frequency) * std::exp((e.position.x - origin.x) * fine / r.getWidth() * std::log(maximum() / 20))));
        if (b.type < 3) b.gain = float(jlimit(-24.0, 24.0, before.gain - (e.position.y - origin.y) * fine / r.getHeight() * 48));
        eq.setBand(selected, b); onChange(); repaint();
    }
    void mouseUp(const MouseEvent&) override { dragging = false; }
    void mouseWheelMove(const MouseEvent&, const MouseWheelDetails& wheel) override { auto b = eq.band(selected); b.q = float(jlimit(0.1, 12.0, b.q * std::exp(wheel.deltaY * 2.5))); eq.setBand(selected, b); onChange(); repaint(); }
    void mouseDoubleClick(const MouseEvent& e) override { for (int i = 0; i < 4; ++i) if (point(i).getDistanceFrom(e.position) < 18) { selected = i; auto b = eq.band(i); b.gain = 0; eq.setBand(i, b); onChange(); repaint(); return; } }
private:
    Equalizer& eq;
    int& selected;
    std::function<void()> onChange;
    bool dragging = false;
    Point<float> origin;
    EqBand before;
};

class EqualizerEditor : public AudioProcessorEditor, private Timer {
public:
    explicit EqualizerEditor(Equalizer& processor) : AudioProcessorEditor(processor), eq(processor), graph(eq, selected, [this] { sync(); }) {
        theme.setColour(ResizableWindow::backgroundColourId, bg); theme.setColour(Label::textColourId, ink); theme.setColour(Slider::thumbColourId, colours[0]);
        theme.setColour(Slider::trackColourId, colours[0]); theme.setColour(Slider::textBoxTextColourId, ink); theme.setColour(Slider::textBoxBackgroundColourId, Colour(0xff1a222b));
        theme.setColour(TextButton::buttonColourId, Colour(0xff29333e)); theme.setColour(TextButton::textColourOffId, ink);
        theme.setColour(ComboBox::backgroundColourId, Colour(0xff29333e)); theme.setColour(ComboBox::textColourId, ink); theme.setColour(ToggleButton::textColourId, ink);
        setLookAndFeel(&theme); addAndMakeVisible(graph);
        for (int i = 0; i < 4; ++i) { auto& button = bandButtons[size_t(i)]; addAndMakeVisible(button); button.setComponentID("eqBand" + String(i + 1)); button.setWantsKeyboardFocus(false); button.setColour(TextButton::buttonOnColourId, colours[size_t(i)].withAlpha(0.3f)); button.setColour(TextButton::textColourOnId, colours[size_t(i)]); button.onClick = [this, i] { selected = i; sync(true); }; }
        auto slider = [&](Slider& s, Label& label, const char* id, const String& title, double low, double high, double step, const String& suffix) {
            addAndMakeVisible(s); addAndMakeVisible(label); label.setText(title, dontSendNotification); s.setComponentID(id); s.setRange(low, high, step); s.setTextValueSuffix(suffix);
            s.setSliderStyle(Slider::LinearHorizontal); s.setTextBoxStyle(Slider::TextBoxRight, false, 90, 26); s.setWantsKeyboardFocus(false);
        };
        slider(frequency, frequencyLabel, "eqFrequency", String::fromUTF8("周波数"), 20, 20000, 0.1, " Hz"); frequency.setSkewFactorFromMidPoint(1000);
        slider(gain, gainLabel, "eqGain", String::fromUTF8("増減"), -24, 24, 0.1, " dB");
        slider(q, qLabel, "eqQ", String::fromUTF8("Q（幅）"), 0.1, 12, 0.01, ""); q.setSkewFactorFromMidPoint(1);
        q.setTooltip(String::fromUTF8("大きいほど狭い帯域に効きます。カットでは共振の強さを調整します。"));
        slider(output, outputLabel, "eqOutput", String::fromUTF8("出力"), -24, 12, 0.1, " dB");
        frequency.onValueChange = [this] { if (!syncing) { auto b = eq.band(selected); b.frequency = float(frequency.getValue()); eq.setBand(selected, b); graph.repaint(); } };
        gain.onValueChange = [this] { if (!syncing) { auto b = eq.band(selected); b.gain = float(gain.getValue()); eq.setBand(selected, b); graph.repaint(); } };
        q.onValueChange = [this] { if (!syncing) { auto b = eq.band(selected); b.q = float(q.getValue()); eq.setBand(selected, b); graph.repaint(); } };
        output.onValueChange = [this] { if (!syncing) eq.setOutputDb(float(output.getValue())); };
        addAndMakeVisible(type); addAndMakeVisible(typeLabel); typeLabel.setText(String::fromUTF8("種類"), dontSendNotification); type.setComponentID("eqType"); type.addItemList(types, 1);
        type.onChange = [this] { if (syncing) return; auto b = eq.band(selected); b.type = type.getSelectedId() - 1; eq.setBand(selected, b); sync(); };
        addAndMakeVisible(enabled); enabled.setComponentID("eqEnabled"); enabled.setButtonText(String::fromUTF8("このバンドを有効")); enabled.onClick = [this] { auto b = eq.band(selected); b.enabled = enabled.getToggleState(); eq.setBand(selected, b); graph.repaint(); };
        addAndMakeVisible(bypass); bypass.setComponentID("eqBypass"); bypass.setButtonText(String::fromUTF8("バイパス")); bypass.onClick = [this] { eq.setBypassed(bypass.getToggleState()); graph.repaint(); };
        addAndMakeVisible(reset); reset.setComponentID("eqReset"); reset.setButtonText(String::fromUTF8("初期化")); reset.onClick = [this] { eq.restoreDefaults(); sync(true); };
        setResizable(true, false); setResizeLimits(720, 480, 1400, 900); setSize(900, 570); sync(true); startTimerHz(20);
    }
    ~EqualizerEditor() override { stopTimer(); setLookAndFeel(nullptr); }
    void paint(Graphics& g) override {
        g.fillAll(bg); g.setColour(ink); g.setFont(22); g.drawText("Auralis EQ", 20, 12, 220, 30, Justification::centredLeft);
        g.setColour(dim); g.setFont(11); g.drawText(String::fromUTF8("点をドラッグ: 周波数・増減 / ホイール: Q / Ctrl: 微調整 / ダブルクリック: 0 dB"), 20, getHeight() - 28, getWidth() - 40, 22, Justification::centredLeft);
    }
    void resized() override {
        const int w = getWidth(), h = getHeight(), half = w / 2;
        reset.setBounds(w - 265, 14, 110, 28); bypass.setBounds(w - 145, 14, 125, 28);
        for (int i = 0; i < 4; ++i) bandButtons[size_t(i)].setBounds(20 + i * (w - 40) / 4, 58, (w - 40) / 4 - 7, 34);
        graph.setBounds(12, 103, w - 24, h - 269);
        frequencyLabel.setBounds(20, h - 150, 64, 28); frequency.setBounds(86, h - 150, half - 106, 28);
        gainLabel.setBounds(half + 5, h - 150, 55, 28); gain.setBounds(half + 64, h - 150, half - 84, 28);
        qLabel.setBounds(20, h - 110, 64, 28); q.setBounds(86, h - 110, half - 106, 28);
        typeLabel.setBounds(half + 5, h - 110, 55, 28); type.setBounds(half + 64, h - 110, half - 84, 28);
        enabled.setBounds(20, h - 70, 220, 28); outputLabel.setBounds(half + 5, h - 70, 55, 28); output.setBounds(half + 64, h - 70, half - 84, 28);
    }
private:
    Equalizer& eq;
    LookAndFeel_V4 theme;
    int selected = 0;
    EqGraph graph;
    std::array<TextButton, 4> bandButtons;
    Slider frequency, gain, q, output;
    Label frequencyLabel, gainLabel, qLabel, outputLabel, typeLabel;
    ComboBox type;
    ToggleButton enabled, bypass;
    TextButton reset;
    TooltipWindow tooltips {this, 600};
    bool syncing = false;
    void sync(bool force = false) {
        syncing = true; const auto b = eq.band(selected);
        const double maximum = jmin(20000.0, eq.currentRate() * 0.49);
        if (std::abs(frequency.getMaximum() - maximum) > 0.1) { frequency.setRange(20, maximum, 0.1); frequency.setSkewFactorFromMidPoint(1000); }
        auto value = [&](Slider& s, double v) { if (force || (!s.hasKeyboardFocus(true) && !s.isMouseButtonDown())) s.setValue(v, dontSendNotification); };
        value(frequency, jmin(double(b.frequency), maximum)); value(gain, b.gain); value(q, b.q); value(output, eq.outputDb()); gain.setEnabled(b.type < 3);
        if (force || !type.hasKeyboardFocus(true)) type.setSelectedId(b.type + 1, dontSendNotification);
        enabled.setToggleState(b.enabled, dontSendNotification); bypass.setToggleState(eq.bypassed(), dontSendNotification);
        for (int i = 0; i < 4; ++i) { const auto state = eq.band(i); bandButtons[size_t(i)].setButtonText(String(i + 1) + "   " + types[state.type] + (state.enabled ? "" : "  [off]")); bandButtons[size_t(i)].setToggleState(i == selected, dontSendNotification); }
        syncing = false; graph.repaint();
    }
    void timerCallback() override { sync(); }
};
}
AudioProcessorEditor* Equalizer::createEditor() { return new EqualizerEditor(*this); }
}
