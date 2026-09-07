#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>

namespace mashup::dsp
{
/** Allocation-free EBU R128 meter for the audio thread: momentary (400 ms), short-term (3 s), integrated
    (gated, via a fixed histogram) and true peak (4x oversampled estimate). UI reads the atomics. */
class LoudnessMeterRT
{
public:
    void prepare (double sampleRate);
    void reset() noexcept;
    void process (const float* l, const float* r, int numSamples) noexcept;

    std::atomic<float> momentary { -100.0f }, shortTerm { -100.0f }, integrated { -100.0f }, truePeakDb { -100.0f }, peakHoldDb { -100.0f };
    void resetIntegrated() noexcept { resetRequested.store (true); }

private:
    static double lufs (double e) noexcept { return -0.691 + 10.0 * std::log10 (std::max (1.0e-12, e)); }
    double sr = 48000.0;
    juce::dsp::IIR::Filter<float> shelf[2], hp[2];
    int subBlock = 4800, acc = 0; double accE = 0.0;
    std::array<double, 30> ring {}; int ringPos = 0, ringFill = 0;       // 100 ms energies (3 s)
    static constexpr int histBins = 800;                                 // -70 .. +10 LUFS in 0.1 steps
    std::array<int, histBins> hist {}; int histCount = 0;
    std::array<double, histBins> histEnergy {};
    float peakDecay = 0.0f; int holdCounter = 0;
    std::atomic<bool> resetRequested { false };
    float lastL[3] { 0, 0, 0 }, lastR[3] { 0, 0, 0 };
};
} // namespace mashup::dsp
