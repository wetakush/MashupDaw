#pragma once
#include <atomic>
#include <juce_core/juce_core.h>

namespace mashup
{
/** Lock-free transport state shared between UI and audio thread. Positions are timeline samples at the engine rate. */
class Transport
{
public:
    void play() noexcept                { playing.store (true, std::memory_order_release); }
    void stop() noexcept                { playing.store (false, std::memory_order_release); recording.store (false); }
    void togglePlay() noexcept          { if (isPlaying()) stop(); else play(); }
    bool isPlaying() const noexcept     { return playing.load (std::memory_order_acquire); }
    bool isRecording() const noexcept   { return recording.load (std::memory_order_acquire); }
    void setRecording (bool r) noexcept { recording.store (r); }

    /** Requests a jump; applied by the audio thread at the next block boundary (or immediately if stopped). */
    void locate (juce::int64 sample) noexcept
    {
        pendingLocate.store (std::max<juce::int64> (0, sample), std::memory_order_release);
        if (! isPlaying()) position.store (std::max<juce::int64> (0, sample), std::memory_order_release);
    }
    juce::int64 getPosition() const noexcept { return position.load (std::memory_order_acquire); }

    void setLoop (juce::int64 start, juce::int64 end, bool enabled) noexcept
    {
        loopStart.store (start); loopEnd.store (end); loopEnabled.store (enabled && end > start + 16);
    }
    bool isLoopEnabled() const noexcept { return loopEnabled.load(); }
    juce::int64 getLoopStart() const noexcept { return loopStart.load(); }
    juce::int64 getLoopEnd() const noexcept { return loopEnd.load(); }

    void setPreRoll (juce::int64 samples) noexcept { preRoll.store (samples); }
    juce::int64 getPreRoll() const noexcept { return preRoll.load(); }

    // --- audio thread ---
    /** Returns true and the new position if a locate was requested. */
    bool consumeLocate (juce::int64& newPos) noexcept
    {
        const auto p = pendingLocate.exchange (-1, std::memory_order_acq_rel);
        if (p < 0) return false;
        newPos = p; position.store (p, std::memory_order_release); return true;
    }
    void advance (juce::int64 samples) noexcept { position.fetch_add (samples, std::memory_order_acq_rel); }
    void setPositionRT (juce::int64 p) noexcept { position.store (p, std::memory_order_release); }

    std::atomic<bool> metronome { false };

private:
    std::atomic<bool> playing { false }, recording { false }, loopEnabled { false };
    std::atomic<juce::int64> position { 0 }, pendingLocate { -1 }, loopStart { 0 }, loopEnd { 0 }, preRoll { 0 };
};
} // namespace mashup
