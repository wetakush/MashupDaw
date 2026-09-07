#include "ToneEffects.h"

namespace mashup
{
// ---- EQ ----------------------------------------------------------------------------------------------------------------
ParametricEQEffect::ParametricEQEffect()
    : BuiltInEffect ("Parametric EQ", { fx::pb ("hpOn", "HP on", false), fx::pf ("hpFreq", "HP freq", 20.0f, 1000.0f, 80.0f, 100.0f, "Hz"),
                                        fx::pf ("lsFreq", "Low shelf freq", 30.0f, 800.0f, 120.0f, 120.0f, "Hz"), fx::pf ("lsGain", "Low shelf gain", -18.0f, 18.0f, 0.0f, 1.0f, "dB"),
                                        fx::pf ("p1Freq", "Peak 1 freq", 50.0f, 8000.0f, 400.0f, 500.0f, "Hz"), fx::pf ("p1Gain", "Peak 1 gain", -18.0f, 18.0f, 0.0f, 1.0f, "dB"), fx::pf ("p1Q", "Peak 1 Q", 0.2f, 10.0f, 1.0f, 1.0f),
                                        fx::pf ("p2Freq", "Peak 2 freq", 200.0f, 16000.0f, 2500.0f, 2000.0f, "Hz"), fx::pf ("p2Gain", "Peak 2 gain", -18.0f, 18.0f, 0.0f, 1.0f, "dB"), fx::pf ("p2Q", "Peak 2 Q", 0.2f, 10.0f, 1.0f, 1.0f),
                                        fx::pf ("hsFreq", "High shelf freq", 1000.0f, 16000.0f, 8000.0f, 5000.0f, "Hz"), fx::pf ("hsGain", "High shelf gain", -18.0f, 18.0f, 0.0f, 1.0f, "dB"),
                                        fx::pb ("lpOn", "LP on", false), fx::pf ("lpFreq", "LP freq", 1000.0f, 20000.0f, 16000.0f, 8000.0f, "Hz"), fx::pf ("gain", "Output", -12.0f, 12.0f, 0.0f, 1.0f, "dB") }) {}

void ParametricEQEffect::prepareToPlay (double s, int) { sr = s; std::fill (std::begin (lastParams), std::end (lastParams), -1.0f); updateCoefficients(); reset(); }

void ParametricEQEffect::updateCoefficients()
{
    const char* names[] = { "hpOn", "hpFreq", "lsFreq", "lsGain", "p1Freq", "p1Gain", "p1Q", "p2Freq", "p2Gain", "p2Q", "hsFreq", "hsGain", "lpOn", "lpFreq" };
    float v[14]; bool changed = false;
    for (int i = 0; i < 14; ++i) { v[i] = param (names[i]); if (v[i] != lastParams[i]) changed = true; lastParams[i] = v[i]; }
    if (! changed) return;
    using C = juce::dsp::IIR::Coefficients<float>;
    auto set = [&] (int band, C::Ptr c) { filters[band][0].coefficients = c; filters[band][1].coefficients = c; };
    set (0, v[0] > 0.5f ? C::makeHighPass (sr, v[1]) : C::makeAllPass (sr, 1000.0f));
    set (1, C::makeLowShelf (sr, v[2], 0.707f, juce::Decibels::decibelsToGain (v[3])));
    set (2, C::makePeakFilter (sr, v[4], v[6], juce::Decibels::decibelsToGain (v[5])));
    set (3, C::makePeakFilter (sr, v[7], v[9], juce::Decibels::decibelsToGain (v[8])));
    set (4, C::makeHighShelf (sr, v[10], 0.707f, juce::Decibels::decibelsToGain (v[11])));
    set (5, v[12] > 0.5f ? C::makeLowPass (sr, juce::jmin ((float) sr * 0.45f, v[13])) : C::makeAllPass (sr, 1000.0f));
}

void ParametricEQEffect::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    updateCoefficients();
    const float g = juce::Decibels::decibelsToGain (param ("gain"));
    for (int c = 0; c < 2; ++c)
    {
        float* d = b.getWritePointer (c);
        for (int i = 0; i < b.getNumSamples(); ++i) { float x = d[i]; for (auto& f : filters) x = f[c].processSample (x); d[i] = x * g; }
    }
}

// ---- Saturation ----------------------------------------------------------------------------------------------------------
SaturationEffect::SaturationEffect()
    : BuiltInEffect ("Saturation", { fx::pf ("drive", "Drive", 0.0f, 36.0f, 6.0f, 1.0f, "dB"), fx::pf ("tone", "Tone", 200.0f, 12000.0f, 4000.0f, 3000.0f, "Hz"),
                                     fx::pf ("asym", "Asymmetry", 0.0f, 1.0f, 0.2f), fx::pf ("mix", "Mix", 0.0f, 1.0f, 1.0f), fx::pf ("output", "Output", -24.0f, 12.0f, -3.0f, 1.0f, "dB") }) {}
void SaturationEffect::prepareToPlay (double s, int bs)
{
    sr = s; dry.setSize (2, bs, false, true, true);
    for (int c = 0; c < 2; ++c) { tone[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, 4000.0f); dc[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 10.0f); }
    reset();
}
void SaturationEffect::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    const int n = b.getNumSamples(); if (n > dry.getNumSamples()) return;
    const float drive = juce::Decibels::decibelsToGain (param ("drive")), asym = param ("asym"), m = param ("mix"), out = juce::Decibels::decibelsToGain (param ("output"));
    static float lastTone = -1; const float t = param ("tone");
    if (t != lastTone) { lastTone = t; for (int c = 0; c < 2; ++c) tone[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, t); }
    for (int c = 0; c < 2; ++c)
    {
        dry.copyFrom (c, 0, b, c, 0, n);
        float* d = b.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            float x = d[i] * drive + asym * 0.3f;
            x = std::tanh (x) - std::tanh (asym * 0.3f);          // remove the offset the asymmetry introduces
            x = tone[c].processSample (x);
            x = dc[c].processSample (x);
            d[i] = (x * m + dry.getSample (c, i) * (1 - m)) * out;
        }
    }
}

// ---- Distortion ------------------------------------------------------------------------------------------------------------
DistortionEffect::DistortionEffect()
    : BuiltInEffect ("Distortion", { fx::pc ("type", "Type", { "Soft clip", "Hard clip", "Foldback", "Tube" }, 0), fx::pf ("drive", "Drive", 0.0f, 48.0f, 12.0f, 1.0f, "dB"),
                                     fx::pf ("lowpass", "Low-pass", 500.0f, 20000.0f, 12000.0f, 6000.0f, "Hz"), fx::pf ("mix", "Mix", 0.0f, 1.0f, 1.0f), fx::pf ("output", "Output", -24.0f, 12.0f, -6.0f, 1.0f, "dB") }) {}
void DistortionEffect::prepareToPlay (double s, int bs)
{
    oversampling.initProcessing ((size_t) bs); oversampling.reset();
    lp.prepare ({ s, (juce::uint32) bs, 2 }); lp.setType (juce::dsp::StateVariableTPTFilterType::lowpass); lp.reset();
    dry.setSize (2, bs, false, true, true);
    setLatencySamples ((int) oversampling.getLatencyInSamples());
}
void DistortionEffect::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    const int n = b.getNumSamples(); if (n > dry.getNumSamples()) return;
    for (int c = 0; c < 2; ++c) dry.copyFrom (c, 0, b, c, 0, n);
    const int type = (int) param ("type"); const float drive = juce::Decibels::decibelsToGain (param ("drive")), m = param ("mix"), out = juce::Decibels::decibelsToGain (param ("output"));
    juce::dsp::AudioBlock<float> block (b);
    auto up = oversampling.processSamplesUp (block);
    for (size_t c = 0; c < up.getNumChannels(); ++c)
    {
        float* d = up.getChannelPointer (c);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
        {
            float x = d[i] * drive;
            switch (type)
            {
                case 0: x = x / (1.0f + std::abs (x)); break;
                case 1: x = juce::jlimit (-1.0f, 1.0f, x); break;
                case 2: { x = std::fmod (x + 1.0f, 4.0f); if (x < 0) x += 4.0f; x = std::abs (x - 2.0f) - 1.0f; break; }
                default: x = x >= 0 ? 1.0f - std::exp (-x) : -1.0f + std::exp (x * 0.7f); break;
            }
            d[i] = x;
        }
    }
    oversampling.processSamplesDown (block);
    lp.setCutoffFrequency (param ("lowpass"));
    for (int c = 0; c < 2; ++c) { float* d = b.getWritePointer (c); for (int i = 0; i < n; ++i) d[i] = (lp.processSample (c, d[i]) * m + dry.getSample (c, i) * (1 - m)) * out; }
}

// ---- Widener ---------------------------------------------------------------------------------------------------------------
StereoWidenerEffect::StereoWidenerEffect()
    : BuiltInEffect ("Stereo Widener", { fx::pf ("width", "Width", 0.0f, 2.0f, 1.3f), fx::pf ("haas", "Haas delay", 0.0f, 20.0f, 0.0f, 5.0f, "ms"), fx::pf ("monoBelow", "Mono below", 20.0f, 500.0f, 120.0f, 100.0f, "Hz") }) {}
void StereoWidenerEffect::prepareToPlay (double s, int bs)
{
    sr = s; delay.prepare ({ s, (juce::uint32) bs, 1 }); delay.setMaximumDelayInSamples ((int) (s * 0.03));
    lowMid.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (s, 120.0f); lowSide.coefficients = lowMid.coefficients; reset();
}
void StereoWidenerEffect::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    const float w = param ("width"), haas = param ("haas"); const float mono = param ("monoBelow");
    static float lastMono = -1; if (mono != lastMono) { lastMono = mono; lowMid.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, mono); lowSide.coefficients = lowMid.coefficients; }
    delay.setDelay ((float) (haas * 0.001 * sr));
    float* l = b.getWritePointer (0); float* r = b.getWritePointer (1);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        float L = l[i], R = r[i];
        if (haas > 0.01f) { delay.pushSample (0, R); R = delay.popSample (0); }
        float mid = 0.5f * (L + R), side = 0.5f * (L - R);
        // keep lows mono: remove the low part of the side signal
        const float sideLow = lowSide.processSample (side);
        side = (side - sideLow) * w;
        l[i] = mid + side; r[i] = mid - side;
    }
}
} // namespace mashup
