#pragma once
#include <JuceHeader.h>
namespace auralis {
void initialiseJapanese();
juce::String japaneseMessage(const juce::String&);
void runJapaneseTests(const juce::File&, juce::StringArray&);
}
