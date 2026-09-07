#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>

namespace mashup
{
/** Captures device input while the transport records. The audio thread writes into a lock-free ring buffer;
    a message-thread timer drains it into a growing buffer. On stop, the take is handed to a callback. */
class Recorder : private juce::Timer
{
public:
    Recorder();
    ~Recorder() override;

    void prepare (double sampleRate, int numInputChannels);

    /** Message thread: begin/finish a take. */
    void start (juce::int64 timelineStartSample);
    void stop();
    bool isRecording() const noexcept { return active.load(); }
    juce::int64 getTakeStartSample() const noexcept { return takeStart; }
    double getRecordedSeconds() const noexcept { return sampleRate > 0 ? take.getNumSamples() / sampleRate : 0.0; }

    /** Audio thread. */
    void pushInput (const float* const* input, int numChannels, int numSamples) noexcept;

    std::function<void (juce::AudioBuffer<float>&& take, double sampleRate, juce::int64 timelineStart)> onTakeFinished;

private:
    void timerCallback() override;
    void drain();
    juce::AbstractFifo fifo { 1 << 19 };
    juce::AudioBuffer<float> ring;
    juce::AudioBuffer<float> take;
    int takeLength = 0;
    std::atomic<bool> active { false };
    std::atomic<int> overruns { 0 };
    double sampleRate = 48000.0;
    int channels = 2;
    juce::int64 takeStart = 0;
};
} // namespace mashup
