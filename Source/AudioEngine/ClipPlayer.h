#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Import/AudioSource.h"
#include "Clips/ClipModel.h"
#include "Core/RealtimeUtils.h"
#include "DSP/Stretcher.h"

namespace mashup
{
/** Renders one clip into a stereo buffer. Owns its (pre-allocated) stretcher.
    Static parameters (source, mode, positions) are fixed at construction; live ones are atomics. */
class ClipPlayer
{
public:
    struct Static
    {
        juce::String clipId;
        AudioSourcePtr source;
        double startBeat = 0, lengthBeats = 0;
        double offsetSeconds = 0;        // source position at clip start
        double sourceEndSeconds = 0;     // loop region end in source (0 = source length)
        bool reverse = false, loop = false;
        StretchMode mode = StretchMode::Realtime;
        bool synced = false;
        double clipBpm = 120.0;
        FadeShape fadeInShape = FadeShape::Linear, fadeOutShape = FadeShape::Linear;

        bool sameAs (const Static& o) const noexcept
        {
            return clipId == o.clipId && source == o.source && startBeat == o.startBeat && lengthBeats == o.lengthBeats
                && offsetSeconds == o.offsetSeconds && sourceEndSeconds == o.sourceEndSeconds && reverse == o.reverse
                && loop == o.loop && mode == o.mode && synced == o.synced && clipBpm == o.clipBpm;
        }
    };

    struct Live
    {
        std::atomic<float> gain { 1.0f }, pan { 0.0f }, rate { 1.0f }, pitchScale { 1.0f }, formantScale { 1.0f };
        std::atomic<float> fadeInBeats { 0.0f }, fadeOutBeats { 0.0f };
        std::atomic<bool> mute { false };
    };

    ClipPlayer (Static s, double deviceSampleRate, int maxBlockSize);

    const Static& getStatic() const noexcept { return st; }
    Live& live() noexcept { return lv; }

    /** Context for one contiguous render segment (constant tempo). */
    struct Segment
    {
        double startTimeSeconds;   // timeline time at output sample 0
        double startBeat;          // timeline beat at output sample 0
        double bpm;                // constant across the segment
        int numSamples;
        double sampleRate;
    };

    /** Adds the clip's audio (already gained/panned) into `out` (2 channels, >= numSamples). Realtime-safe. */
    void render (juce::AudioBuffer<float>& out, const Segment& seg) noexcept;

    /** Informs the player that the next render is not contiguous with the previous one. */
    void notifyDiscontinuity() noexcept { contiguous = false; }

    bool isActiveAt (double beat) const noexcept { return beat >= st.startBeat && beat < st.startBeat + st.lengthBeats; }
    bool overlaps (double beatStart, double beatEnd) const noexcept { return beatStart < st.startBeat + st.lengthBeats && beatEnd > st.startBeat; }

private:
    double sourceTimeAtBeat (double beat, double bpm) const noexcept;
    float fadeGain (double beatInClip) const noexcept;
    void renderRepitch (float* l, float* r, int n, double srcPosSamples, double speedSamples) noexcept;
    void renderStretched (float* l, float* r, int n, double srcPosSamples, double speed) noexcept;
    void readSourceInterpolated (int ch, double pos, float& out) const noexcept;
    void feedStretcher (int frames) noexcept;
    double wrapSourcePos (double pos) const noexcept;

    Static st;
    Live lv;
    double deviceRate;
    std::unique_ptr<dsp::Stretcher> stretcher;
    juce::AudioBuffer<float> scratch;      // clip output before gain/pan
    juce::AudioBuffer<float> feed;         // input block for the stretcher
    double feedPos = 0.0;                  // source read position (samples) for the stretcher input
    juce::int64 lastEndSample = -1;        // for contiguity detection
    bool contiguous = false;
    int dropSamples = 0;
    double loopStartS = 0, loopEndS = 0;   // source loop region in samples
};
} // namespace mashup
