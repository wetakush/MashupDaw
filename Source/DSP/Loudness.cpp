#include "Loudness.h"

namespace mashup::dsp
{
void LoudnessMeter::prepare (double sr, int numChannels)
{
    sampleRate = sr; channels = juce::jlimit (1, 2, numChannels);
    // BS.1770-4 K-weighting designed for the actual sample rate (same analogue prototype as the 48 kHz reference)
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        const float b0 = (float) ((Vh + Vb * K / Q + K * K) / a0), b1 = (float) (2.0 * (K * K - Vh) / a0), b2 = (float) ((Vh - Vb * K / Q + K * K) / a0);
        const float a1 = (float) (2.0 * (K * K - 1.0) / a0), a2 = (float) ((1.0 - K / Q + K * K) / a0);
        auto shelf = juce::dsp::IIR::Coefficients<float>::Ptr (new juce::dsp::IIR::Coefficients<float> (b0, b1, b2, 1.0f, a1, a2));
        const double f1 = 38.13547087602444, Q1 = 0.5003270373238773;
        const double K1 = std::tan (juce::MathConstants<double>::pi * f1 / sr);
        const double d0 = 1.0 + K1 / Q1 + K1 * K1;
        auto hp = juce::dsp::IIR::Coefficients<float>::Ptr (new juce::dsp::IIR::Coefficients<float> (1.0f, -2.0f, 1.0f, (float) d0, (float) (2.0 * (K1 * K1 - 1.0)), (float) (1.0 - K1 / Q1 + K1 * K1)));
        for (int c = 0; c < 2; ++c) { stage1[c].coefficients = shelf; stage2[c].coefficients = hp; }
    }
    subBlockSamples = (int) std::round (sr * 0.1);
    tmp.resize (8192);
    reset();
}

void LoudnessMeter::reset()
{
    for (int c = 0; c < 2; ++c) { stage1[c].reset(); stage2[c].reset(); }
    ring100ms.clear(); blocks400.clear(); blocks3s.clear();
    accum = 0.0; accumCount = 0; momentary = shortTerm = -100.0; truePeak = 0.0f;
}

void LoudnessMeter::process (const float* const* input, int numChannels, int numSamples) noexcept
{
    numChannels = juce::jmin (numChannels, 2);
    for (int i = 0; i < numSamples; ++i)
    {
        double e = 0.0;
        for (int c = 0; c < numChannels; ++c)
        {
            const float x = input[c][i];
            truePeak = juce::jmax (truePeak, std::abs (x));
            const float y = stage2[c].processSample (stage1[c].processSample (x));
            e += (double) y * y;
        }
        accum += e; ++accumCount;
        if (accumCount >= subBlockSamples)
        {
            ring100ms.push_back (accum / accumCount);
            accum = 0.0; accumCount = 0;
            if (ring100ms.size() >= 4)
            {
                double m = 0; for (size_t k = ring100ms.size() - 4; k < ring100ms.size(); ++k) m += ring100ms[k];
                m /= 4.0; blocks400.push_back (m); momentary = energyToLufs (m);
            }
            if (ring100ms.size() >= 30)
            {
                double s = 0; for (size_t k = ring100ms.size() - 30; k < ring100ms.size(); ++k) s += ring100ms[k];
                s /= 30.0; blocks3s.push_back (s); shortTerm = energyToLufs (s);
            }
            if (ring100ms.size() > 64) ring100ms.erase (ring100ms.begin(), ring100ms.begin() + 32);   // keep the buffer bounded
        }
    }
}

double LoudnessMeter::getIntegrated() const
{
    if (blocks400.empty()) return -100.0;
    // absolute gate -70 LUFS
    double sum = 0; int n = 0;
    for (double e : blocks400) if (energyToLufs (e) > -70.0) { sum += e; ++n; }
    if (n == 0) return -100.0;
    const double rel = energyToLufs (sum / n) - 10.0;
    sum = 0; n = 0;
    for (double e : blocks400) { const double l = energyToLufs (e); if (l > -70.0 && l > rel) { sum += e; ++n; } }
    return n ? energyToLufs (sum / n) : -100.0;
}

double LoudnessMeter::getLoudnessRange() const
{
    std::vector<double> v;
    for (double e : blocks3s) { const double l = energyToLufs (e); if (l > -70.0) v.push_back (l); }
    if (v.size() < 2) return 0.0;
    double sum = 0; for (double l : v) sum += std::pow (10.0, (l + 0.691) / 10.0);
    const double rel = energyToLufs (sum / v.size()) - 20.0;
    std::vector<double> g; for (double l : v) if (l > rel) g.push_back (l);
    if (g.size() < 2) return 0.0;
    std::sort (g.begin(), g.end());
    return g[(size_t) ((g.size() - 1) * 0.95)] - g[(size_t) ((g.size() - 1) * 0.10)];
}

void LoudnessMeter::analyse (const juce::AudioBuffer<float>& buffer, double sr, double& lufs, double& truePeak, double& lra)
{
    LoudnessMeter m; m.prepare (sr, buffer.getNumChannels());
    const int block = 4096;
    std::vector<const float*> ptrs ((size_t) buffer.getNumChannels());
    for (int pos = 0; pos < buffer.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, buffer.getNumSamples() - pos);
        for (int c = 0; c < buffer.getNumChannels(); ++c) ptrs[(size_t) c] = buffer.getReadPointer (c, pos);
        m.process (ptrs.data(), buffer.getNumChannels(), n);
    }
    lufs = m.getIntegrated(); truePeak = m.getTruePeak(); lra = m.getLoudnessRange();
}
} // namespace mashup::dsp
