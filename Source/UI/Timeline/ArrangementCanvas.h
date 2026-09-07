#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"
#include "TimelineViewState.h"
#include "Clips/ClipModel.h"
#include "Tracks/TrackModel.h"

namespace mashup::ui
{
/** The arrangement area: grid, clips with waveforms, playhead, selection; all editing gestures. */
class ArrangementCanvas : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          public juce::DragAndDropTarget,
                          private juce::ValueTree::Listener,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    ArrangementCanvas (Session&, TimelineViewState&);
    ~ArrangementCanvas() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    /** Layout helpers shared with the panel. */
    int trackYForIndex (int index) const;         // relative to canvas (scrolled)
    int trackIndexAtY (int y) const;              // -1 if none
    TrackModel trackAtY (int y) const;
    int getTotalTracksHeight() const;
    double getContentEndBeat() const;

    /** Editing entry points used by commands. */
    void splitSelectedAtPlayhead();
    void deleteSelected();
    void duplicateSelected();
    void selectAll();
    std::vector<ClipModel> getSelectedClips() const;
    void copySelection();
    void pasteAtPlayhead();
    void cutSelection();
    void markDirty() { dirty = true; repaint(); }

    std::function<void (const juce::StringArray& files, double beat, TrackModel)> onFilesDropped;
    std::function<void (const juce::String& sourceId, double beat, TrackModel)> onSourceDropped;

private:
    enum class HitZone { None, Body, LeftEdge, RightEdge, FadeIn, FadeOut, AutomationLane };
    struct Hit { ClipModel clip; TrackModel track; HitZone zone = HitZone::None; juce::Rectangle<int> bounds; };
    Hit hitTest (juce::Point<int>) const;
    juce::Rectangle<int> clipBounds (const ClipModel&, int trackY, int trackH) const;
    void paintClip (juce::Graphics&, const ClipModel&, const TrackModel&, juce::Rectangle<int>, double bpm);
    void paintGrid (juce::Graphics&, juce::Rectangle<int>);
    void paintAutomation (juce::Graphics&, const TrackModel&, juce::Rectangle<int> laneArea);
    juce::Rectangle<int> automationArea (int trackIndex) const;
    static float yToValue (juce::Rectangle<int> lane, int y) { return juce::jlimit (0.0f, 1.0f, 1.0f - (float) (y - lane.getY() - 4) / (float) juce::jmax (1, lane.getHeight() - 8)); }
    static int valueToY (juce::Rectangle<int> lane, float v) { return lane.getY() + 4 + (int) ((1.0f - v) * (lane.getHeight() - 8)); }
    void rebuildImage();
    void showClipMenu (const juce::MouseEvent&, Hit);
    void showEmptyMenu (const juce::MouseEvent&);
    double beatsPerBar() const;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { markDirty(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { markDirty(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { markDirty(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { markDirty(); }
    void valueTreeRedirected (juce::ValueTree&) override { markDirty(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { markDirty(); }
    void timerCallback() override;

    Session& session;
    TimelineViewState& view;
    juce::Image cache; bool dirty = true;
    int lastPlayheadX = -1;

    // gesture state
    enum class Gesture { None, Move, TrimLeft, TrimRight, FadeIn, FadeOut, TimeSelect, RubberBand, Duplicate, AutomationPoint } gesture = Gesture::None;
    int autoPointIndex = -1; juce::ValueTree autoLane; juce::Rectangle<int> autoLaneBounds;
    Hit gestureHit;
    double gestureStartBeat = 0, previewDeltaBeat = 0; int previewDeltaTrack = 0;
    juce::Point<int> dragStartPos;
    std::vector<std::pair<ClipModel, double>> gestureClipStarts;
    double previewValue = 0;   // for trims/fades
    bool showDropIndicator = false; double dropBeat = 0; int dropTrackIndex = -1;
    juce::Rectangle<int> rubberBand;
    juce::ValueTree clipboard;
};
} // namespace mashup::ui
