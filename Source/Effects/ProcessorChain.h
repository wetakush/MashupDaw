#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <memory>

namespace mashup
{
/** Ordered list of AudioProcessors (built-in effects or hosted plugins) with per-slot bypass.
    Prepared on the message thread; processBlock is realtime-safe as long as the processors are. */
class ProcessorChain
{
public:
    struct Slot
    {
        std::unique_ptr<juce::AudioProcessor> processor;
        std::atomic<bool> bypass { false };
        juce::String effectId;
    };

    void prepare (double sampleRate, int maxBlockSize);
    void process (juce::AudioBuffer<float>& stereo, int numSamples) noexcept;
    void reset() noexcept;
    int getLatencySamples() const noexcept;

    std::vector<std::unique_ptr<Slot>> slots;   // filled before publishing

private:
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> view;
};
} // namespace mashup
