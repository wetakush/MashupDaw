#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include "Core/Identifiers.h"

namespace mashup
{
enum class AutomationMode { Read = 0, Write, Touch, Latch, Off };
inline const char* automationModeName (AutomationMode m) { switch (m) { case AutomationMode::Read: return "Read"; case AutomationMode::Write: return "Write"; case AutomationMode::Touch: return "Touch"; case AutomationMode::Latch: return "Latch"; case AutomationMode::Off: return "Off"; } return "?"; }

/** Piecewise-linear (with optional tension) curve of normalised values (0..1) over beats. Immutable copies are
    shared with the audio thread. */
class AutomationCurve
{
public:
    struct Point { double beat; float value; float curve = 0.0f; };   // curve: -1..1 tension (0 = linear)

    void clear() { points.clear(); }
    bool isEmpty() const noexcept { return points.empty(); }
    const std::vector<Point>& getPoints() const noexcept { return points; }
    void addPoint (double beat, float value, float curve = 0.0f);
    void removePointsInRange (double startBeat, double endBeat);
    int indexNear (double beat, double tolerance) const;
    void removeIndex (int i) { if (i >= 0 && i < (int) points.size()) points.erase (points.begin() + i); }
    void setPoint (int i, double beat, float value);
    /** Value at beat: holds the first/last value outside the range; linear/tension interpolation inside. */
    float valueAt (double beat) const noexcept;

    // ValueTree LANE <-> curve
    static AutomationCurve fromLane (const juce::ValueTree& lane);
    void writeToLane (juce::ValueTree lane, juce::UndoManager*) const;

    /** Parameter ids: "volume", "pan", "send0", "send1", "fx:<effectId>:<paramId>". */
    static juce::ValueTree findLane (const juce::ValueTree& track, const juce::String& param);
    static juce::ValueTree getOrCreateLane (juce::ValueTree track, const juce::String& param, juce::UndoManager*);

    // normalised <-> real value helpers for the track parameters
    static float volumeToNorm (double gain) noexcept;      // -60..+12 dB
    static double normToVolume (float n) noexcept;
    static float panToNorm (double pan) noexcept { return (float) ((pan + 1.0) * 0.5); }
    static double normToPan (float n) noexcept { return n * 2.0 - 1.0; }

private:
    std::vector<Point> points;
};
} // namespace mashup
