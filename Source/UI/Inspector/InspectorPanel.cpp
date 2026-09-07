#include "InspectorPanel.h"
#include "UI/Theme/Theme.h"
#include "Clips/ClipOperations.h"
#include "Core/MusicalKey.h"
#include "Core/MusicalTime.h"
#include "Timeline/TempoMap.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace mashup::ui
{
InspectorPanel::InspectorPanel (Session& s, TimelineViewState& v) : session (s), view (v)
{
    addAndMakeVisible (title); title.setFont (Theme::ui (13.0f, true));
    addAndMakeVisible (sourceInfo); sourceInfo.setFont (Theme::mono (10.0f)); sourceInfo.setColour (juce::Label::textColourId, colours::textDim); sourceInfo.setJustificationType (juce::Justification::topLeft);

    auto setupLabel = [this] (juce::Label& l, const juce::String& t) { addAndMakeVisible (l); l.setText (t, juce::dontSendNotification); l.setFont (Theme::ui (11.0f)); l.setColour (juce::Label::textColourId, colours::textDim); clipControls.push_back (&l); };
    setupLabel (nameLabel, "Name"); setupLabel (gainLabel, "Gain"); setupLabel (panLabel, "Pan"); setupLabel (pitchLabel, "Pitch (st)"); setupLabel (centsLabel, "Cents");
    setupLabel (formantLabel, "Formant"); setupLabel (modeLabel, "Stretch"); setupLabel (bpmLabel, "Clip BPM"); setupLabel (rateLabel, "Rate"); setupLabel (keyLabel, "Key");
    setupLabel (fadeInLabel, "Fade in"); setupLabel (fadeOutLabel, "Fade out"); setupLabel (startLabel, "Start"); setupLabel (lengthLabel, "Length"); setupLabel (offsetLabel, "Offset");

    auto setupSlider = [this] (juce::Slider& sl, double lo, double hi, double step, const juce::String& suffix, const juce::String& undoName, std::function<void (ClipModel, double)> apply)
    {
        addAndMakeVisible (sl); clipControls.push_back (&sl);
        sl.setSliderStyle (juce::Slider::LinearHorizontal); sl.setRange (lo, hi, step);
        sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18); sl.setTextValueSuffix (suffix);
        sl.onDragStart = [this, undoName] { um()->beginNewTransaction (undoName); };
        sl.onValueChange = [this, &sl, apply] { if (updating) return; auto c = currentClip(); if (c.isValid()) { updating = true; apply (c, sl.getValue()); updating = false; } };
    };
    setupSlider (gain, -48.0, 24.0, 0.1, " dB", "Clip gain", [this] (ClipModel c, double v) { c.setGain (juce::Decibels::decibelsToGain ((float) v, -48.0f), um()); });
    gain.setSkewFactorFromMidPoint (0.0); gain.setDoubleClickReturnValue (true, 0.0);
    setupSlider (pan, -1.0, 1.0, 0.01, "", "Clip pan", [this] (ClipModel c, double v) { c.setPan (v, um()); }); pan.setDoubleClickReturnValue (true, 0.0);
    setupSlider (pitch, -24, 24, 1, " st", "Clip pitch", [this] (ClipModel c, double v) { c.setPitch ((int) v, c.getPitchCents(), um()); }); pitch.setDoubleClickReturnValue (true, 0.0);
    setupSlider (cents, -100, 100, 1, " c", "Clip pitch cents", [this] (ClipModel c, double v) { c.setPitch (c.getPitchSemis(), (int) v, um()); }); cents.setDoubleClickReturnValue (true, 0.0);
    setupSlider (formant, -12, 12, 0.1, " st", "Clip formant", [this] (ClipModel c, double v) { c.setFormant (v, um()); }); formant.setDoubleClickReturnValue (true, 0.0);
    setupSlider (rate, 0.25, 4.0, 0.001, "x", "Clip rate", [this] (ClipModel c, double v)
    {
        // keep the source span: changing speed changes the clip's length on the timeline
        const double bpm = session.getProject().getBpm();
        const double span = clipops::beatsToSourceSeconds (c, c.getLength(), bpm);
        c.setRate (v, um());
        c.setLength (clipops::sourceSecondsToBeats (c, span, bpm), um());
    });
    rate.setSkewFactorFromMidPoint (1.0); rate.setDoubleClickReturnValue (true, 1.0);
    setupSlider (fadeIn, 0.0, 16.0, 0.01, " b", "Fade in", [this] (ClipModel c, double v) { c.setFadeIn (v, um()); });
    setupSlider (fadeOut, 0.0, 16.0, 0.01, " b", "Fade out", [this] (ClipModel c, double v) { c.setFadeOut (v, um()); });
    fadeIn.setSkewFactorFromMidPoint (2.0); fadeOut.setSkewFactorFromMidPoint (2.0);

    addAndMakeVisible (nameEditor); clipControls.push_back (&nameEditor);
    nameEditor.onReturnKey = [this] { auto c = currentClip(); if (c.isValid()) { um()->beginNewTransaction ("Rename clip"); c.setName (nameEditor.getText(), um()); } };
    nameEditor.onFocusLost = nameEditor.onReturnKey;

    addAndMakeVisible (mode); clipControls.push_back (&mode);
    for (int i = 0; i <= (int) StretchMode::Percussive; ++i) mode.addItem (stretchModeName ((StretchMode) i), i + 1);
    mode.onChange = [this] { if (updating) return; auto c = currentClip(); if (c.isValid()) { um()->beginNewTransaction ("Stretch mode"); c.setStretchMode ((StretchMode) (mode.getSelectedId() - 1), um()); } };

    addAndMakeVisible (keyBox); clipControls.push_back (&keyBox);
    keyBox.addItem ("unknown", 1);
    for (int m = 0; m < 2; ++m) for (int r = 0; r < 12; ++r) keyBox.addItem (key::name (r, m) + "  (" + key::camelot (r, m) + ")", 2 + m * 12 + r);
    keyBox.onChange = [this] { if (updating) return; auto c = currentClip(); if (! c.isValid()) return; um()->beginNewTransaction ("Clip key"); const int id = keyBox.getSelectedId(); if (id <= 1) c.setKey (-1, 0, um()); else c.setKey ((id - 2) % 12, (id - 2) / 12, um()); };

    for (auto* fs : { &fadeInShape, &fadeOutShape })
    {
        addAndMakeVisible (*fs); clipControls.push_back (fs);
        fs->addItem ("Linear", 1); fs->addItem ("Equal power", 2); fs->addItem ("Exponential", 3); fs->addItem ("S-curve", 4);
        fs->onChange = [this] { if (updating) return; auto c = currentClip(); if (c.isValid()) { um()->beginNewTransaction ("Fade shape"); c.setFadeShapes ((FadeShape) (fadeInShape.getSelectedId() - 1), (FadeShape) (fadeOutShape.getSelectedId() - 1), um()); } };
    }

    addAndMakeVisible (clipBpm); clipControls.push_back (&clipBpm);
    clipBpm.setFont (Theme::mono (12.0f)); clipBpm.setJustification (juce::Justification::centred);
    clipBpm.onReturnKey = [this]
    {
        auto c = currentClip(); if (! c.isValid()) return;
        const double v = clipBpm.getText().getDoubleValue();
        if (v >= 20 && v <= 400) { um()->beginNewTransaction ("Clip BPM"); const double bpm = session.getProject().getBpm(); const double span = clipops::beatsToSourceSeconds (c, c.getLength(), bpm); c.setClipBpm (v, um()); if (c.isSyncedToProject()) c.setLength (clipops::sourceSecondsToBeats (c, span, bpm), um()); }
        refresh();
    };
    clipBpm.onFocusLost = clipBpm.onReturnKey;

    for (auto* t : { &sync, &reverse, &loop, &mute }) { addAndMakeVisible (*t); clipControls.push_back (t); }
    sync.onClick = [this] { if (updating) return; auto c = currentClip(); if (! c.isValid()) return; const double bpm = session.getProject().getBpm(); if (sync.getToggleState()) { double src = c.getClipBpm(); if (src <= 0) { auto n = ProjectModel::findByIdIn (session.getProject().sources(), ids::SOURCE, c.getSourceId()); src = (double) n.getProperty (ids::bpm, 0.0); } if (src > 0) clipops::matchToProjectBpm (session.getProject(), c, src); else refresh(); } else { um()->beginNewTransaction ("Unsync clip"); const double span = clipops::beatsToSourceSeconds (c, c.getLength(), bpm); c.setSyncedToProject (false, um()); c.setLength (clipops::sourceSecondsToBeats (c, span, bpm), um()); } };
    reverse.onClick = [this] { if (updating) return; auto c = currentClip(); if (c.isValid()) clipops::setReversed (session.getProject(), c, reverse.getToggleState(), session.getProject().getBpm()); };
    loop.onClick = [this] { if (updating) return; auto c = currentClip(); if (! c.isValid()) return; um()->beginNewTransaction ("Loop clip"); if (loop.getToggleState()) c.getState().setProperty (ids::sourceEnd, c.getOffset() + clipops::beatsToSourceSeconds (c, c.getLength(), session.getProject().getBpm()), um()); c.setLooped (loop.getToggleState(), um()); };
    mute.onClick = [this] { if (updating) return; auto c = currentClip(); if (c.isValid()) { um()->beginNewTransaction ("Mute clip"); c.setMuted (mute.getToggleState(), um()); } };

    for (auto* b : { &matchBpm, &matchKey, &halfBpm, &doubleBpm, &useAsProjectBpm, &useAsProjectKey }) { addAndMakeVisible (*b); clipControls.push_back (b); }
    matchBpm.onClick = [this] { auto c = currentClip(); if (! c.isValid()) return; double src = c.getClipBpm(); if (src <= 0) { auto n = ProjectModel::findByIdIn (session.getProject().sources(), ids::SOURCE, c.getSourceId()); src = (double) n.getProperty (ids::bpm, 0.0); } if (src > 0) clipops::matchToProjectBpm (session.getProject(), c, src); };
    matchKey.onClick = [this] { auto c = currentClip(); if (c.isValid()) clipops::matchToKey (session.getProject(), c, session.getProject().getKeyRoot()); };
    halfBpm.onClick = [this] { auto c = currentClip(); if (c.isValid() && c.getClipBpm() > 0) { um()->beginNewTransaction ("Halve clip BPM"); const double bpm = session.getProject().getBpm(); const double span = clipops::beatsToSourceSeconds (c, c.getLength(), bpm); c.setClipBpm (c.getClipBpm() / 2, um()); if (c.isSyncedToProject()) c.setLength (clipops::sourceSecondsToBeats (c, span, bpm), um()); } };
    doubleBpm.onClick = [this] { auto c = currentClip(); if (c.isValid() && c.getClipBpm() > 0) { um()->beginNewTransaction ("Double clip BPM"); const double bpm = session.getProject().getBpm(); const double span = clipops::beatsToSourceSeconds (c, c.getLength(), bpm); c.setClipBpm (c.getClipBpm() * 2, um()); if (c.isSyncedToProject()) c.setLength (clipops::sourceSecondsToBeats (c, span, bpm), um()); } };
    useAsProjectBpm.onClick = [this] { auto c = currentClip(); if (c.isValid() && c.getClipBpm() > 0) { um()->beginNewTransaction ("Set project BPM"); session.getProject().setBpm (c.getClipBpm()); } };
    useAsProjectKey.onClick = [this] { auto c = currentClip(); if (c.isValid() && c.getKeyRoot() >= 0) { um()->beginNewTransaction ("Set project key"); session.getProject().setKey (c.getKeyRoot(), c.getKeyMode()); } };

    for (auto* l : { &startValue, &lengthValue, &offsetValue }) { addAndMakeVisible (*l); l->setFont (Theme::mono (11.0f)); clipControls.push_back (l); }

    view.addChangeListener (this);
    session.getProject().getRoot().addListener (this);
    refresh();
}

InspectorPanel::~InspectorPanel() { view.removeChangeListener (this); session.getProject().getRoot().removeListener (this); }

juce::UndoManager* InspectorPanel::um() { return &session.getUndoManager(); }

ClipModel InspectorPanel::currentClip() const
{
    if (view.selectedClips.empty()) return {};
    return ClipModel (session.getProject().findById (ids::CLIP, *view.selectedClips.begin()));
}

void InspectorPanel::refresh()
{
    auto c = currentClip();
    const bool has = c.isValid();
    for (auto* comp : clipControls) comp->setVisible (has);
    if (! has)
    {
        title.setText (view.selectedClips.size() > 1 ? juce::String ((int) view.selectedClips.size()) + " clips selected" : "No clip selected", juce::dontSendNotification);
        auto& p = session.getProject();
        sourceInfo.setText ("Project: " + p.getName() + "\n" + juce::String (p.getBpm(), 2) + " BPM  " + key::name (p.getKeyRoot(), p.getKeyMode()) + "  " + juce::String (p.getTimeSigNumerator()) + "/" + juce::String (p.getTimeSigDenominator())
                            + "\nTracks: " + juce::String (p.tracks().getNumChildren()) + "   Sources: " + juce::String (p.sources().getNumChildren()), juce::dontSendNotification);
        return;
    }
    updating = true;
    const double bpm = session.getProject().getBpm();
    title.setText (view.selectedClips.size() > 1 ? "Clip (" + juce::String ((int) view.selectedClips.size()) + " selected)" : "Clip", juce::dontSendNotification);
    auto src = ProjectModel::findByIdIn (session.getProject().sources(), ids::SOURCE, c.getSourceId());
    juce::String info = "Source: " + src[ids::name].toString() + "\n";
    if ((bool) src.getProperty (ids::analysed, false))
        info << "Analysis: " << juce::String ((double) src[ids::bpm], 1) << " BPM (" << (int) ((double) src[ids::bpmConfidence] * 100) << "%)  " << key::name ((int) src[ids::keyRoot], (int) src[ids::keyMode]) << " (" << (int) ((double) src[ids::keyConfidence] * 100) << "%)\n"
             << juce::String ((double) src[ids::lufs], 1) << " LUFS  peak " << juce::String (juce::Decibels::gainToDecibels ((double) src[ids::peak]), 1) << " dBFS";
    else info << "not analysed yet";
    const double sr = (double) src[ids::sampleRate];
    if (sr > 0) info << "\n" << juce::String ((int) sr) << " Hz  " << (int) src[ids::channels] << " ch  " << formatDurationShort ((double) src[ids::lengthSamples] / sr);
    sourceInfo.setText (info, juce::dontSendNotification);

    nameEditor.setText (c.getName(), false);
    gain.setValue (juce::Decibels::gainToDecibels (c.getGain(), -48.0), juce::dontSendNotification);
    pan.setValue (c.getPan(), juce::dontSendNotification);
    pitch.setValue (c.getPitchSemis(), juce::dontSendNotification);
    cents.setValue (c.getPitchCents(), juce::dontSendNotification);
    formant.setValue (c.getFormant(), juce::dontSendNotification);
    mode.setSelectedId ((int) c.getStretchMode() + 1, juce::dontSendNotification);
    clipBpm.setText (c.getClipBpm() > 0 ? juce::String (c.getClipBpm(), 2) : "-", false);
    sync.setToggleState (c.isSyncedToProject(), juce::dontSendNotification);
    rate.setValue (c.getRate(), juce::dontSendNotification);
    rate.setEnabled (! c.isSyncedToProject());
    keyBox.setSelectedId (c.getKeyRoot() < 0 ? 1 : 2 + c.getKeyMode() * 12 + c.getKeyRoot(), juce::dontSendNotification);
    fadeIn.setValue (c.getFadeIn(), juce::dontSendNotification); fadeOut.setValue (c.getFadeOut(), juce::dontSendNotification);
    fadeInShape.setSelectedId ((int) c.getFadeInShape() + 1, juce::dontSendNotification); fadeOutShape.setSelectedId ((int) c.getFadeOutShape() + 1, juce::dontSendNotification);
    reverse.setToggleState (c.isReversed(), juce::dontSendNotification); loop.setToggleState (c.isLooped(), juce::dontSendNotification); mute.setToggleState (c.isMuted(), juce::dontSendNotification);
    TempoMap tm = TempoMap::fromProject (session.getProject().getRoot());
    startValue.setText (tm.formatBeat (c.getStart()) + "  " + formatTime (tm.beatToTime (c.getStart())), juce::dontSendNotification);
    lengthValue.setText (juce::String (c.getLength(), 2) + " beats  " + formatTime (tm.beatToTime (c.getEnd()) - tm.beatToTime (c.getStart())), juce::dontSendNotification);
    offsetValue.setText (formatTime (c.getOffset()) + "  speed " + juce::String (c.getPlaybackSpeed (bpm), 3) + "x", juce::dontSendNotification);
    formant.setEnabled (c.getStretchMode() == StretchMode::Vocal || c.getStretchMode() == StretchMode::HighQuality);
    updating = false;
}

void InspectorPanel::layoutRows (juce::Rectangle<int>& area, std::initializer_list<std::pair<juce::Label*, juce::Component*>> rows, int rowH)
{
    for (auto& [label, control] : rows)
    {
        auto row = area.removeFromTop (rowH);
        if (label) label->setBounds (row.removeFromLeft (70));
        if (control) control->setBounds (row.reduced (0, 2));
    }
}

void InspectorPanel::paint (juce::Graphics& g)
{
    g.fillAll (colours::panelBg);
    drawPanelHeader (g, getLocalBounds().removeFromTop (22), "Inspector");
    g.setColour (colours::border); g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void InspectorPanel::resized()
{
    auto r = getLocalBounds().withTrimmedTop (22).reduced (8, 4);
    title.setBounds (r.removeFromTop (20));
    sourceInfo.setBounds (r.removeFromTop (58));
    r.removeFromTop (4);
    layoutRows (r, { { &nameLabel, &nameEditor }, { &gainLabel, &gain }, { &panLabel, &pan } });
    r.removeFromTop (6);
    layoutRows (r, { { &modeLabel, &mode }, { &pitchLabel, &pitch }, { &centsLabel, &cents }, { &formantLabel, &formant } });
    r.removeFromTop (6);
    { auto row = r.removeFromTop (24); bpmLabel.setBounds (row.removeFromLeft (70)); halfBpm.setBounds (row.removeFromRight (28).reduced (1)); doubleBpm.setBounds (row.removeFromRight (28).reduced (1)); clipBpm.setBounds (row.reduced (0, 2)); }
    sync.setBounds (r.removeFromTop (22));
    layoutRows (r, { { &rateLabel, &rate } });
    { auto row = r.removeFromTop (24); matchBpm.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (1)); useAsProjectBpm.setBounds (row.reduced (1)); }
    r.removeFromTop (6);
    layoutRows (r, { { &keyLabel, &keyBox } });
    { auto row = r.removeFromTop (24); matchKey.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (1)); useAsProjectKey.setBounds (row.reduced (1)); }
    r.removeFromTop (6);
    { auto row = r.removeFromTop (24); fadeInLabel.setBounds (row.removeFromLeft (70)); fadeInShape.setBounds (row.removeFromRight (90).reduced (1, 2)); fadeIn.setBounds (row.reduced (0, 2)); }
    { auto row = r.removeFromTop (24); fadeOutLabel.setBounds (row.removeFromLeft (70)); fadeOutShape.setBounds (row.removeFromRight (90).reduced (1, 2)); fadeOut.setBounds (row.reduced (0, 2)); }
    r.removeFromTop (6);
    { auto row = r.removeFromTop (22); reverse.setBounds (row.removeFromLeft (row.getWidth() / 3)); loop.setBounds (row.removeFromLeft (row.getWidth() / 2)); mute.setBounds (row); }
    r.removeFromTop (6);
    layoutRows (r, { { &startLabel, &startValue }, { &lengthLabel, &lengthValue }, { &offsetLabel, &offsetValue } }, 20);
}
} // namespace mashup::ui
