#include "RenderWorkers.h"
#include <juce_core/juce_core.h>

namespace mashup
{
class RenderWorkers::Worker : public juce::Thread
{
public:
    Worker (RenderWorkers& o, int i) : Thread ("render worker " + juce::String (i)), owner (o) {}
    void run() override { owner.workerLoop(); }
    RenderWorkers& owner;
};

RenderWorkers::RenderWorkers (int n)
{
    for (int i = 0; i < juce::jmax (0, n); ++i)
    {
        auto w = std::make_unique<Worker> (*this, i);
        if (! w->startRealtimeThread (juce::Thread::RealtimeOptions().withPriority (9))) w->startThread (juce::Thread::Priority::highest);
        threads.push_back (std::move (w));
    }
}

RenderWorkers::~RenderWorkers()
{
    { std::lock_guard<std::mutex> l (mutex); quit.store (true); generation.fetch_add (1); }
    cv.notify_all();
    for (auto& t : threads) t->stopThread (2000);
}

void RenderWorkers::renderNext() noexcept
{
    for (;;)
    {
        const int i = nextIndex.fetch_add (1, std::memory_order_acq_rel);
        if (jobTracks == nullptr || i >= (int) jobTracks->size()) return;
        (*jobTracks)[(size_t) i]->render (*jobSeg, jobSolo);
        remaining.fetch_sub (1, std::memory_order_acq_rel);
    }
}

void RenderWorkers::workerLoop()
{
    uint64_t seen = 0;
    while (! quit.load())
    {
        {
            std::unique_lock<std::mutex> l (mutex);
            cv.wait_for (l, std::chrono::milliseconds (200), [&] { return quit.load() || generation.load() != seen; });
            if (generation.load() == seen) continue;
            seen = generation.load();
        }
        if (quit.load()) break;
        renderNext();
    }
}

void RenderWorkers::renderAll (std::vector<std::shared_ptr<TrackRenderer>>& tracks, const ClipPlayer::Segment& seg, bool anySolo) noexcept
{
    if (tracks.empty()) return;
    if (threads.empty() || tracks.size() == 1) { for (auto& t : tracks) t->render (seg, anySolo); return; }
    jobTracks = &tracks; jobSeg = &seg; jobSolo = anySolo;
    nextIndex.store (0, std::memory_order_release);
    remaining.store ((int) tracks.size(), std::memory_order_release);
    generation.fetch_add (1, std::memory_order_acq_rel);
    cv.notify_all();
    renderNext();   // the audio thread takes part
    // wait for stragglers (bounded spin; workers are RT so this is short)
    int spins = 0;
    while (remaining.load (std::memory_order_acquire) > 0)
    {
        if (++spins > 2000) { std::this_thread::yield(); }
    }
    jobTracks = nullptr;
}
} // namespace mashup
