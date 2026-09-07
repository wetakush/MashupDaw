#pragma once
#include "BuiltInEffect.h"

namespace mashup
{
/** 6-band parametric EQ: HP, low shelf, 2 peaks, high shelf, LP. */
class ParametricEQEffect : public BuiltInEffect
{
public:
    ParametricEQEffect();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { for (auto& band : filters) for (auto& f : band) f.reset(); }
private:
    void updateCoefficients();
    juce::dsp::IIR::Filter<float> filters[6][2];
    double sr = 48000.0; float lastParams[20] {};
};

/** Analogue-style saturation: drive, tone, asymmetry, mix. */
class SaturationEffect : public BuiltInEffect
{
public:
    SaturationEffect();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { for (int c = 0; c < 2; ++c) { tone[c].reset(); dc[c].reset(); } }
private:
    juce::dsp::IIR::Filter<float> tone[2], dc[2]; juce::AudioBuffer<float> dry; double sr = 48000.0;
};

/** Distortion with selectable waveshape and 2x oversampling. */
class DistortionEffect : public BuiltInEffect
{
public:
    DistortionEffect();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { oversampling.reset(); lp.reset(); }
private:
    juce::dsp::Oversampling<float> oversampling { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR };
    juce::dsp::StateVariableTPTFilter<float> lp; juce::AudioBuffer<float> dry;
};

/** Stereo widener: mid/side width, Haas micro-delay and mono-below frequency. */
class StereoWidenerEffect : public BuiltInEffect
{
public:
    StereoWidenerEffect();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { delay.reset(); lowMid.reset(); lowSide.reset(); }
private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delay { 4800 };
    juce::dsp::IIR::Filter<float> lowMid, lowSide; double sr = 48000.0;
};
} // namespace mashup
