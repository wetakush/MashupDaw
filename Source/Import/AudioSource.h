#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace mashup
{
/** Immutable decoded audio held in memory. Shared between the message thread, workers and the audio thread
    via shared_ptr; never modified after construction, so no locking is required. */
class AudioSource
{
public:
    AudioSource (juce::String id_, juce::File file_, juce::AudioBuffer<float>&& data, double sampleRate_)
        : id (std::move (id_)), file (std::move (file_)), buffer (std::move (data)), sampleRate (sampleRate_) {}

    const juce::String id;
    const juce::File file;
    const juce::AudioBuffer<float> buffer;
    const double sampleRate;

    int getNumChannels() const noexcept   { return buffer.getNumChannels(); }
    juce::int64 getLengthSamples() const noexcept { return buffer.getNumSamples(); }
    double getLengthSeconds() const noexcept { return buffer.getNumSamples() / sampleRate; }

    /** Realtime-safe sample fetch with bounds check (returns 0 outside the buffer). */
    float getSample (int channel, juce::int64 index) const noexcept
    {
        if (index < 0 || index >= buffer.getNumSamples()) return 0.0f;
        return buffer.getReadPointer (juce::jmin (channel, buffer.getNumChannels() - 1))[index];
    }
};

using AudioSourcePtr = std::shared_ptr<const AudioSource>;
} // namespace mashup
