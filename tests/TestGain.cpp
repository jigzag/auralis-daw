#include <JuceHeader.h>
class TestGain : public juce::AudioProcessor {
public:
    TestGain() : AudioProcessor(BusesProperties().withInput("In", juce::AudioChannelSet::stereo(), true).withOutput("Out", juce::AudioChannelSet::stereo(), true)) {
        gain = new juce::AudioParameterFloat(juce::ParameterID("gain", 1), "Gain", 0.0f, 1.0f, 0.5f); addParameter(gain);
    }
    const juce::String getName() const override { return "Auralis Test Gain"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& b) const override { return b.getMainInputChannelSet() == juce::AudioChannelSet::stereo() && b.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) override { b.applyGain(gain->get()); }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& b) override { auto v = gain->get(); b.replaceWith(&v, sizeof(v)); }
    void setStateInformation(const void* d, int n) override { if (n == sizeof(float)) { float v; std::memcpy(&v, d, sizeof(float)); *gain = v; } }
private:
    juce::AudioParameterFloat* gain = nullptr;
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestGain(); }
