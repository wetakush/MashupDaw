#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

namespace mashup::ui
{
enum class Tool { Select = 0, Blade, Automation };

/** Zoom/scroll/selection state shared between ruler, headers, canvas and inspector. */
class TimelineViewState : public juce::ChangeBroadcaster
{
public:
    // --- horizontal mapping ---
    double pixelsPerBeat = 40.0;
    double scrollBeat = 0.0;
    int verticalScroll = 0;   // pixels

    double beatToX (double beat) const noexcept { return (beat - scrollBeat) * pixelsPerBeat; }
    double xToBeat (double x) const noexcept { return scrollBeat + x / pixelsPerBeat; }

    void setZoom (double ppb, double anchorX = 0.0)
    {
        const double anchorBeat = xToBeat (anchorX);
        pixelsPerBeat = juce::jlimit (0.5, 4000.0, ppb);
        scrollBeat = juce::jmax (0.0, anchorBeat - anchorX / pixelsPerBeat);
        sendChangeMessage();
    }
    void setScrollBeat (double b) { scrollBeat = juce::jmax (0.0, b); sendChangeMessage(); }
    void setVerticalScroll (int px) { verticalScroll = juce::jmax (0, px); sendChangeMessage(); }

    // --- grid / snap ---
    bool snapEnabled = true;
    double gridDivision = 0.0;   // beats; 0 = adaptive to zoom
    bool tripletGrid = false;

    /** Effective grid in beats for the current zoom (adaptive when gridDivision == 0). */
    double effectiveGrid (double beatsPerBar) const noexcept
    {
        if (gridDivision > 0.0) return gridDivision * (tripletGrid ? 2.0 / 3.0 : 1.0);
        const double minPixels = 14.0;
        double g = 1.0 / 32.0;
        while (g * pixelsPerBeat < minPixels) g *= 2.0;
        if (g > beatsPerBar) { g = beatsPerBar; while (g * pixelsPerBeat < minPixels) g *= 2.0; }
        return g;
    }
    double snap (double beat, double beatsPerBar, bool force = false) const noexcept
    {
        if (! snapEnabled && ! force) return juce::jmax (0.0, beat);
        const double g = effectiveGrid (beatsPerBar);
        return juce::jmax (0.0, std::round (beat / g) * g);
    }

    // --- tool / selection ---
    Tool tool = Tool::Select;
    std::set<juce::String> selectedClips;
    juce::String selectedTrack;
    bool hasTimeSelection = false;
    double timeSelStart = 0.0, timeSelEnd = 0.0;

    void selectClip (const juce::String& id, bool add = false)
    {
        if (! add) selectedClips.clear();
        if (id.isNotEmpty()) selectedClips.insert (id);
        sendChangeMessage();
    }
    void clearSelection() { selectedClips.clear(); hasTimeSelection = false; sendChangeMessage(); }
    bool isSelected (const juce::String& id) const { return selectedClips.count (id) > 0; }
    void setTimeSelection (double a, double b) { hasTimeSelection = true; timeSelStart = juce::jmin (a, b); timeSelEnd = juce::jmax (a, b); sendChangeMessage(); }

    // --- display options ---
    bool showBeatMarkers = true, showTransients = false, showPhrases = true;
    bool followPlayhead = true;
    int headerWidth = 200;
    int rulerHeight = 30;
};
} // namespace mashup::ui
