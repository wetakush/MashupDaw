#include "MixerPanel.h"
#include "UI/Theme/Theme.h"
#include "UI/Plugins/PluginWindows.h"
#include "Mixer/MixerOps.h"
#include "Effects/EffectFactory.h"
#include "Plugins/PluginHost.h"
#include "AudioEngine/AudioEngine.h"
#include "Automation/AutomationRecorder.h"

namespace mashup::ui
{
// ---- EffectChainEditor ---------------------------------------------------------------------------------------------------
EffectChainEditor::Slot::Slot (EffectChainEditor& o, juce::ValueTree n) : owner (o), node (n)
{
    addAndMakeVisible (bypass); addAndMakeVisible (remove);
    bypass.setClickingTogglesState (true); bypass.setToggleState ((bool) node[ids::bypass], juce::dontSendNotification);
    bypass.setColour (juce::TextButton::buttonOnColourId, colours::warning.darker (0.3f));
    bypass.setTooltip ("Bypass");
    bypass.onClick = [this] { mixerops::setBypass (owner.session.getProject(), node, bypass.getToggleState()); };
    remove.onClick = [this] { auto n2 = node; auto& s = owner.session; juce::MessageManager::callAsync ([&s, n2] { mixerops::removeEffect (s.getProject(), n2); }); };
}
void EffectChainEditor::Slot::resized() { auto r = getLocalBounds(); remove.setBounds (r.removeFromRight (18).reduced (1)); bypass.setBounds (r.removeFromRight (18).reduced (1)); }
void EffectChainEditor::Slot::paint (juce::Graphics& g)
{
    g.setColour ((bool) node[ids::bypass] ? colours::headerBg : colours::accentDim.withAlpha (0.55f));
    g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (1), 3.0f);
    g.setColour (colours::text); g.setFont (Theme::ui (11.0f));
    g.drawText (mixerops::effectDisplayName (node), getLocalBounds().reduced (5, 0).withTrimmedRight (38), juce::Justification::centredLeft, true);
}
void EffectChainEditor::Slot::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m; m.addItem (1, "Edit..."); m.addItem (2, "Move up"); m.addItem (3, "Move down"); m.addItem (4, "Remove");
        m.showMenuAsync ({}, [this] (int r)
        {
            auto& p = owner.session.getProject(); const int idx = node.getParent().indexOf (node);
            if (r == 1) owner.windows.openEditor (node); else if (r == 2 && idx > 0) mixerops::moveEffect (p, node, idx - 1); else if (r == 3) mixerops::moveEffect (p, node, idx + 1); else if (r == 4) mixerops::removeEffect (p, node);
        });
    }
    else if (e.x < getWidth() - 38) owner.windows.openEditor (node);
}

EffectChainEditor::EffectChainEditor (Session& s, PluginWindows& w, juce::ValueTree l) : session (s), windows (w)
{
    addAndMakeVisible (addButton);
    addButton.onClick = [this] { showAddMenu(); };
    setEffectsList (l);
}
EffectChainEditor::~EffectChainEditor() { if (list.isValid()) list.removeListener (this); }
void EffectChainEditor::setEffectsList (juce::ValueTree l)
{
    if (list.isValid()) list.removeListener (this);
    list = l; if (list.isValid()) list.addListener (this);
    rebuild();
}
void EffectChainEditor::rebuild()
{
    slots.clear();
    for (auto e : list) { auto slot = std::make_unique<Slot> (*this, e); addAndMakeVisible (*slot); slots.push_back (std::move (slot)); }
    resized();
}
void EffectChainEditor::resized()
{
    auto r = getLocalBounds();
    addButton.setBounds (r.removeFromBottom (20).reduced (1));
    for (auto& s : slots) s->setBounds (r.removeFromTop (20));
}
void EffectChainEditor::paint (juce::Graphics& g) { g.setColour (colours::windowBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f); }

void EffectChainEditor::showAddMenu()
{
    juce::PopupMenu m;
    auto types = session.getEffectFactory().getBuiltInTypes();
    std::map<juce::String, juce::PopupMenu> cats;
    static const char* pretty[][2] = { { "eq", "Parametric EQ" }, { "filter", "Filter" }, { "saturation", "Saturation" }, { "distortion", "Distortion" }, { "widener", "Stereo Widener" }, { "compressor", "Compressor" }, { "limiter", "Limiter" }, { "gate", "Gate" }, { "delay", "Delay" }, { "reverb", "Reverb" }, { "chorus", "Chorus" }, { "flanger", "Flanger" }, { "phaser", "Phaser" } };
    auto prettyName = [&] (const juce::String& t) { for (auto& p : pretty) if (t == p[0]) return juce::String (p[1]); return t; };
    std::vector<juce::String> typeList;
    for (auto& e : types) { cats[e.category].addItem ((int) typeList.size() + 1, prettyName (e.type)); typeList.push_back (e.type); }
    for (auto& [cat, sub] : cats) m.addSubMenu (cat, sub);
    juce::PopupMenu vst; auto plugs = session.getPluginHost().getKnownPlugins().getTypes();
    for (int i = 0; i < plugs.size(); ++i) vst.addItem (1000 + i, plugs[i].name + "  (" + plugs[i].manufacturerName + ")");
    if (plugs.isEmpty()) vst.addItem (999, "No VST3 plugins scanned - open Plugin manager", false);
    m.addSubMenu ("VST3", vst);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (addButton), [this, typeList, plugs, prettyName] (int r)
    {
        if (r >= 1 && r <= (int) typeList.size()) mixerops::addEffect (session.getProject(), list, typeList[(size_t) r - 1], {}, prettyName (typeList[(size_t) r - 1]));
        else if (r >= 1000 && r - 1000 < plugs.size()) { auto d = plugs[r - 1000]; mixerops::addEffect (session.getProject(), list, "vst3", d.createIdentifierString(), d.name); }
    });
}

// ---- ChannelStrip -------------------------------------------------------------------------------------------------------------
ChannelStrip::ChannelStrip (Session& s, TimelineViewState& v, PluginWindows& w, juce::ValueTree n, Kind k) : session (s), view (v), node (n), kind (k)
{
    addAndMakeVisible (name); name.setFont (Theme::ui (11.0f, true)); name.setJustificationType (juce::Justification::centred);
    name.setEditable (false, kind == Kind::Track, false);
    name.onTextChange = [this] { um()->beginNewTransaction ("Rename"); node.setProperty (ids::name, name.getText(), um()); };
    addAndMakeVisible (fader); fader.setSliderStyle (juce::Slider::LinearVertical); fader.setRange (-60.0, 12.0, 0.1); fader.setSkewFactorFromMidPoint (-6.0); fader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0); fader.setDoubleClickReturnValue (true, 0.0);
    fader.onDragStart = [this] { um()->beginNewTransaction ("Volume"); if (kind == Kind::Track) session.getAutomationRecorder().touchBegin (node[ids::id].toString(), "volume"); };
    fader.onDragEnd = [this] { if (kind == Kind::Track) session.getAutomationRecorder().touchEnd (node[ids::id].toString(), "volume"); };
    fader.onValueChange = [this] { if (updating) return; node.setProperty (ids::volume, juce::Decibels::decibelsToGain ((float) fader.getValue(), -60.0f), um()); faderValue.setText (juce::String (fader.getValue(), 1), juce::dontSendNotification); };
    addAndMakeVisible (faderValue); faderValue.setFont (Theme::mono (10.0f)); faderValue.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (pan); pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); pan.setRange (-1.0, 1.0, 0.01); pan.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0); pan.setDoubleClickReturnValue (true, 0.0); pan.setTooltip ("Pan");
    pan.onDragStart = [this] { um()->beginNewTransaction ("Pan"); if (kind == Kind::Track) session.getAutomationRecorder().touchBegin (node[ids::id].toString(), "pan"); };
    pan.onDragEnd = [this] { if (kind == Kind::Track) session.getAutomationRecorder().touchEnd (node[ids::id].toString(), "pan"); }; pan.onValueChange = [this] { if (! updating) node.setProperty (ids::pan, pan.getValue(), um()); };
    addAndMakeVisible (inputGain); inputGain.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); inputGain.setRange (-24.0, 24.0, 0.1); inputGain.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0); inputGain.setDoubleClickReturnValue (true, 0.0); inputGain.setTooltip ("Input gain (dB)");
    inputGain.onDragStart = [this] { um()->beginNewTransaction ("Input gain"); }; inputGain.onValueChange = [this] { if (! updating) node.setProperty (ids::inputGain, juce::Decibels::decibelsToGain ((float) inputGain.getValue()), um()); };
    for (auto* sl : { &sendA, &sendB }) { addAndMakeVisible (*sl); sl->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); sl->setRange (0.0, 1.0, 0.01); sl->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0); sl->setDoubleClickReturnValue (true, 0.0); }
    sendA.setTooltip ("Reverb send"); sendB.setTooltip ("Delay send");
    sendA.onDragStart = [this] { um()->beginNewTransaction ("Send"); }; sendA.onValueChange = [this] { if (! updating) mixerops::setSend (session.getProject(), node, 0, sendA.getValue()); };
    sendB.onDragStart = [this] { um()->beginNewTransaction ("Send"); }; sendB.onValueChange = [this] { if (! updating) mixerops::setSend (session.getProject(), node, 1, sendB.getValue()); };
    for (auto* b : { &mute, &solo, &phase, &arm }) { addAndMakeVisible (*b); b->setClickingTogglesState (true); }
    mute.setColour (juce::TextButton::buttonOnColourId, colours::warning.darker (0.2f)); solo.setColour (juce::TextButton::buttonOnColourId, colours::accentDim); arm.setColour (juce::TextButton::buttonOnColourId, colours::record); phase.setColour (juce::TextButton::buttonOnColourId, colours::accentDim);
    mute.onClick = [this] { um()->beginNewTransaction ("Mute"); node.setProperty (ids::mute, mute.getToggleState(), um()); };
    solo.onClick = [this] { um()->beginNewTransaction ("Solo"); node.setProperty (ids::solo, solo.getToggleState(), um()); };
    phase.onClick = [this] { um()->beginNewTransaction ("Phase"); node.setProperty (ids::phaseInvert, phase.getToggleState(), um()); };
    arm.onClick = [this] { um()->beginNewTransaction ("Arm"); node.setProperty (ids::arm, arm.getToggleState(), um()); };
    addAndMakeVisible (lufsLabel); lufsLabel.setFont (Theme::mono (9.5f)); lufsLabel.setJustificationType (juce::Justification::centredLeft); lufsLabel.setColour (juce::Label::textColourId, colours::textDim);
    chain = std::make_unique<EffectChainEditor> (session, w, node.getChildWithName (ids::EFFECTS));
    addAndMakeVisible (*chain);
    const bool track = kind == Kind::Track;
    for (auto* c : std::initializer_list<juce::Component*> { &sendA, &sendB, &inputGain, &phase, &arm, &solo }) c->setVisible (track);
    mute.setVisible (kind != Kind::Master);
    lufsLabel.setVisible (kind == Kind::Master);
    node.addListener (this);
    refresh();
    startTimerHz (25);
}
ChannelStrip::~ChannelStrip() { node.removeListener (this); }
juce::UndoManager* ChannelStrip::um() { return &session.getUndoManager(); }

void ChannelStrip::refresh()
{
    updating = true;
    name.setText (node.getProperty (ids::name, kind == Kind::Master ? "Master" : "Bus"), juce::dontSendNotification);
    fader.setValue (juce::Decibels::gainToDecibels ((double) node.getProperty (ids::volume, 1.0), -60.0), juce::dontSendNotification);
    faderValue.setText (juce::String (fader.getValue(), 1), juce::dontSendNotification);
    pan.setValue ((double) node.getProperty (ids::pan, 0.0), juce::dontSendNotification);
    inputGain.setValue (juce::Decibels::gainToDecibels ((double) node.getProperty (ids::inputGain, 1.0)), juce::dontSendNotification);
    sendA.setValue (mixerops::getSend (node, 0), juce::dontSendNotification); sendB.setValue (mixerops::getSend (node, 1), juce::dontSendNotification);
    mute.setToggleState ((bool) node[ids::mute], juce::dontSendNotification); solo.setToggleState ((bool) node[ids::solo], juce::dontSendNotification);
    phase.setToggleState ((bool) node[ids::phaseInvert], juce::dontSendNotification); arm.setToggleState ((bool) node[ids::arm], juce::dontSendNotification);
    updating = false; repaint();
}

void ChannelStrip::timerCallback()
{
    auto& e = session.getAudioEngine();
    for (int c = 0; c < 2; ++c)
    {
        const float pk = kind == Kind::Track ? e.readTrackPeak (node[ids::id].toString(), c) : kind == Kind::Master ? e.readMasterPeak (c) : 0.0f;
        meter[c] = juce::jmax (pk, meter[c] * 0.82f);
    }
    if (kind == Kind::Master)
        if (auto* lm = e.getMasterLoudness())
            lufsLabel.setText ("M  " + juce::String (lm->momentary.load(), 1) + " LUFS\nS  " + juce::String (lm->shortTerm.load(), 1) + "\nI  " + juce::String (lm->integrated.load(), 1) + "\nTP " + juce::String (lm->peakHoldDb.load(), 1) + " dB", juce::dontSendNotification);
    repaint (getWidth() - 14, 0, 14, getHeight());
}

void ChannelStrip::paint (juce::Graphics& g)
{
    const bool selected = kind == Kind::Track && view.selectedTrack == node[ids::id].toString();
    g.fillAll (selected ? colours::panelBgAlt.brighter (0.06f) : colours::panelBgAlt);
    g.setColour (colours::border); g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
    juce::Colour col = kind == Kind::Track ? juce::Colour::fromString (node.getProperty (ids::color, "ff1ec8aa").toString()) : kind == Kind::Master ? colours::accent : colours::textDim;
    g.setColour (col); g.fillRect (0, 0, getWidth(), 3);
    // meter next to the fader
    auto m = fader.getBounds().withX (fader.getRight() - 2).withWidth (10);
    g.setColour (colours::windowBg); g.fillRect (m);
    for (int c = 0; c < 2; ++c)
    {
        auto bar = m.withWidth (m.getWidth() / 2).withX (m.getX() + c * (m.getWidth() / 2)).reduced (1, 0);
        const float db = juce::Decibels::gainToDecibels (meter[c], -60.0f);
        const int hgt = (int) (bar.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 72.0f));
        g.setColour (db > 0.0f ? colours::meterHigh : db > -12.0f ? colours::meterMid : colours::meterLow);
        g.fillRect (bar.removeFromBottom (hgt));
    }
    g.setColour (colours::textDim); g.setFont (Theme::ui (9.0f));
    if (kind == Kind::Track) { g.drawText ("REV", sendA.getBounds().translated (0, -10).withHeight (10), juce::Justification::centred); g.drawText ("DLY", sendB.getBounds().translated (0, -10).withHeight (10), juce::Justification::centred); g.drawText ("IN", inputGain.getBounds().translated (0, -10).withHeight (10), juce::Justification::centred); }
    g.drawText ("PAN", pan.getBounds().translated (0, -10).withHeight (10), juce::Justification::centred);
}

void ChannelStrip::mouseDown (const juce::MouseEvent&) { if (kind == Kind::Track) { view.selectedTrack = node[ids::id].toString(); view.sendChangeMessage(); } }

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced (3, 4);
    name.setBounds (r.removeFromTop (18));
    chain->setBounds (r.removeFromTop (juce::jmax (44, juce::jmin (120, r.getHeight() / 3))));
    r.removeFromTop (4);
    if (kind == Kind::Track)
    {
        auto knobs = r.removeFromTop (34).withTrimmedTop (10);
        const int kw = knobs.getWidth() / 3;
        inputGain.setBounds (knobs.removeFromLeft (kw)); sendA.setBounds (knobs.removeFromLeft (kw)); sendB.setBounds (knobs);
    }
    { auto row = r.removeFromTop (34).withTrimmedTop (10); pan.setBounds (row.removeFromLeft (row.getWidth() / 2)); }
    if (kind == Kind::Master) lufsLabel.setBounds (r.removeFromTop (44));
    auto buttons = r.removeFromBottom (20);
    const int bw = buttons.getWidth() / (kind == Kind::Track ? 4 : 1);
    if (kind == Kind::Track) { mute.setBounds (buttons.removeFromLeft (bw).reduced (1)); solo.setBounds (buttons.removeFromLeft (bw).reduced (1)); arm.setBounds (buttons.removeFromLeft (bw).reduced (1)); phase.setBounds (buttons.reduced (1)); }
    else if (kind == Kind::Bus) mute.setBounds (buttons.reduced (1));
    faderValue.setBounds (r.removeFromBottom (14));
    fader.setBounds (r.withTrimmedRight (10));
}

// ---- MixerPanel ----------------------------------------------------------------------------------------------------------------
MixerPanel::MixerPanel (Session& s, TimelineViewState& v) : session (s), view (v)
{
    windows = std::make_unique<PluginWindows> (session);
    addAndMakeVisible (viewport); viewport.setViewedComponent (&stripHolder, false); viewport.setScrollBarsShown (false, true);
    session.getProject().getRoot().addListener (this);
    view.addChangeListener (this);
    rebuild();
}
MixerPanel::~MixerPanel() { session.getProject().getRoot().removeListener (this); view.removeChangeListener (this); }

void MixerPanel::rebuild()
{
    strips.clear();
    auto& p = session.getProject();
    for (auto t : p.tracks()) { auto st = std::make_unique<ChannelStrip> (session, view, *windows, t, ChannelStrip::Kind::Track); stripHolder.addAndMakeVisible (*st); strips.push_back (std::move (st)); }
    for (auto b : p.buses()) { auto st = std::make_unique<ChannelStrip> (session, view, *windows, b, ChannelStrip::Kind::Bus); stripHolder.addAndMakeVisible (*st); strips.push_back (std::move (st)); }
    master = std::make_unique<ChannelStrip> (session, view, *windows, p.master(), ChannelStrip::Kind::Master);
    addAndMakeVisible (*master);
    resized();
}
void MixerPanel::paint (juce::Graphics& g) { g.fillAll (colours::panelBg); }
void MixerPanel::resized()
{
    auto r = getLocalBounds();
    master->setBounds (r.removeFromRight (110));
    viewport.setBounds (r);
    const int w = 96, h = r.getHeight() - (viewport.getHorizontalScrollBar().isVisible() ? 12 : 0);
    stripHolder.setSize (juce::jmax (r.getWidth(), (int) strips.size() * w), juce::jmax (100, h));
    int x = 0; for (auto& s : strips) { s->setBounds (x, 0, w, stripHolder.getHeight()); x += w; }
}
} // namespace mashup::ui
