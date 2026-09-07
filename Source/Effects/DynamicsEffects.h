#pragma once
#include "BuiltInEffect.h"

namespace mashup
{
/** Feed-forward compressor with soft knee, program-dependent release option, makeup and mix. */
class CompressorEffect : public BuiltInEffect
{
public:
    CompressorEffect();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { env = 0.0f; }
    std::atomic<float> gainReductionDb { 0.0f };   // for meters
private:
    std::atomic<float>* threshold = nullptr; std::atomic<float>* ratio = nullptr; std::atomic<float>* attack = nullptr; std::atomic<float>* release = nullptr; std::atomic<float>* knee = nullptr; std::atomic<float>* makeup = nullptr; std::atomic<float>* mix = nullptr;
    float env = 0.0f; double sr = 48000.0;
    juce::AudioBuffer<float> dry;
};

/** Brickwall limiter with lookahead, ceiling and release. */
class LimiterEffect : public BuiltInEffect
{
public:
    LimiterEffect();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;
    std::atomic<float> gainReductionDb { 0.0f };
private:
    std::atomic<float>* ceiling = nullptr; std::atomic<float>* release = nullptr; std::atomic<float>* inputGain = nullptr; std::atomic<float>* lookaheadMs = nullptr;
    juce::AudioBuffer<float> delayBuf; int delayPos = 0, delaySamples = 0;
    std::vector<float> peakWindow; int peakPos = 0;
    float gain = 1.0f; double sr = 48000.0;
};

/** Noise gate / expander with hysteresis, attack, hold and release. */
class GateEffect : public BuiltInEffect
{
public:
    GateEffect();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { env = 0; gainState = 0; holdCounter = 0; }
private:
    std::atomic<float>* threshold = nullptr; std::atomic<float>* attack = nullptr; std::atomic<float>* hold = nullptr; std::atomic<float>* release = nullptr; std::atomic<float>* range = nullptr;
    float env = 0.0f, gainState = 0.0f; int holdCounter = 0; double sr = 48000.0;
};
} // namespace mashup
