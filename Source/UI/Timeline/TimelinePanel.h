#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"
#include "TimelineViewState.h"
#include "Ruler.h"
#include "TrackHeader.h"
#include "ArrangementCanvas.h"

namespace mashup::ui
{
/** Timeline: toolbar, ruler, track headers, arrangement canvas and scrollbars. */
class TimelinePanel : public juce::Component, private juce::ChangeListener, private juce::ScrollBar::Listener, private juce::ValueTree::Listener
{
public:
    explicit TimelinePanel (Session&);
    ~TimelinePanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    TimelineViewState& getView() { return view; }
    ArrangementCanvas& getCanvas() { return *canvas; }

    void zoomIn(); void zoomOut(); void zoomToFit(); void zoomToSelection();
    void importFiles (const juce::StringArray& files, double beat, TrackModel track);
    void placeSource (const juce::String& sourceId, double beat, TrackModel track);
    void setTool (Tool);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { updateScrollbars(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { updateScrollbars(); }
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i) override { if (i == ids::height || i == ids::start || i == ids::length || i == juce::Identifier ("showAutomation")) updateScrollbars(); }
    void updateScrollbars();

    Session& session;
    TimelineViewState view;
    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<TrackHeaderList> headers;
    std::unique_ptr<ArrangementCanvas> canvas;
    juce::ScrollBar hScroll { false }, vScroll { true };
    juce::TextButton snapButton { "Snap" }, selectTool { "Select" }, bladeTool { "Blade" }, addTrackButton { "+ Track" }, zoomInButton { "+" }, zoomOutButton { "-" }, fitButton { "Fit" }, followButton { "Follow" };
    juce::ComboBox gridBox;
    juce::Label gridLabel;
    bool updatingScroll = false;
};
}
