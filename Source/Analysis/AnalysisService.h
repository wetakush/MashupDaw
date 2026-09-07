#pragma once
#include <juce_events/juce_events.h>
#include <mutex>
#include <set>
#include "Import/SourceLibrary.h"
#include "AnalysisData.h"

namespace mashup
{
class ProjectModel;

/** Runs the full analysis chain (loudness, peak, BPM, beat grid, downbeats, key, transients, phrases) for a
    source on the thread pool and writes the results into its SOURCE node on the message thread. */
class AnalysisService : public juce::ChangeBroadcaster, private SourceLibrary::Listener, private juce::AsyncUpdater
{
public:
    struct Result
    {
        juce::String sourceId;
        double bpm = 0, bpmConfidence = 0;
        int keyRoot = -1, keyMode = 0; double keyConfidence = 0;
        double lufs = -100, truePeak = 0, lra = 0;
        std::vector<double> beats, downbeats, transients;
        std::vector<analysis::Phrase> phrases;
        double firstDownbeat = 0;
    };

    AnalysisService (ProjectModel&, SourceLibrary&, juce::ThreadPool&);
    ~AnalysisService() override;

    /** Queues analysis (runs when the source has finished decoding). Re-analyses if `force`. */
    void analyseSource (const juce::String& sourceId, bool force = false);
    bool isAnalysing (const juce::String& sourceId) const;
    float getProgress (const juce::String& sourceId) const;

    /** Synchronous analysis of a buffer (used by tests and the mashup assistant). */
    static Result analyseBuffer (const juce::AudioBuffer<float>&, double sampleRate, const std::function<bool (float)>& progress = {});

    /** Message thread: writes the result into the SOURCE node (and updates clips that reference it). */
    void applyResult (const Result&);

private:
    void sourceLoaded (const juce::String& sourceId) override;
    void handleAsyncUpdate() override;
    void startJob (const juce::String& sourceId);

    ProjectModel& project;
    SourceLibrary& sources;
    juce::ThreadPool& pool;
    mutable std::mutex lock;
    std::set<juce::String> pending, running;
    std::map<juce::String, float> progress;
    std::vector<Result> finished;
    class Job; friend class Job;
};
} // namespace mashup
