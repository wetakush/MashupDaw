#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <memory>
#include "TrackRenderer.h"
#include "Timeline/TempoMap.h"
#include "Core/RealtimeUtils.h"
#include "DSP/LoudnessMeterRT.h"
#include "RenderWorkers.h"

namespace mashup
{
class ProcessorChain;

/** Immutable snapshot of everything the audio thread needs to render the timeline. Built on the message
    thread by GraphBuilder and published through RealtimeSwap. Track renderers/clip players are shared with
    the previous graph when unchanged so playback state (stretchers) survives edits. */
class RenderGraph : public RealtimeShared
{
public:
    RenderGraph (double sampleRate, int maxBlockSize);
    ~RenderGraph();

    std::vector<std::shared_ptr<TrackRenderer>> tracks;
    std::shared_ptr<TempoMap> tempo;
    std::shared_ptr<ProcessorChain> masterChain;
    std::shared_ptr<ProcessorChain> busChains[2];
    std::shared_ptr<RealtimeParameter> masterVolume;
    std::shared_ptr<RealtimeParameter> busVolume[2];
    double sampleRate;
    int maxBlockSize;
    double endBeat = 0.0;    // last clip end, for auto-stop / export length

    /** Renders `numSamples` starting at timeline sample `startSample` into `out` (stereo, cleared by caller).
        Splits internally at tempo changes so every ClipPlayer sees constant tempo. */
    void process (juce::AudioBuffer<float>& out, juce::int64 startSample, int numSamples) noexcept;
    void notifyDiscontinuity() noexcept;

    std::atomic<float> masterPeakL { 0.0f }, masterPeakR { 0.0f };
    std::shared_ptr<dsp::LoudnessMeterRT> loudness;   // shared across rebuilds so the integrated value survives edits
    std::shared_ptr<RenderWorkers> workers;             // optional parallel renderer (null = sequential)

private:
    void processSegment (juce::AudioBuffer<float>& out, int outOffset, juce::int64 startSample, int numSamples) noexcept;
    juce::AudioBuffer<float> mix, bus[2], segView;
};
} // namespace mashup
