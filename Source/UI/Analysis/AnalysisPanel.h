#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Project/Session.h"
#include "UI/Timeline/TimelineViewState.h"
#include "UI/Mashup/CamelotWheel.h"
#include "Clips/ClipModel.h"
#include "Analysis/AnalysisData.h"

namespace mashup::ui
{
/** Bottom tab: analysis results of the selected clip's source with manual correction (BPM, downbeat, key,
    phrases) plus the Camelot wheel and key-compatibility summary of all clips. */
class AnalysisPanel : public juce::Component, private juce::ChangeListener, private juce::ValueTree::Listener, private juce::ListBoxModel
{
public:
    AnalysisPanel (Session&, TimelineViewState&);
    ~AnalysisPanel() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { if (! updating) refresh(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { refresh(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { refresh(); }
    int getNumRows() override;
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override;
    void listBoxItemClicked (int, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override;

    void refresh();
    ClipModel currentClip() const;
    juce::ValueTree currentSource() const;
    void setSourceBpm (double bpm, bool regenerateGrid);
    void shiftDownbeat (int beats);
    void setDownbeatFromPlayhead();
    void regenerateGrid (double bpm, double firstDownbeat);

    Session& session;
    TimelineViewState& view;
    bool updating = false;
    juce::Label title, info, compatText, phrasesLabel;
    juce::TextEditor bpmEditor;
    juce::TextButton halve { "/2" }, dbl { "x2" }, reanalyse { "Re-analyse" }, dbLeft { "< downbeat" }, dbRight { "downbeat >" }, dbPlayhead { "Downbeat = playhead" }, applyToClips { "Apply to clips" }, toProject { "To project" };
    juce::ComboBox keyBox;
    juce::ToggleButton showBeats { "Beat markers" }, showTransients { "Transients" }, showPhrases { "Phrases" };
    juce::ListBox phraseList;
    CamelotWheel wheel;
    std::vector<analysis::Phrase> phrases;
};
}
