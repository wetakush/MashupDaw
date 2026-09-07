#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <map>

namespace mashup
{
class PluginHost;

/** Creates AudioProcessors for EFFECT nodes: built-ins by type name, VST3 by plugin id. */
class EffectFactory
{
public:
    EffectFactory();
    using Creator = std::function<std::unique_ptr<juce::AudioProcessor>()>;

    void registerBuiltIn (const juce::String& type, const juce::String& category, Creator);
    void setPluginHost (PluginHost* h) { pluginHost = h; }

    /** Returns nullptr for unknown types. `type` == "vst3" uses `pluginId`. */
    std::unique_ptr<juce::AudioProcessor> create (const juce::String& type, const juce::String& pluginId, double sampleRate, int blockSize) const;

    struct Entry { juce::String type, category; };
    std::vector<Entry> getBuiltInTypes() const;

private:
    std::map<juce::String, std::pair<juce::String, Creator>> builtIns;
    PluginHost* pluginHost = nullptr;
};
} // namespace mashup
