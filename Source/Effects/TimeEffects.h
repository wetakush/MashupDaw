#pragma once
#include "BuiltInEffect.h"

namespace mashup
{
/** Stereo / ping-pong delay with tempo sync, feedback filtering and mix. */
class DelayEffect : public BuiltInEffect
{
public:
    DelayEffect();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { for (auto& d : lines) d.reset(); for (auto& f : hp) f.reset(); for (auto& f : lp) f.reset(); }
private:
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> lines[2] { juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> (192000), juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> (192000) };
    juce::dsp::IIR::Filter<float> hp[2], lp[2]; juce::SmoothedValue<float> timeSmoothed; double sr = 48000.0;
};

class ReverbEffect : public BuiltInEffect
{
public:
    ReverbEffect();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { reverb.reset(); predelay.reset(); }
private:
    juce::dsp::Reverb reverb; juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> predelay { 48000 }; juce::AudioBuffer<float> dry; double sr = 48000.0;
};

class ChorusEffect : public BuiltInEffect
{
public:
    ChorusEffect();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { chorus.reset(); }
private:
    juce::dsp::Chorus<float> chorus;
};

class FlangerEffect : public BuiltInEffect
{
public:
    FlangerEffect();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { chorus.reset(); }
private:
    juce::dsp::Chorus<float> chorus;
};

class PhaserEffect : public BuiltInEffect
{
public:
    PhaserEffect();
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override { phaser.reset(); }
private:
    juce::dsp::Phaser<float> phaser;
};
} // namespace mashup
