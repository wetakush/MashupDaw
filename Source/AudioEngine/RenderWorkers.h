#pragma once
#include <vector>
#include <juce_core/juce_core.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include "TrackRenderer.h"

namespace mashup
{
/** Renders the tracks of a block in parallel on realtime-priority worker threads. The audio thread joins in and
    spins until every track has been rendered. No allocation happens per block. */
class RenderWorkers
{
public:
    explicit RenderWorkers (int numThreads);
    ~RenderWorkers();

    /** Blocks until all tracks have rendered. Realtime-safe apart from the futex wake of the workers. */
    void renderAll (std::vector<std::shared_ptr<TrackRenderer>>& tracks, const ClipPlayer::Segment& seg, bool anySolo) noexcept;
    int getNumThreads() const noexcept { return (int) threads.size(); }

private:
    void workerLoop();
    void renderNext() noexcept;

    class Worker;
    std::vector<std::unique_ptr<Worker>> threads;
    std::mutex mutex; std::condition_variable cv;
    std::atomic<uint64_t> generation { 0 };
    std::atomic<bool> quit { false };
    // current job
    std::vector<std::shared_ptr<TrackRenderer>>* jobTracks = nullptr;
    const ClipPlayer::Segment* jobSeg = nullptr;
    bool jobSolo = false;
    std::atomic<int> nextIndex { 0 }, remaining { 0 };
};
} // namespace mashup
