#include "TrackHeader.h"
#include "UI/Theme/Theme.h"
#include "AudioEngine/AudioEngine.h"
#include "Clips/ClipOperations.h"
#include "Automation/AutomationCurve.h"
#include "Automation/AutomationRecorder.h"
#include "Mixer/MixerOps.h"
#include "Effects/EffectFactory.h"

namespace mashup::ui
{
TrackHeader::TrackHeader (Session& s, TimelineViewState& v, TrackModel t) : session (s), view (v), track (std::move (t)), trackState (track.getState())
{
    addAndMakeVisible (name);
    name.setEditable (false, true, false);
    name.setFont (Theme::ui (12.0f, true));
    name.onTextChange = [this] { session.getUndoManager().beginNewTransaction ("Rename track"); track.setName (name.getText(), &session.getUndoManager()); };
    for (auto* b : { &mute, &solo, &arm }) { addAndMakeVisible (*b); b->setClickingTogglesState (true); }
    mute.setColour (juce::TextButton::buttonOnColourId, colours::warning.darker (0.2f));
    solo.setColour (juce::TextButton::buttonOnColourId, colours::accentDim);
    arm.setColour (juce::TextButton::buttonOnColourId, colours::record);
    mute.onClick = [this] { session.getUndoManager().beginNewTransaction ("Mute"); track.setMuted (mute.getToggleState(), &session.getUndoManager()); };
    solo.onClick = [this] { session.getUndoManager().beginNewTransaction ("Solo"); track.setSolo (solo.getToggleState(), &session.getUndoManager()); };
    arm.onClick = [this] { session.getUndoManager().beginNewTransaction ("Arm"); track.setArmed (arm.getToggleState(), &session.getUndoManager()); };

    addAndMakeVisible (volume);
    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setRange (-60.0, 12.0, 0.1);
    volume.setSkewFactorFromMidPoint (-6.0);
    volume.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    volume.setTooltip ("Volume (dB)");
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.onValueChange = [this] { track.setVolume (juce::Decibels::decibelsToGain ((float) volume.getValue(), -60.0f), &session.getUndoManager()); };
    addAndMakeVisible (pan);
    pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    pan.setRange (-1.0, 1.0, 0.01);
    pan.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    pan.setDoubleClickReturnValue (true, 0.0);
    pan.setTooltip ("Pan");
    pan.onValueChange = [this] { track.setPan (pan.getValue(), &session.getUndoManager()); };

    addAndMakeVisible (autoButton); autoButton.setClickingTogglesState (true); autoButton.setTooltip ("Show automation lane");
    autoButton.setColour (juce::TextButton::buttonOnColourId, colours::accentDim);
    autoButton.onClick = [this] { session.getUndoManager().beginNewTransaction ("Automation lane"); track.setAutomationShown (autoButton.getToggleState(), &session.getUndoManager()); };
    addAndMakeVisible (autoParam); addAndMakeVisible (autoMode);
    for (int i = 0; i <= (int) AutomationMode::Off; ++i) autoMode.addItem (automationModeName ((AutomationMode) i), i + 1);
    autoMode.onChange = [this] { session.getUndoManager().beginNewTransaction ("Automation mode"); track.setAutomationMode (autoMode.getSelectedId() - 1, &session.getUndoManager()); };
    autoParam.onChange = [this] { const auto id = autoParam.getSelectedIdAsValue().toString(); auto p = autoParam.getItemText (autoParam.getSelectedItemIndex()); session.getUndoManager().beginNewTransaction ("Automation parameter"); track.setAutomationParam (autoParamIds[(size_t) juce::jlimit (0, (int) autoParamIds.size() - 1, autoParam.getSelectedItemIndex())], &session.getUndoManager()); };
    volume.onDragStart = [this] { session.getUndoManager().beginNewTransaction ("Track volume"); session.getAutomationRecorder().touchBegin (track.getId(), "volume"); };
    volume.onDragEnd = [this] { session.getAutomationRecorder().touchEnd (track.getId(), "volume"); };
    pan.onDragStart = [this] { session.getUndoManager().beginNewTransaction ("Track pan"); session.getAutomationRecorder().touchBegin (track.getId(), "pan"); };
    pan.onDragEnd = [this] { session.getAutomationRecorder().touchEnd (track.getId(), "pan"); };
    trackState.addListener (this);
    refresh();
    startTimerHz (25);
}

void TrackHeader::fillAutomationParams()
{
    autoParam.clear (juce::dontSendNotification); autoParamIds.clear();
    auto add = [&] (const juce::String& id, const juce::String& label) { autoParamIds.push_back (id); autoParam.addItem (label, (int) autoParamIds.size()); };
    add ("volume", "Volume"); add ("pan", "Pan"); add ("send0", "Send: Reverb"); add ("send1", "Send: Delay");
    for (auto e : track.effects())
    {
        if (e[ids::type].toString() == "vst3") continue;
        auto params = e.getChildWithName (juce::Identifier ("PARAMS"));
        // list the parameters known to the model (after the first edit) plus the common ones by creating a temporary instance
        if (auto proc = session.getEffectFactory().create (e[ids::type].toString(), {}, 48000.0, 512))
            for (auto* prm : proc->getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) add ("fx:" + e[ids::id].toString() + ":" + rp->paramID, mixerops::effectDisplayName (e) + ": " + rp->getName (24));
    }
    const auto cur = track.getAutomationParam();
    for (size_t i = 0; i < autoParamIds.size(); ++i) if (autoParamIds[i] == cur) autoParam.setSelectedItemIndex ((int) i, juce::dontSendNotification);
}

TrackHeader::~TrackHeader() { trackState.removeListener (this); }

void TrackHeader::refresh()
{
    name.setText (track.getName(), juce::dontSendNotification);
    mute.setToggleState (track.isMuted(), juce::dontSendNotification);
    solo.setToggleState (track.isSolo(), juce::dontSendNotification);
    arm.setToggleState (track.isArmed(), juce::dontSendNotification);
    autoButton.setToggleState (track.isAutomationShown(), juce::dontSendNotification);
    autoMode.setSelectedId (track.getAutomationMode() + 1, juce::dontSendNotification);
    if (track.isAutomationShown()) fillAutomationParams();
    autoParam.setVisible (track.isAutomationShown()); autoMode.setVisible (track.isAutomationShown());
    resized();
    resized();
    volume.setValue (juce::Decibels::gainToDecibels ((float) track.getVolume(), -60.0f), juce::dontSendNotification);
    pan.setValue (track.getPan(), juce::dontSendNotification);
    repaint();
}

void TrackHeader::timerCallback()
{
    auto& e = session.getAudioEngine();
    for (int c = 0; c < 2; ++c) meter[c] = juce::jmax (e.readTrackPeak (track.getId(), c), meter[c] * 0.8f);
    repaint (getWidth() - 10, 0, 10, getHeight());
}

void TrackHeader::paint (juce::Graphics& g)
{
    const bool selected = view.selectedTrack == track.getId();
    g.fillAll (selected ? colours::panelBgAlt.brighter (0.06f) : colours::panelBgAlt);
    g.setColour (track.getColour()); g.fillRect (0, 0, 4, getHeight());
    g.setColour (colours::border); g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    // meter
    auto m = getLocalBounds().removeFromRight (8).reduced (1, 3);
    g.setColour (colours::windowBg); g.fillRect (m);
    for (int c = 0; c < 2; ++c)
    {
        auto bar = m.withWidth (m.getWidth() / 2).withX (m.getX() + c * (m.getWidth() / 2)).reduced (1, 0);
        const float db = juce::Decibels::gainToDecibels (meter[c], -60.0f);
        const int hgt = (int) (bar.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f));
        g.setColour (db > -3.0f ? colours::meterHigh : db > -12.0f ? colours::meterMid : colours::meterLow);
        g.fillRect (bar.removeFromBottom (hgt));
    }
    if (! track.getStemType().isEmpty())
    {
        g.setColour (colours::textDim); g.setFont (Theme::ui (9.0f));
        g.drawText (track.getStemType().toUpperCase(), 8, getHeight() - 14, 80, 12, juce::Justification::centredLeft);
    }
}

void TrackHeader::resized()
{
    auto r = getLocalBounds().reduced (6, 3).withTrimmedRight (8);
    if (track.isAutomationShown())
    {
        auto autoArea = r.removeFromBottom (TrackModel::automationLaneHeight - 4);
        autoParam.setBounds (autoArea.removeFromTop (22).reduced (0, 1));
        autoMode.setBounds (autoArea.removeFromTop (22).reduced (0, 1).removeFromLeft (90));
    }
    const bool tall = track.getHeight() >= 56;
    auto row1 = r.removeFromTop (20);
    name.setBounds (row1.removeFromLeft (juce::jmax (40, row1.getWidth() - 94)));
    for (auto* b : { &mute, &solo, &arm, &autoButton }) { b->setBounds (row1.removeFromLeft (22).reduced (1, 0)); }
    if (tall)
    {
        auto row2 = r.removeFromTop (22);
        pan.setBounds (row2.removeFromRight (24));
        volume.setBounds (row2.reduced (0, 2));
        volume.setVisible (true); pan.setVisible (true);
    }
    else { volume.setVisible (false); pan.setVisible (false); }
}

void TrackHeader::mouseMove (const juce::MouseEvent& e) { const int laneBottom = track.getHeight(); setMouseCursor (std::abs (e.y - laneBottom) < 5 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor); }

void TrackHeader::mouseDown (const juce::MouseEvent& e)
{
    view.selectedTrack = track.getId(); view.sendChangeMessage();
    if (e.mods.isPopupMenu()) { showMenu (e); return; }
    if (std::abs (e.y - track.getHeight()) < 5) { resizing = true; resizeStartHeight = track.getTotalHeight(); session.getUndoManager().beginNewTransaction ("Resize track"); }
}
void TrackHeader::mouseDrag (const juce::MouseEvent& e)
{
    if (resizing) track.setHeight (resizeStartHeight + e.getDistanceFromDragStartY(), &session.getUndoManager());
}
void TrackHeader::mouseUp (const juce::MouseEvent&) { resizing = false; }

void TrackHeader::showMenu (const juce::MouseEvent& e)
{
    juce::PopupMenu m;
    m.addItem (1, "Rename...");
    m.addItem (2, "Duplicate track");
    m.addItem (3, "Delete track");
    m.addSeparator();
    juce::PopupMenu colourMenu;
    for (int i = 0; i < 8; ++i) colourMenu.addColouredItem (100 + i, "Colour " + juce::String (i + 1), colours::trackPalette[i]);
    m.addSubMenu ("Colour", colourMenu);
    m.addItem (4, "Add track below");
    m.addItem (5, "Move up", trackops::indexOf (session.getProject(), track) > 0);
    m.addItem (6, "Move down", trackops::indexOf (session.getProject(), track) < session.getProject().tracks().getNumChildren() - 1);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }), [this] (int r)
    {
        auto& p = session.getProject(); auto* um = &session.getUndoManager();
        if (r == 1) name.showEditor();
        else if (r == 2) trackops::duplicateTrack (p, track);
        else if (r == 3) trackops::removeTrack (p, track);
        else if (r == 4) trackops::addTrack (p, {}, trackops::indexOf (p, track) + 1);
        else if (r == 5) trackops::moveTrack (p, track, trackops::indexOf (p, track) - 1);
        else if (r == 6) trackops::moveTrack (p, track, trackops::indexOf (p, track) + 1);
        else if (r >= 100 && r < 108)
        {
            um->beginNewTransaction ("Track colour");
            track.setColour (colours::trackPalette[r - 100], um);
            for (auto c : track.clips()) ClipModel (c).setColour (colours::trackPalette[r - 100].darker (0.35f), um);
        }
    });
}

// ---------------------------------------------------------------------------------------------------------------
TrackHeaderList::TrackHeaderList (Session& s, TimelineViewState& v) : session (s), view (v)
{
    session.getProject().getRoot().addListener (this);
    view.addChangeListener (this);
    rebuild();
}
TrackHeaderList::~TrackHeaderList() { session.getProject().getRoot().removeListener (this); view.removeChangeListener (this); }

void TrackHeaderList::rebuild()
{
    headers.clear();
    for (const auto& t : session.getProject().tracks())
    {
        auto h = std::make_unique<TrackHeader> (session, view, TrackModel (t));
        addAndMakeVisible (*h);
        headers.push_back (std::move (h));
    }
    resized();
}

int TrackHeaderList::getTotalHeight() const
{
    int y = 0; for (const auto& t : session.getProject().tracks()) y += TrackModel (t).getTotalHeight(); return y;
}

void TrackHeaderList::resized()
{
    int y = -view.verticalScroll;
    for (auto& h : headers) { const int hh = h->getTrack().getTotalHeight(); h->setBounds (0, y, getWidth(), hh); y += hh; }
    repaint();
}

void TrackHeaderList::paint (juce::Graphics& g)
{
    g.fillAll (colours::panelBg);
    g.setColour (colours::border);
    g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
}

void TrackHeaderList::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m; m.addItem (1, "Add audio track");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }), [this] (int r) { if (r == 1) trackops::addTrack (session.getProject(), {}); });
    }
    else if (e.mouseWasClicked()) { view.selectedTrack = {}; view.sendChangeMessage(); }
}
} // namespace mashup::ui
