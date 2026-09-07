#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace mashup
{
/** VST3 scanning and instantiation. Scanning runs on a background thread; the known-plugin list is persisted. */
class PluginHost : public juce::ChangeBroadcaster
{
public:
    explicit PluginHost (juce::ApplicationProperties&);
    ~PluginHost() override;

    juce::KnownPluginList& getKnownPlugins() { return knownPlugins; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }

    /** Starts an asynchronous scan of the default VST3 paths (plus user-added paths). */
    void scanAsync (bool rescanExisting);
    bool isScanning() const noexcept { return scanning.load(); }
    float getScanProgress() const noexcept { return scanProgress.load(); }
    juce::String getCurrentlyScanning() const { const juce::ScopedLock l (nameLock); return currentName; }

    /** Creates a plugin instance by identifier string (KnownPluginList createIdentifierString). Message thread. */
    std::unique_ptr<juce::AudioProcessor> createInstance (const juce::String& pluginId, double sampleRate, int blockSize);

    juce::FileSearchPath getSearchPath() const;
    void setSearchPath (const juce::FileSearchPath&);

private:
    void saveList();
    juce::ApplicationProperties& props;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    std::atomic<bool> scanning { false };
    std::atomic<float> scanProgress { 0.0f };
    juce::CriticalSection nameLock;
    juce::String currentName;
    class ScanThread;
    std::unique_ptr<ScanThread> scanThread;
};
} // namespace mashup
