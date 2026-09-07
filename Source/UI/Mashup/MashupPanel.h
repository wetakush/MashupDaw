#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"
#include "UI/Timeline/TimelineViewState.h"
#include "Mashup/MashupAssistant.h"

namespace mashup::ui
{
/** Bottom tab: pick a vocal clip and an instrumental clip, get ranked tempo/key plans, apply, A/B preview. */
class MashupPanel : public juce::Component, private juce::ChangeListener, private juce::ListBoxModel, private juce::ValueTree::Listener
{
public:
    MashupPanel (Session&, TimelineViewState&);
    ~MashupPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void openWithSelection();

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refreshClipLists(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { refreshClipLists(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { refreshClipLists(); }
    void valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier& i) override { if (v.hasType (ids::CLIP) && i == ids::name) refreshClipLists(); }
    int getNumRows() override { return (int) candidates.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { applySelected(); }

    void refreshClipLists();
    void propose();
    void applySelected();
    void toggleAB();
    void loopAtVocal();
    ClipModel clipForCombo (const juce::ComboBox&) const;

    Session& session;
    TimelineViewState& view;
    juce::Label vocalLabel, instrLabel, infoLabel, hint;
    juce::ComboBox vocalBox, instrBox;
    juce::TextButton proposeButton { "Analyse & propose" }, applyButton { "Apply plan" }, abButton { "A/B (undo/redo)" }, loopButton { "Loop 8 bars & play" }, swapButton { "Swap" };
    juce::ToggleButton alignPhrases { "Align phrases (else: downbeats)" };
    juce::ListBox list;
    std::vector<juce::String> clipIds;
    std::vector<MashupAssistant::Candidate> candidates;
    MashupAssistant::Part vocalPart, instrPart;
    bool applied = false, showingB = false;
};
}
