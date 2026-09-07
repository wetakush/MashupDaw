#include "TransportBar.h"
#include "UI/Theme/Theme.h"
#include "AudioEngine/AudioEngine.h"
#include "Core/MusicalKey.h"
#include "Core/MusicalTime.h"
#include "Timeline/TempoMap.h"

namespace mashup::ui
{
TransportBar::TransportBar (Session& s) : session (s)
{
    auto& engine = session.getAudioEngine();
    auto& transport = engine.getTransport();

    for (auto* b : { &goStart, &rewind, &stopButton, &playButton, &recordButton, &loopButton, &metronomeButton, &bypassFxButton })
        addAndMakeVisible (*b);
    goStart.setTooltip ("Return to start (Home)");
    goStart.onClick = [&] { engine.locateSeconds (0.0); };
    rewind.onClick = [&] { engine.locateSeconds (juce::jmax (0.0, engine.getPositionSeconds() - 5.0)); };
    stopButton.onClick = [&] { if (! transport.isPlaying()) engine.locateSeconds (0.0); transport.stop(); };
    playButton.onClick = [&] { transport.togglePlay(); };
    recordButton.setColour (juce::TextButton::buttonOnColourId, colours::record);
    recordButton.setClickingTogglesState (true);
    recordButton.onClick = [&] { transport.setRecording (recordButton.getToggleState()); if (recordButton.getToggleState() && ! transport.isPlaying()) transport.play(); };
    loopButton.setClickingTogglesState (true);
    loopButton.onClick = [&] { auto& p = session.getProject(); p.setLoop (p.getLoopStart(), p.getLoopEnd(), loopButton.getToggleState()); };
    metronomeButton.setClickingTogglesState (true);
    metronomeButton.onClick = [&] { transport.metronome.store (metronomeButton.getToggleState()); };
    bypassFxButton.setClickingTogglesState (true);
    bypassFxButton.setTooltip ("Bypass all effects (A/B)");
    bypassFxButton.onClick = [&] { engine.setBypassAllEffects (bypassFxButton.getToggleState()); };

    for (auto* l : { &positionBars, &positionTime, &bpmLabel, &keyLabel, &timeSigLabel, &cpuLabel })
    {
        addAndMakeVisible (*l);
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::backgroundColourId, colours::windowBg);
        l->setColour (juce::Label::outlineColourId, colours::separator);
    }
    positionBars.setFont (Theme::mono (18.0f));
    positionTime.setFont (Theme::mono (14.0f));
    bpmLabel.setFont (Theme::mono (16.0f));
    keyLabel.setFont (Theme::mono (16.0f));
    timeSigLabel.setFont (Theme::mono (14.0f));
    cpuLabel.setFont (Theme::mono (11.0f));
    cpuLabel.setColour (juce::Label::textColourId, colours::textDim);
    bpmLabel.setTooltip ("Project tempo - double-click to edit");
    keyLabel.setTooltip ("Project key - double-click to edit");
    timeSigLabel.setTooltip ("Time signature - double-click to edit");
    bpmLabel.setInterceptsMouseClicks (true, false); keyLabel.setInterceptsMouseClicks (true, false); timeSigLabel.setInterceptsMouseClicks (true, false);
    bpmLabel.setEditable (false, true, false);
    keyLabel.setEditable (false, true, false);
    timeSigLabel.setEditable (false, true, false);
    bpmLabel.onEditorShow = [this] { if (auto* e = bpmLabel.getCurrentTextEditor()) e->setText (juce::String (session.getProject().getBpm(), 2), false); };
    bpmLabel.onTextChange = [this] { editBpm(); };
    keyLabel.onTextChange = [this] { editKey(); };
    timeSigLabel.onTextChange = [this] { editTimeSig(); };
    // mouse-wheel/drag on bpm: handled via mouse drag in the label parent -> simple: arrow keys inside editor

    session.getProject().getRoot().addListener (this);
    refreshFromModel();
    startTimerHz (30);
}

TransportBar::~TransportBar() { session.getProject().getRoot().removeListener (this); }

void TransportBar::valueTreePropertyChanged (juce::ValueTree& v, const juce::Identifier&) { if (v.hasType (ids::PROJECT)) refreshFromModel(); }

void TransportBar::refreshFromModel()
{
    auto& p = session.getProject();
    bpmLabel.setText (juce::String (p.getBpm(), 2) + " BPM", juce::dontSendNotification);
    keyLabel.setText (key::name (p.getKeyRoot(), p.getKeyMode()) + "  " + key::camelot (p.getKeyRoot(), p.getKeyMode()), juce::dontSendNotification);
    timeSigLabel.setText (juce::String (p.getTimeSigNumerator()) + "/" + juce::String (p.getTimeSigDenominator()), juce::dontSendNotification);
    loopButton.setToggleState (p.isLoopEnabled(), juce::dontSendNotification);
}

void TransportBar::editBpm()
{
    const double v = bpmLabel.getText().retainCharacters ("0123456789.").getDoubleValue();
    if (v >= 20.0 && v <= 400.0) { session.getUndoManager().beginNewTransaction ("Change BPM"); session.getProject().setBpm (v); }
    refreshFromModel();
}
void TransportBar::editKey()
{
    int r, m;
    if (key::parse (keyLabel.getText().upToFirstOccurrenceOf ("  ", false, false), r, m)) { session.getUndoManager().beginNewTransaction ("Change key"); session.getProject().setKey (r, m); }
    refreshFromModel();
}
void TransportBar::editTimeSig()
{
    auto t = timeSigLabel.getText();
    const int n = t.upToFirstOccurrenceOf ("/", false, false).getIntValue(), d = t.fromFirstOccurrenceOf ("/", false, false).getIntValue();
    if (n >= 1 && n <= 32 && (d == 2 || d == 4 || d == 8 || d == 16)) { session.getUndoManager().beginNewTransaction ("Change time signature"); session.getProject().setTimeSignature (n, d); }
    refreshFromModel();
}

void TransportBar::timerCallback()
{
    auto& engine = session.getAudioEngine();
    auto& t = engine.getTransport();
    const double secs = engine.getPositionSeconds();
    TempoMap tm = TempoMap::fromProject (session.getProject().getRoot());
    positionBars.setText (tm.formatBeat (tm.timeToBeat (secs)), juce::dontSendNotification);
    positionTime.setText (formatTime (secs), juce::dontSendNotification);
    playButton.setButtonText (t.isPlaying() ? "Pause" : "Play");
    playButton.setColour (juce::TextButton::buttonColourId, t.isPlaying() ? colours::accentDim : colours::headerBg);
    recordButton.setToggleState (t.isRecording(), juce::dontSendNotification);
    cpuLabel.setText ("CPU " + juce::String ((int) (engine.getCpuLoad() * 100.0)) + "%  " + juce::String ((int) engine.getSampleRate()) + "Hz/" + juce::String (engine.getBlockSize()), juce::dontSendNotification);
    const float decay = 0.85f;
    meterL = juce::jmax (engine.readMasterPeak (0), meterL * decay);
    meterR = juce::jmax (engine.readMasterPeak (1), meterR * decay);
    repaint (meterArea);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (colours::panelBg);
    g.setColour (colours::border);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    // master meter
    auto m = meterArea.reduced (0, 6);
    g.setColour (colours::windowBg); g.fillRect (m);
    auto drawBar = [&] (juce::Rectangle<int> r, float level)
    {
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        auto fill = r.withWidth ((int) (r.getWidth() * frac));
        g.setColour (colours::meterLow); g.fillRect (fill.getIntersection (r.withWidth (r.getWidth() * 3 / 4)));
        g.setColour (colours::meterMid); g.fillRect (fill.getIntersection (r.withX (r.getX() + r.getWidth() * 3 / 4).withWidth (r.getWidth() / 6)));
        g.setColour (colours::meterHigh); g.fillRect (fill.getIntersection (r.withX (r.getX() + r.getWidth() * 11 / 12)));
    };
    drawBar (m.removeFromTop (m.getHeight() / 2).reduced (0, 1), meterL);
    drawBar (m.reduced (0, 1), meterR);
    g.setColour (colours::separator); g.drawRect (meterArea.reduced (0, 6));
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (8, 10);
    const int h = r.getHeight();
    auto place = [&] (juce::Component& c, int w) { c.setBounds (r.removeFromLeft (w)); r.removeFromLeft (4); };
    place (goStart, 36); place (rewind, 36); place (stopButton, 54); place (playButton, 64); place (recordButton, 46); place (loopButton, 50);
    r.removeFromLeft (12);
    place (positionBars, 130); place (positionTime, 110);
    r.removeFromLeft (12);
    place (bpmLabel, 120); place (keyLabel, 110); place (timeSigLabel, 56);
    r.removeFromLeft (12);
    place (metronomeButton, 50); place (bypassFxButton, 60);
    cpuLabel.setBounds (r.removeFromRight (150));
    meterArea = r.removeFromRight (160).withHeight (h);
}
} // namespace mashup::ui
