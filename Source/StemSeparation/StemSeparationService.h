#pragma once
#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>
#include <deque>
#include "Import/SourceLibrary.h"

namespace mashup
{
class ProjectModel;

/** Runs Demucs (PyTorch, CUDA with CPU fallback) in an external Python worker and turns the resulting stems into
    sources + tracks. Jobs are queued and processed one at a time; the active job can be cancelled. */
class StemSeparationService : public juce::ChangeBroadcaster, private juce::Thread, private juce::AsyncUpdater
{
public:
    enum class Mode { FourStems, Acapella, Instrumental };

    struct Job
    {
        int id = 0;
        juce::String sourceId, sourceName;
        juce::File inputFile, outputDir;
        Mode mode = Mode::FourStems;
        juce::String model = "htdemucs";
        juce::String device = "auto";
        // status
        enum class State { Queued, Running, Done, Failed, Cancelled } state = State::Queued;
        float progress = 0.0f;
        juce::String stage, error, usedDevice;
        std::map<juce::String, juce::File> files;   // stem name -> file
    };

    StemSeparationService (ProjectModel&, SourceLibrary&, std::function<juce::File()> cacheDirProvider);
    ~StemSeparationService() override;

    /** Queues separation of a project source. Returns the job id. */
    int separate (const juce::String& sourceId, Mode, const juce::String& model = "htdemucs", const juce::String& device = "auto");
    void cancel (int jobId);
    void clearFinished();

    const std::vector<Job>& getJobs() const { return jobs; }
    bool isBusy() const noexcept { return isThreadRunning(); }

    /** Environment check: python interpreter used and whether it looks usable. */
    juce::File getPythonExecutable() const;
    juce::File getWorkerScript() const;
    juce::String getEnvironmentStatus() const;   // human readable
    static juce::String modeName (Mode m) { return m == Mode::FourStems ? "4 stems" : m == Mode::Acapella ? "acapella" : "instrumental"; }

    /** Called on the message thread when a job finished successfully; adds sources/tracks. */
    std::function<void (const Job&)> onJobFinished;
    bool muteOriginalAfterSeparation = true;

private:
    void run() override;   // worker thread: runs queued jobs sequentially
    void handleAsyncUpdate() override;
    void postUpdate (int jobId, std::function<void (Job&)> update);
    Job* findJob (int id);
    void processJob (Job jobCopy);

    ProjectModel& project;
    SourceLibrary& sources;
    std::function<juce::File()> cacheDir;
    std::vector<Job> jobs;                // message thread view
    std::deque<int> queue;                // ids waiting; guarded by lock
    juce::CriticalSection lock;
    std::vector<std::pair<int, std::function<void (Job&)>>> pendingUpdates;
    std::unique_ptr<juce::ChildProcess> activeProcess;
    std::atomic<int> activeJobId { -1 }, cancelRequested { -1 };
    int nextId = 1;
};
} // namespace mashup
