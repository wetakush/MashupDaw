#pragma once
#include "BuiltInEffect.h"

namespace mashup
{
/** Multimode state-variable filter (LP/HP/BP) with resonance and drive. */
class FilterEffect : public BuiltInEffect
{
public:
    FilterEffect();
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { filter.reset(); }
private:
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::SmoothedValue<float> cutoffSmoothed;
    std::atomic<float>* cutoff = nullptr; std::atomic<float>* resonance = nullptr; std::atomic<float>* type = nullptr; std::atomic<float>* drive = nullptr; std::atomic<float>* mix = nullptr;
    juce::AudioBuffer<float> dry;
};
}
