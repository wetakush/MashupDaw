#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

namespace mashup
{
/** Bars/beats formatting helpers (1-based like every DAW). */
struct BarsBeats
{
    int bar = 1, beat = 1;
    double tick = 0.0;   // fraction of a beat [0,1)
    juce::String toString (int decimals = 2) const
    {
        return juce::String (bar) + "." + juce::String (beat) + "." + juce::String ((int) std::round (tick * 100.0)).paddedLeft ('0', decimals);
    }
};

inline juce::String formatTime (double seconds, bool withMillis = true)
{
    const bool neg = seconds < 0; seconds = std::abs (seconds);
    int mins = (int) (seconds / 60.0);
    double secs = seconds - mins * 60.0;
    juce::String s = (neg ? "-" : "") + juce::String (mins).paddedLeft ('0', 2) + ":";
    if (withMillis) s += juce::String (secs, 3).paddedLeft ('0', 6);
    else            s += juce::String ((int) secs).paddedLeft ('0', 2);
    return s;
}

inline juce::String formatDurationShort (double seconds)
{
    int mins = (int) (seconds / 60.0);
    int secs = (int) std::round (seconds - mins * 60.0);
    if (secs == 60) { ++mins; secs = 0; }
    return juce::String (mins) + ":" + juce::String (secs).paddedLeft ('0', 2);
}

} // namespace mashup
