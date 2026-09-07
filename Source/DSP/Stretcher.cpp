#include "Stretcher.h"
#include <rubberband/RubberBandStretcher.h>

namespace mashup::dsp
{
using RB = RubberBand::RubberBandStretcher;

static int optionsFor (StretchMode m, bool realtime)
{
    int o = realtime ? RB::OptionProcessRealTime : RB::OptionProcessOffline;
    o |= RB::OptionThreadingNever | RB::OptionChannelsTogether;
    switch (m)
    {
        case StretchMode::Realtime:
            o |= RB::OptionEngineFaster | RB::OptionWindowStandard | RB::OptionTransientsMixed | RB::OptionPitchHighSpeed; break;
        case StretchMode::HighQuality:
            o |= RB::OptionEngineFiner | RB::OptionWindowStandard | RB::OptionPitchHighQuality | RB::OptionSmoothingOn; break;
        case StretchMode::Vocal:
            o |= RB::OptionEngineFiner | RB::OptionFormantPreserved | RB::OptionPitchHighConsistency | RB::OptionWindowStandard; break;
        case StretchMode::Percussive:
            o |= RB::OptionEngineFaster | RB::OptionTransientsCrisp | RB::OptionDetectorPercussive | RB::OptionWindowShort | RB::OptionPitchHighSpeed; break;
        case StretchMode::Repitch: break;
    }
    return o;
}

Stretcher::Stretcher (double sampleRate, int numChannels, StretchMode m, int maxBlockSize)
    : channels (juce::jmax (1, numChannels)), mode (m)
{
    rb = std::make_unique<RB> ((size_t) sampleRate, (size_t) channels, optionsFor (mode, true), 1.0, 1.0);
    rb->setMaxProcessSize ((size_t) juce::jmax (256, maxBlockSize * 4));
}

Stretcher::~Stretcher() = default;

void Stretcher::setSpeed (double speed) noexcept { rb->setTimeRatio (1.0 / juce::jlimit (0.05, 20.0, speed)); }
void Stretcher::setPitchScale (double s) noexcept { rb->setPitchScale (juce::jlimit (0.125, 8.0, s)); }
void Stretcher::setFormantScale (double s) noexcept { rb->setFormantScale (juce::jlimit (0.25, 4.0, s)); }
void Stretcher::reset() noexcept { rb->reset(); }
int Stretcher::getSamplesRequired() const noexcept { return (int) rb->getSamplesRequired(); }
void Stretcher::process (const float* const* input, int numFrames, bool finalBlock) noexcept { rb->process (input, (size_t) numFrames, finalBlock); }
int Stretcher::available() const noexcept { return juce::jmax (0, rb->available()); }
int Stretcher::retrieve (float* const* output, int numFrames) noexcept { return (int) rb->retrieve (output, (size_t) numFrames); }
int Stretcher::getPreferredStartPad() const noexcept { return (int) rb->getPreferredStartPad(); }
int Stretcher::getStartDelay() const noexcept { return (int) rb->getStartDelay(); }

juce::AudioBuffer<float> stretchOffline (const juce::AudioBuffer<float>& in, double sampleRate, double timeRatio, double pitchScale,
                                         StretchMode mode, const std::function<bool (float)>& progress)
{
    const int ch = in.getNumChannels();
    if (mode == StretchMode::Repitch) mode = StretchMode::HighQuality;
    RB rb ((size_t) sampleRate, (size_t) ch, optionsFor (mode, false), timeRatio, pitchScale);
    rb.setExpectedInputDuration ((size_t) in.getNumSamples());
    const int block = 8192;
    std::vector<const float*> ptrs ((size_t) ch);
    // study pass
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, in.getNumSamples() - pos);
        for (int c = 0; c < ch; ++c) ptrs[(size_t) c] = in.getReadPointer (c, pos);
        rb.study (ptrs.data(), (size_t) n, pos + n >= in.getNumSamples());
        if (progress && ! progress (0.3f * pos / (float) in.getNumSamples())) return {};
    }
    const int expected = (int) std::ceil (in.getNumSamples() * timeRatio) + 8192;
    juce::AudioBuffer<float> out (ch, expected);
    int written = 0;
    std::vector<float*> outPtrs ((size_t) ch);
    auto drain = [&]
    {
        int avail;
        while ((avail = rb.available()) > 0)
        {
            if (written + avail > out.getNumSamples()) out.setSize (ch, (written + avail) * 2, true, false, true);
            for (int c = 0; c < ch; ++c) outPtrs[(size_t) c] = out.getWritePointer (c, written);
            written += (int) rb.retrieve (outPtrs.data(), (size_t) avail);
        }
    };
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, in.getNumSamples() - pos);
        for (int c = 0; c < ch; ++c) ptrs[(size_t) c] = in.getReadPointer (c, pos);
        rb.process (ptrs.data(), (size_t) n, pos + n >= in.getNumSamples());
        drain();
        if (progress && ! progress (0.3f + 0.7f * pos / (float) in.getNumSamples())) return {};
    }
    drain();
    out.setSize (ch, written, true, true, true);
    return out;
}
} // namespace mashup::dsp
