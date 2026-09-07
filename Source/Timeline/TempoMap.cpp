#include "TempoMap.h"
#include "Core/Identifiers.h"
#include <algorithm>

namespace mashup
{
TempoMap::TempoMap (std::vector<Marker> m, int num, int den) : markers (std::move (m)), tsNum (juce::jmax (1, num)), tsDen (juce::jmax (1, den))
{
    if (markers.empty()) markers.push_back ({ 0.0, 120.0 });
    std::sort (markers.begin(), markers.end(), [] (auto& a, auto& b) { return a.beat < b.beat; });
    if (markers.front().beat > 0.0) markers.insert (markers.begin(), { 0.0, markers.front().bpm });
    for (auto& mk : markers) mk.bpm = juce::jlimit (1.0, 999.0, mk.bpm);
    rebuildTimes();
}

TempoMap TempoMap::fromProject (const juce::ValueTree& root)
{
    std::vector<Marker> m;
    m.push_back ({ 0.0, (double) root.getProperty (ids::bpm, 120.0) });
    for (const auto& t : root.getChildWithName (ids::TEMPOMAP))
        if (t.hasType (ids::TEMPO) && (double) t[ids::beat] > 0.0)
            m.push_back ({ (double) t[ids::beat], (double) t[ids::bpm] });
    return TempoMap (std::move (m), (int) root.getProperty (ids::timeSigNum, 4), (int) root.getProperty (ids::timeSigDen, 4));
}

void TempoMap::setConstant (double bpm) { markers = { { 0.0, juce::jlimit (1.0, 999.0, bpm) } }; rebuildTimes(); }

void TempoMap::rebuildTimes()
{
    times.assign (markers.size(), 0.0);
    for (size_t i = 1; i < markers.size(); ++i)
        times[i] = times[i - 1] + (markers[i].beat - markers[i - 1].beat) * 60.0 / markers[i - 1].bpm;
}

double TempoMap::beatToTime (double beat) const noexcept
{
    size_t i = 0;
    while (i + 1 < markers.size() && markers[i + 1].beat <= beat) ++i;
    return times[i] + (beat - markers[i].beat) * 60.0 / markers[i].bpm;
}

double TempoMap::timeToBeat (double time) const noexcept
{
    size_t i = 0;
    while (i + 1 < times.size() && times[i + 1] <= time) ++i;
    return markers[i].beat + (time - times[i]) * markers[i].bpm / 60.0;
}

double TempoMap::bpmAtBeat (double beat) const noexcept
{
    size_t i = 0;
    while (i + 1 < markers.size() && markers[i + 1].beat <= beat) ++i;
    return markers[i].bpm;
}

BarsBeats TempoMap::beatToBarsBeats (double beat) const noexcept
{
    const double bpb = beatsPerBar();
    const double beatUnit = 4.0 / tsDen;   // one displayed beat in quarter notes
    BarsBeats bb;
    const double barF = std::floor (beat / bpb);
    bb.bar = (int) barF + 1;
    const double inBar = beat - barF * bpb;
    const double beatF = std::floor (inBar / beatUnit + 1.0e-9);
    bb.beat = (int) beatF + 1;
    bb.tick = juce::jlimit (0.0, 0.999, (inBar - beatF * beatUnit) / beatUnit);
    return bb;
}

double TempoMap::barsBeatsToBeat (int bar, int beat, double tick) const noexcept
{
    return (bar - 1) * beatsPerBar() + ((beat - 1) + tick) * 4.0 / tsDen;
}
} // namespace mashup
