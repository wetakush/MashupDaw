#pragma once
#include <vector>
#include <functional>

namespace mashup::analysis
{
/** Tempo estimation (onset autocorrelation + tempo prior), beat tracking (dynamic programming), downbeat
    estimation (bar-phase scoring on low-frequency onsets and harmonic change). */
class BpmDetector
{
public:
    struct Result
    {
        double bpm = 0.0;
        double confidence = 0.0;              // 0..1
        std::vector<double> beats;            // seconds
        std::vector<double> downbeats;        // seconds (every beatsPerBar-th beat)
        std::vector<std::pair<double, double>> candidates;   // (bpm, strength) alternatives
    };

    int beatsPerBar = 4;
    double minBpm = 60.0, maxBpm = 200.0;

    Result analyse (const float* mono, int numSamples, double sampleRate, const std::function<bool (float)>& progress = {});

    /** Re-derives a clean beat grid from a bpm and a phase (first beat time). */
    static std::vector<double> gridFrom (double bpm, double firstBeat, double lengthSeconds);
    /** Estimates the bpm of a beat list via the median interval. */
    static double bpmFromBeats (const std::vector<double>& beats);
};
} // namespace mashup::analysis
