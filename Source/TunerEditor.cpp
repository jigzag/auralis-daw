#include "Tuner.h"
#include "NativeEditorTheme.h"

namespace auralis {
namespace {
class TunerEditor final : public AudioProcessorEditor, private Timer {
public:
    explicit TunerEditor(Tuner& processor) : AudioProcessorEditor(processor), tuner(processor) {
        nativeUI::style(theme); setLookAndFeel(&theme);
        nativeUI::slider(reference, "tunerReference", 400, 480, 0.1, " Hz"); addAndMakeVisible(reference);
        reference.onValueChange = [this] { if (!syncing) tuner.setReferenceHz(float(reference.getValue())); };
        channel.setComponentID("tunerChannel"); channel.addItemList({String::fromUTF8("入力: 自動"), String::fromUTF8("入力: 左 (L)"), String::fromUTF8("入力: 右 (R)")}, 1); addAndMakeVisible(channel);
        channel.onChange = [this] { if (!syncing) tuner.setInputChannel(channel.getSelectedId() - 1); };
        mute.setComponentID("tunerMute"); mute.setButtonText(String::fromUTF8("ミュートして調律")); addAndMakeVisible(mute);
        mute.onClick = [this] { tuner.setMuted(mute.getToggleState()); };
        reset.setComponentID("tuner440"); reset.setButtonText("440 Hz"); addAndMakeVisible(reset); reset.onClick = [this] { tuner.setReferenceHz(440); sync(); };
        setResizable(true, false); setResizeLimits(620, 420, 1300, 800); setSize(760, 470); sync(); startTimerHz(20);
    }
    ~TunerEditor() override { stopTimer(); setLookAndFeel(nullptr); }
    void paint(Graphics& g) override {
        using namespace nativeUI; g.fillAll(background); const int w = getWidth(), h = getHeight();
        g.setColour(ink); g.setFont(22); g.drawText("Auralis Tuner", 22, 14, 260, 32, Justification::centredLeft);
        g.setColour(dim); g.setFont(12); g.drawText(String::fromUTF8("単音 / 25–1500 Hz"), w - 205, 20, 180, 22, Justification::centredRight);
        const auto reading = tuner.reading(); const bool valid = reading.frequency > 0;
        const double midi = valid ? 69 + 12 * std::log2(reading.frequency / tuner.referenceHz()) : 69;
        const int note = int(std::round(midi)); const double cents = (midi - note) * 100;
        g.setColour(panel); g.fillRoundedRectangle(20.0f, 64.0f, float(w - 40), float(h - 235), 12);
        const bool tuned = valid && std::abs(cents) <= 3; g.setColour(tuned ? mint : ink); g.setFont(jmin(94.0f, float(h - 350)));
        g.drawText(valid ? MidiMessage::getMidiNoteName(note, true, true, 4) : String::fromUTF8("—"), 35, 74, w - 70, h - 340, Justification::centred);
        g.setColour(valid ? (tuned ? mint : amber) : dim); g.setFont(24);
        g.drawText(valid ? (cents >= 0 ? "+" : "") + String(cents, 1) + " cents" : String::fromUTF8("単音を鳴らしてください"), 35, h - 263, w - 70, 35, Justification::centred);
        const float left = 58, width = float(w - 116), y = float(h - 201);
        g.setColour(mint.withAlpha(0.12f)); g.fillRect(left + width * 0.47f, y - 20, width * 0.06f, 37.0f);
        for (int c = -50; c <= 50; c += 5) {
            const float x = left + (c + 50) * width / 100; g.setColour(c == 0 ? mint : dim); g.drawLine(x, y - (c % 25 == 0 ? 13 : 6), x, y + 6, c == 0 ? 2.0f : 1.0f);
            if (c % 25 == 0) { g.setFont(11); g.drawText(String(c), int(x) - 22, int(y) + 10, 44, 18, Justification::centred); }
        }
        if (valid) { const float x = left + float(jlimit(-50.0, 50.0, cents) + 50) * width / 100; g.setColour(tuned ? mint : amber); g.fillEllipse(x - 5, y - 29, 10, 10); g.drawLine(x, y - 20, x, y + 4, 2); }
        g.setFont(12); g.setColour(dim); g.drawText(valid ? String(reading.frequency, 2) + " Hz" : "— Hz", 30, h - 165, w / 2 - 30, 22, Justification::centredLeft);
        g.drawText("INPUT " + String(reading.inputDb, 1) + " dBFS", w / 2, h - 165, w / 2 - 30, 22, Justification::centredRight);
        g.drawText("A4", 22, h - 117, 32, 28, Justification::centredLeft);
        g.drawText(String::fromUTF8("入力を調律するときはトラックの MON を ON にしてください。"), 22, h - 35, w - 44, 24, Justification::centredLeft);
    }
    void resized() override {
        const int w = getWidth(), h = getHeight(); reference.setBounds(55, h - 117, w - 200, 28); reset.setBounds(w - 130, h - 117, 108, 28);
        channel.setBounds(22, h - 75, w / 2 - 42, 28); mute.setBounds(w / 2 + 5, h - 75, w / 2 - 27, 28);
    }
private:
    void sync() { syncing = true; if (!nativeUI::editing(reference)) reference.setValue(tuner.referenceHz(), dontSendNotification); channel.setSelectedId(tuner.inputChannel() + 1, dontSendNotification); mute.setToggleState(tuner.muted(), dontSendNotification); syncing = false; repaint(); }
    void timerCallback() override { sync(); }
    Tuner& tuner; LookAndFeel_V4 theme; Slider reference; ComboBox channel; ToggleButton mute; TextButton reset; bool syncing = false;
};
}
AudioProcessorEditor* Tuner::createEditor() { return new TunerEditor(*this); }
}
