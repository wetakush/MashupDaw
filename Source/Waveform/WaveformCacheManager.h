#pragma once
#include <juce_events/juce_events.h>
#include <map>
#include <mutex>
#include "WaveformCache.h"
#include "Import/SourceLibrary.h"

namespace mashup
{
/** Builds and stores WaveformCaches for every loaded source in the background; notifies UI when ready. */
class WaveformCacheManager : public juce::ChangeBroadcaster, private SourceLibrary::Listener, private juce::AsyncUpdater
{
public:
    WaveformCacheManager (SourceLibrary&, juce::ThreadPool&, std::function<juce::File()> cacheDirProvider);
    ~WaveformCacheManager() override;

    /** Returns the cache (may not be ready yet) or nullptr if the source is unknown/unloaded. */
    WaveformCachePtr get (const juce::String& sourceId) const;
    float getBuildProgress (const juce::String& sourceId) const;
    void clear();

private:
    void sourceLoaded (const juce::String& sourceId) override;
    void handleAsyncUpdate() override { sendChangeMessage(); }
    SourceLibrary& sources;
    juce::ThreadPool& pool;
    std::function<juce::File()> cacheDir;
    mutable std::mutex lock;
    std::map<juce::String, WaveformCachePtr> caches;
    std::map<juce::String, float> progress;
    class BuildJob;
    friend class BuildJob;
};
} // namespace mashup
