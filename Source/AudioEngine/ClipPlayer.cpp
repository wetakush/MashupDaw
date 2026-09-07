#include "ClipPlayer.h"
#include <cmath>

namespace mashup
{
ClipPlayer::ClipPlayer (Static s, double deviceSampleRate, int maxBlockSize)
    : st (std::move (s)), deviceRate (deviceSampleRate)
{
    scratch.setSize (2, juce::jmax (64, maxBlockSize), false, true, true);
    feed.setSize (2, juce::jmax (256, maxBlockSize * 4), false, true, true);
    if (st.source)
    {
        const double srcRate = st.source->sampleRate;
        loopStartS = st.offsetSeconds * srcRate;
        loopEndS   = st.sourceEndSeconds > st.offsetSeconds ? st.sourceEndSeconds * srcRate : (double) st.source->getLengthSamples();
        if (dsp::Stretcher::usesStretcher (st.mode))
            stretcher = std::make_unique<dsp::Stretcher> (deviceRate, 2, st.mode, maxBlockSize);
    }
}

double ClipPlayer::sourceTimeAtBeat (double beat, double bpm) const noexcept
{
    const double beatsIn = beat - st.startBeat;
    const double secondsIn = beatsIn * 60.0 / bpm;                        // timeline seconds since clip start
    const double speed = st.synced ? bpm / st.clipBpm : (double) lv.rate.load (std::memory_order_relaxed);
    const double pitchSpeed = st.mode == StretchMode::Repitch ? (double) lv.pitchScale.load (std::memory_order_relaxed) : 1.0;
    if (st.reverse)
    {
        const double regionEnd = st.sourceEndSeconds > st.offsetSeconds ? st.sourceEndSeconds : st.source->getLengthSeconds();
        return regionEnd - secondsIn * speed * pitchSpeed;
    }
    return st.offsetSeconds + secondsIn * speed * pitchSpeed;
}

float ClipPlayer::fadeGain (double b) const noexcept
{
    const double fi = lv.fadeInBeats.load (std::memory_order_relaxed), fo = lv.fadeOutBeats.load (std::memory_order_relaxed);
    auto shape = [] (double x, FadeShape s) -> double
    {
        x = juce::jlimit (0.0, 1.0, x);
        switch (s)
        {
            case FadeShape::Linear: return x;
            case FadeShape::EqualPower: return std::sin (x * juce::MathConstants<double>::halfPi);
            case FadeShape::Exponential: return x * x;
            case FadeShape::SCurve: return 0.5 - 0.5 * std::cos (x * juce::MathConstants<double>::pi);
        }
        return x;
    };
    double g = 1.0;
    if (fi > 0.0 && b < fi) g *= shape (b / fi, st.fadeInShape);
    const double fromEnd = st.lengthBeats - b;
    if (fo > 0.0 && fromEnd < fo) g *= shape (fromEnd / fo, st.fadeOutShape);
    return (float) g;
}

double ClipPlayer::wrapSourcePos (double pos) const noexcept
{
    if (! st.loop) return pos;
    const double len = loopEndS - loopStartS;
    if (len <= 1.0) return pos;
    double rel = std::fmod (pos - loopStartS, len);
    if (rel < 0) rel += len;
    return loopStartS + rel;
}

void ClipPlayer::readSourceInterpolated (int ch, double pos, float& out) const noexcept
{
    // 4-point cubic Hermite
    const auto& src = *st.source;
    const juce::int64 i1 = (juce::int64) std::floor (pos);
    const float t = (float) (pos - (double) i1);
    const float y0 = src.getSample (ch, i1 - 1), y1 = src.getSample (ch, i1), y2 = src.getSample (ch, i1 + 1), y3 = src.getSample (ch, i1 + 2);
    const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    out = ((c3 * t + c2) * t + c1) * t + c0;
}

void ClipPlayer::renderRepitch (float* l, float* r, int n, double srcPos, double speedSamples) noexcept
{
    const int chans = st.source->getNumChannels();
    const double dir = st.reverse ? -1.0 : 1.0;
    for (int i = 0; i < n; ++i)
    {
        const double p = wrapSourcePos (srcPos + dir * i * speedSamples);
        readSourceInterpolated (0, p, l[i]);
        if (chans > 1) readSourceInterpolated (1, p, r[i]); else r[i] = l[i];
    }
}

void ClipPlayer::feedStretcher (int frames) noexcept
{
    const int chans = st.source->getNumChannels();
    const double step = st.source->sampleRate / deviceRate;      // source samples per stretcher input frame (stretcher runs at device rate)
    const double dir = st.reverse ? -1.0 : 1.0;
    frames = juce::jmin (frames, feed.getNumSamples());
    float* fl = feed.getWritePointer (0); float* fr = feed.getWritePointer (1);
    for (int i = 0; i < frames; ++i)
    {
        const double p = wrapSourcePos (feedPos);
        readSourceInterpolated (0, p, fl[i]);
        if (chans > 1) readSourceInterpolated (1, p, fr[i]); else fr[i] = fl[i];
        feedPos += dir * step;
    }
    const float* in[2] = { fl, fr };
    stretcher->process (in, frames, false);
}

void ClipPlayer::renderStretched (float* l, float* r, int n, double srcPosSamples, double speed) noexcept
{
    stretcher->setSpeed (speed);
    stretcher->setPitchScale (lv.pitchScale.load (std::memory_order_relaxed));
    stretcher->setFormantScale (lv.formantScale.load (std::memory_order_relaxed));

    if (! contiguous)
    {
        stretcher->reset();
        feedPos = srcPosSamples;
        // prime: feed the preferred start pad of silence and schedule dropping the start delay
        const int pad = stretcher->getPreferredStartPad();
        feed.clear();
        const float* z[2] = { feed.getReadPointer (0), feed.getReadPointer (1) };
        int remaining = pad;
        while (remaining > 0) { const int k = juce::jmin (remaining, feed.getNumSamples()); stretcher->process (z, k, false); remaining -= k; }
        dropSamples = stretcher->getStartDelay();
        contiguous = true;
    }

    int produced = 0;
    int guard = 0;
    while (produced < n && guard++ < 64)
    {
        while (stretcher->available() < (n - produced) + dropSamples && guard++ < 64)
        {
            int req = stretcher->getSamplesRequired();
            if (req <= 0) req = 256;
            feedStretcher (juce::jmin (req, feed.getNumSamples()));
        }
        if (dropSamples > 0)
        {
            float* junk[2] = { scratch.getWritePointer (0), scratch.getWritePointer (1) };
            const int k = juce::jmin (dropSamples, scratch.getNumSamples(), stretcher->available());
            if (k <= 0) break;
            stretcher->retrieve (junk, k);
            dropSamples -= k;
            continue;
        }
        float* out[2] = { l + produced, r + produced };
        const int got = stretcher->retrieve (out, n - produced);
        if (got <= 0) break;
        produced += got;
    }
    for (int i = produced; i < n; ++i) { l[i] = 0; r[i] = 0; }
}

void ClipPlayer::render (juce::AudioBuffer<float>& out, const Segment& seg) noexcept
{
    if (! st.source || lv.mute.load (std::memory_order_relaxed)) { contiguous = false; return; }

    const double beatsPerSample = seg.bpm / 60.0 / seg.sampleRate;
    const double clipEnd = st.startBeat + st.lengthBeats;
    const double segEndBeat = seg.startBeat + seg.numSamples * beatsPerSample;
    if (! (seg.startBeat < clipEnd && segEndBeat > st.startBeat)) { contiguous = false; return; }

    // sample range within this segment where the clip is active
    int s0 = 0, s1 = seg.numSamples;
    if (st.startBeat > seg.startBeat) s0 = (int) std::ceil ((st.startBeat - seg.startBeat) / beatsPerSample);
    if (clipEnd < segEndBeat)         s1 = (int) std::ceil ((clipEnd - seg.startBeat) / beatsPerSample);
    s0 = juce::jlimit (0, seg.numSamples, s0); s1 = juce::jlimit (s0, seg.numSamples, s1);
    const int n = s1 - s0;
    if (n <= 0) { contiguous = false; return; }
    if (n > scratch.getNumSamples()) { contiguous = false; return; }   // block larger than prepared; skip safely

    const juce::int64 absStart = (juce::int64) std::llround (seg.startTimeSeconds * seg.sampleRate) + s0;
    if (absStart != lastEndSample) contiguous = false;
    lastEndSample = absStart + n;

    const double beatAtS0 = seg.startBeat + s0 * beatsPerSample;
    const double srcTime = sourceTimeAtBeat (beatAtS0, seg.bpm);
    const double srcRate = st.source->sampleRate;
    const double speed = (st.synced ? seg.bpm / st.clipBpm : (double) lv.rate.load (std::memory_order_relaxed));

    float* l = scratch.getWritePointer (0); float* r = scratch.getWritePointer (1);
    const float pitch = lv.pitchScale.load (std::memory_order_relaxed), formant = lv.formantScale.load (std::memory_order_relaxed);
    const bool unity = std::abs (speed - 1.0) < 1e-4 && std::abs (pitch - 1.0f) < 1e-4f && std::abs (formant - 1.0f) < 1e-4f && std::abs (srcRate - seg.sampleRate) < 0.5;
    if (stretcher && ! unity)
        renderStretched (l, r, n, srcTime * srcRate, speed);
    else
    {
        if (stretcher) contiguous = false;   // stretcher state is stale once we bypass it
        renderRepitch (l, r, n, srcTime * srcRate, speed * (stretcher ? 1.0 : (double) pitch) * srcRate / seg.sampleRate);
    }

    // gain, fades, constant-power pan; accumulate into out
    const float gain = lv.gain.load (std::memory_order_relaxed);
    const float pan = juce::jlimit (-1.0f, 1.0f, lv.pan.load (std::memory_order_relaxed));
    const float angle = (pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
    const float gl = std::cos (angle) * juce::MathConstants<float>::sqrt2 * gain;
    const float gr = std::sin (angle) * juce::MathConstants<float>::sqrt2 * gain;
    float* ol = out.getWritePointer (0, s0); float* orr = out.getWritePointer (1, s0);
    const bool hasFades = lv.fadeInBeats.load (std::memory_order_relaxed) > 0.0f || lv.fadeOutBeats.load (std::memory_order_relaxed) > 0.0f;
    if (hasFades)
    {
        for (int i = 0; i < n; ++i)
        {
            const float f = fadeGain (beatAtS0 + i * beatsPerSample - st.startBeat);
            ol[i] += l[i] * gl * f; orr[i] += r[i] * gr * f;
        }
    }
    else
    {
        juce::FloatVectorOperations::addWithMultiply (ol, l, gl, n);
        juce::FloatVectorOperations::addWithMultiply (orr, r, gr, n);
    }
    contiguous = true;
}
} // namespace mashup
