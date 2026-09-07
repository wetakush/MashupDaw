#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <memory>
#include "ClipPlayer.h"
#include "Automation/AutomationSet.h"

namespace mashup
{
class ProcessorChain;

/** Mixes a track's clips and applies the channel strip. One per TRACK node, reused between graph rebuilds. */
class TrackRenderer
{
public:
    TrackRenderer (juce::String trackId, double sampleRate, int maxBlockSize);
    ~TrackRenderer();

    const juce::String id;

    struct Live
    {
        RealtimeParameter volume { 1.0f }, pan { 0.0f }, inputGain { 1.0f };
        std::atomic<bool> mute { false }, solo { false }, phaseInvert { false };
        std::atomic<float> sendLevel[2] { { 0.0f }, { 0.0f } };   // reverb, delay
    };
    Live& live() noexcept { return lv; }

    std::vector<std::shared_ptr<ClipPlayer>> clips;   // set by the builder before publishing
    std::shared_ptr<const AutomationSet> automation;   // may be null
    std::atomic<bool> automationPlayback { true };     // false while this track's automation is being recorded
    void setEffectChain (std::unique_ptr<ProcessorChain>);
    ProcessorChain* getEffectChain() const noexcept { return chain.get(); }

    /** Renders the track into its own buffer (post-fader) and fills sendBuffer[0..1]. Realtime-safe, thread-safe
        w.r.t. other tracks (touches only its own state). The graph sums the buffers afterwards. */
    void render (const ClipPlayer::Segment& seg, bool anySolo) noexcept;
    juce::AudioBuffer<float> sendBuffer[2];
    int lastRendered = 0;   // samples rendered in the last call
    void notifyDiscontinuity() noexcept { for (auto& c : clips) c->notifyDiscontinuity(); }

    // metering (audio thread writes, UI reads)
    std::atomic<float> peakL { 0.0f }, peakR { 0.0f };
    float readAndResetPeak (int ch) noexcept { auto& p = ch == 0 ? peakL : peakR; return p.exchange (0.0f); }

    juce::AudioBuffer<float>& getBuffer() noexcept { return buffer; }
    bool wasAudible() const noexcept { return audible; }

private:
    Live lv;
    juce::AudioBuffer<float> buffer;
    std::unique_ptr<ProcessorChain> chain;
    bool audible = false;
};
} // namespace mashup
