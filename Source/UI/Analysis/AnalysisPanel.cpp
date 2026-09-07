#include "AnalysisPanel.h"
#include "UI/Theme/Theme.h"
#include "Analysis/AnalysisService.h"
#include "Analysis/AnalysisData.h"
#include "Analysis/BpmDetector.h"
#include "Core/MusicalKey.h"
#include "Core/MusicalTime.h"
#include "Clips/ClipOperations.h"
#include "Tracks/TrackModel.h"
#include "AudioEngine/AudioEngine.h"
#include "Import/SourceLibrary.h"

namespace mashup::ui
{
AnalysisPanel::AnalysisPanel (Session& s, TimelineViewState& v) : session (s), view (v)
{
    addAndMakeVisible (title); title.setFont (Theme::ui (13.0f, true));
    addAndMakeVisible (info); info.setFont (Theme::mono (11.0f)); info.setColour (juce::Label::textColourId, colours::textDim); info.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (compatText); compatText.setFont (Theme::ui (11.0f)); compatText.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (phrasesLabel); phrasesLabel.setText ("Phrases (double-click: rename, click: select on timeline)", juce::dontSendNotification); phrasesLabel.setFont (Theme::ui (11.0f)); phrasesLabel.setColour (juce::Label::textColourId, colours::textDim);
    addAndMakeVisible (bpmEditor); bpmEditor.setFont (Theme::mono (14.0f)); bpmEditor.setJustification (juce::Justification::centred);
    bpmEditor.onReturnKey = [this] { const double v = bpmEditor.getText().getDoubleValue(); if (v >= 20 && v <= 400) setSourceBpm (v, true); };
    for (auto* b : { &halve, &dbl, &reanalyse, &dbLeft, &dbRight, &dbPlayhead, &applyToClips, &toProject }) addAndMakeVisible (*b);
    halve.onClick = [this] { auto src = currentSource(); if (src.isValid()) setSourceBpm ((double) src[ids::bpm] / 2, true); };
    dbl.onClick = [this] { auto src = currentSource(); if (src.isValid()) setSourceBpm ((double) src[ids::bpm] * 2, true); };
    reanalyse.onClick = [this] { auto src = currentSource(); if (src.isValid()) session.getAnalysis().analyseSource (src[ids::id], true); };
    dbLeft.onClick = [this] { shiftDownbeat (-1); };
    dbRight.onClick = [this] { shiftDownbeat (1); };
    dbPlayhead.onClick = [this] { setDownbeatFromPlayhead(); };
    dbPlayhead.setTooltip ("Marks the source position under the playhead (inside the selected clip) as a bar start");
    applyToClips.onClick = [this]
    {
        auto src = currentSource(); if (! src.isValid()) return;
        auto& p = session.getProject(); p.getUndoManager().beginNewTransaction ("Apply analysis to clips");
        for (auto t : p.tracks()) for (auto c : TrackModel (t).clips()) { ClipModel clip (c); if (clip.getSourceId() != src[ids::id].toString()) continue; clip.setKey ((int) src[ids::keyRoot], (int) src[ids::keyMode], &p.getUndoManager()); if (clip.isSyncedToProject()) clipops::matchToProjectBpm (p, clip, (double) src[ids::bpm]); else clip.setClipBpm ((double) src[ids::bpm], &p.getUndoManager()); }
    };
    toProject.onClick = [this] { auto src = currentSource(); if (! src.isValid()) return; auto& p = session.getProject(); p.getUndoManager().beginNewTransaction ("Project tempo/key from source"); p.setBpm ((double) src[ids::bpm]); if ((int) src[ids::keyRoot] >= 0) p.setKey ((int) src[ids::keyRoot], (int) src[ids::keyMode]); };
    addAndMakeVisible (keyBox);
    keyBox.addItem ("unknown", 1);
    for (int m = 0; m < 2; ++m) for (int r = 0; r < 12; ++r) keyBox.addItem (key::name (r, m) + "  (" + key::camelot (r, m) + ")", 2 + m * 12 + r);
    keyBox.onChange = [this] { if (updating) return; auto src = currentSource(); if (! src.isValid()) return; const int id = keyBox.getSelectedId(); src.setProperty (ids::keyRoot, id <= 1 ? -1 : (id - 2) % 12, nullptr); src.setProperty (ids::keyMode, id <= 1 ? 0 : (id - 2) / 12, nullptr); src.setProperty (ids::keyConfidence, 1.0, nullptr); refresh(); };
    for (auto* t : { &showBeats, &showTransients, &showPhrases }) addAndMakeVisible (*t);
    showBeats.setToggleState (view.showBeatMarkers, juce::dontSendNotification); showTransients.setToggleState (view.showTransients, juce::dontSendNotification); showPhrases.setToggleState (view.showPhrases, juce::dontSendNotification);
    showBeats.onClick = [this] { view.showBeatMarkers = showBeats.getToggleState(); view.sendChangeMessage(); };
    showTransients.onClick = [this] { view.showTransients = showTransients.getToggleState(); view.sendChangeMessage(); };
    showPhrases.onClick = [this] { view.showPhrases = showPhrases.getToggleState(); view.sendChangeMessage(); };
    addAndMakeVisible (phraseList); phraseList.setModel (this); phraseList.setRowHeight (20);
    addAndMakeVisible (wheel);
    wheel.onKeyClicked = [this] (int r, int m) { auto& p = session.getProject(); p.getUndoManager().beginNewTransaction ("Project key"); p.setKey (r, m); };

    view.addChangeListener (this);
    session.getProject().getRoot().addListener (this);
    session.getAnalysis().addChangeListener (this);
    refresh();
}

AnalysisPanel::~AnalysisPanel() { view.removeChangeListener (this); session.getProject().getRoot().removeListener (this); session.getAnalysis().removeChangeListener (this); }

ClipModel AnalysisPanel::currentClip() const { if (view.selectedClips.empty()) return {}; return ClipModel (session.getProject().findById (ids::CLIP, *view.selectedClips.begin())); }
juce::ValueTree AnalysisPanel::currentSource() const { auto c = currentClip(); return c.isValid() ? ProjectModel::findByIdIn (session.getProject().sources(), ids::SOURCE, c.getSourceId()) : juce::ValueTree(); }

void AnalysisPanel::regenerateGrid (double bpm, double firstDownbeat)
{
    auto src = currentSource(); if (! src.isValid() || bpm <= 0) return;
    const double length = (double) src[ids::lengthSamples] / juce::jmax (1.0, (double) src[ids::sampleRate]);
    auto beats = analysis::BpmDetector::gridFrom (bpm, firstDownbeat, length);
    std::vector<double> downbeats;
    const int bpb = session.getProject().getTimeSigNumerator();
    // downbeats every bpb beats, phase such that firstDownbeat is one of them
    double t = firstDownbeat; const double bar = bpb * 60.0 / bpm;
    while (t - bar >= 0) t -= bar;
    for (; t < length; t += bar) downbeats.push_back (t);
    src.setProperty (ids::bpm, bpm, nullptr);
    src.setProperty (ids::firstDownbeat, firstDownbeat, nullptr);
    src.setProperty (ids::bpmConfidence, 1.0, nullptr);
    analysis::writeTimes (src, ids::BEATS, beats, nullptr);
    analysis::writeTimes (src, ids::DOWNBEATS, downbeats, nullptr);
}

void AnalysisPanel::setSourceBpm (double bpm, bool regen)
{
    auto src = currentSource(); if (! src.isValid()) return;
    if (regen) regenerateGrid (bpm, (double) src.getProperty (ids::firstDownbeat, 0.0));
    else src.setProperty (ids::bpm, bpm, nullptr);
    refresh();
}

void AnalysisPanel::shiftDownbeat (int beats)
{
    auto src = currentSource(); if (! src.isValid()) return;
    const double bpm = (double) src[ids::bpm]; if (bpm <= 0) return;
    regenerateGrid (bpm, (double) src.getProperty (ids::firstDownbeat, 0.0) + beats * 60.0 / bpm);
    refresh();
}

void AnalysisPanel::setDownbeatFromPlayhead()
{
    auto c = currentClip(); auto src = currentSource(); if (! src.isValid()) return;
    const double bpm = session.getProject().getBpm();
    const double beat = session.getAudioEngine().getPositionBeat();
    if (beat < c.getStart() || beat > c.getEnd()) return;
    const double srcTime = c.getOffset() + clipops::beatsToSourceSeconds (c, beat - c.getStart(), bpm);
    const double srcBpm = (double) src[ids::bpm];
    if (srcBpm > 0) regenerateGrid (srcBpm, srcTime);
    refresh();
}

void AnalysisPanel::refresh()
{
    updating = true;
    auto& p = session.getProject();
    auto c = currentClip(); auto src = currentSource();
    const bool has = src.isValid();
    for (auto* comp : std::initializer_list<juce::Component*> { &bpmEditor, &halve, &dbl, &reanalyse, &dbLeft, &dbRight, &dbPlayhead, &applyToClips, &toProject, &keyBox, &phraseList, &phrasesLabel }) comp->setVisible (has);
    wheel.setProjectKey (p.getKeyRoot(), p.getKeyMode());
    // marks: every clip's key
    std::vector<CamelotWheel::Mark> marks;
    juce::String compat;
    struct Entry { juce::String name; int root, mode; juce::Colour colour; };
    std::vector<Entry> entries;
    for (auto t : p.tracks()) for (auto cn : TrackModel (t).clips()) { ClipModel cl (cn); if (cl.getKeyRoot() >= 0) { int r = (cl.getKeyRoot() + cl.getPitchSemis() + 1200) % 12; marks.push_back ({ r, cl.getKeyMode(), TrackModel (t).getColour(), cl.getName() }); entries.push_back ({ cl.getName(), r, cl.getKeyMode(), TrackModel (t).getColour() }); } }
    wheel.setMarks (marks);
    static const char* words[] = { "same key", "compatible", "energy boost / mood change", "clash" };
    for (auto& e : entries)
        compat << e.name << " (" << key::name (e.root, e.mode) << ", " << key::camelot (e.root, e.mode) << ") vs project: " << words[key::compatibility (e.root, e.mode, p.getKeyRoot(), p.getKeyMode())] << "\n";
    for (size_t i = 0; i < entries.size(); ++i) for (size_t j = i + 1; j < entries.size(); ++j)
        compat << entries[i].name << " + " << entries[j].name << ": " << words[key::compatibility (entries[i].root, entries[i].mode, entries[j].root, entries[j].mode)] << "\n";
    compatText.setText (compat, juce::dontSendNotification);

    if (! has) { title.setText ("Analysis - select a clip", juce::dontSendNotification); info.setText ("", juce::dontSendNotification); phrases.clear(); phraseList.updateContent(); updating = false; return; }
    title.setText ("Analysis: " + src[ids::name].toString(), juce::dontSendNotification);
    juce::String s;
    if (session.getAnalysis().isAnalysing (src[ids::id])) s << "analysing... " << (int) (session.getAnalysis().getProgress (src[ids::id]) * 100) << "%\n";
    else if (! (bool) src.getProperty (ids::analysed, false)) s << "not analysed\n";
    s << "BPM " << juce::String ((double) src[ids::bpm], 2) << "  (confidence " << (int) ((double) src[ids::bpmConfidence] * 100) << "%)\n";
    s << "Key " << key::name ((int) src[ids::keyRoot], (int) src[ids::keyMode]) << " " << key::camelot ((int) src[ids::keyRoot], (int) src[ids::keyMode]) << "  (confidence " << (int) ((double) src[ids::keyConfidence] * 100) << "%)\n";
    s << "First downbeat " << formatTime ((double) src.getProperty (ids::firstDownbeat, 0.0)) << "\n";
    s << juce::String ((double) src[ids::lufs], 1) << " LUFS   peak " << juce::String (juce::Decibels::gainToDecibels ((double) src[ids::peak]), 1) << " dBFS\n";
    const double sr = (double) src[ids::sampleRate];
    s << (int) sr << " Hz  " << (int) src[ids::channels] << " ch  " << formatTime (sr > 0 ? (double) src[ids::lengthSamples] / sr : 0.0) << "\n";
    s << analysis::readTimes (src, ids::BEATS).size() << " beats, " << analysis::readTimes (src, ids::TRANSIENTS).size() << " transients";
    info.setText (s, juce::dontSendNotification);
    bpmEditor.setText (juce::String ((double) src[ids::bpm], 2), false);
    keyBox.setSelectedId ((int) src[ids::keyRoot] < 0 ? 1 : 2 + (int) src[ids::keyMode] * 12 + (int) src[ids::keyRoot], juce::dontSendNotification);
    phrases = analysis::readPhrases (src);
    phraseList.updateContent(); phraseList.repaint();
    updating = false;
}

int AnalysisPanel::getNumRows() { return (int) phrases.size(); }
void AnalysisPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel)
{
    if (row >= (int) phrases.size()) return;
    const auto& ph = phrases[(size_t) row];
    g.fillAll (sel ? colours::accentDim.withAlpha (0.3f) : (row % 2 ? colours::panelBg : colours::panelBgAlt));
    g.setColour (colours::text); g.setFont (Theme::mono (11.0f));
    g.drawText (formatTime (ph.start, false) + " - " + formatTime (ph.end, false), 6, 0, 110, h, juce::Justification::centredLeft);
    g.setFont (Theme::ui (12.0f, true)); g.drawText (ph.label, 120, 0, w - 200, h, juce::Justification::centredLeft);
    g.setColour (ph.confidence > 0.7f ? colours::meterLow : ph.confidence > 0.45f ? colours::warning : colours::danger);
    g.setFont (Theme::ui (10.0f)); g.drawText (juce::String ((int) (ph.confidence * 100)) + "% guess", w - 80, 0, 74, h, juce::Justification::centredRight);
}
void AnalysisPanel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    auto c = currentClip(); if (! c.isValid() || row >= (int) phrases.size()) return;
    const auto& ph = phrases[(size_t) row];
    const double bpm = session.getProject().getBpm();
    const double b0 = c.getStart() + clipops::sourceSecondsToBeats (c, ph.start - c.getOffset(), bpm), b1 = c.getStart() + clipops::sourceSecondsToBeats (c, ph.end - c.getOffset(), bpm);
    view.setTimeSelection (juce::jmax (c.getStart(), b0), juce::jmin (c.getEnd(), b1));
}
void AnalysisPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    auto src = currentSource(); if (! src.isValid() || row >= (int) phrases.size()) return;
    juce::PopupMenu m; int i = 1;
    for (auto* l : { "intro", "verse", "pre-chorus", "chorus", "bridge", "breakdown", "drop", "outro", "section" }) m.addItem (i++, l);
    m.showMenuAsync ({}, [this, row, src] (int r) mutable
    {
        static const char* labels[] = { "intro", "verse", "pre-chorus", "chorus", "bridge", "breakdown", "drop", "outro", "section" };
        if (r <= 0 || row >= (int) phrases.size()) return;
        phrases[(size_t) row].label = labels[r - 1]; phrases[(size_t) row].confidence = 1.0f;
        analysis::writePhrases (src, phrases, nullptr);
        refresh();
    });
}

void AnalysisPanel::paint (juce::Graphics& g) { g.fillAll (colours::panelBg); }

void AnalysisPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    const int wheelSize = juce::jmin (r.getHeight(), 260);
    auto right = r.removeFromRight (juce::jmin (wheelSize + 260, r.getWidth() / 2));
    wheel.setBounds (right.removeFromLeft (wheelSize).reduced (2));
    compatText.setBounds (right.reduced (6, 0));
    auto left = r.removeFromLeft (juce::jmin (330, r.getWidth() / 2));
    title.setBounds (left.removeFromTop (20));
    info.setBounds (left.removeFromTop (92));
    { auto row = left.removeFromTop (26); bpmEditor.setBounds (row.removeFromLeft (90).reduced (0, 2)); row.removeFromLeft (4); halve.setBounds (row.removeFromLeft (34).reduced (1)); dbl.setBounds (row.removeFromLeft (34).reduced (1)); row.removeFromLeft (4); reanalyse.setBounds (row.removeFromLeft (90).reduced (1)); }
    { auto row = left.removeFromTop (26); dbLeft.setBounds (row.removeFromLeft (84).reduced (1)); dbRight.setBounds (row.removeFromLeft (84).reduced (1)); dbPlayhead.setBounds (row.reduced (1)); }
    { auto row = left.removeFromTop (26); keyBox.setBounds (row.removeFromLeft (150).reduced (1, 2)); applyToClips.setBounds (row.removeFromLeft (100).reduced (1)); toProject.setBounds (row.reduced (1)); }
    { auto row = left.removeFromTop (22); showBeats.setBounds (row.removeFromLeft (110)); showTransients.setBounds (row.removeFromLeft (90)); showPhrases.setBounds (row); }
    r.removeFromLeft (8);
    phrasesLabel.setBounds (r.removeFromTop (18));
    phraseList.setBounds (r);
}
} // namespace mashup::ui
