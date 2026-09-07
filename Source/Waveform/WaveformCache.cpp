#include "WaveformCache.h"

namespace mashup
{
WaveformCache::WaveformCache (AudioSourcePtr s) : source (std::move (s))
{
    if (source) { channels = source->getNumChannels(); length = source->getLengthSamples(); sampleRate = source->sampleRate; }
}

bool WaveformCache::build (const std::function<bool (float)>& progress)
{
    if (! source || length <= 0) return false;
    // level 0 from raw samples
    {
        const int spb = baseBin;
        const juce::int64 nBins = (length + spb - 1) / spb;
        for (int c = 0; c < channels; ++c)
        {
            auto& bins = levels[0].emplace_back ((size_t) nBins);
            const float* data = source->buffer.getReadPointer (c);
            for (juce::int64 b = 0; b < nBins; ++b)
            {
                const juce::int64 s0 = b * spb, s1 = juce::jmin (length, s0 + spb);
                auto mm = juce::FloatVectorOperations::findMinAndMax (data + s0, (int) (s1 - s0));
                double sq = 0; for (juce::int64 i = s0; i < s1; ++i) sq += data[i] * data[i];
                bins[(size_t) b] = { mm.getStart(), mm.getEnd(), (float) std::sqrt (sq / (double) (s1 - s0)) };
                if (progress && (b & 4095) == 0 && ! progress ((float) ((c * nBins + b) / (double) (nBins * channels)) * 0.8f)) return false;
            }
        }
    }
    // higher levels by 4:1 reduction
    for (int lv = 1; lv < numLevels; ++lv)
        for (int c = 0; c < channels; ++c)
        {
            const auto& src = levels[lv - 1][(size_t) c];
            auto& dst = levels[lv].emplace_back ((src.size() + 3) / 4);
            for (size_t b = 0; b < dst.size(); ++b)
            {
                Bin acc { 1.0e9f, -1.0e9f, 0.0f }; int n = 0;
                for (size_t k = b * 4; k < juce::jmin (src.size(), b * 4 + 4); ++k)
                { acc.min = juce::jmin (acc.min, src[k].min); acc.max = juce::jmax (acc.max, src[k].max); acc.rms += src[k].rms * src[k].rms; ++n; }
                acc.rms = n ? std::sqrt (acc.rms / n) : 0.0f;
                dst[b] = acc;
            }
        }
    ready.store (true, std::memory_order_release);
    if (progress) progress (1.0f);
    return true;
}

int WaveformCache::chooseLevel (double spp) const noexcept
{
    if (spp < baseBin) return -1;
    int lv = 0;
    while (lv + 1 < numLevels && samplesPerBin (lv + 1) <= spp) ++lv;
    return lv;
}

WaveformCache::Bin WaveformCache::summarise (int ch, juce::int64 s0, juce::int64 s1) const noexcept
{
    Bin r { 0, 0, 0 };
    if (! ready.load (std::memory_order_acquire) || ch >= channels) return r;
    s0 = juce::jlimit<juce::int64> (0, length, s0); s1 = juce::jlimit<juce::int64> (0, length, s1);
    if (s1 <= s0) return r;
    const int lv = chooseLevel ((double) (s1 - s0));
    if (lv < 0)
    {
        const float* d = source->buffer.getReadPointer (ch);
        auto mm = juce::FloatVectorOperations::findMinAndMax (d + s0, (int) (s1 - s0));
        double sq = 0; for (juce::int64 i = s0; i < s1; ++i) sq += d[i] * d[i];
        return { mm.getStart(), mm.getEnd(), (float) std::sqrt (sq / (double) (s1 - s0)) };
    }
    const int spb = samplesPerBin (lv);
    const auto& bins = levels[lv][(size_t) ch];
    const juce::int64 b0 = s0 / spb, b1 = std::min<juce::int64> ((juce::int64) bins.size(), (s1 + spb - 1) / spb);
    r = { 1.0e9f, -1.0e9f, 0.0f }; int n = 0;
    for (juce::int64 b = b0; b < b1; ++b) { const auto& x = bins[(size_t) b]; r.min = juce::jmin (r.min, x.min); r.max = juce::jmax (r.max, x.max); r.rms += x.rms * x.rms; ++n; }
    if (n == 0) return { 0, 0, 0 };
    r.rms = std::sqrt (r.rms / n);
    return r;
}

void WaveformCache::render (int ch, double start, double end, int numPixels, std::vector<Bin>& out) const noexcept
{
    out.assign ((size_t) juce::jmax (0, numPixels), Bin {});
    if (numPixels <= 0 || ! ready.load (std::memory_order_acquire)) return;
    const double step = (end - start) / numPixels;
    for (int x = 0; x < numPixels; ++x)
    {
        double a = start + x * step, b = a + step;
        if (b < a) std::swap (a, b);
        juce::int64 s0 = (juce::int64) std::floor (a), s1 = (juce::int64) std::ceil (b);
        if (s1 <= s0) s1 = s0 + 1;
        out[(size_t) x] = summarise (ch, s0, s1);
    }
}

bool WaveformCache::saveTo (const juce::File& f) const
{
    if (! ready) return false;
    f.getParentDirectory().createDirectory();
    juce::FileOutputStream os (f);
    if (! os.openedOk()) return false;
    os.setPosition (0); os.truncate();
    os.writeInt (0x57464331); os.writeInt (channels); os.writeInt64 (length); os.writeDouble (sampleRate);
    for (int lv = 0; lv < numLevels; ++lv)
        for (int c = 0; c < channels; ++c)
        {
            const auto& bins = levels[lv][(size_t) c];
            os.writeInt64 ((juce::int64) bins.size());
            os.write (bins.data(), bins.size() * sizeof (Bin));
        }
    return os.getStatus().wasOk();
}

bool WaveformCache::loadFrom (const juce::File& f)
{
    juce::FileInputStream is (f);
    if (! is.openedOk() || is.readInt() != 0x57464331) return false;
    const int ch = is.readInt(); const juce::int64 len = is.readInt64(); const double sr = is.readDouble();
    if (ch != channels || len != length || sr != sampleRate) return false;
    for (int lv = 0; lv < numLevels; ++lv)
    {
        levels[lv].clear();
        for (int c = 0; c < channels; ++c)
        {
            const juce::int64 n = is.readInt64();
            if (n < 0 || n > length) return false;
            auto& bins = levels[lv].emplace_back ((size_t) n);
            if (is.read (bins.data(), (int) (n * (juce::int64) sizeof (Bin))) != (int) (n * (juce::int64) sizeof (Bin))) return false;
        }
    }
    ready.store (true, std::memory_order_release);
    return true;
}
} // namespace mashup
