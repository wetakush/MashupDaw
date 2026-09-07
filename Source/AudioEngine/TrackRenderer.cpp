#include "TrackRenderer.h"
#include "Effects/ProcessorChain.h"

namespace mashup
{
TrackRenderer::TrackRenderer (juce::String trackId, double, int maxBlockSize) : id (std::move (trackId))
{
    buffer.setSize (2, juce::jmax (64, maxBlockSize), false, true, true);
    for (auto& b : sendBuffer) b.setSize (2, juce::jmax (64, maxBlockSize), false, true, true);
}
TrackRenderer::~TrackRenderer() = default;

void TrackRenderer::setEffectChain (std::unique_ptr<ProcessorChain> c) { chain = std::move (c); }

void TrackRenderer::render (const ClipPlayer::Segment& seg, bool anySolo) noexcept
{
    const int n = juce::jmin (seg.numSamples, buffer.getNumSamples());
    lastRendered = n;
    buffer.clear (0, n);
    if (automation && automationPlayback.load (std::memory_order_relaxed))
    {
        const double b = seg.startBeat;
        if (! automation->volume.isEmpty()) lv.volume.set ((float) AutomationCurve::normToVolume (automation->volume.valueAt (b)));
        if (! automation->pan.isEmpty()) lv.pan.set ((float) AutomationCurve::normToPan (automation->pan.valueAt (b)));
        for (int k = 0; k < 2; ++k) if (! automation->send[k].isEmpty()) lv.sendLevel[k].store (automation->send[k].valueAt (b), std::memory_order_relaxed);
        for (const auto& f : automation->fx) if (f.param && ! f.curve.isEmpty()) f.param->setValue (f.curve.valueAt (b));
    }
    bool any = false;
    for (auto& c : clips)
    {
        if (c->overlaps (seg.startBeat, seg.startBeat + n * seg.bpm / 60.0 / seg.sampleRate)) { c->render (buffer, seg); any = true; }
        else c->notifyDiscontinuity();
    }
    const bool muted = lv.mute.load (std::memory_order_relaxed) || (anySolo && ! lv.solo.load (std::memory_order_relaxed));
    audible = any && ! muted;

    // input gain & phase
    const float ig = lv.inputGain.next (0.05f) * (lv.phaseInvert.load (std::memory_order_relaxed) ? -1.0f : 1.0f);
    if (ig != 1.0f) buffer.applyGain (0, n, ig);

    if (chain && any) chain->process (buffer, n);   // keep effects tails alive even while muted? keep simple: only when audio present

    float* l = buffer.getWritePointer (0); float* r = buffer.getWritePointer (1);
    // volume + pan (per-sample smoothed)
    const float panT = lv.pan.getTarget();
    const float angle = (juce::jlimit (-1.0f, 1.0f, panT) + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    const float pl = std::cos (angle) * juce::MathConstants<float>::sqrt2, pr = std::sin (angle) * juce::MathConstants<float>::sqrt2;
    float pk[2] = { 0, 0 };
    for (int i = 0; i < n; ++i)
    {
        const float v = lv.volume.next (0.02f) * (muted ? 0.0f : 1.0f);
        l[i] *= v * pl; r[i] *= v * pr;
        pk[0] = juce::jmax (pk[0], std::abs (l[i])); pk[1] = juce::jmax (pk[1], std::abs (r[i]));
    }
    if (pk[0] > peakL.load (std::memory_order_relaxed)) peakL.store (pk[0], std::memory_order_relaxed);
    if (pk[1] > peakR.load (std::memory_order_relaxed)) peakR.store (pk[1], std::memory_order_relaxed);

    for (int b = 0; b < 2; ++b)
    {
        const float lvl = lv.sendLevel[b].load (std::memory_order_relaxed);
        if (lvl > 0.0001f && ! muted) { sendBuffer[b].copyFrom (0, 0, buffer, 0, 0, n); sendBuffer[b].copyFrom (1, 0, buffer, 1, 0, n); sendBuffer[b].applyGain (0, n, lvl); }
        else sendBuffer[b].clear (0, n);
    }
}
} // namespace mashup
