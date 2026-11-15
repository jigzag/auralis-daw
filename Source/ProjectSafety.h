#pragma once
#include <JuceHeader.h>
#include <map>

namespace auralis {
struct ProjectLimits {
    static constexpr juce::int64 jsonBytes = 32 * 1024 * 1024, audioBytes = 1024LL * 1024 * 1024;
    static constexpr int depth = 48, jsonTokens = 2000000, tracks = 128, clips = 8192, notes = 100000, events = 100000;
    static constexpr int plugins = 128, pluginsPerChain = 32, stateBytes = 8 * 1024 * 1024, allStateBytes = 16 * 1024 * 1024;
    static constexpr int pitchNotes = 20000, curvePoints = 1000000;
};
struct ProjectLoadApproval { bool externalPlugins = false, externalMedia = false; };
class ProjectDocument {
public:
    static std::shared_ptr<const ProjectDocument> read(const juce::File&);
    const juce::File& file() const { return source; }
    const juce::StringArray& pluginsToApprove() const { return pluginLabels; }
    const juce::StringArray& mediaToApprove() const { return externalMedia; }
    void requireApproval(ProjectLoadApproval) const;
private:
    friend class Engine;
    juce::File source;
    juce::var root;
    juce::StringArray pluginLabels, externalMedia;
    std::map<juce::String, juce::File> media;
};
void validateLocalProjectPath(const juce::File&);
void validateProjectPluginPath(const juce::PluginDescription&);
juce::MemoryBlock decodeProjectBlock(const juce::String&, size_t maximum);
}
