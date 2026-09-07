#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include "Core/MusicalTime.h"

namespace mashup
{
/** Piecewise-constant tempo map: a sorted list of (beat, bpm) markers. Beat 0 == time 0.
    Immutable once built, so it can be shared with the audio thread. */
class TempoMap
{
public:
    struct Marker { double beat = 0.0; double bpm = 120.0; };

    TempoMap() { setConstant (120.0); }
    explicit TempoMap (double constantBpm) { setConstant (constantBpm); }
    TempoMap (std::vector<Marker> markers, int tsNum, int tsDen);

    /** Builds from the project: TEMPOMAP children override the global bpm as segments. */
    static TempoMap fromProject (const juce::ValueTree& projectRoot);

    void setConstant (double bpm);

    double beatToTime (double beat) const noexcept;
    double timeToBeat (double time) const noexcept;
    double bpmAtBeat (double beat) const noexcept;
    double bpmAtTime (double time) const noexcept { return bpmAtBeat (timeToBeat (time)); }

    int getTimeSigNumerator() const noexcept { return tsNum; }
    int getTimeSigDenominator() const noexcept { return tsDen; }
    /** Beats per bar in quarter-note beats (e.g. 6/8 -> 3.0). */
    double beatsPerBar() const noexcept { return tsNum * 4.0 / tsDen; }

    BarsBeats beatToBarsBeats (double beat) const noexcept;
    double barsBeatsToBeat (int bar, int beat, double tick = 0.0) const noexcept;
    juce::String formatBeat (double beat) const { return beatToBarsBeats (beat).toString(); }

    /** Snaps a beat position to the nearest multiple of `division` beats (e.g. 0.25 = 1/16). */
    static double snapBeat (double beat, double division) noexcept { return division <= 0 ? beat : std::round (beat / division) * division; }
    static double floorBeat (double beat, double division) noexcept { return division <= 0 ? beat : std::floor (beat / division) * division; }

    const std::vector<Marker>& getMarkers() const noexcept { return markers; }

private:
    void rebuildTimes();
    std::vector<Marker> markers;
    std::vector<double> times;   // time of each marker
    int tsNum = 4, tsDen = 4;
};
} // namespace mashup
