#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"
#include "Tracks/TrackModel.h"
#include "TimelineViewState.h"

namespace mashup::ui
{
/** Left-hand header for one track: colour, name, M/S/R, volume, pan, meter, height handle. */
class TrackHeader : public juce::Component, private juce::ValueTree::Listener, private juce::Timer
{
public:
    TrackHeader (Session&, TimelineViewState&, TrackModel);
    ~TrackHeader() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    TrackModel getTrack() const { return track; }
    std::function<void (TrackHeader&, int deltaIndex)> onReorder;
private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { refresh(); }
    void timerCallback() override;
    void refresh();
    void showMenu (const juce::MouseEvent&);
    Session& session;
    TimelineViewState& view;
    TrackModel track;
    juce::ValueTree trackState;   // keeps the ValueTree wrapper (and therefore the listener registration) alive
    juce::Label name;
    juce::TextButton mute { "M" }, solo { "S" }, arm { "R" }, autoButton { "A" };
    juce::ComboBox autoParam, autoMode;
    void fillAutomationParams();
    juce::Slider volume, pan;
    float meter[2] { 0, 0 };
    bool resizing = false; int resizeStartHeight = 0;
    std::vector<juce::String> autoParamIds;
};

/** Vertical stack of TrackHeaders kept in sync with the project's TRACKS node. */
class TrackHeaderList : public juce::Component, private juce::ValueTree::Listener, private juce::ChangeListener
{
public:
    TrackHeaderList (Session&, TimelineViewState&);
    ~TrackHeaderList() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void rebuild();
    int getTotalHeight() const;
    void mouseDown (const juce::MouseEvent&) override;
private:
    void valueTreeChildAdded (juce::ValueTree& p, juce::ValueTree&) override { if (p.hasType (ids::TRACKS)) rebuild(); }
    void valueTreeChildRemoved (juce::ValueTree& p, juce::ValueTree&, int) override { if (p.hasType (ids::TRACKS)) rebuild(); }
    void valueTreeChildOrderChanged (juce::ValueTree& p, int, int) override { if (p.hasType (ids::TRACKS)) rebuild(); }
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i) override { if (v.hasType (ids::TRACK) && (i == ids::height || i == juce::Identifier ("showAutomation"))) resized(); }
    void valueTreeRedirected (juce::ValueTree&) override { rebuild(); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { resized(); }
    Session& session;
    TimelineViewState& view;
    std::vector<std::unique_ptr<TrackHeader>> headers;
};
}
