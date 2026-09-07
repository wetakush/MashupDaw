#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include "Clips/ClipModel.h"

namespace RubberBand { class RubberBandStretcher; }

namespace mashup::dsp
{
/** Realtime-safe wrapper around Rubber Band (R3 engine) in realtime mode.
    Construction/reset allocate; process()/retrieve() do not. */
class Stretcher
{
public:
    Stretcher (double sampleRate, int numChannels, StretchMode mode, int maxBlockSize);
    ~Stretcher();

    static bool usesStretcher (StretchMode m) noexcept { return m != StretchMode::Repitch; }

    /** speed = source samples consumed per output sample (1.0 = unity, 2.0 = twice as fast). */
    void setSpeed (double speed) noexcept;
    void setPitchScale (double scale) noexcept;
    void setFormantScale (double scale) noexcept;
    void reset() noexcept;

    /** Number of input frames the stretcher wants before more output becomes available. */
    int getSamplesRequired() const noexcept;
    /** Push planar input frames. */
    void process (const float* const* input, int numFrames, bool finalBlock) noexcept;
    int available() const noexcept;
    int retrieve (float* const* output, int numFrames) noexcept;

    /** Zeros to feed after reset() so the first real output sample is aligned, and how many output samples to drop. */
    int getPreferredStartPad() const noexcept;
    int getStartDelay() const noexcept;

    int getNumChannels() const noexcept { return channels; }
    StretchMode getMode() const noexcept { return mode; }

private:
    std::unique_ptr<RubberBand::RubberBandStretcher> rb;
    int channels;
    StretchMode mode;
};

/** Offline, highest quality stretch of a whole buffer (worker thread). */
juce::AudioBuffer<float> stretchOffline (const juce::AudioBuffer<float>& in, double sampleRate, double timeRatio,
                                         double pitchScale, StretchMode mode, const std::function<bool (float)>& progress = {});
} // namespace mashup::dsp
