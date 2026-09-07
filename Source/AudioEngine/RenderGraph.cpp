#include "RenderGraph.h"
#include "Effects/ProcessorChain.h"
#include "Effects/EffectContext.h"

namespace mashup
{
RenderGraph::RenderGraph (double sr, int mbs) : sampleRate (sr), maxBlockSize (juce::jmax (64, mbs))
{
    mix.setSize (2, maxBlockSize, false, true, true);
    bus[0].setSize (2, maxBlockSize, false, true, true);
    bus[1].setSize (2, maxBlockSize, false, true, true);
    masterVolume = std::make_shared<RealtimeParameter> (1.0f);
    busVolume[0] = std::make_shared<RealtimeParameter> (1.0f);
    busVolume[1] = std::make_shared<RealtimeParameter> (1.0f);
    tempo = std::make_shared<TempoMap> (120.0);
    loudness = std::make_shared<dsp::LoudnessMeterRT>(); loudness->prepare (sr);
}
RenderGraph::~RenderGraph() = default;

void RenderGraph::notifyDiscontinuity() noexcept { for (auto& t : tracks) t->notifyDiscontinuity(); }

void RenderGraph::process (juce::AudioBuffer<float>& out, juce::int64 startSample, int numSamples) noexcept
{
    int done = 0;
    const auto& markers = tempo->getMarkers();
    while (done < numSamples)
    {
        int n = juce::jmin (numSamples - done, maxBlockSize);
        // split at the next tempo marker if inside this chunk
        const double t0 = (startSample + done) / sampleRate;
        const double b0 = tempo->timeToBeat (t0);
        for (const auto& m : markers)
            if (m.beat > b0 + 1.0e-9)
            {
                const double tm = tempo->beatToTime (m.beat);
                const int k = (int) std::ceil ((tm - t0) * sampleRate);
                if (k > 0 && k < n) n = k;
                break;
            }
        processSegment (out, done, startSample + done, n);
        done += n;
    }
}

void RenderGraph::processSegment (juce::AudioBuffer<float>& out, int outOffset, juce::int64 startSample, int n) noexcept
{
    ClipPlayer::Segment seg;
    seg.startTimeSeconds = startSample / sampleRate;
    seg.startBeat = tempo->timeToBeat (seg.startTimeSeconds);
    seg.bpm = tempo->bpmAtBeat (seg.startBeat + 1.0e-9);
    seg.numSamples = n;
    seg.sampleRate = sampleRate;
    EffectContext::currentBpm().store (seg.bpm, std::memory_order_relaxed);

    mix.clear (0, n); bus[0].clear (0, n); bus[1].clear (0, n);
    bool anySolo = false;
    for (auto& t : tracks) if (t->live().solo.load (std::memory_order_relaxed)) { anySolo = true; break; }
    if (workers) workers->renderAll (tracks, seg, anySolo); else for (auto& t : tracks) t->render (seg, anySolo);
    for (auto& t : tracks)
    {
        const int k = juce::jmin (n, t->lastRendered);
        mix.addFrom (0, 0, t->getBuffer(), 0, 0, k); mix.addFrom (1, 0, t->getBuffer(), 1, 0, k);
        for (int b = 0; b < 2; ++b) { bus[b].addFrom (0, 0, t->sendBuffer[b], 0, 0, k); bus[b].addFrom (1, 0, t->sendBuffer[b], 1, 0, k); }
    }

    for (int b = 0; b < 2; ++b)
    {
        if (busChains[b]) busChains[b]->process (bus[b], n);
        float* l = bus[b].getWritePointer (0); float* r = bus[b].getWritePointer (1);
        for (int i = 0; i < n; ++i) { const float v = busVolume[b]->next (0.02f); l[i] *= v; r[i] *= v; }
        mix.addFrom (0, 0, bus[b], 0, 0, n); mix.addFrom (1, 0, bus[b], 1, 0, n);
    }
    if (masterChain) masterChain->process (mix, n);

    float* l = mix.getWritePointer (0); float* r = mix.getWritePointer (1);
    float pk[2] = { 0, 0 };
    for (int i = 0; i < n; ++i)
    {
        const float v = masterVolume->next (0.02f);
        l[i] *= v; r[i] *= v;
        pk[0] = juce::jmax (pk[0], std::abs (l[i])); pk[1] = juce::jmax (pk[1], std::abs (r[i]));
    }
    if (pk[0] > masterPeakL.load (std::memory_order_relaxed)) masterPeakL.store (pk[0], std::memory_order_relaxed);
    if (pk[1] > masterPeakR.load (std::memory_order_relaxed)) masterPeakR.store (pk[1], std::memory_order_relaxed);
    loudness->process (l, r, n);
    out.addFrom (0, outOffset, mix, 0, 0, n);
    out.addFrom (1, outOffset, mix, 1, 0, n);
}
} // namespace mashup
