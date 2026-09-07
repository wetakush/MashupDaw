#include "EffectFactory.h"
#include "Plugins/PluginHost.h"
#include "Effects/BuiltInEffects.h"

namespace mashup
{
EffectFactory::EffectFactory() { registerAllBuiltInEffects (*this); }

void EffectFactory::registerBuiltIn (const juce::String& type, const juce::String& category, Creator c) { builtIns[type] = { category, std::move (c) }; }

std::unique_ptr<juce::AudioProcessor> EffectFactory::create (const juce::String& type, const juce::String& pluginId, double sampleRate, int blockSize) const
{
    if (type == "vst3")
    {
        if (pluginHost) return pluginHost->createInstance (pluginId, sampleRate, blockSize);
        return nullptr;
    }
    auto it = builtIns.find (type);
    return it == builtIns.end() ? nullptr : it->second.second();
}

std::vector<EffectFactory::Entry> EffectFactory::getBuiltInTypes() const
{
    std::vector<Entry> e;
    for (auto& [k, v] : builtIns) e.push_back ({ k, v.first });
    return e;
}
}
