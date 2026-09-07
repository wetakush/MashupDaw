#include "MashupPanel.h"
#include "UI/Theme/Theme.h"
#include "Tracks/TrackModel.h"
#include "Clips/ClipOperations.h"
#include "AudioEngine/AudioEngine.h"
#include "Core/MusicalKey.h"

namespace mashup::ui
{
MashupPanel::MashupPanel (Session& s, TimelineViewState& v) : session (s), view (v)
{
    for (auto* l : { &vocalLabel, &instrLabel, &infoLabel, &hint }) { addAndMakeVisible (*l); l->setFont (Theme::ui (11.0f)); l->setColour (juce::Label::textColourId, colours::textDim); }
    vocalLabel.setText ("VOCAL", juce::dontSendNotification); instrLabel.setText ("INSTRUMENTAL", juce::dontSendNotification);
    infoLabel.setFont (Theme::mono (11.0f)); infoLabel.setJustificationType (juce::Justification::topLeft);
    hint.setText ("Pick the clips, press Analyse. Double-click a plan or press Apply. A/B toggles the applied plan on/off while playing.", juce::dontSendNotification);
    addAndMakeVisible (vocalBox); addAndMakeVisible (instrBox);
    for (auto* b : { &proposeButton, &applyButton, &abButton, &loopButton, &swapButton }) addAndMakeVisible (*b);
    proposeButton.onClick = [this] { propose(); };
    applyButton.onClick = [this] { applySelected(); };
    abButton.onClick = [this] { toggleAB(); };
    loopButton.onClick = [this] { loopAtVocal(); };
    swapButton.onClick = [this] { const int a = vocalBox.getSelectedId(), b = instrBox.getSelectedId(); vocalBox.setSelectedId (b); instrBox.setSelectedId (a); };
    addAndMakeVisible (alignPhrases); alignPhrases.setToggleState (true, juce::dontSendNotification);
    addAndMakeVisible (list); list.setModel (this); list.setRowHeight (40);
    applyButton.setEnabled (false); abButton.setEnabled (false);
    session.getProject().getRoot().addListener (this);
    view.addChangeListener (this);
    refreshClipLists();
}
MashupPanel::~MashupPanel() { session.getProject().getRoot().removeListener (this); view.removeChangeListener (this); }

void MashupPanel::refreshClipLists()
{
    const auto prevV = clipForCombo (vocalBox).getId(), prevI = clipForCombo (instrBox).getId();
    clipIds.clear(); vocalBox.clear (juce::dontSendNotification); instrBox.clear (juce::dontSendNotification);
    int id = 1;
    for (auto t : session.getProject().tracks())
        for (auto c : TrackModel (t).clips())
        {
            ClipModel clip (c);
            const auto label = TrackModel (t).getName() + " / " + clip.getName() + (clip.getClipBpm() > 0 ? "  " + juce::String (clip.getClipBpm(), 1) : "") + (clip.getKeyRoot() >= 0 ? "  " + key::name (clip.getKeyRoot(), clip.getKeyMode()) : "");
            vocalBox.addItem (label, id); instrBox.addItem (label, id);
            clipIds.push_back (clip.getId()); ++id;
        }
    auto select = [&] (juce::ComboBox& box, const juce::String& prev, const char* stemGuess)
    {
        for (size_t i = 0; i < clipIds.size(); ++i) if (clipIds[i] == prev) { box.setSelectedId ((int) i + 1, juce::dontSendNotification); return; }
        for (size_t i = 0; i < clipIds.size(); ++i)
        {
            auto c = ClipModel (session.getProject().findById (ids::CLIP, clipIds[i]));
            auto src = ProjectModel::findByIdIn (session.getProject().sources(), ids::SOURCE, c.getSourceId());
            const auto nm = (src[ids::name].toString() + " " + src[ids::stemType].toString()).toLowerCase();
            if (nm.contains (stemGuess)) { box.setSelectedId ((int) i + 1, juce::dontSendNotification); return; }
        }
    };
    select (vocalBox, prevV, "voc"); select (instrBox, prevI, "instr");
    if (instrBox.getSelectedId() == 0 && vocalBox.getSelectedId() == 0 && clipIds.size() >= 2) { vocalBox.setSelectedId (1, juce::dontSendNotification); instrBox.setSelectedId (2, juce::dontSendNotification); }
}

ClipModel MashupPanel::clipForCombo (const juce::ComboBox& b) const
{
    const int i = b.getSelectedId() - 1;
    if (i < 0 || i >= (int) clipIds.size()) return {};
    return ClipModel (session.getProject().findById (ids::CLIP, clipIds[(size_t) i]));
}

void MashupPanel::openWithSelection()
{
    // if two clips are selected, use them as vocal/instrumental in selection order
    if (view.selectedClips.size() >= 2)
    {
        auto it = view.selectedClips.begin();
        for (size_t i = 0; i < clipIds.size(); ++i) if (clipIds[i] == *it) vocalBox.setSelectedId ((int) i + 1);
        ++it;
        for (size_t i = 0; i < clipIds.size(); ++i) if (clipIds[i] == *it) instrBox.setSelectedId ((int) i + 1);
    }
    propose();
}

void MashupPanel::propose()
{
    auto v = clipForCombo (vocalBox), in = clipForCombo (instrBox);
    candidates.clear(); applied = false; showingB = false; abButton.setEnabled (false);
    if (! v.isValid() || ! in.isValid() || v.getId() == in.getId()) { infoLabel.setText ("Select two different clips.", juce::dontSendNotification); list.updateContent(); return; }
    auto& p = session.getProject();
    vocalPart = MashupAssistant::partFromSource (ProjectModel::findByIdIn (p.sources(), ids::SOURCE, v.getSourceId()));
    instrPart = MashupAssistant::partFromSource (ProjectModel::findByIdIn (p.sources(), ids::SOURCE, in.getSourceId()));
    // clip-level overrides (user corrected bpm/key on the clip)
    if (v.getClipBpm() > 0) vocalPart.bpm = v.getClipBpm(); if (in.getClipBpm() > 0) instrPart.bpm = in.getClipBpm();
    if (v.getKeyRoot() >= 0) { vocalPart.keyRoot = v.getKeyRoot(); vocalPart.keyMode = v.getKeyMode(); }
    if (in.getKeyRoot() >= 0) { instrPart.keyRoot = in.getKeyRoot(); instrPart.keyMode = in.getKeyMode(); }
    juce::String info;
    info << "Vocal: " << vocalPart.name << "  " << juce::String (vocalPart.bpm, 1) << " BPM  " << key::name (vocalPart.keyRoot, vocalPart.keyMode) << " (" << key::camelot (vocalPart.keyRoot, vocalPart.keyMode) << ")\n";
    info << "Instrumental: " << instrPart.name << "  " << juce::String (instrPart.bpm, 1) << " BPM  " << key::name (instrPart.keyRoot, instrPart.keyMode) << " (" << key::camelot (instrPart.keyRoot, instrPart.keyMode) << ")\n";
    if (vocalPart.bpm > 0 && instrPart.bpm > 0) info << "Tempo difference " << juce::String (instrPart.bpm - vocalPart.bpm, 1) << " BPM (" << juce::String ((instrPart.bpm / vocalPart.bpm - 1.0) * 100.0, 1) << "%)";
    if (vocalPart.keyRoot >= 0 && instrPart.keyRoot >= 0) { static const char* w[] = { "same key", "compatible", "energy boost", "clash" }; info << "   Key: " << w[key::compatibility (vocalPart.keyRoot, vocalPart.keyMode, instrPart.keyRoot, instrPart.keyMode)] << ", shortest shift " << key::shortestShift (vocalPart.keyRoot, instrPart.keyRoot) << " st"; }
    infoLabel.setText (info, juce::dontSendNotification);
    candidates = MashupAssistant::propose (vocalPart, instrPart);
    if (candidates.empty()) infoLabel.setText (info + "\nNot enough analysis data (BPM missing). Analyse the sources first.", juce::dontSendNotification);
    list.updateContent(); list.selectRow (0);
    applyButton.setEnabled (! candidates.empty());
}

void MashupPanel::applySelected()
{
    const int row = list.getSelectedRow();
    if (row < 0 || row >= (int) candidates.size()) return;
    auto v = clipForCombo (vocalBox), in = clipForCombo (instrBox);
    if (! v.isValid() || ! in.isValid()) return;
    if (applied && ! showingB) session.getUndoManager().redo();   // make sure we are at "B" before re-applying
    if (applied) session.getUndoManager().undo();
    MashupAssistant::apply (session.getProject(), v, in, vocalPart, instrPart, candidates[(size_t) row], alignPhrases.getToggleState());
    applied = true; showingB = true; abButton.setEnabled (true);
    view.selectClip (v.getId()); view.selectClip (in.getId(), true);
    session.getAudioEngine().updateLoopFromProject();
}

void MashupPanel::toggleAB()
{
    if (! applied) return;
    if (showingB) session.getUndoManager().undo(); else session.getUndoManager().redo();
    showingB = ! showingB;
    abButton.setButtonText (showingB ? "A/B: hearing B (plan)" : "A/B: hearing A (original)");
}

void MashupPanel::loopAtVocal()
{
    auto v = clipForCombo (vocalBox);
    if (! v.isValid()) return;
    auto& p = session.getProject();
    const double bpb = p.getTimeSigNumerator() * 4.0 / p.getTimeSigDenominator();
    const double start = std::floor (v.getStart() / bpb) * bpb;
    p.setLoop (start, start + 8 * bpb, true);
    session.getAudioEngine().updateLoopFromProject();
    session.getAudioEngine().locateBeat (start);
    session.getAudioEngine().getTransport().play();
}

void MashupPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    if (row >= (int) candidates.size()) return;
    const auto& c = candidates[(size_t) row];
    g.fillAll (sel ? colours::accentDim.withAlpha (0.35f) : (row % 2 ? colours::panelBg : colours::panelBgAlt));
    // score bar
    auto bar = juce::Rectangle<int> (6, 6, 46, h - 12);
    g.setColour (colours::windowBg); g.fillRect (bar);
    g.setColour (c.score > 70 ? colours::meterLow : c.score > 40 ? colours::warning : colours::danger);
    g.fillRect (bar.withHeight ((int) (bar.getHeight() * c.score / 100.0)).withBottomY (bar.getBottom()));
    g.setColour (colours::text); g.setFont (Theme::ui (11.0f, true)); g.drawText (juce::String ((int) c.score), bar, juce::Justification::centred);
    g.setFont (Theme::ui (12.0f, true)); g.drawText (c.tempoText, 60, 3, w - 66, 16, juce::Justification::centredLeft, true);
    g.setFont (Theme::ui (11.0f)); g.setColour (colours::textDim); g.drawText (c.keyText, 60, 21, w - 66, 16, juce::Justification::centredLeft, true);
}

void MashupPanel::paint (juce::Graphics& g) { g.fillAll (colours::panelBg); }

void MashupPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto left = r.removeFromLeft (juce::jmin (420, r.getWidth() / 2));
    { auto row = left.removeFromTop (24); vocalLabel.setBounds (row.removeFromLeft (100)); vocalBox.setBounds (row.reduced (0, 1)); }
    { auto row = left.removeFromTop (24); instrLabel.setBounds (row.removeFromLeft (100)); instrBox.setBounds (row.reduced (0, 1)); }
    { auto row = left.removeFromTop (26); swapButton.setBounds (row.removeFromLeft (60).reduced (1)); proposeButton.setBounds (row.removeFromLeft (140).reduced (1)); alignPhrases.setBounds (row); }
    infoLabel.setBounds (left.removeFromTop (54));
    { auto row = left.removeFromTop (26); applyButton.setBounds (row.removeFromLeft (110).reduced (1)); abButton.setBounds (row.removeFromLeft (160).reduced (1)); loopButton.setBounds (row.reduced (1)); }
    hint.setBounds (left.removeFromTop (30));
    r.removeFromLeft (8);
    list.setBounds (r);
}
} // namespace mashup::ui
