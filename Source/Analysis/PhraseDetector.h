#pragma once
#include <vector>
#include "AnalysisData.h"

namespace mashup::analysis
{
/** Structural segmentation into intro/verse/pre-chorus/chorus/bridge/outro using bar-level features,
    a self-similarity novelty curve and repetition clustering. Results carry a confidence the UI shows. */
struct PhraseDetector
{
    static std::vector<Phrase> detect (const float* mono, int numSamples, double sampleRate,
                                       const std::vector<double>& downbeats, double bpm);
};
}
