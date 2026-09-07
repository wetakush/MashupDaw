#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>
#include "RenderGraph.h"
#include "GraphBuilder.h"
#include "Transport.h"
#include "Recorder.h"
#include "Core/RealtimeUtils.h"
#include "Import/SourceLibrary.h"

namespace mashup
{
class ProjectModel;
class EffectFactory;

/** Owns the audio device and the realtime callback. Rebuilds the RenderGraph when the project changes.
    All public methods are message-thread unless stated. */
class AudioEngine : private juce::AudioIODeviceCallback,
                    private juce::ValueTree::Listener,
                    private juce::AsyncUpdater,
                    private SourceLibrary::Listener,
                    private juce::ChangeListener
{
public:
    AudioEngine (ProjectModel&, SourceLibrary&, EffectFactory&, juce::ApplicationProperties&);
    ~AudioEngine() override;

    /** Opens the default device (or the one saved in settings). Returns an error string if nothing could be opened. */
    juce::String initialiseDevice();
    juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    Transport& getTransport() noexcept { return transport; }
    Recorder& getRecorder() noexcept { return recorder; }
    double getSampleRate() const noexcept { return sampleRate.load(); }
    int getBlockSize() const noexcept { return blockSize.load(); }
    double getCpuLoad() const { return deviceManager.getCpuUsage(); }
    int getXrunCount() const noexcept { return xruns.load(); }
    /** Latest wall-clock output latency estimate in samples. */
    int getOutputLatencySamples() const;

    // positions in seconds (convenience)
    double getPositionSeconds() const noexcept { return transport.getPosition() / getSampleRate(); }
    void locateSeconds (double s) noexcept { transport.locate ((juce::int64) std::llround (s * getSampleRate())); }
    void locateBeat (double beat);
    double getPositionBeat() const;
    void updateLoopFromProject();

    /** Forces a graph rebuild (e.g. after the project was replaced). */
    void rebuildGraph();
    const RenderGraph* getCurrentGraph() const noexcept { return graph.getMessageThreadView(); }

    /** Master meters (peak since last read). */
    float readMasterPeak (int ch) noexcept;
    /** Master loudness meter (may be null before the first graph). */
    dsp::LoudnessMeterRT* getMasterLoudness() const noexcept { auto* g = graph.getMessageThreadView(); return g ? g->loudness.get() : nullptr; }
    /** Per-track peak by track id (peak since last read), 0 if unknown. */
    float readTrackPeak (const juce::String& trackId, int ch) noexcept;

    /** Audition (browser preview) of a source independent of the timeline. */
    void startAudition (AudioSourcePtr source, double startSeconds = 0.0, double gain = 1.0, double endSeconds = 0.0, bool reverse = false, double speed = 1.0);
    void stopAudition();
    bool isAuditioning() const noexcept;
    double getAuditionPositionSeconds() const noexcept;

    /** Offline render of the current graph state into `out` at the engine rate: used by export and tests.
        Renders [startSample, startSample+numSamples). Must not be called while the device is running the same graph. */
    void renderOffline (RenderGraph& g, juce::AudioBuffer<float>& out, juce::int64 startSample, int numSamples, int blockSize);
    std::unique_ptr<RenderGraph> buildOfflineGraph (double sampleRate, int blockSize);

    /** Effects bypass (A/B) for the whole mix. */
    void setBypassAllEffects (bool b) noexcept { bypassEffects.store (b); }
    bool isBypassingAllEffects() const noexcept { return bypassEffects.load(); }

    struct Listener { virtual ~Listener() = default; virtual void deviceChanged() {} virtual void graphRebuilt() {} };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceError (const juce::String&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { scheduleRebuild(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { scheduleRebuild(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { scheduleRebuild(); }
    void valueTreeParentChanged (juce::ValueTree&) override { scheduleRebuild(); }
    void valueTreeRedirected (juce::ValueTree&) override { scheduleRebuild(); }
    void handleAsyncUpdate() override;
    void scheduleRebuild() { rebuildPending = true; triggerAsyncUpdate(); }

    void sourceLoaded (const juce::String&) override { scheduleRebuild(); }

    struct Audition : public RealtimeShared
    {
        AudioSourcePtr source; double positionSamples = 0, endSamples = 0, startBound = 0, speed = 1.0; bool reverse = false; float gain = 1.0f; bool finished = false;
        std::atomic<double> positionSeconds { 0.0 };
    };
    void renderAudition (juce::AudioBuffer<float>& out, int numSamples) noexcept;

    ProjectModel& project;
    SourceLibrary& sources;
    EffectFactory& effects;
    juce::ApplicationProperties& props;
    juce::AudioDeviceManager deviceManager;
    GraphBuilder builder;
    RealtimeSwap<RenderGraph> graph;
    RealtimeSwap<Audition> audition;
    Transport transport;
    Recorder recorder;
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<int> blockSize { 512 };
    std::atomic<int> xruns { 0 };
    std::atomic<bool> bypassEffects { false };
    bool rebuildPending = false;
    bool wasPlaying = false;
    juce::AudioBuffer<float> outBuffer;
    juce::ListenerList<Listener> listeners;
    juce::int64 lastCallbackEnd = -1;
    juce::CriticalSection graphBuildLock;
};
} // namespace mashup
