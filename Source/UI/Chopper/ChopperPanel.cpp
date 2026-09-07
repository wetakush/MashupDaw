#include "ChopperPanel.h"
#include "UI/Theme/Theme.h"
#include "Tracks/TrackModel.h"
#include "Clips/ClipOperations.h"
#include "Slicing/Slicer.h"
#include "AudioEngine/AudioEngine.h"
#include "Import/SourceLibrary.h"
#include "Waveform/WaveformCacheManager.h"
#include "Waveform/WaveformRenderer.h"
#include "Core/MusicalKey.h"

namespace mashup::ui
{
ChopperPanel::ChopperPanel (Session& s, TimelineViewState& v) : session (s), view (v)
{
    addAndMakeVisible (title); title.setFont (Theme::ui (13.0f, true));
    addAndMakeVisible (hint); hint.setFont (Theme::ui (11.0f)); hint.setColour (juce::Label::textColourId, colours::textDim);
    hint.setText ("Click a pad to audition. Drag a pad onto another to swap them on the timeline. Keys 1-0 / Q-P and MIDI notes from C1 trigger pads. Right-click a pad for reverse / pitch / gate / stutter / mute.", juce::dontSendNotification);
    for (auto* b : { &sliceTransients, &slice8, &slice16, &sliceBeat, &reverseAll, &shuffle, &restore, &followButton, &midiButton }) addAndMakeVisible (*b);
    sliceTransients.onClick = [this] { sliceSelected (0); }; slice8.onClick = [this] { sliceSelected (1); }; slice16.onClick = [this] { sliceSelected (2); }; sliceBeat.onClick = [this] { sliceSelected (3); };
    reverseAll.onClick = [this] { auto sl = slices(); auto& p = session.getProject(); p.getUndoManager().beginNewTransaction ("Reverse slices"); for (auto& c : sl) { const bool r = ! c.isReversed(); c.setReversed (r, &p.getUndoManager()); c.getState().setProperty (ids::sourceEnd, c.getOffset() + clipops::beatsToSourceSeconds (c, c.getLength(), p.getBpm()), &p.getUndoManager()); } };
    shuffle.onClick = [this]
    {
        auto sl = slices(); if (sl.size() < 2) return;
        auto& p = session.getProject(); p.getUndoManager().beginNewTransaction ("Shuffle slices");
        std::vector<double> starts; for (auto& c : sl) starts.push_back (c.getStart());
        juce::Random rng; for (size_t i = starts.size() - 1; i > 0; --i) std::swap (starts[i], starts[(size_t) rng.nextInt ((int) i + 1)]);
        for (size_t i = 0; i < sl.size(); ++i) sl[i].setStart (starts[i], &p.getUndoManager());
    };
    restore.onClick = [this]
    {
        auto sl = slices(); if (sl.empty()) return;
        auto& p = session.getProject(); p.getUndoManager().beginNewTransaction ("Restore slice order");
        // order by source offset, lay out back to back from the earliest start
        std::sort (sl.begin(), sl.end(), [] (const ClipModel& a, const ClipModel& b) { return a.getOffset() < b.getOffset(); });
        double pos = 1e18; for (auto& c : sl) pos = juce::jmin (pos, c.getStart());
        for (auto& c : sl) { c.setStart (pos, &p.getUndoManager()); pos += c.getLength(); }
    };
    followButton.setClickingTogglesState (true); followButton.setToggleState (true, juce::dontSendNotification);
    followButton.onClick = [this] { following = followButton.getToggleState(); if (following) refreshFromSelection(); };
    midiButton.setClickingTogglesState (true);
    midiButton.onClick = [this]
    {
        auto& dm = session.getAudioEngine().getDeviceManager();
        for (auto& d : juce::MidiInput::getAvailableDevices())
        {
            dm.setMidiInputDeviceEnabled (d.identifier, midiButton.getToggleState());
            if (midiButton.getToggleState()) dm.addMidiInputDeviceCallback (d.identifier, this); else dm.removeMidiInputDeviceCallback (d.identifier, this);
        }
    };
    setWantsKeyboardFocus (true);
    view.addChangeListener (this);
    session.getProject().getRoot().addListener (this);
    startTimerHz (20);
    refreshFromSelection();
}

ChopperPanel::~ChopperPanel()
{
    view.removeChangeListener (this); session.getProject().getRoot().removeListener (this);
    auto& dm = session.getAudioEngine().getDeviceManager();
    for (auto& d : juce::MidiInput::getAvailableDevices()) dm.removeMidiInputDeviceCallback (d.identifier, this);
}

void ChopperPanel::openWithSelection() { following = true; followButton.setToggleState (true, juce::dontSendNotification); refreshFromSelection(); grabKeyboardFocus(); }

std::vector<ClipModel> ChopperPanel::slices() const
{
    std::vector<ClipModel> out;
    for (auto& id : sliceIds) { auto c = ClipModel (session.getProject().findById (ids::CLIP, id)); if (c.isValid()) out.push_back (c); }
    return out;
}

void ChopperPanel::refreshFromSelection()
{
    // slices = selected clips; if a single clip is selected, use every clip on its track that touches it end-to-end
    sliceIds.clear(); trackId.clear();
    auto& p = session.getProject();
    std::vector<ClipModel> sel;
    for (auto& id : view.selectedClips) { auto c = ClipModel (p.findById (ids::CLIP, id)); if (c.isValid()) sel.push_back (c); }
    if (sel.empty()) { rebuildPads(); return; }
    TrackModel track (sel.front().getTrackState()); trackId = track.getId();
    if (sel.size() == 1)
    {
        // gather contiguous chain around the clip
        std::vector<ClipModel> all; for (auto c : track.clips()) all.emplace_back (c);
        std::sort (all.begin(), all.end(), [] (const ClipModel& a, const ClipModel& b) { return a.getStart() < b.getStart(); });
        int idx = -1; for (size_t i = 0; i < all.size(); ++i) if (all[i].getId() == sel[0].getId()) idx = (int) i;
        int lo = idx, hi = idx;
        while (lo > 0 && std::abs (all[(size_t) lo - 1].getEnd() - all[(size_t) lo].getStart()) < 1e-3 && all[(size_t) lo - 1].getSourceId() == sel[0].getSourceId()) --lo;
        while (hi + 1 < (int) all.size() && std::abs (all[(size_t) hi].getEnd() - all[(size_t) hi + 1].getStart()) < 1e-3 && all[(size_t) hi + 1].getSourceId() == sel[0].getSourceId()) ++hi;
        for (int i = lo; i <= hi; ++i) sliceIds.push_back (all[(size_t) i].getId());
    }
    else
    {
        std::sort (sel.begin(), sel.end(), [] (const ClipModel& a, const ClipModel& b) { return a.getStart() < b.getStart(); });
        for (auto& c : sel) sliceIds.push_back (c.getId());
    }
    rebuildPads();
}

void ChopperPanel::rebuildPads()
{
    // keep ids valid, order by timeline position
    auto sl = slices();
    std::sort (sl.begin(), sl.end(), [] (const ClipModel& a, const ClipModel& b) { return a.getStart() < b.getStart(); });
    sliceIds.clear(); for (auto& c : sl) sliceIds.push_back (c.getId());
    pads.clear();
    for (size_t i = 0; i < sliceIds.size(); ++i) { auto pad = std::make_unique<Pad> (*this, (int) i); addAndMakeVisible (*pad); pads.push_back (std::move (pad)); }
    title.setText (sl.empty() ? "Vocal Chopper - select a clip" : "Vocal Chopper: " + juce::String ((int) sl.size()) + " slices on " + TrackModel (session.getProject().findById (ids::TRACK, trackId)).getName(), juce::dontSendNotification);
    resized(); repaint();
}

void ChopperPanel::sliceSelected (int mode)
{
    auto& p = session.getProject();
    std::vector<ClipModel> sel;
    for (auto& id : view.selectedClips) { auto c = ClipModel (p.findById (ids::CLIP, id)); if (c.isValid()) sel.push_back (c); }
    if (sel.empty()) return;
    std::vector<ClipModel> result;
    for (auto& c : sel)
    {
        std::vector<double> beats;
        if (mode == 0) beats = slicer::atTransients (p, c, 0.125); else beats = slicer::byDivision (c, mode == 1 ? 0.5 : mode == 2 ? 0.25 : 1.0, p.getBpm());
        auto parts = slicer::slice (p, c, beats);
        result.insert (result.end(), parts.begin(), parts.end());
    }
    view.selectedClips.clear(); for (auto& c : result) view.selectedClips.insert (c.getId()); view.sendChangeMessage();
}

void ChopperPanel::trigger (int pad)
{
    if (pad < 0 || pad >= (int) sliceIds.size()) return;
    auto c = ClipModel (session.getProject().findById (ids::CLIP, sliceIds[(size_t) pad]));
    if (! c.isValid()) return;
    auto src = session.getSourceLibrary().get (c.getSourceId()); if (! src) return;
    const double bpm = session.getProject().getBpm();
    const double span = clipops::beatsToSourceSeconds (c, c.getLength(), bpm);
    double start = c.getOffset(), end = start + span;
    if (c.isReversed()) { end = (double) c.getState().getProperty (ids::sourceEnd, 0.0); start = end - span; }
    session.getAudioEngine().startAudition (src, start, c.getGain(), end, c.isReversed(), c.getPlaybackSpeed (bpm) * (c.getStretchMode() == StretchMode::Repitch ? 1.0 : c.getPitchScale()));
    lastTriggered = pad;
    for (auto& pd : pads) pd->hot = pd->idx == pad;
    repaint();
}

void ChopperPanel::swapPads (int a, int b)
{
    if (a == b || a < 0 || b < 0 || a >= (int) sliceIds.size() || b >= (int) sliceIds.size()) return;
    auto& p = session.getProject();
    auto ca = ClipModel (p.findById (ids::CLIP, sliceIds[(size_t) a])), cb = ClipModel (p.findById (ids::CLIP, sliceIds[(size_t) b]));
    if (! ca.isValid() || ! cb.isValid()) return;
    p.getUndoManager().beginNewTransaction ("Swap slices");
    // swap positions; if lengths differ, shift the clips in between so the chain stays contiguous
    auto sl = slices();
    std::sort (sl.begin(), sl.end(), [] (const ClipModel& x, const ClipModel& y) { return x.getStart() < y.getStart(); });
    std::vector<ClipModel> order = sl;
    int ia = -1, ib = -1; for (size_t i = 0; i < order.size(); ++i) { if (order[i].getId() == ca.getId()) ia = (int) i; if (order[i].getId() == cb.getId()) ib = (int) i; }
    std::swap (order[(size_t) ia], order[(size_t) ib]);
    double pos = sl.front().getStart();
    for (auto& c : order) { c.setStart (pos, &p.getUndoManager()); pos += c.getLength(); }
}

bool ChopperPanel::keyPressed (const juce::KeyPress& k)
{
    static const juce::String keys = "1234567890qwertyuiop";
    const int idx = keys.indexOfChar ((juce::juce_wchar) juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) k.getTextCharacter()));
    if (idx < 0 || k.getModifiers().isAnyModifierKeyDown()) return false;
    trigger (idx); return true;
}

void ChopperPanel::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.isNoteOn()) pendingMidiPad.store (m.getNoteNumber() - 36);
}
void ChopperPanel::timerCallback()
{
    const int p = pendingMidiPad.exchange (-1);
    if (p >= 0) trigger (p);
    if (! session.getAudioEngine().isAuditioning() && lastTriggered >= 0) { lastTriggered = -1; for (auto& pd : pads) pd->hot = false; repaint(); }
}

void ChopperPanel::showPadMenu (int pad, const juce::MouseEvent& e)
{
    auto c = ClipModel (session.getProject().findById (ids::CLIP, sliceIds[(size_t) pad]));
    if (! c.isValid()) return;
    juce::PopupMenu m;
    m.addItem (1, "Reverse", true, c.isReversed()); m.addItem (2, "Mute", true, c.isMuted());
    juce::PopupMenu pitch; for (int st = -12; st <= 12; ++st) pitch.addItem (100 + st + 12, (st > 0 ? "+" : "") + juce::String (st) + " st", true, c.getPitchSemis() == st); m.addSubMenu ("Pitch", pitch);
    juce::PopupMenu formant; for (int f = -6; f <= 6; f += 2) formant.addItem (200 + f + 6, (f > 0 ? "+" : "") + juce::String (f), true, (int) c.getFormant() == f); m.addSubMenu ("Formant", formant);
    m.addItem (3, "Gate (short fade out)"); m.addItem (4, "Stutter x4"); m.addItem (5, "Repeat x2"); m.addSeparator(); m.addItem (6, "Delete slice");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }), [this, c] (int r) mutable
    {
        auto& p = session.getProject(); auto* um = &p.getUndoManager(); const double bpm = p.getBpm();
        if (r == 1) clipops::setReversed (p, c, ! c.isReversed(), bpm);
        else if (r == 2) { um->beginNewTransaction ("Mute slice"); c.setMuted (! c.isMuted(), um); }
        else if (r == 3) { um->beginNewTransaction ("Gate slice"); c.setFadeOut (c.getLength() * 0.5, um); c.setFadeShapes (c.getFadeInShape(), FadeShape::Exponential, um); }
        else if (r == 4) { um->beginNewTransaction ("Stutter slice"); const double part = c.getLength() / 4.0; const double start = c.getStart(); c.setLength (part, um); auto parent = c.getState().getParent(); for (int i = 1; i < 4; ++i) { auto copy = c.getState().createCopy(); copy.setProperty (ids::id, ProjectModel::newId(), nullptr); ClipModel cc (copy); cc.setStart (start + i * part, nullptr); parent.appendChild (copy, um); } }
        else if (r == 5) clipops::repeat (p, c, 1);
        else if (r == 6) clipops::remove (p, c);
        else if (r >= 100 && r <= 124) { um->beginNewTransaction ("Slice pitch"); c.setPitch (r - 112, 0, um); }
        else if (r >= 200 && r <= 212) { um->beginNewTransaction ("Slice formant"); c.setFormant (r - 206, um); }
    });
}

// ---- Pad ------------------------------------------------------------------------------------------------------------------
void ChopperPanel::Pad::paint (juce::Graphics& g)
{
    if (idx >= (int) owner.sliceIds.size()) return;
    auto c = ClipModel (owner.session.getProject().findById (ids::CLIP, owner.sliceIds[(size_t) idx]));
    if (! c.isValid()) return;
    auto r = getLocalBounds().reduced (2);
    g.setColour (hot ? colours::accent : owner.dragOver == idx ? colours::accentDim : c.getColour());
    g.fillRoundedRectangle (r.toFloat(), 4.0f);
    auto cache = owner.session.getWaveformCache().get (c.getSourceId()); auto src = owner.session.getSourceLibrary().get (c.getSourceId());
    if (cache && cache->isReady() && src)
    {
        const double bpm = owner.session.getProject().getBpm();
        double s0 = c.getOffset() * src->sampleRate, s1 = s0 + clipops::beatsToSourceSeconds (c, c.getLength(), bpm) * src->sampleRate;
        if (c.isReversed()) { const double e = (double) c.getState().getProperty (ids::sourceEnd, 0.0) * src->sampleRate; s1 = e - (s1 - s0); s0 = e; }
        WaveformRenderer::draw (g, *cache, r.reduced (3, 14), s0, s1, colours::waveform, colours::waveformRms.withAlpha (0.5f), (float) c.getGain(), false);
    }
    g.setColour (colours::text); g.setFont (Theme::ui (11.0f, true));
    static const juce::String keys = "1234567890QWERTYUIOP";
    g.drawText (juce::String (idx + 1) + (idx < 20 ? "  [" + juce::String::charToString (keys[idx]) + "]" : ""), r.reduced (5, 2), juce::Justification::topLeft);
    juce::String info;
    if (c.isReversed()) info << "REV "; if (c.getPitchSemis() != 0) info << (c.getPitchSemis() > 0 ? "+" : "") << c.getPitchSemis() << "st "; if (c.isMuted()) info << "MUTE";
    g.setFont (Theme::ui (10.0f)); g.drawText (info, r.reduced (5, 2), juce::Justification::bottomLeft);
    g.drawText (juce::String (c.getLength(), 2) + " b", r.reduced (5, 2), juce::Justification::bottomRight);
    if (c.isMuted()) { g.setColour (juce::Colours::black.withAlpha (0.5f)); g.fillRoundedRectangle (r.toFloat(), 4.0f); }
}
void ChopperPanel::Pad::mouseDown (const juce::MouseEvent& e)
{
    owner.grabKeyboardFocus();
    if (e.mods.isPopupMenu()) { owner.showPadMenu (idx, e); return; }
    owner.trigger (idx); owner.dragFrom = idx; dragging = false;
    owner.view.selectClip (owner.sliceIds[(size_t) idx], false);
}
void ChopperPanel::Pad::mouseDrag (const juce::MouseEvent& e)
{
    dragging = true;
    auto pos = owner.getLocalPoint (this, e.getPosition());
    int over = -1; for (auto& p : owner.pads) if (p->getBounds().contains (pos)) over = p->idx;
    if (over != owner.dragOver) { owner.dragOver = over; owner.repaint(); }
}
void ChopperPanel::Pad::mouseUp (const juce::MouseEvent&)
{
    if (dragging && owner.dragOver >= 0 && owner.dragOver != idx) owner.swapPads (idx, owner.dragOver);
    owner.dragFrom = -1; owner.dragOver = -1; dragging = false; owner.repaint();
}

void ChopperPanel::paint (juce::Graphics& g) { g.fillAll (colours::panelBg); }

void ChopperPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    auto top = r.removeFromTop (22); title.setBounds (top.removeFromLeft (300));
    auto place = [&] (juce::Component& c, int w) { c.setBounds (top.removeFromLeft (w).reduced (1)); };
    place (sliceTransients, 120); place (sliceBeat, 40); place (slice8, 40); place (slice16, 44); top.removeFromLeft (8); place (reverseAll, 90); place (shuffle, 70); place (restore, 100); top.removeFromLeft (8); place (followButton, 120); place (midiButton, 70);
    hint.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);
    if (pads.empty()) return;
    const int cols = juce::jlimit (4, 16, (int) std::ceil (std::sqrt ((double) pads.size() * 2.0)));
    const int rows = (int) std::ceil (pads.size() / (double) cols);
    const int w = r.getWidth() / cols, h = juce::jmax (44, juce::jmin (90, r.getHeight() / juce::jmax (1, rows)));
    for (auto& p : pads) p->setBounds (r.getX() + (p->idx % cols) * w, r.getY() + (p->idx / cols) * h, w, h);
}
} // namespace mashup::ui
