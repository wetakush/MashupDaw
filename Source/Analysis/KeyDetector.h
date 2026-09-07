#pragma once
#include <vector>

namespace mashup::analysis
{
/** Musical key detection: harmonic-weighted chromagram correlated with Krumhansl/Temperley key profiles. */
class KeyDetector
{
public:
    struct Result
    {
        int root = -1, mode = 0;      // root 0..11 (C=0), mode 0 major / 1 minor
        double confidence = 0.0;      // 0..1
        std::vector<std::pair<int, double>> ranking;   // (root + 12*mode, score) sorted best first
        std::vector<double> chroma;   // 12 averaged values
    };
    Result analyse (const float* mono, int numSamples, double sampleRate);
    /** Key from an already computed 12-bin chroma vector. */
    static Result fromChroma (const std::vector<double>& chroma);
};
} // namespace mashup::analysis
