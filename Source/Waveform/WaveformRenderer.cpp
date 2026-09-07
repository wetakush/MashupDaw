#include "WaveformRenderer.h"

namespace mashup
{
void WaveformRenderer::draw (juce::Graphics& g, const WaveformCache& cache, juce::Rectangle<int> area, double s0, double s1,
                             juce::Colour peakColour, juce::Colour rmsColour, float gain, bool stackChannels)
{
    if (area.isEmpty() || ! cache.isReady()) return;
    const int chans = stackChannels ? cache.getNumChannels() : 1;
    const int w = area.getWidth();
    static thread_local std::vector<WaveformCache::Bin> bins;
    for (int c = 0; c < chans; ++c)
    {
        auto lane = area.withHeight (area.getHeight() / chans).withY (area.getY() + c * (area.getHeight() / chans));
        const float mid = lane.getCentreY(), half = lane.getHeight() * 0.5f - 1.0f;
        if (stackChannels || cache.getNumChannels() == 1) cache.render (c, s0, s1, w, bins);
        else
        {
            // mono-summed display of all channels
            cache.render (0, s0, s1, w, bins);
            static thread_local std::vector<WaveformCache::Bin> other;
            for (int oc = 1; oc < cache.getNumChannels(); ++oc)
            {
                cache.render (oc, s0, s1, w, other);
                for (size_t i = 0; i < bins.size(); ++i) { bins[i].min = juce::jmin (bins[i].min, other[i].min); bins[i].max = juce::jmax (bins[i].max, other[i].max); bins[i].rms = juce::jmax (bins[i].rms, other[i].rms); }
            }
        }
        juce::RectangleList<float> peaks, rmsRects;
        for (int x = 0; x < w; ++x)
        {
            const auto& b = bins[(size_t) x];
            const float top = mid - juce::jlimit (-1.0f, 1.0f, b.max * gain) * half;
            const float bot = mid - juce::jlimit (-1.0f, 1.0f, b.min * gain) * half;
            peaks.addWithoutMerging ({ (float) (area.getX() + x), juce::jmin (top, bot), 1.0f, juce::jmax (1.0f, std::abs (bot - top)) });
            const float r = juce::jlimit (0.0f, 1.0f, b.rms * gain) * half;
            if (r > 0.5f) rmsRects.addWithoutMerging ({ (float) (area.getX() + x), mid - r, 1.0f, 2.0f * r });
        }
        g.setColour (peakColour); g.fillRectList (peaks);
        g.setColour (rmsColour); g.fillRectList (rmsRects);
    }
}
}
