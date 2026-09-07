#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "WaveformCache.h"

namespace mashup
{
/** Paints a waveform (peak + RMS) from a cache into a rectangle. `startSample/endSample` are source positions
    (endSample < startSample draws reversed). Draws channels stacked. */
struct WaveformRenderer
{
    static void draw (juce::Graphics& g, const WaveformCache& cache, juce::Rectangle<int> area,
                      double startSample, double endSample, juce::Colour peak, juce::Colour rms, float gain = 1.0f,
                      bool stackChannels = true);
};
}
