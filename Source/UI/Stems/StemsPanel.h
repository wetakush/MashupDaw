#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"
#include "UI/Timeline/TimelineViewState.h"
#include "StemSeparation/StemSeparationService.h"

namespace mashup::ui
{
/** Bottom tab: start stem separation for the selected clip's source, watch progress, cancel. */
class StemsPanel : public juce::Component, private juce::ChangeListener, private juce::ListBoxModel, private juce::Timer
{
public:
    StemsPanel (Session&, TimelineViewState&);
    ~StemsPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void startForSelection (StemSeparationService::Mode);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { jobList.updateContent(); jobList.repaint(); refreshStatus(); }
    void timerCallback() override { jobList.repaint(); }
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void listBoxItemClicked (int, const juce::MouseEvent&) override;
    void refreshStatus();
    juce::String selectedSourceId() const;

    Session& session;
    TimelineViewState& view;
    juce::Label title, status, modelLabel, deviceLabel;
    juce::ComboBox modelBox, deviceBox;
    juce::TextButton fourStems { "Separate: vocals / drums / bass / other" }, acapella { "Extract acapella" }, instrumental { "Extract instrumental" }, clearButton { "Clear finished" };
    juce::ToggleButton muteOriginal { "Mute original track after separation" };
    juce::ListBox jobList;
};
}
