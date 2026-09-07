#pragma once
#include <vector>
#include <functional>

namespace mashup::analysis
{
/** Shared spectral-flux onset strength signal used by BPM, transient and phrase detection. */
struct OnsetEnvelope
{
    std::vector<float> values;      // one per hop
    std::vector<float> lowBand;     // low-frequency (kick) flux per hop
    std::vector<std::vector<float>> chroma;   // 12 bins per hop (coarse), for downbeat/phrase analysis
    double hopSeconds = 0;
    double frameRate = 0;

    static OnsetEnvelope compute (const float* mono, int numSamples, double sampleRate, int fftSize = 2048, int hop = 512,
                                  bool withChroma = false, const std::function<bool (float)>& progress = {});
};
} // namespace mashup::analysis
