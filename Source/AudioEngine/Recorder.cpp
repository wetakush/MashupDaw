#include "Recorder.h"

namespace mashup
{
Recorder::Recorder() { ring.setSize (2, fifo.getTotalSize(), false, true, true); }
Recorder::~Recorder() { stopTimer(); }

void Recorder::prepare (double sr, int) { sampleRate = sr; channels = 2; }

void Recorder::start (juce::int64 startSample)
{
    takeStart = startSample;
    take.setSize (2, (int) (sampleRate * 30), false, true, true);
    takeLength = 0;
    fifo.reset();
    active.store (true);
    startTimer (50);
}

void Recorder::stop()
{
    if (! active.exchange (false)) return;
    stopTimer();
    drain();
    take.setSize (2, takeLength, true, true, true);
    if (onTakeFinished && takeLength > 0) onTakeFinished (std::move (take), sampleRate, takeStart);
    take = {};
}

void Recorder::pushInput (const float* const* input, int numChannels, int numSamples) noexcept
{
    if (! active.load (std::memory_order_relaxed) || numChannels <= 0) return;
    auto scope = fifo.write (numSamples);
    if (scope.blockSize1 + scope.blockSize2 < numSamples) { overruns.fetch_add (1); return; }
    for (int c = 0; c < 2; ++c)
    {
        const float* src = input[juce::jmin (c, numChannels - 1)];
        if (src == nullptr) { ring.clear (c, scope.startIndex1, scope.blockSize1); if (scope.blockSize2) ring.clear (c, scope.startIndex2, scope.blockSize2); continue; }
        ring.copyFrom (c, scope.startIndex1, src, scope.blockSize1);
        if (scope.blockSize2 > 0) ring.copyFrom (c, scope.startIndex2, src + scope.blockSize1, scope.blockSize2);
    }
}

void Recorder::drain()
{
    const int ready = fifo.getNumReady();
    if (ready <= 0) return;
    if (takeLength + ready > take.getNumSamples()) take.setSize (2, (takeLength + ready) * 2, true, false, true);
    auto scope = fifo.read (ready);
    for (int c = 0; c < 2; ++c)
    {
        take.copyFrom (c, takeLength, ring, c, scope.startIndex1, scope.blockSize1);
        if (scope.blockSize2 > 0) take.copyFrom (c, takeLength + scope.blockSize1, ring, c, scope.startIndex2, scope.blockSize2);
    }
    takeLength += ready;
}

void Recorder::timerCallback() { drain(); }
} // namespace mashup
