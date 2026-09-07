#include "ArrangementCanvas.h"
#include "UI/Theme/Theme.h"
#include "AudioEngine/AudioEngine.h"
#include "Timeline/TempoMap.h"
#include "Clips/ClipOperations.h"
#include "Waveform/WaveformCacheManager.h"
#include "Waveform/WaveformRenderer.h"
#include "Analysis/AnalysisData.h"
#include "Import/FFmpegDecoder.h"
#include "Slicing/Slicer.h"
#include "Slicing/PatternLibrary.h"
#include "Automation/AutomationCurve.h"
#include "Core/MusicalKey.h"

namespace mashup::ui
{
ArrangementCanvas::ArrangementCanvas (Session& s, TimelineViewState& v) : session (s), view (v)
{
    session.getProject().getRoot().addListener (this);
    view.addChangeListener (this);
    session.getWaveformCache().addChangeListener (this);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

ArrangementCanvas::~ArrangementCanvas()
{
    session.getProject().getRoot().removeListener (this);
    view.removeChangeListener (this);
    session.getWaveformCache().removeChangeListener (this);
}

double ArrangementCanvas::beatsPerBar() const { return session.getProject().getTimeSigNumerator() * 4.0 / session.getProject().getTimeSigDenominator(); }

int ArrangementCanvas::trackYForIndex (int index) const
{
    int y = -view.verticalScroll;
    auto tracks = session.getProject().tracks();
    for (int i = 0; i < index && i < tracks.getNumChildren(); ++i) y += TrackModel (tracks.getChild (i)).getTotalHeight();
    return y;
}
int ArrangementCanvas::trackIndexAtY (int y) const
{
    int cy = -view.verticalScroll; int i = 0;
    for (const auto& t : session.getProject().tracks()) { const int h = TrackModel (t).getTotalHeight(); if (y >= cy && y < cy + h) return i; cy += h; ++i; }
    return -1;
}
TrackModel ArrangementCanvas::trackAtY (int y) const { const int i = trackIndexAtY (y); return i < 0 ? TrackModel() : TrackModel (session.getProject().tracks().getChild (i)); }
int ArrangementCanvas::getTotalTracksHeight() const { int h = 0; for (const auto& t : session.getProject().tracks()) h += TrackModel (t).getTotalHeight(); return h; }
double ArrangementCanvas::getContentEndBeat() const { double e = 0; for (const auto& t : session.getProject().tracks()) e = juce::jmax (e, TrackModel (t).getEndBeat()); return e; }

juce::Rectangle<int> ArrangementCanvas::clipBounds (const ClipModel& c, int ty, int th) const
{
    const int x0 = (int) std::floor (view.beatToX (c.getStart())), x1 = (int) std::floor (view.beatToX (c.getEnd()));
    return { x0, ty + 1, juce::jmax (2, x1 - x0), th - 2 };
}

void ArrangementCanvas::resized() { dirty = true; }

void ArrangementCanvas::timerCallback()
{
    auto& engine = session.getAudioEngine();
    const int px = (int) view.beatToX (engine.getPositionBeat());
    if (engine.getTransport().isPlaying() && view.followPlayhead && (px > getWidth() - 20 || px < 0))
        view.setScrollBeat (view.xToBeat (px) - (px < 0 ? 0.0 : 20.0 / view.pixelsPerBeat));
    if (px != lastPlayheadX)
    {
        repaint (juce::jmin (px, lastPlayheadX) - 2, 0, std::abs (px - lastPlayheadX) + 5, getHeight());
        lastPlayheadX = px;
    }
    if (engine.getRecorder().isRecording()) repaint();
}

void ArrangementCanvas::paintGrid (juce::Graphics& g, juce::Rectangle<int> area)
{
    const double bpb = beatsPerBar();
    const double grid = view.effectiveGrid (bpb);
    const double firstBeat = std::floor (view.scrollBeat / grid) * grid;
    for (double b = firstBeat; ; b += grid)
    {
        const int x = (int) view.beatToX (b);
        if (x > area.getRight()) break;
        if (x < 0) continue;
        const bool bar = std::abs (std::fmod (b, bpb)) < 1e-6 || std::abs (std::fmod (b, bpb) - bpb) < 1e-6;
        const bool beat = std::abs (b - std::round (b)) < 1e-6;
        g.setColour (bar ? colours::gridBar : beat ? colours::gridBeat : colours::gridSub);
        g.drawVerticalLine (x, (float) area.getY(), (float) area.getBottom());
    }
}

void ArrangementCanvas::paintClip (juce::Graphics& g, const ClipModel& c, const TrackModel& t, juce::Rectangle<int> r, double bpm)
{
    const bool selected = view.isSelected (c.getId());
    auto col = c.getColour();
    g.setColour (selected ? col.brighter (0.5f) : col);
    g.fillRoundedRectangle (r.toFloat(), 3.0f);
    const int headerH = r.getHeight() >= 40 ? 14 : 0;
    auto body = r.withTrimmedTop (headerH).reduced (1);
    // waveform
    auto cache = session.getWaveformCache().get (c.getSourceId());
    auto source = session.getSourceLibrary().get (c.getSourceId());
    if (cache && cache->isReady() && source && body.getWidth() > 1)
    {
        const double srcRate = source->sampleRate;
        double s0 = c.getOffset() * srcRate;
        double s1 = s0 + clipops::beatsToSourceSeconds (c, c.getLength(), bpm) * srcRate;
        if (c.isReversed())
        {
            const double regionEnd = (double) c.getState().getProperty (ids::sourceEnd, 0.0) * srcRate;
            const double span = s1 - s0;
            s0 = (regionEnd > 0 ? regionEnd : (double) source->getLengthSamples()); s1 = s0 - span;
        }
        // only draw the visible part
        auto vis = body.getIntersection (getLocalBounds());
        if (! vis.isEmpty())
        {
            const double perPx = (s1 - s0) / body.getWidth();
            const double vs0 = s0 + (vis.getX() - body.getX()) * perPx, vs1 = vs0 + vis.getWidth() * perPx;
            const bool loopWrap = c.isLooped();
            if (! loopWrap)
                WaveformRenderer::draw (g, *cache, vis, vs0, vs1, colours::waveform.withAlpha (selected ? 1.0f : 0.85f), colours::waveformRms.withAlpha (0.55f), (float) c.getGain(), body.getHeight() > 40);
            else
            {
                // looped clip: repeat the source region
                const double regionStart = c.getOffset() * srcRate;
                const double regionEndS = (double) c.getState().getProperty (ids::sourceEnd, 0.0) * srcRate;
                const double len = (regionEndS > regionStart ? regionEndS : (double) source->getLengthSamples()) - regionStart;
                double pos = vs0;
                int x = vis.getX();
                while (x < vis.getRight() && len > 0)
                {
                    double rel = std::fmod (pos - regionStart, len); if (rel < 0) rel += len;
                    const double remaining = len - rel;
                    const int w = juce::jmin (vis.getRight() - x, juce::jmax (1, (int) std::ceil (remaining / perPx)));
                    WaveformRenderer::draw (g, *cache, { x, vis.getY(), w, vis.getHeight() }, regionStart + rel, regionStart + rel + w * perPx, colours::waveform.withAlpha (0.85f), colours::waveformRms.withAlpha (0.55f), (float) c.getGain(), body.getHeight() > 40);
                    x += w; pos += w * perPx;
                }
            }
        }
    }
    else if (session.getSourceLibrary().isLoading (c.getSourceId()) || (cache && ! cache->isReady()))
    {
        g.setColour (colours::textDim); g.setFont (Theme::ui (11.0f));
        g.drawText ("loading " + juce::String ((int) (100 * (cache ? session.getWaveformCache().getBuildProgress (c.getSourceId()) : session.getSourceLibrary().getProgress (c.getSourceId())))) + "%", body, juce::Justification::centred);
    }
    // fades
    const double fi = c.getFadeIn(), fo = c.getFadeOut();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    if (fi > 0) { juce::Path p; const float fx = (float) (r.getX() + fi * view.pixelsPerBeat); p.startNewSubPath ((float) r.getX(), (float) r.getBottom()); p.lineTo ((float) r.getX(), (float) r.getY() + headerH); p.lineTo (fx, (float) r.getY() + headerH); p.closeSubPath(); g.fillPath (p); g.setColour (colours::text); g.drawLine ((float) r.getX(), (float) r.getBottom(), fx, (float) r.getY() + headerH, 1.0f); g.setColour (juce::Colours::black.withAlpha (0.35f)); }
    if (fo > 0) { juce::Path p; const float fx = (float) (r.getRight() - fo * view.pixelsPerBeat); p.startNewSubPath ((float) r.getRight(), (float) r.getBottom()); p.lineTo ((float) r.getRight(), (float) r.getY() + headerH); p.lineTo (fx, (float) r.getY() + headerH); p.closeSubPath(); g.fillPath (p); g.setColour (colours::text); g.drawLine ((float) r.getRight(), (float) r.getBottom(), fx, (float) r.getY() + headerH, 1.0f); }
    // fade handles
    if (r.getWidth() > 30) { g.setColour (colours::text.withAlpha (0.8f)); g.fillEllipse ((float) (r.getX() + fi * view.pixelsPerBeat) - 3.0f, (float) r.getY() + headerH + 2.0f, 6.0f, 6.0f); g.fillEllipse ((float) (r.getRight() - fo * view.pixelsPerBeat) - 3.0f, (float) r.getY() + headerH + 2.0f, 6.0f, 6.0f); }
    // header
    if (headerH > 0)
    {
        g.setColour (juce::Colours::black.withAlpha (0.25f)); g.fillRect (r.getX(), r.getY(), r.getWidth(), headerH);
        g.setColour (colours::text); g.setFont (Theme::ui (10.0f, true));
        juce::String info = c.getName();
        if (c.getClipBpm() > 0) info += "  " + juce::String (c.getClipBpm(), 1) + (c.isSyncedToProject() ? " sync" : "");
        if (c.getKeyRoot() >= 0) info += "  " + key::name (c.getKeyRoot(), c.getKeyMode());
        if (c.getPitchSemis() != 0 || c.getPitchCents() != 0) info += "  " + juce::String (c.getPitchSemis() > 0 ? "+" : "") + juce::String (c.getPitchSemis()) + "st";
        if (c.isReversed()) info += "  REV";
        if (c.isLooped()) info += "  LOOP";
        if (c.isMuted()) info += "  (muted)";
        g.drawText (info, r.reduced (4, 0).withHeight (headerH), juce::Justification::centredLeft, true);
    }
    if (c.isMuted()) { g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRoundedRectangle (r.toFloat(), 3.0f); }
    // beat markers of the source (from analysis), mapped through the clip
    if (view.showBeatMarkers && source && r.getWidth() > 60 && view.pixelsPerBeat > 6)
    {
        auto srcNode = ProjectModel::findByIdIn (session.getProject().sources(), ids::SOURCE, c.getSourceId());
        auto downbeats = analysis::readTimes (srcNode, ids::DOWNBEATS);
        if (! downbeats.empty() && ! c.isReversed())
        {
            g.setColour (colours::warning.withAlpha (0.5f));
            const double speed = c.getPlaybackSpeed (bpm);
            for (double t : downbeats)
            {
                const double beatInClip = (t - c.getOffset()) / speed * bpm / 60.0;
                if (beatInClip < 0 || beatInClip > c.getLength()) continue;
                const int x = (int) view.beatToX (c.getStart() + beatInClip);
                g.drawVerticalLine (x, (float) r.getY() + headerH, (float) r.getBottom());
            }
        }
    }
    g.setColour (selected ? colours::accent : juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 3.0f, selected ? 1.5f : 1.0f);
}

void ArrangementCanvas::rebuildImage()
{
    if (getWidth() <= 0 || getHeight() <= 0) return;
    if (cache.getWidth() != getWidth() || cache.getHeight() != getHeight()) cache = juce::Image (juce::Image::RGB, getWidth(), getHeight(), false);
    juce::Graphics g (cache);
    g.fillAll (colours::windowBg);
    auto& p = session.getProject();
    const double bpm = p.getBpm();
    int y = -view.verticalScroll;
    int idx = 0;
    for (const auto& tn : p.tracks())
    {
        TrackModel t (tn);
        const int h = t.getTotalHeight(), clipH = t.getHeight();
        juce::Rectangle<int> lane (0, y, getWidth(), h);
        if (lane.getBottom() >= 0 && lane.getY() <= getHeight())
        {
            g.setColour (idx % 2 ? colours::panelBg : colours::panelBg.brighter (0.02f));
            if (view.selectedTrack == t.getId()) g.setColour (colours::panelBg.brighter (0.06f));
            g.fillRect (lane);
            paintGrid (g, lane);
            g.setColour (colours::border); g.drawHorizontalLine (lane.getBottom() - 1, 0.0f, (float) getWidth());
            if (t.isAutomationShown()) paintAutomation (g, t, juce::Rectangle<int> (0, y + clipH, getWidth(), TrackModel::automationLaneHeight));
            for (const auto& cn : t.clips())
            {
                ClipModel c (cn);
                auto r = clipBounds (c, y, clipH);
                if (r.getRight() < 0 || r.getX() > getWidth()) continue;
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (r);
                paintClip (g, c, t, r, bpm);
            }
        }
        y += h; ++idx;
    }
    // area below the tracks
    if (y < getHeight()) { g.setColour (colours::windowBg); g.fillRect (0, y, getWidth(), getHeight() - y); paintGrid (g, { 0, y, getWidth(), getHeight() - y }); }
    dirty = false;
}

void ArrangementCanvas::paint (juce::Graphics& g)
{
    if (dirty || cache.isNull()) rebuildImage();
    g.drawImageAt (cache, 0, 0);
    auto& p = session.getProject();
    // loop region
    if (p.isLoopEnabled() && p.getLoopEnd() > p.getLoopStart())
    {
        const int x0 = (int) view.beatToX (p.getLoopStart()), x1 = (int) view.beatToX (p.getLoopEnd());
        g.setColour (colours::loopRegion); g.fillRect (x0, 0, x1 - x0, getHeight());
    }
    if (view.hasTimeSelection)
    {
        const int x0 = (int) view.beatToX (view.timeSelStart), x1 = (int) view.beatToX (view.timeSelEnd);
        g.setColour (colours::selection); g.fillRect (x0, 0, juce::jmax (1, x1 - x0), getHeight());
        g.setColour (colours::accent.withAlpha (0.6f)); g.drawVerticalLine (x0, 0.0f, (float) getHeight()); g.drawVerticalLine (x1, 0.0f, (float) getHeight());
    }
    // gesture preview
    if (gesture == Gesture::Move || gesture == Gesture::Duplicate)
    {
        g.setColour (colours::accent.withAlpha (0.35f));
        for (auto& [c, startBeat] : gestureClipStarts)
        {
            auto track = TrackModel (c.getTrackState());
            int ti = trackops::indexOf (session.getProject(), track) + previewDeltaTrack;
            ti = juce::jlimit (0, juce::jmax (0, p.tracks().getNumChildren() - 1), ti);
            const int ty = trackYForIndex (ti), th = TrackModel (p.tracks().getChild (ti)).getHeight();
            const int x0 = (int) view.beatToX (startBeat + previewDeltaBeat), x1 = (int) view.beatToX (startBeat + previewDeltaBeat + c.getLength());
            g.fillRoundedRectangle (juce::Rectangle<int> (x0, ty + 1, x1 - x0, th - 2).toFloat(), 3.0f);
            g.setColour (colours::accent); g.drawRoundedRectangle (juce::Rectangle<int> (x0, ty + 1, x1 - x0, th - 2).toFloat(), 3.0f, 1.0f); g.setColour (colours::accent.withAlpha (0.35f));
        }
    }
    else if (gesture == Gesture::TrimLeft || gesture == Gesture::TrimRight)
    {
        const int x = (int) view.beatToX (previewValue);
        g.setColour (colours::accent); g.drawVerticalLine (x, (float) gestureHit.bounds.getY(), (float) gestureHit.bounds.getBottom());
    }
    if (gesture == Gesture::RubberBand) { g.setColour (colours::selection); g.fillRect (rubberBand); g.setColour (colours::accent); g.drawRect (rubberBand); }
    if (showDropIndicator)
    {
        const int x = (int) view.beatToX (dropBeat);
        g.setColour (colours::accent);
        g.drawVerticalLine (x, 0.0f, (float) getHeight());
        if (dropTrackIndex >= 0 && dropTrackIndex < p.tracks().getNumChildren())
        {
            const int ty = trackYForIndex (dropTrackIndex), th = TrackModel (p.tracks().getChild (dropTrackIndex)).getTotalHeight();
            g.setColour (colours::accent.withAlpha (0.15f)); g.fillRect (0, ty, getWidth(), th);
        }
        else { g.setColour (colours::accent.withAlpha (0.15f)); g.fillRect (0, trackYForIndex (p.tracks().getNumChildren()), getWidth(), 60); }
    }
    // playhead
    auto& engine = session.getAudioEngine();
    const int px = (int) view.beatToX (engine.getPositionBeat());
    g.setColour (engine.getTransport().isRecording() ? colours::record : colours::playhead);
    g.drawVerticalLine (px, 0.0f, (float) getHeight());
    // recording preview
    if (engine.getRecorder().isRecording())
    {
        const double sr = engine.getSampleRate();
        TempoMap tm = TempoMap::fromProject (p.getRoot());
        const double startBeat = tm.timeToBeat (engine.getRecorder().getTakeStartSample() / sr);
        for (const auto& tn : p.tracks())
        {
            TrackModel t (tn);
            if (! t.isArmed()) continue;
            const int ti = trackops::indexOf (session.getProject(), t);
            const int ty = trackYForIndex (ti), th = t.getTotalHeight();
            g.setColour (colours::record.withAlpha (0.35f));
            g.fillRect ((int) view.beatToX (startBeat), ty + 1, juce::jmax (1, px - (int) view.beatToX (startBeat)), th - 2);
        }
    }
}

juce::Rectangle<int> ArrangementCanvas::automationArea (int index) const
{
    auto t = TrackModel (session.getProject().tracks().getChild (index));
    return { 0, trackYForIndex (index) + t.getHeight(), getWidth(), TrackModel::automationLaneHeight };
}

void ArrangementCanvas::paintAutomation (juce::Graphics& g, const TrackModel& t, juce::Rectangle<int> area)
{
    g.setColour (colours::windowBg.withAlpha (0.6f)); g.fillRect (area);
    g.setColour (colours::border); g.drawHorizontalLine (area.getY(), 0.0f, (float) getWidth());
    const auto param = t.getAutomationParam();
    auto lane = AutomationCurve::findLane (t.getState(), param);
    auto curve = AutomationCurve::fromLane (lane);
    const juce::Colour col = t.getColour().brighter (0.3f);
    // current (non-automated) value as a dashed reference line
    float current = 0.5f;
    if (param == "volume") current = AutomationCurve::volumeToNorm (t.getVolume()); else if (param == "pan") current = AutomationCurve::panToNorm (t.getPan());
    if (curve.isEmpty())
    {
        g.setColour (col.withAlpha (0.4f));
        const float yy = (float) valueToY (area, current);
        const float dash[] = { 4.0f, 4.0f }; g.drawDashedLine ({ 0.0f, yy, (float) getWidth(), yy }, dash, 2, 1.0f);
    }
    else
    {
        juce::Path path;
        const double b0 = view.xToBeat (0), b1 = view.xToBeat (getWidth());
        path.startNewSubPath (0.0f, (float) valueToY (area, curve.valueAt (b0)));
        for (const auto& pt : curve.getPoints()) { if (pt.beat < b0 || pt.beat > b1) continue; path.lineTo ((float) view.beatToX (pt.beat), (float) valueToY (area, pt.value)); }
        path.lineTo ((float) getWidth(), (float) valueToY (area, curve.valueAt (b1)));
        g.setColour (col); g.strokePath (path, juce::PathStrokeType (1.5f));
        for (const auto& pt : curve.getPoints()) { const float x = (float) view.beatToX (pt.beat); if (x < -4 || x > getWidth() + 4) continue; g.fillEllipse (x - 3.5f, (float) valueToY (area, pt.value) - 3.5f, 7.0f, 7.0f); }
    }
    g.setColour (colours::textDim); g.setFont (Theme::ui (9.0f, true));
    static const char* modes[] = { "READ", "WRITE", "TOUCH", "LATCH", "OFF" };
    g.drawText (param.toUpperCase() + "  " + modes[juce::jlimit (0, 4, t.getAutomationMode())], area.reduced (6, 2), juce::Justification::topLeft);
}

// ---------------------------------------------------------------------------------------------------------------------
ArrangementCanvas::Hit ArrangementCanvas::hitTest (juce::Point<int> pos) const
{
    Hit h;
    auto& p = session.getProject();
    int y = -view.verticalScroll;
    for (const auto& tn : p.tracks())
    {
        TrackModel t (tn); const int th = t.getTotalHeight(), clipH = t.getHeight();
        if (pos.y >= y && pos.y < y + th)
        {
            h.track = t;
            if (pos.y >= y + clipH) { h.zone = HitZone::AutomationLane; h.bounds = { 0, y + clipH, getWidth(), TrackModel::automationLaneHeight }; return h; }
            // iterate in reverse so topmost (later) clips win
            for (int i = t.getNumClips() - 1; i >= 0; --i)
            {
                ClipModel c = t.getClip (i);
                auto r = clipBounds (c, y, clipH);
                if (! r.contains (pos)) continue;
                h.clip = c; h.bounds = r;
                const int headerH = r.getHeight() >= 40 ? 14 : 0;
                const int fiX = r.getX() + (int) (c.getFadeIn() * view.pixelsPerBeat), foX = r.getRight() - (int) (c.getFadeOut() * view.pixelsPerBeat);
                if (pos.y < r.getY() + headerH + 12 && r.getWidth() > 30 && std::abs (pos.x - fiX) <= 6) h.zone = HitZone::FadeIn;
                else if (pos.y < r.getY() + headerH + 12 && r.getWidth() > 30 && std::abs (pos.x - foX) <= 6) h.zone = HitZone::FadeOut;
                else if (pos.x - r.getX() < 7 && r.getWidth() > 20) h.zone = HitZone::LeftEdge;
                else if (r.getRight() - pos.x < 7 && r.getWidth() > 20) h.zone = HitZone::RightEdge;
                else h.zone = HitZone::Body;
                return h;
            }
            return h;
        }
        y += th;
    }
    return h;
}

void ArrangementCanvas::mouseMove (const juce::MouseEvent& e)
{
    auto h = hitTest (e.getPosition());
    if (view.tool == Tool::Blade) setMouseCursor (juce::MouseCursor::CrosshairCursor);
    else switch (h.zone)
    {
        case HitZone::LeftEdge: case HitZone::RightEdge: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case HitZone::FadeIn: case HitZone::FadeOut: setMouseCursor (juce::MouseCursor::PointingHandCursor); break;
        case HitZone::AutomationLane: setMouseCursor (juce::MouseCursor::CrosshairCursor); break;
        case HitZone::Body: setMouseCursor (e.mods.isAltDown() ? juce::MouseCursor::CopyingCursor : juce::MouseCursor::NormalCursor); break;
        default: setMouseCursor (juce::MouseCursor::NormalCursor);
    }
}

void ArrangementCanvas::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    auto& p = session.getProject();
    const double bpb = beatsPerBar();
    auto h = hitTest (e.getPosition());
    dragStartPos = e.getPosition();
    gestureStartBeat = view.xToBeat (e.x);
    if (h.track.isValid()) { view.selectedTrack = h.track.getId(); }

    if (h.zone == HitZone::AutomationLane)
    {
        auto* um = &session.getUndoManager();
        autoLane = AutomationCurve::getOrCreateLane (h.track.getState(), h.track.getAutomationParam(), nullptr);
        auto curve = AutomationCurve::fromLane (autoLane);
        autoLaneBounds = h.bounds;
        const double beat = view.xToBeat (e.x);
        const int near = curve.indexNear (beat, 6.0 / view.pixelsPerBeat);
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m; m.addItem (1, "Delete point", near >= 0); m.addItem (2, "Clear lane"); m.addItem (3, "Hide automation lane");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }), [this, near, h, curve] (int r) mutable
            {
                auto* u = &session.getUndoManager();
                if (r == 1) { u->beginNewTransaction ("Delete automation point"); curve.removeIndex (near); curve.writeToLane (autoLane, u); }
                else if (r == 2) { u->beginNewTransaction ("Clear automation"); curve.clear(); curve.writeToLane (autoLane, u); }
                else if (r == 3) { u->beginNewTransaction ("Hide automation"); TrackModel (h.track).setAutomationShown (false, u); }
            });
            return;
        }
        if (e.getNumberOfClicks() >= 2 && near >= 0) { um->beginNewTransaction ("Delete automation point"); curve.removeIndex (near); curve.writeToLane (autoLane, um); return; }
        um->beginNewTransaction (near >= 0 ? "Move automation point" : "Add automation point");
        if (near < 0) { curve.addPoint (view.snap (beat, bpb), yToValue (h.bounds, e.y)); curve.writeToLane (autoLane, um); autoPointIndex = curve.indexNear (view.snap (beat, bpb), 1e-6); }
        else autoPointIndex = near;
        gesture = Gesture::AutomationPoint; gestureHit = h;
        return;
    }
    if (e.mods.isPopupMenu())
    {
        if (h.clip.isValid()) { if (! view.isSelected (h.clip.getId())) view.selectClip (h.clip.getId()); showClipMenu (e, h); }
        else showEmptyMenu (e);
        return;
    }

    if (view.tool == Tool::Blade)
    {
        if (h.clip.isValid()) clipops::split (p, h.clip, view.snap (view.xToBeat (e.x), bpb), p.getBpm());
        return;
    }

    if (! h.clip.isValid())
    {
        if (! e.mods.isShiftDown()) view.selectClip ({});
        gesture = Gesture::TimeSelect;
        view.hasTimeSelection = false; view.sendChangeMessage();
        return;
    }

    if (e.mods.isShiftDown() || e.mods.isCtrlDown()) { view.selectClip (h.clip.getId(), true); }
    else if (! view.isSelected (h.clip.getId())) view.selectClip (h.clip.getId());

    gestureHit = h;
    gestureClipStarts.clear();
    for (auto& c : getSelectedClips()) gestureClipStarts.push_back ({ c, c.getStart() });
    previewDeltaBeat = 0; previewDeltaTrack = 0;
    switch (h.zone)
    {
        case HitZone::Body:      gesture = e.mods.isAltDown() ? Gesture::Duplicate : Gesture::Move; break;
        case HitZone::LeftEdge:  gesture = Gesture::TrimLeft; previewValue = h.clip.getStart(); break;
        case HitZone::RightEdge: gesture = Gesture::TrimRight; previewValue = h.clip.getEnd(); break;
        case HitZone::FadeIn:    gesture = Gesture::FadeIn; session.getUndoManager().beginNewTransaction ("Fade in"); break;
        case HitZone::FadeOut:   gesture = Gesture::FadeOut; session.getUndoManager().beginNewTransaction ("Fade out"); break;
        default: gesture = Gesture::None;
    }
}

void ArrangementCanvas::mouseDrag (const juce::MouseEvent& e)
{
    auto& p = session.getProject();
    const double bpb = beatsPerBar();
    const double beat = view.xToBeat (e.x);
    switch (gesture)
    {
        case Gesture::TimeSelect:
            if (e.getDistanceFromDragStart() > 4)
            {
                if (e.mods.isShiftDown() || std::abs (e.getDistanceFromDragStartY()) > 40) { gesture = Gesture::RubberBand; }
                else view.setTimeSelection (view.snap (gestureStartBeat, bpb), view.snap (beat, bpb));
            }
            break;
        case Gesture::RubberBand:
        {
            rubberBand = juce::Rectangle<int> (dragStartPos, e.getPosition());
            view.selectedClips.clear();
            int y = -view.verticalScroll;
            for (const auto& tn : p.tracks())
            {
                TrackModel t (tn); const int th = t.getTotalHeight();
                for (const auto& cn : t.clips()) { ClipModel c (cn); if (clipBounds (c, y, t.getHeight()).intersects (rubberBand)) view.selectedClips.insert (c.getId()); }
                y += th;
            }
            view.sendChangeMessage();
            break;
        }
        case Gesture::Move: case Gesture::Duplicate:
        {
            const double grabOffset = gestureStartBeat - gestureHit.clip.getStart();
            const double newStart = view.snap (beat - grabOffset, bpb);
            previewDeltaBeat = newStart - gestureHit.clip.getStart();
            // don't allow any clip to go negative
            for (auto& [c, s] : gestureClipStarts) previewDeltaBeat = juce::jmax (previewDeltaBeat, -s);
            const int ti = trackIndexAtY (e.y);
            previewDeltaTrack = ti < 0 ? 0 : ti - trackops::indexOf (p, gestureHit.track);
            repaint();
            break;
        }
        case Gesture::AutomationPoint:
        {
            auto curve = AutomationCurve::fromLane (autoLane);
            if (autoPointIndex >= 0 && autoPointIndex < (int) curve.getPoints().size())
            {
                const double nb = e.mods.isShiftDown() ? curve.getPoints()[(size_t) autoPointIndex].beat : view.snap (beat, bpb);
                curve.setPoint (autoPointIndex, nb, yToValue (autoLaneBounds, e.y));
                curve.writeToLane (autoLane, &session.getUndoManager());
                autoPointIndex = curve.indexNear (nb, 1e-6);
            }
            break;
        }
        case Gesture::TrimLeft:  previewValue = juce::jlimit (0.0, gestureHit.clip.getEnd() - 1.0 / 64.0, view.snap (beat, bpb)); repaint(); break;
        case Gesture::TrimRight: previewValue = juce::jmax (gestureHit.clip.getStart() + 1.0 / 64.0, view.snap (beat, bpb)); repaint(); break;
        case Gesture::FadeIn:    gestureHit.clip.setFadeIn (juce::jmax (0.0, view.snap (beat, bpb) - gestureHit.clip.getStart()), &session.getUndoManager()); break;
        case Gesture::FadeOut:   gestureHit.clip.setFadeOut (juce::jmax (0.0, gestureHit.clip.getEnd() - view.snap (beat, bpb)), &session.getUndoManager()); break;
        default: break;
    }
}

void ArrangementCanvas::mouseUp (const juce::MouseEvent& e)
{
    auto& p = session.getProject(); auto* um = &session.getUndoManager();
    const double bpm = p.getBpm();
    switch (gesture)
    {
        case Gesture::Move:
            if (e.mouseWasDraggedSinceMouseDown() && (std::abs (previewDeltaBeat) > 1e-9 || previewDeltaTrack != 0))
            {
                um->beginNewTransaction ("Move clips");
                for (auto& [c, s] : gestureClipStarts)
                {
                    int ti = trackops::indexOf (p, TrackModel (c.getTrackState())) + previewDeltaTrack;
                    ti = juce::jlimit (0, p.tracks().getNumChildren() - 1, ti);
                    clipops::move (p, c, s + previewDeltaBeat, TrackModel (p.tracks().getChild (ti)));
                }
            }
            break;
        case Gesture::Duplicate:
            if (e.mouseWasDraggedSinceMouseDown())
            {
                um->beginNewTransaction ("Duplicate clips");
                std::set<juce::String> newSel;
                for (auto& [c, s] : gestureClipStarts)
                {
                    int ti = juce::jlimit (0, p.tracks().getNumChildren() - 1, trackops::indexOf (p, TrackModel (c.getTrackState())) + previewDeltaTrack);
                    auto copy = c.getState().createCopy(); copy.setProperty (ids::id, ProjectModel::newId(), nullptr);
                    ClipModel nc (copy); nc.setStart (s + previewDeltaBeat, nullptr);
                    TrackModel (p.tracks().getChild (ti)).clips().appendChild (copy, um);
                    newSel.insert (nc.getId());
                }
                view.selectedClips = newSel; view.sendChangeMessage();
            }
            break;
        case Gesture::TrimLeft:  if (e.mouseWasDraggedSinceMouseDown()) clipops::trimStart (p, gestureHit.clip, previewValue, bpm); break;
        case Gesture::TrimRight: if (e.mouseWasDraggedSinceMouseDown()) clipops::trimEnd (p, gestureHit.clip, previewValue); break;
        case Gesture::TimeSelect:
            if (! e.mouseWasDraggedSinceMouseDown()) session.getAudioEngine().locateBeat (view.snap (gestureStartBeat, beatsPerBar()));
            break;
        default: break;
    }
    gesture = Gesture::None; gestureClipStarts.clear();
    repaint();
}

void ArrangementCanvas::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto h = hitTest (e.getPosition());
    if (! h.clip.isValid() && h.track.isValid()) return;
    if (h.clip.isValid()) { view.selectClip (h.clip.getId()); view.setTimeSelection (h.clip.getStart(), h.clip.getEnd()); }
}

void ArrangementCanvas::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) view.setZoom (view.pixelsPerBeat * std::pow (1.15, (w.deltaY) * 10.0), e.x);
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY)) view.setScrollBeat (view.scrollBeat - (w.deltaY + w.deltaX) * 160.0 / view.pixelsPerBeat);
    else view.setVerticalScroll (juce::jlimit (0, juce::jmax (0, getTotalTracksHeight() - getHeight() + 60), view.verticalScroll - (int) (w.deltaY * 120)));
}

// ---------------------------------------------------------------------------------------------------------------------
std::vector<ClipModel> ArrangementCanvas::getSelectedClips() const
{
    std::vector<ClipModel> out;
    for (const auto& tn : session.getProject().tracks())
        for (const auto& cn : TrackModel (tn).clips())
            if (view.isSelected (cn[ids::id].toString())) out.emplace_back (cn);
    return out;
}

void ArrangementCanvas::splitSelectedAtPlayhead()
{
    auto& p = session.getProject();
    const double beat = view.snap (session.getAudioEngine().getPositionBeat(), beatsPerBar());
    auto sel = getSelectedClips();
    session.getUndoManager().beginNewTransaction ("Split");
    if (sel.empty())
    {
        // split every clip under the playhead
        for (const auto& tn : p.tracks()) for (const auto& cn : TrackModel (tn).clips()) { ClipModel c (cn); if (beat > c.getStart() && beat < c.getEnd()) clipops::splitNoTransaction (p, c, beat, p.getBpm()); }
        return;
    }
    for (auto& c : sel) clipops::splitNoTransaction (p, c, beat, p.getBpm());
}

void ArrangementCanvas::deleteSelected()
{
    auto sel = getSelectedClips();
    auto& p = session.getProject();
    session.getUndoManager().beginNewTransaction ("Delete");
    if (! sel.empty()) { for (auto& c : sel) c.getState().getParent().removeChild (c.getState(), &session.getUndoManager()); view.selectClip ({}); return; }
    if (view.hasTimeSelection)
    {
        for (const auto& tn : p.tracks())
        {
            TrackModel t (tn);
            if (view.selectedTrack.isNotEmpty() && t.getId() != view.selectedTrack) continue;
            std::vector<ClipModel> clips; for (const auto& cn : t.clips()) clips.emplace_back (cn);
            for (auto& c : clips) clipops::deleteRange (p, c, view.timeSelStart, view.timeSelEnd, p.getBpm());
        }
    }
}

void ArrangementCanvas::duplicateSelected()
{
    auto sel = getSelectedClips();
    if (sel.empty()) return;
    auto& p = session.getProject(); auto* um = &session.getUndoManager();
    um->beginNewTransaction ("Duplicate");
    double minStart = 1e9, maxEnd = 0; for (auto& c : sel) { minStart = juce::jmin (minStart, c.getStart()); maxEnd = juce::jmax (maxEnd, c.getEnd()); }
    const double span = maxEnd - minStart;
    std::set<juce::String> newSel;
    for (auto& c : sel)
    {
        auto copy = c.getState().createCopy(); copy.setProperty (ids::id, ProjectModel::newId(), nullptr);
        ClipModel nc (copy); nc.setStart (c.getStart() + span, nullptr);
        c.getState().getParent().appendChild (copy, um); newSel.insert (nc.getId());
    }
    view.selectedClips = newSel; view.sendChangeMessage();
}

void ArrangementCanvas::selectAll()
{
    view.selectedClips.clear();
    for (const auto& tn : session.getProject().tracks()) for (const auto& cn : TrackModel (tn).clips()) view.selectedClips.insert (cn[ids::id].toString());
    view.sendChangeMessage();
}

void ArrangementCanvas::copySelection()
{
    auto sel = getSelectedClips();
    if (sel.empty()) return;
    clipboard = juce::ValueTree ("CLIPBOARD");
    double minStart = 1e9; for (auto& c : sel) minStart = juce::jmin (minStart, c.getStart());
    for (auto& c : sel)
    {
        auto copy = c.getState().createCopy();
        copy.setProperty (ids::start, c.getStart() - minStart, nullptr);
        copy.setProperty ("trackIndex", trackops::indexOf (session.getProject(), TrackModel (c.getTrackState())), nullptr);
        clipboard.appendChild (copy, nullptr);
    }
}

void ArrangementCanvas::cutSelection() { copySelection(); deleteSelected(); }

void ArrangementCanvas::pasteAtPlayhead()
{
    if (! clipboard.isValid() || clipboard.getNumChildren() == 0) return;
    auto& p = session.getProject(); auto* um = &session.getUndoManager();
    um->beginNewTransaction ("Paste");
    const double beat = view.snap (session.getAudioEngine().getPositionBeat(), beatsPerBar());
    int minTrack = 1 << 30; for (const auto& c : clipboard) minTrack = juce::jmin (minTrack, (int) c["trackIndex"]);
    int targetBase = view.selectedTrack.isNotEmpty() ? trackops::indexOf (p, TrackModel (p.findById (ids::TRACK, view.selectedTrack))) : minTrack;
    if (targetBase < 0) targetBase = minTrack;
    std::set<juce::String> newSel;
    for (const auto& c : clipboard)
    {
        int ti = targetBase + ((int) c["trackIndex"] - minTrack);
        while (ti >= p.tracks().getNumChildren()) trackops::addTrack (p, {});
        auto copy = c.createCopy(); copy.removeProperty ("trackIndex", nullptr);
        copy.setProperty (ids::id, ProjectModel::newId(), nullptr);
        copy.setProperty (ids::start, beat + (double) c[ids::start], nullptr);
        TrackModel (p.tracks().getChild (ti)).clips().appendChild (copy, um);
        newSel.insert (copy[ids::id].toString());
    }
    view.selectedClips = newSel; view.sendChangeMessage();
}

// ---------------------------------------------------------------------------------------------------------------------
void ArrangementCanvas::showClipMenu (const juce::MouseEvent& e, Hit h)
{
    auto& p = session.getProject();
    const double bpb = beatsPerBar();
    const double beat = view.snap (view.xToBeat (e.x), bpb);
    juce::PopupMenu m;
    m.addItem (1, "Split here");
    m.addItem (2, "Duplicate");
    m.addItem (3, "Delete");
    m.addSeparator();
    m.addItem (4, h.clip.isReversed() ? "Un-reverse" : "Reverse");
    m.addItem (5, "Loop", true, h.clip.isLooped());
    m.addItem (6, "Mute", true, h.clip.isMuted());
    m.addSeparator();
    juce::PopupMenu rep; for (int n : { 1, 2, 3, 4, 7, 8, 16 }) rep.addItem (100 + n, "x" + juce::String (n)); m.addSubMenu ("Repeat", rep);
    m.addItem (7, "Crop to time selection", view.hasTimeSelection);
    m.addItem (8, "Crossfade with next", true);
    m.addSeparator();
    m.addItem (9, "Match to project BPM", h.clip.getClipBpm() > 0 || true);
    m.addItem (10, "Match key to project", h.clip.getKeyRoot() >= 0);
    m.addItem (11, "Set as project BPM", h.clip.getClipBpm() > 0);
    m.addItem (12, "Set as project key", h.clip.getKeyRoot() >= 0);
    m.addSeparator();
    {
        juce::PopupMenu sl;
        sl.addItem (200, "1/1 (beat)"); sl.addItem (201, "1/2"); sl.addItem (202, "1/4"); sl.addItem (203, "1/8"); sl.addItem (204, "1/16"); sl.addItem (205, "Bars"); sl.addItem (206, "2 bars"); sl.addItem (207, "4 bars");
        sl.addSeparator(); sl.addItem (210, "At transients"); sl.addItem (211, "At phrases");
        m.addSubMenu ("Slice", sl);
        juce::PopupMenu pats;
        for (auto& cat : PatternLibrary::categories())
        {
            juce::PopupMenu sub; int idx = 300;
            for (auto& pt : PatternLibrary::all()) { if (pt.category == cat) sub.addItem (idx, pt.name); ++idx; }
            pats.addSubMenu (cat, sub);
        }
        m.addSubMenu ("Patterns", pats);
    }
    m.addSeparator();
    m.addItem (13, "Rename...");
    m.addItem (14, "Normalize gain to -1 dBFS");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }), [this, h, beat] (int r) mutable
    {
        auto& proj = session.getProject(); auto* um = &session.getUndoManager(); const double bpm = proj.getBpm();
        switch (r)
        {
            case 1: clipops::split (proj, h.clip, beat, bpm); break;
            case 2: duplicateSelected(); break;
            case 3: deleteSelected(); break;
            case 4: clipops::setReversed (proj, h.clip, ! h.clip.isReversed(), bpm); break;
            case 5: um->beginNewTransaction ("Loop clip"); if (! h.clip.isLooped()) h.clip.getState().setProperty (ids::sourceEnd, h.clip.getOffset() + clipops::beatsToSourceSeconds (h.clip, h.clip.getLength(), bpm), um); h.clip.setLooped (! h.clip.isLooped(), um); break;
            case 6: um->beginNewTransaction ("Mute clip"); h.clip.setMuted (! h.clip.isMuted(), um); break;
            case 7: clipops::cropToRange (proj, h.clip, view.timeSelStart, view.timeSelEnd, bpm); break;
            case 8:
            {
                TrackModel t (h.clip.getTrackState()); ClipModel next; double best = 1e9;
                for (const auto& cn : t.clips()) { ClipModel c (cn); if (c.getId() != h.clip.getId() && c.getStart() >= h.clip.getStart() && c.getStart() < best) { best = c.getStart(); next = c; } }
                if (next.isValid()) clipops::crossfade (proj, h.clip, next, juce::jmin (1.0, h.clip.getLength() * 0.25));
                break;
            }
            case 9:
            {
                double srcBpm = h.clip.getClipBpm();
                if (srcBpm <= 0) { auto src = ProjectModel::findByIdIn (proj.sources(), ids::SOURCE, h.clip.getSourceId()); srcBpm = (double) src.getProperty (ids::bpm, 0.0); }
                if (srcBpm > 0) clipops::matchToProjectBpm (proj, h.clip, srcBpm);
                break;
            }
            case 10: clipops::matchToKey (proj, h.clip, proj.getKeyRoot()); break;
            case 11: um->beginNewTransaction ("Set project BPM"); proj.setBpm (h.clip.getClipBpm()); break;
            case 12: um->beginNewTransaction ("Set project key"); proj.setKey (h.clip.getKeyRoot(), h.clip.getKeyMode()); break;
            case 13:
            {
                auto* w = new juce::AlertWindow ("Rename clip", "", juce::MessageBoxIconType::NoIcon);
                w->addTextEditor ("name", h.clip.getName()); w->addButton ("OK", 1); w->addButton ("Cancel", 0);
                w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, h] (int res) mutable { if (res == 1) { session.getUndoManager().beginNewTransaction ("Rename clip"); h.clip.setName (w->getTextEditorContents ("name"), &session.getUndoManager()); } }), true);
                break;
            }
            case 14:
            {
                auto cache = session.getWaveformCache().get (h.clip.getSourceId());
                auto src = session.getSourceLibrary().get (h.clip.getSourceId());
                if (cache && src && cache->isReady())
                {
                    const double s0 = h.clip.getOffset() * src->sampleRate, s1 = s0 + clipops::beatsToSourceSeconds (h.clip, h.clip.getLength(), bpm) * src->sampleRate;
                    float peak = 0; for (int c = 0; c < src->getNumChannels(); ++c) { auto b = cache->summarise (c, (juce::int64) s0, (juce::int64) s1); peak = juce::jmax (peak, std::abs (b.min), std::abs (b.max)); }
                    if (peak > 1e-5f) { um->beginNewTransaction ("Normalize clip"); h.clip.setGain (juce::Decibels::decibelsToGain (-1.0f) / peak, um); }
                }
                break;
            }
            default:
                if (r > 100 && r <= 116) clipops::repeat (proj, h.clip, r - 100);
                else if (r >= 200 && r <= 207)
                {
                    const double bpb = proj.getTimeSigNumerator() * 4.0 / proj.getTimeSigDenominator();
                    const double divs[] = { 1.0, 2.0, 1.0, 0.5, 0.25, bpb, 2 * bpb, 4 * bpb };
                    // 200: every beat(1/4 note) 201: 1/2 note ... mapping: 1/1 = whole note(4 beats)? use musical values: 1/1=4 beats,1/2=2,1/4=1,1/8=0.5,1/16=0.25
                    const double musical[] = { 4.0, 2.0, 1.0, 0.5, 0.25, bpb, 2 * bpb, 4 * bpb };
                    (void) divs;
                    auto parts = slicer::slice (proj, h.clip, slicer::byDivision (h.clip, musical[r - 200], bpm));
                    view.selectedClips.clear(); for (auto& pc : parts) view.selectedClips.insert (pc.getId()); view.sendChangeMessage();
                }
                else if (r == 210) { auto parts = slicer::slice (proj, h.clip, slicer::atTransients (proj, h.clip)); view.selectedClips.clear(); for (auto& pc : parts) view.selectedClips.insert (pc.getId()); view.sendChangeMessage(); }
                else if (r == 211) { auto parts = slicer::slice (proj, h.clip, slicer::atPhrases (proj, h.clip)); view.selectedClips.clear(); for (auto& pc : parts) view.selectedClips.insert (pc.getId()); view.sendChangeMessage(); }
                else if (r >= 300 && r < 300 + (int) PatternLibrary::all().size())
                {
                    auto parts = PatternLibrary::all()[(size_t) (r - 300)].apply (proj, h.clip);
                    view.selectedClips.clear(); for (auto& pc : parts) if (pc.isValid()) view.selectedClips.insert (pc.getId()); view.sendChangeMessage();
                }
        }
    });
}

void ArrangementCanvas::showEmptyMenu (const juce::MouseEvent& e)
{
    juce::PopupMenu m;
    m.addItem (1, "Add audio track");
    m.addItem (2, "Paste here", clipboard.isValid() && clipboard.getNumChildren() > 0);
    m.addItem (3, "Import audio file...");
    m.addSeparator();
    m.addItem (4, "Set loop to time selection", view.hasTimeSelection);
    const double beat = view.snap (view.xToBeat (e.x), beatsPerBar());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }), [this, beat] (int r)
    {
        auto& p = session.getProject();
        if (r == 1) trackops::addTrack (p, {});
        else if (r == 2) { session.getAudioEngine().locateBeat (beat); pasteAtPlayhead(); }
        else if (r == 3)
        {
            auto chooser = std::make_shared<juce::FileChooser> ("Import audio", juce::File(), FFmpegDecoder::getSupportedWildcards());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems, [this, chooser, beat] (const juce::FileChooser& fc)
            {
                juce::StringArray files; for (auto& f : fc.getResults()) files.add (f.getFullPathName());
                if (! files.isEmpty() && onFilesDropped) onFilesDropped (files, beat, TrackModel());
            });
        }
        else if (r == 4) p.setLoop (view.timeSelStart, view.timeSelEnd, true);
    });
}

// ---- drag & drop -----------------------------------------------------------------------------------------------------
bool ArrangementCanvas::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (FFmpegDecoder::isSupportedExtension (juce::File (f).getFileExtension())) return true;
    return false;
}
void ArrangementCanvas::fileDragMove (const juce::StringArray&, int x, int y) { showDropIndicator = true; dropBeat = view.snap (view.xToBeat (x), beatsPerBar()); dropTrackIndex = trackIndexAtY (y); repaint(); }
void ArrangementCanvas::fileDragExit (const juce::StringArray&) { showDropIndicator = false; repaint(); }
void ArrangementCanvas::filesDropped (const juce::StringArray& files, int x, int y)
{
    showDropIndicator = false; repaint();
    if (onFilesDropped) onFilesDropped (files, view.snap (view.xToBeat (x), beatsPerBar()), trackAtY (y));
}
bool ArrangementCanvas::isInterestedInDragSource (const SourceDetails& d) { return d.description.toString().startsWith ("source:") || d.description.toString().startsWith ("file:"); }
void ArrangementCanvas::itemDragMove (const SourceDetails& d) { fileDragMove ({}, d.localPosition.x, d.localPosition.y); }
void ArrangementCanvas::itemDragExit (const SourceDetails&) { showDropIndicator = false; repaint(); }
void ArrangementCanvas::itemDropped (const SourceDetails& d)
{
    showDropIndicator = false; repaint();
    const auto desc = d.description.toString();
    const double beat = view.snap (view.xToBeat (d.localPosition.x), beatsPerBar());
    auto track = trackAtY (d.localPosition.y);
    if (desc.startsWith ("source:") && onSourceDropped) onSourceDropped (desc.fromFirstOccurrenceOf ("source:", false, false), beat, track);
    else if (desc.startsWith ("file:") && onFilesDropped) onFilesDropped (juce::StringArray (desc.fromFirstOccurrenceOf ("file:", false, false)), beat, track);
}
} // namespace mashup::ui
