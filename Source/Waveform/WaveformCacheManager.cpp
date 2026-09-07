#include "WaveformCacheManager.h"

namespace mashup
{
class WaveformCacheManager::BuildJob : public juce::ThreadPoolJob
{
public:
    BuildJob (WaveformCacheManager& o, juce::String id_, WaveformCachePtr c, juce::File f) : ThreadPoolJob ("waveform " + id_), owner (o), id (std::move (id_)), cache (std::move (c)), file (std::move (f)) {}
    JobStatus runJob() override
    {
        bool loaded = file.existsAsFile() && cache->loadFrom (file);
        if (! loaded)
        {
            const bool ok = cache->build ([this] (float p) { { std::lock_guard<std::mutex> l (owner.lock); owner.progress[id] = p; } if ((++tick & 7) == 0) owner.triggerAsyncUpdate(); return ! shouldExit(); });
            if (ok && file != juce::File()) cache->saveTo (file);
        }
        { std::lock_guard<std::mutex> l (owner.lock); owner.progress.erase (id); }
        owner.triggerAsyncUpdate();
        return jobHasFinished;
    }
private:
    WaveformCacheManager& owner; juce::String id; WaveformCachePtr cache; juce::File file; int tick = 0;
};

WaveformCacheManager::WaveformCacheManager (SourceLibrary& s, juce::ThreadPool& p, std::function<juce::File()> dir) : sources (s), pool (p), cacheDir (std::move (dir))
{
    sources.addListener (this);
}
WaveformCacheManager::~WaveformCacheManager() { sources.removeListener (this); cancelPendingUpdate(); }

void WaveformCacheManager::sourceLoaded (const juce::String& id)
{
    auto src = sources.get (id);
    if (! src) return;
    auto cache = std::make_shared<WaveformCache> (src);
    { std::lock_guard<std::mutex> l (lock); caches[id] = cache; progress[id] = 0.0f; }
    juce::File f;
    if (auto dir = cacheDir(); dir != juce::File()) f = dir.getChildFile ("waveforms").getChildFile (id + ".wfc");
    pool.addJob (new BuildJob (*this, id, cache, f), true);
}

WaveformCachePtr WaveformCacheManager::get (const juce::String& id) const
{
    std::lock_guard<std::mutex> l (lock);
    auto it = caches.find (id);
    return it == caches.end() ? nullptr : it->second;
}
float WaveformCacheManager::getBuildProgress (const juce::String& id) const { std::lock_guard<std::mutex> l (lock); auto it = progress.find (id); return it == progress.end() ? 1.0f : it->second; }
void WaveformCacheManager::clear() { std::lock_guard<std::mutex> l (lock); caches.clear(); progress.clear(); }
} // namespace mashup
