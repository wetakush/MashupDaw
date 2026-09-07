#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"
#include "Clips/ClipModel.h"
#include "UI/Timeline/TimelineViewState.h"

namespace mashup::ui
{
/** Right panel: properties of the selected clip (or track / project when nothing is selected). */
class InspectorPanel : public juce::Component, private juce::ChangeListener, private juce::ValueTree::Listener
{
public:
    InspectorPanel (Session&, TimelineViewState&);
    ~InspectorPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { if (! updating) refresh(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { refresh(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { refresh(); }
    void refresh();
    ClipModel currentClip() const;
    juce::UndoManager* um();

    struct Row { juce::Label label; juce::Component* control = nullptr; };
    juce::Label& makeLabel (const juce::String&);
    void layoutRows (juce::Rectangle<int>& area, std::initializer_list<std::pair<juce::Label*, juce::Component*>> rows, int rowH = 24);

    Session& session;
    TimelineViewState& view;
    bool updating = false;

    juce::Label title, sourceInfo;
    // clip controls
    juce::Label nameLabel, gainLabel, panLabel, pitchLabel, centsLabel, formantLabel, modeLabel, bpmLabel, syncLabel, rateLabel, keyLabel, fadeInLabel, fadeOutLabel, startLabel, lengthLabel, offsetLabel;
    juce::TextEditor nameEditor;
    juce::Slider gain, pan, pitch, cents, formant, rate, fadeIn, fadeOut;
    juce::ComboBox mode, keyBox, fadeInShape, fadeOutShape;
    juce::TextEditor clipBpm;
    juce::ToggleButton sync { "Sync to project tempo" }, reverse { "Reverse" }, loop { "Loop" }, mute { "Mute" };
    juce::TextButton matchBpm { "Match project BPM" }, matchKey { "Match project key" }, halfBpm { "/2" }, doubleBpm { "x2" }, useAsProjectBpm { "Set as project BPM" }, useAsProjectKey { "Set as project key" };
    juce::Label startValue, lengthValue, offsetValue;
    juce::Component clipSection;
    std::vector<juce::Component*> clipControls;
};
}
