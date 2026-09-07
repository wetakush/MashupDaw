#include "OnsetEnvelope.h"
#include "DSP/FFT.h"
#include <cmath>
#include <algorithm>

namespace mashup::analysis
{
OnsetEnvelope OnsetEnvelope::compute (const float* mono, int numSamples, double sr, int fftSize, int hop, bool withChroma, const std::function<bool (float)>& progress)
{
    OnsetEnvelope env;
    env.hopSeconds = hop / sr; env.frameRate = sr / hop;
    if (numSamples < fftSize) return env;
    dsp::FFT fft (fftSize);
    std::vector<float> window; dsp::FFT::hann (window, fftSize);
    const int bins = fft.getNumBins();
    std::vector<float> frame ((size_t) fftSize), mag ((size_t) bins), prev ((size_t) bins, 0.0f), prevLog ((size_t) bins, 0.0f);
    const int numFrames = (numSamples - fftSize) / hop + 1;
    env.values.reserve ((size_t) numFrames); env.lowBand.reserve ((size_t) numFrames);
    const int lowBinEnd = (int) (200.0 / sr * fftSize);   // < 200 Hz

    // chroma mapping: bin -> pitch class (only bins between 60 Hz and 5 kHz)
    std::vector<int> binToPc ((size_t) bins, -1);
    if (withChroma)
        for (int b = 1; b < bins; ++b)
        {
            const double f = b * sr / fftSize;
            if (f < 60.0 || f > 5000.0) continue;
            const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
            binToPc[(size_t) b] = ((int) std::lround (midi) % 12 + 12) % 12;
        }

    for (int i = 0; i < numFrames; ++i)
    {
        const float* src = mono + (size_t) i * hop;
        for (int k = 0; k < fftSize; ++k) frame[(size_t) k] = src[k] * window[(size_t) k];
        fft.magnitudes (frame.data(), mag.data());
        float flux = 0, low = 0;
        for (int b = 1; b < bins; ++b)
        {
            const float lm = std::log1p (mag[(size_t) b] * 10.0f);   // compressed magnitude
            const float d = lm - prevLog[(size_t) b];
            if (d > 0) { flux += d; if (b < lowBinEnd) low += d; }
            prevLog[(size_t) b] = lm;
        }
        env.values.push_back (flux);
        env.lowBand.push_back (low);
        if (withChroma)
        {
            std::vector<float> c (12, 0.0f);
            for (int b = 1; b < bins; ++b) if (binToPc[(size_t) b] >= 0) c[(size_t) binToPc[(size_t) b]] += mag[(size_t) b] * mag[(size_t) b];
            env.chroma.push_back (std::move (c));
        }
        if (progress && (i & 255) == 0 && ! progress ((float) i / numFrames)) return env;
    }
    // first frame flux is meaningless
    if (! env.values.empty()) { env.values[0] = 0; env.lowBand[0] = 0; }
    return env;
}
} // namespace mashup::analysis
