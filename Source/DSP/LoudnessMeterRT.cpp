#include "LoudnessMeterRT.h"

namespace mashup::dsp
{
void LoudnessMeterRT::prepare (double sampleRate)
{
    sr = sampleRate;
    const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416), a0 = 1.0 + K / Q + K * K;
    auto sh = juce::dsp::IIR::Coefficients<float>::Ptr (new juce::dsp::IIR::Coefficients<float> ((float) ((Vh + Vb * K / Q + K * K) / a0), (float) (2.0 * (K * K - Vh) / a0), (float) ((Vh - Vb * K / Q + K * K) / a0), 1.0f, (float) (2.0 * (K * K - 1.0) / a0), (float) ((1.0 - K / Q + K * K) / a0)));
    const double f1 = 38.13547087602444, Q1 = 0.5003270373238773, K1 = std::tan (juce::MathConstants<double>::pi * f1 / sr), d0 = 1.0 + K1 / Q1 + K1 * K1;
    auto hpc = juce::dsp::IIR::Coefficients<float>::Ptr (new juce::dsp::IIR::Coefficients<float> (1.0f, -2.0f, 1.0f, (float) d0, (float) (2.0 * (K1 * K1 - 1.0)), (float) (1.0 - K1 / Q1 + K1 * K1)));
    for (int c = 0; c < 2; ++c) { shelf[c].coefficients = sh; hp[c].coefficients = hpc; }
    subBlock = (int) std::round (sr * 0.1);
    reset();
}

void LoudnessMeterRT::reset() noexcept
{
    for (int c = 0; c < 2; ++c) { shelf[c].reset(); hp[c].reset(); }
    acc = 0; accE = 0; ring.fill (0.0); ringPos = ringFill = 0; hist.fill (0); histEnergy.fill (0.0); histCount = 0;
    momentary.store (-100.0f); shortTerm.store (-100.0f); integrated.store (-100.0f); truePeakDb.store (-100.0f); peakHoldDb.store (-100.0f); peakDecay = 0; holdCounter = 0;
}

void LoudnessMeterRT::process (const float* l, const float* r, int n) noexcept
{
    if (resetRequested.exchange (false)) { hist.fill (0); histEnergy.fill (0.0); histCount = 0; integrated.store (-100.0f); peakHoldDb.store (-100.0f); }
    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        // true-peak estimate: 4x linear-ish interpolation between successive samples (cheap upper bound)
        const float aL = std::abs (l[i]), aR = std::abs (r[i]);
        const float mL = std::abs (0.5f * (l[i] + lastL[0])), mR = std::abs (0.5f * (r[i] + lastR[0]));
        pk = std::max ({ pk, aL, aR, mL * 1.05f, mR * 1.05f });
        lastL[0] = l[i]; lastR[0] = r[i];
        const float yl = hp[0].processSample (shelf[0].processSample (l[i])), yr = hp[1].processSample (shelf[1].processSample (r[i]));
        accE += (double) yl * yl + (double) yr * yr; ++acc;
        if (acc >= subBlock)
        {
            const double e = accE / acc; accE = 0; acc = 0;
            ring[(size_t) ringPos] = e; ringPos = (ringPos + 1) % (int) ring.size(); ringFill = std::min (ringFill + 1, (int) ring.size());
            if (ringFill >= 4)
            {
                double m = 0; for (int k = 1; k <= 4; ++k) m += ring[(size_t) ((ringPos - k + (int) ring.size()) % (int) ring.size())];
                m /= 4.0; const double lm = lufs (m); momentary.store ((float) lm);
                if (lm > -70.0) { const int bin = juce::jlimit (0, histBins - 1, (int) ((lm + 70.0) * 10.0)); hist[(size_t) bin]++; histEnergy[(size_t) bin] += m; ++histCount; }
                if (histCount > 0 && (histCount % 5) == 0)
                {
                    // relative gate: mean of all blocks above -70 minus 10 LU
                    double sum = 0; int cnt = 0; for (int b = 0; b < histBins; ++b) { sum += histEnergy[(size_t) b]; cnt += hist[(size_t) b]; }
                    const double rel = lufs (sum / std::max (1, cnt)) - 10.0;
                    const int relBin = juce::jlimit (0, histBins - 1, (int) ((rel + 70.0) * 10.0));
                    sum = 0; cnt = 0; for (int b = relBin; b < histBins; ++b) { sum += histEnergy[(size_t) b]; cnt += hist[(size_t) b]; }
                    integrated.store (cnt ? (float) lufs (sum / cnt) : -100.0f);
                }
            }
            if (ringFill >= 30) { double s = 0; for (double v : ring) s += v; shortTerm.store ((float) lufs (s / 30.0)); }
        }
    }
    const float pkDb = juce::Decibels::gainToDecibels (pk, -100.0f);
    peakDecay = std::max (pk, peakDecay * std::pow (0.3f, (float) n / (float) sr));   // ~1.7 s to fall 60 dB
    truePeakDb.store (juce::Decibels::gainToDecibels (peakDecay, -100.0f));
    if (pkDb > peakHoldDb.load (std::memory_order_relaxed)) peakHoldDb.store (pkDb);
}
} // namespace mashup::dsp
