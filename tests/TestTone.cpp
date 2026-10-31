#include <JuceHeader.h>
// A deterministic instrument: held notes output a DC value, allowing sample-exact host tests.
class TestTone : public juce::AudioProcessor {
public:
    TestTone() : AudioProcessor(BusesProperties().withOutput("Out", juce::AudioChannelSet::stereo(), true)) {}
    const juce::String getName() const override { return "Auralis Test Tone"; }
    void prepareToPlay(double, int) override { reset(); }
    void releaseResources() override {}
    void reset() override { for (auto& row : voices) row.fill(0); }
    bool isBusesLayoutSupported(const BusesLayout& b) const override { return b.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override {
        b.clear(); int at = 0;
        auto fill = [&](int end) { float level = 0; for (auto& row : voices) for (float v : row) level += v; for (int ch = 0; ch < b.getNumChannels(); ++ch) for (int i = at; i < end; ++i) b.setSample(ch, i, level); at = end; };
        for (const auto event : midi) {
            fill(juce::jlimit(at, b.getNumSamples(), event.samplePosition)); auto m = event.getMessage();
            if (m.getChannel() == 0) continue;
            auto& row = voices[size_t(m.getChannel() - 1)];
            if (m.isNoteOn()) row[size_t(m.getNoteNumber())] = m.getFloatVelocity() * 0.25f;
            else if (m.isNoteOff()) row[size_t(m.getNoteNumber())] = 0;
            else if (m.isAllNotesOff() || m.isAllSoundOff()) row.fill(0);
        }
        fill(b.getNumSamples());
    }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& b) override { b.setSize(1); }
    void setStateInformation(const void*, int) override {}
private:
    std::array<std::array<float, 128>, 16> voices {};
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestTone(); }
