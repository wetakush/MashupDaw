#include "KeyDetector.h"
#include "DSP/FFT.h"
#include <cmath>
#include <algorithm>

namespace mashup::analysis
{
namespace
{
    // Temperley-adjusted Krumhansl profiles
    const double majorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    const double minorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    double correlate (const std::vector<double>& c, const double* profile, int shift)
    {
        double mc = 0, mp = 0; for (int i = 0; i < 12; ++i) { mc += c[(size_t) i]; mp += profile[i]; } mc /= 12; mp /= 12;
        double num = 0, dc = 0, dp = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double x = c[(size_t) ((i + shift) % 12)] - mc, y = profile[i] - mp;
            num += x * y; dc += x * x; dp += y * y;
        }
        return dc > 0 ? num / std::sqrt (dc * dp) : 0.0;
    }
}

KeyDetector::Result KeyDetector::fromChroma (const std::vector<double>& chroma)
{
    Result r; r.chroma = chroma;
    if (chroma.size() != 12) return r;
    for (int mode = 0; mode < 2; ++mode)
        for (int root = 0; root < 12; ++root)
            r.ranking.push_back ({ root + 12 * mode, correlate (chroma, mode == 0 ? majorProfile : minorProfile, root) });
    std::sort (r.ranking.begin(), r.ranking.end(), [] (auto& a, auto& b) { return a.second > b.second; });
    r.root = r.ranking[0].first % 12; r.mode = r.ranking[0].first / 12;
    const double best = r.ranking[0].second, second = r.ranking[1].second;
    r.confidence = std::clamp ((best - second) * 4.0 + best * 0.3, 0.0, 1.0);
    return r;
}

KeyDetector::Result KeyDetector::analyse (const float* mono, int numSamples, double sr)
{
    const int fftSize = 8192, hop = 4096;
    std::vector<double> chroma (12, 0.0);
    if (numSamples < fftSize) return fromChroma (chroma);
    dsp::FFT fft (fftSize);
    std::vector<float> window; dsp::FFT::hann (window, fftSize);
    std::vector<float> frame ((size_t) fftSize), mag ((size_t) fft.getNumBins());
    // precompute bin -> pitch class with a tuning-tolerant Gaussian weight
    const int bins = fft.getNumBins();
    std::vector<int> pc ((size_t) bins, -1); std::vector<float> weight ((size_t) bins, 0.0f);
    for (int b = 1; b < bins; ++b)
    {
        const double f = b * sr / fftSize;
        if (f < 55.0 || f > 4200.0) continue;
        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        const double nearest = std::round (midi);
        const double dev = midi - nearest;
        pc[(size_t) b] = ((int) nearest % 12 + 12) % 12;
        // de-emphasise high octaves (harmonics) and off-tuned bins
        weight[(size_t) b] = (float) (std::exp (-dev * dev / 0.08) / (1.0 + std::max (0.0, midi - 60.0) / 24.0));
    }
    const int numFrames = (numSamples - fftSize) / hop + 1;
    for (int i = 0; i < numFrames; ++i)
    {
        const float* src = mono + (size_t) i * hop;
        for (int k = 0; k < fftSize; ++k) frame[(size_t) k] = src[k] * window[(size_t) k];
        fft.magnitudes (frame.data(), mag.data());
        // spectral whitening-ish: log compression, then peak picking to favour tonal components
        for (int b = 2; b < bins - 2; ++b)
        {
            if (pc[(size_t) b] < 0) continue;
            const float m = mag[(size_t) b];
            if (m < mag[(size_t) b - 1] || m < mag[(size_t) b + 1]) continue;   // spectral peaks only
            chroma[(size_t) pc[(size_t) b]] += std::log1p (m * 4.0f) * weight[(size_t) b];
        }
    }
    double mx = 0; for (double v : chroma) mx = std::max (mx, v);
    if (mx > 0) for (double& v : chroma) v /= mx;
    return fromChroma (chroma);
}
} // namespace mashup::analysis
