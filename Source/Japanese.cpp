#include "Japanese.h"
#include "BrandingData.h"
namespace auralis {
void initialiseJapanese() {
    const auto catalog = juce::JSON::parse(juce::String::fromUTF8(AuralisBranding::ja_json, AuralisBranding::ja_jsonSize));
    juce::String contents = "language: Japanese\ncountries: JP\n";
    if (auto* object = catalog.getDynamicObject()) {
        const auto& entries = object->getProperties();
        for (int i = 0; i < entries.size(); ++i) contents += juce::JSON::toString(juce::var(entries.getName(i).toString())) + " = " + juce::JSON::toString(entries.getValueAt(i)) + "\n";
    }
    juce::LocalisedStrings::setCurrentMappings(new juce::LocalisedStrings(contents, false));
}
juce::String japaneseMessage(const juce::String& text) {
    const auto exact = juce::translate(text); if (exact != text) return exact;
    if (const auto* locale = juce::LocalisedStrings::getCurrentMappings()) {
        const auto& keys = locale->getMappings().getAllKeys(); const auto& values = locale->getMappings().getAllValues(); int longest = -1;
        for (int i = 0; i < keys.size(); ++i) if (keys[i].endsWithChar(' ') && text.startsWith(keys[i]) && (longest < 0 || keys[i].length() > keys[longest].length())) longest = i;
        if (longest >= 0) return values[longest] + text.substring(keys[longest].length());
    }
    return text;
}
}
