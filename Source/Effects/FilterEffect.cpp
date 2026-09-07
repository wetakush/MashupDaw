#include "FilterEffect.h"

namespace mashup
{
FilterEffect::FilterEffect()
    : BuiltInEffect ("Filter", { fx::pc ("type", "Type", { "Low-pass", "High-pass", "Band-pass" }, 0),
                                 fx::pf ("cutoff", "Cutoff", 20.0f, 20000.0f, 1000.0f, 1000.0f, "Hz"),
                                 fx::pf ("resonance", "Resonance", 0.1f, 10.0f, 0.707f, 1.0f),
                                 fx::pf ("drive", "Drive", 0.0f, 24.0f, 0.0f, 1.0f, "dB"),
                                 fx::pf ("mix", "Mix", 0.0f, 1.0f, 1.0f) })
{
    cutoff = rawPtr ("cutoff"); resonance = rawPtr ("resonance"); type = rawPtr ("type"); drive = rawPtr ("drive"); mix = rawPtr ("mix");
}

void FilterEffect::prepareToPlay (double sr, int bs)
{
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) bs, 2 };
    filter.prepare (spec);
    filter.reset();
    cutoffSmoothed.reset (sr, 0.02);
    cutoffSmoothed.setCurrentAndTargetValue (cutoff->load());
    dry.setSize (2, bs, false, true, true);
}

void FilterEffect::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    if (n > dry.getNumSamples()) return;
    const int t = (int) type->load();
    filter.setType (t == 0 ? juce::dsp::StateVariableTPTFilterType::lowpass : t == 1 ? juce::dsp::StateVariableTPTFilterType::highpass : juce::dsp::StateVariableTPTFilterType::bandpass);
    filter.setResonance (resonance->load());
    cutoffSmoothed.setTargetValue (cutoff->load());
    const float driveGain = juce::Decibels::decibelsToGain (drive->load());
    const float m = mix->load();
    for (int c = 0; c < 2; ++c) dry.copyFrom (c, 0, buffer, c, 0, n);

    float* l = buffer.getWritePointer (0); float* r = buffer.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        if (cutoffSmoothed.isSmoothing() || (i & 31) == 0) filter.setCutoffFrequency (cutoffSmoothed.getNextValue());
        float a = l[i] * driveGain, b = r[i] * driveGain;
        if (driveGain > 1.0f) { a = std::tanh (a); b = std::tanh (b); }
        l[i] = filter.processSample (0, a);
        r[i] = filter.processSample (1, b);
    }
    if (m < 1.0f)
        for (int c = 0; c < 2; ++c)
        {
            buffer.applyGain (c, 0, n, m);
            buffer.addFrom (c, 0, dry, c, 0, n, 1.0f - m);
        }
}
}
