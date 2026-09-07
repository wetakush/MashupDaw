#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>
#include <memory>
#include <functional>
#include "Import/AudioSource.h"

namespace mashup
{
/** Multi-resolution min/max/RMS mip-map of one AudioSource. Built once on a worker thread, read by the UI.
    Levels: 256, 1024, 4096, 16384, 65536 samples per bin. */
class WaveformCache
{
public:
    static constexpr int numLevels = 5;
    static constexpr int baseBin = 256;
    static int samplesPerBin (int level) noexcept { return baseBin << (2 * level); }

    struct Bin { float min = 0, max = 0, rms = 0; };

    explicit WaveformCache (AudioSourcePtr source);

    /** Builds all levels. Calls `progress` (0..1) and stops early if it returns false. Worker thread. */
    bool build (const std::function<bool (float)>& progress = {});
    bool isReady() const noexcept { return ready.load (std::memory_order_acquire); }

    int getNumChannels() const noexcept { return channels; }
    juce::int64 getLengthSamples() const noexcept { return length; }
    double getSampleRate() const noexcept { return sampleRate; }
    AudioSourcePtr getSource() const noexcept { return source; }

    /** Best level whose bin is <= samplesPerPixel (or 0). Returns -1 if raw samples should be read directly. */
    int chooseLevel (double samplesPerPixel) const noexcept;

    /** Aggregates the source range [startSample, endSample) into one Bin for channel ch. Thread-safe once ready. */
    Bin summarise (int ch, juce::int64 startSample, juce::int64 endSample) const noexcept;

    /** Fills `out` (numPixels bins) covering [startSample, endSample) in source samples (may be reversed if start > end). */
    void render (int ch, double startSample, double endSample, int numPixels, std::vector<Bin>& out) const noexcept;

    bool saveTo (const juce::File&) const;
    bool loadFrom (const juce::File&);

private:
    AudioSourcePtr source;
    int channels = 0;
    juce::int64 length = 0;
    double sampleRate = 0;
    std::vector<std::vector<Bin>> levels[numLevels];   // [level][channel][bin]
    std::atomic<bool> ready { false };
};

using WaveformCachePtr = std::shared_ptr<WaveformCache>;
} // namespace mashup
