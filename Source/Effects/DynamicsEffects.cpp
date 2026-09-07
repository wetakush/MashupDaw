#include "DynamicsEffects.h"

namespace mashup
{
// ---- Compressor -------------------------------------------------------------------------------------------------------
CompressorEffect::CompressorEffect()
    : BuiltInEffect ("Compressor", { fx::pf ("threshold", "Threshold", -60.0f, 0.0f, -18.0f, 1.0f, "dB"), fx::pf ("ratio", "Ratio", 1.0f, 20.0f, 4.0f, 4.0f, ":1"),
                                     fx::pf ("attack", "Attack", 0.1f, 200.0f, 10.0f, 20.0f, "ms"), fx::pf ("release", "Release", 5.0f, 2000.0f, 120.0f, 200.0f, "ms"),
                                     fx::pf ("knee", "Knee", 0.0f, 24.0f, 6.0f, 1.0f, "dB"), fx::pf ("makeup", "Makeup", -12.0f, 24.0f, 0.0f, 1.0f, "dB"), fx::pf ("mix", "Mix", 0.0f, 1.0f, 1.0f) })
{
    threshold = rawPtr ("threshold"); ratio = rawPtr ("ratio"); attack = rawPtr ("attack"); release = rawPtr ("release"); knee = rawPtr ("knee"); makeup = rawPtr ("makeup"); mix = rawPtr ("mix");
}
void CompressorEffect::prepareToPlay (double s, int bs) { sr = s; dry.setSize (2, bs, false, true, true); env = 0; }

void CompressorEffect::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    const int n = b.getNumSamples(); if (n > dry.getNumSamples()) return;
    const float T = threshold->load(), R = ratio->load(), K = knee->load(), mk = juce::Decibels::decibelsToGain (makeup->load()), m = mix->load();
    const float aCoef = std::exp (-1.0f / (float) (sr * attack->load() * 0.001)), rCoef = std::exp (-1.0f / (float) (sr * release->load() * 0.001));
    for (int c = 0; c < 2; ++c) dry.copyFrom (c, 0, b, c, 0, n);
    float* l = b.getWritePointer (0); float* r = b.getWritePointer (1);
    float minGain = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float in = juce::jmax (std::abs (l[i]), std::abs (r[i]));
        env = in > env ? aCoef * env + (1 - aCoef) * in : rCoef * env + (1 - rCoef) * in;
        const float lvl = juce::Decibels::gainToDecibels (env, -100.0f);
        float over = lvl - T, gr = 0.0f;
        if (K > 0 && over > -K / 2 && over < K / 2) { const float x = over + K / 2; gr = (1.0f / R - 1.0f) * x * x / (2 * K); }
        else if (over >= K / 2) gr = (1.0f / R - 1.0f) * over;
        const float g = juce::Decibels::decibelsToGain (gr) * mk;
        minGain = juce::jmin (minGain, juce::Decibels::decibelsToGain (gr));
        l[i] = l[i] * g * m + dry.getSample (0, i) * (1 - m);
        r[i] = r[i] * g * m + dry.getSample (1, i) * (1 - m);
    }
    gainReductionDb.store (juce::Decibels::gainToDecibels (minGain));
}

// ---- Limiter ------------------------------------------------------------------------------------------------------------
LimiterEffect::LimiterEffect()
    : BuiltInEffect ("Limiter", { fx::pf ("ceiling", "Ceiling", -12.0f, 0.0f, -0.3f, 1.0f, "dB"), fx::pf ("release", "Release", 1.0f, 1000.0f, 60.0f, 100.0f, "ms"),
                                  fx::pf ("input", "Input gain", -12.0f, 24.0f, 0.0f, 1.0f, "dB"), fx::pf ("lookahead", "Lookahead", 0.5f, 10.0f, 3.0f, 1.0f, "ms") })
{
    ceiling = rawPtr ("ceiling"); release = rawPtr ("release"); inputGain = rawPtr ("input"); lookaheadMs = rawPtr ("lookahead");
}
void LimiterEffect::prepareToPlay (double s, int)
{
    sr = s; delaySamples = juce::jmax (1, (int) (sr * 0.010)); delayBuf.setSize (2, delaySamples, false, true, true); peakWindow.assign ((size_t) delaySamples, 0.0f); reset();
    setLatencySamples ((int) (sr * lookaheadMs->load() * 0.001));
}
void LimiterEffect::reset() { delayBuf.clear(); std::fill (peakWindow.begin(), peakWindow.end(), 0.0f); delayPos = 0; peakPos = 0; gain = 1.0f; }

void LimiterEffect::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    const int n = b.getNumSamples();
    const float ceil = juce::Decibels::decibelsToGain (ceiling->load()), ig = juce::Decibels::decibelsToGain (inputGain->load());
    const int la = juce::jlimit (1, delaySamples, (int) (sr * lookaheadMs->load() * 0.001));
    const float rCoef = std::exp (-1.0f / (float) (sr * release->load() * 0.001));
    float* l = b.getWritePointer (0); float* r = b.getWritePointer (1);
    float minG = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float inL = l[i] * ig, inR = r[i] * ig;
        const float pk = juce::jmax (std::abs (inL), std::abs (inR));
        // required gain so that the peak arriving in `la` samples does not exceed the ceiling
        const float need = pk > ceil ? ceil / pk : 1.0f;
        // attack instantly toward the lowest needed gain within the lookahead window, release slowly
        peakWindow[(size_t) peakPos] = need; peakPos = (peakPos + 1) % la;
        float target = 1.0f; for (int k = 0; k < la; ++k) target = juce::jmin (target, peakWindow[(size_t) k]);
        gain = target < gain ? target : rCoef * gain + (1 - rCoef) * target;
        minG = juce::jmin (minG, gain);
        // delayed output
        const float outL = delayBuf.getSample (0, delayPos), outR = delayBuf.getSample (1, delayPos);
        delayBuf.setSample (0, delayPos, inL); delayBuf.setSample (1, delayPos, inR);
        delayPos = (delayPos + 1) % la;
        l[i] = juce::jlimit (-ceil, ceil, outL * gain); r[i] = juce::jlimit (-ceil, ceil, outR * gain);
    }
    gainReductionDb.store (juce::Decibels::gainToDecibels (minG));
}

// ---- Gate --------------------------------------------------------------------------------------------------------------
GateEffect::GateEffect()
    : BuiltInEffect ("Gate", { fx::pf ("threshold", "Threshold", -80.0f, 0.0f, -40.0f, 1.0f, "dB"), fx::pf ("attack", "Attack", 0.1f, 100.0f, 1.0f, 5.0f, "ms"),
                               fx::pf ("hold", "Hold", 0.0f, 500.0f, 50.0f, 50.0f, "ms"), fx::pf ("release", "Release", 5.0f, 1000.0f, 100.0f, 100.0f, "ms"), fx::pf ("range", "Range", -90.0f, 0.0f, -90.0f, 1.0f, "dB") })
{
    threshold = rawPtr ("threshold"); attack = rawPtr ("attack"); hold = rawPtr ("hold"); release = rawPtr ("release"); range = rawPtr ("range");
}
void GateEffect::prepareToPlay (double s, int) { sr = s; reset(); }
void GateEffect::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    const int n = b.getNumSamples();
    const float T = juce::Decibels::decibelsToGain (threshold->load()), Tclose = T * 0.7f, floor_ = juce::Decibels::decibelsToGain (range->load());
    const float aCoef = std::exp (-1.0f / (float) (sr * attack->load() * 0.001)), rCoef = std::exp (-1.0f / (float) (sr * release->load() * 0.001));
    const int holdSamples = (int) (sr * hold->load() * 0.001);
    const float envCoef = std::exp (-1.0f / (float) (sr * 0.002));
    float* l = b.getWritePointer (0); float* r = b.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        const float in = juce::jmax (std::abs (l[i]), std::abs (r[i]));
        env = in > env ? in : envCoef * env + (1 - envCoef) * in;
        float target;
        if (env > T) { target = 1.0f; holdCounter = holdSamples; }
        else if (env > Tclose && gainState > 0.5f) { target = 1.0f; }
        else if (holdCounter > 0) { --holdCounter; target = 1.0f; }
        else target = floor_;
        gainState = target > gainState ? aCoef * gainState + (1 - aCoef) * target : rCoef * gainState + (1 - rCoef) * target;
        l[i] *= gainState; r[i] *= gainState;
    }
}
} // namespace mashup
