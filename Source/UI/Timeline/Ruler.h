#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"
#include "TimelineViewState.h"

namespace mashup::ui
{
/** Bars/beats ruler with loop region, markers, and click-to-locate / drag-to-select. */
class Ruler : public juce::Component, private juce::ChangeListener
{
public:
    Ruler (Session&, TimelineViewState&);
    ~Ruler() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
    void showMenu (const juce::MouseEvent&);
    Session& session;
    TimelineViewState& view;
    double dragStartBeat = 0.0;
    enum class Drag { None, Selection, LoopStart, LoopEnd, LoopMove, Marker } drag = Drag::None;
    juce::ValueTree draggedMarker;
    double loopMoveOffset = 0.0;
};
}
