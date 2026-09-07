#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace mashup::dsp
{
/** EBU R128 / ITU-R BS.1770-4 loudness: integrated (gated), short-term and momentary. Also usable in realtime
    (processBlock is allocation-free after prepare). */
class LoudnessMeter
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();
    /** Feeds a block; keeps running momentary/short-term values and integrated gating blocks. */
    void process (const float* const* input, int numChannels, int numSamples) noexcept;

    double getMomentary() const noexcept { return momentary; }     // LUFS over 400 ms
    double getShortTerm() const noexcept { return shortTerm; }     // LUFS over 3 s
    double getIntegrated() const;                                  // LUFS gated, whole programme
    double getLoudnessRange() const;                               // LRA in LU
    double getTruePeak() const noexcept { return truePeak; }       // linear

    /** Offline convenience: integrated loudness and true peak of a whole buffer. */
    static void analyse (const juce::AudioBuffer<float>& buffer, double sampleRate, double& lufs, double& truePeak, double& lra);

private:
    static double energyToLufs (double e) noexcept { return -0.691 + 10.0 * std::log10 (juce::jmax (1.0e-12, e)); }
    double sampleRate = 48000.0;
    int channels = 2;
    juce::dsp::IIR::Filter<float> stage1[2], stage2[2];   // K-weighting per channel
    std::vector<double> ring100ms;   // energy of 100 ms sub-blocks (per channel summed)
    double accum = 0.0; int accumCount = 0; int subBlockSamples = 4800;
    std::vector<double> blocks400;   // 400 ms gating block energies (overlap 75%)
    std::vector<double> blocks3s;
    double momentary = -100.0, shortTerm = -100.0;
    float truePeak = 0.0f;
    juce::dsp::Oversampling<float>* oversampler = nullptr;
    std::vector<float> tmp;
};
} // namespace mashup::dsp
