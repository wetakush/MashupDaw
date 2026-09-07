#include "ClipOperations.h"
#include "Project/ProjectModel.h"
#include "Core/MusicalKey.h"
#include "UI/Theme/Theme.h"

namespace mashup
{
namespace clipops
{
double sourceSecondsToBeats (const ClipModel& c, double seconds, double bpm) { return seconds / c.getPlaybackSpeed (bpm) * bpm / 60.0; }
double beatsToSourceSeconds (const ClipModel& c, double beats, double bpm) { return beats * 60.0 / bpm * c.getPlaybackSpeed (bpm); }

ClipModel splitNoTransaction (ProjectModel& p, ClipModel clip, double beat, double bpm)
{
    if (beat <= clip.getStart() + 1e-6 || beat >= clip.getEnd() - 1e-6) return {};
    auto* um = &p.getUndoManager();
    const double leftLen = beat - clip.getStart();
    auto right = clip.getState().createCopy();
    right.setProperty (ids::id, ProjectModel::newId(), nullptr);
    ClipModel r (right);
    r.setStart (beat, nullptr);
    r.setLength (clip.getLength() - leftLen, nullptr);
    const double leftSpan = beatsToSourceSeconds (clip, leftLen, bpm);
    if (! clip.isReversed())
        r.setOffset (clip.getOffset() + leftSpan, nullptr);
    else
    {
        // reversed: timeline-left part reads the later part of the source region [offset, sourceEnd]
        const double regionEnd = (double) clip.getState().getProperty (ids::sourceEnd, 0.0);
        const double cut = regionEnd - leftSpan;
        right.setProperty (ids::sourceEnd, cut, nullptr);
        clip.setOffset (cut, um);
    }
    r.setFadeIn (0.0, nullptr); clip.setFadeOut (0.0, um);
    clip.setLength (leftLen, um);
    auto parent = clip.getState().getParent();
    parent.addChild (right, parent.indexOf (clip.getState()) + 1, um);
    return r;
}

ClipModel split (ProjectModel& p, ClipModel clip, double beat, double bpm)
{
    p.getUndoManager().beginNewTransaction ("Split clip");
    return splitNoTransaction (p, clip, beat, bpm);
}

void trimStart (ProjectModel& p, ClipModel clip, double beat, double bpm)
{
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Trim clip");
    beat = juce::jlimit (0.0, clip.getEnd() - 1e-3, beat);
    const double delta = beat - clip.getStart();
    if (! clip.isReversed()) clip.setOffset (clip.getOffset() + beatsToSourceSeconds (clip, delta, bpm), um);
    else clip.getState().setProperty (ids::sourceEnd, (double) clip.getState().getProperty (ids::sourceEnd, 0.0) - beatsToSourceSeconds (clip, delta, bpm), um);
    clip.setLength (clip.getLength() - delta, um);
    clip.setStart (beat, um);
    clip.setFadeIn (juce::jmin (clip.getFadeIn(), clip.getLength()), um);
}

void trimEnd (ProjectModel& p, ClipModel clip, double beat)
{
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Trim clip");
    clip.setLength (juce::jmax (1e-3, beat - clip.getStart()), um);
    clip.setFadeOut (juce::jmin (clip.getFadeOut(), clip.getLength()), um);
}

ClipModel duplicate (ProjectModel& p, ClipModel clip, double atBeat, TrackModel target)
{
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Duplicate clip");
    auto copy = clip.getState().createCopy();
    copy.setProperty (ids::id, ProjectModel::newId(), nullptr);
    ClipModel c (copy); c.setStart (atBeat, nullptr);
    (target.isValid() ? target : trackops::trackOfClip (p, clip)).clips().appendChild (copy, um);
    return c;
}

void remove (ProjectModel& p, ClipModel clip)
{
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Delete clip");
    clip.getState().getParent().removeChild (clip.getState(), um);
}

void move (ProjectModel& p, ClipModel clip, double newStart, TrackModel newTrack)
{
    auto* um = &p.getUndoManager();
    auto cur = clip.getState().getParent();
    clip.setStart (newStart, um);
    if (newTrack.isValid() && newTrack.clips() != cur)
    {
        auto st = clip.getState();
        cur.removeChild (st, um);
        newTrack.clips().appendChild (st, um);
    }
}

void deleteRange (ProjectModel& p, ClipModel clip, double s, double e, double bpm)
{
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Delete range");
    if (e <= clip.getStart() || s >= clip.getEnd()) return;
    if (s <= clip.getStart() && e >= clip.getEnd()) { clip.getState().getParent().removeChild (clip.getState(), um); return; }
    if (s > clip.getStart() && e < clip.getEnd())
    {
        auto right = splitNoTransaction (p, clip, e, bpm);
        clip.setLength (s - clip.getStart(), um);
        return;
    }
    if (s <= clip.getStart()) { const double d = e - clip.getStart(); if (! clip.isReversed()) clip.setOffset (clip.getOffset() + beatsToSourceSeconds (clip, d, bpm), um); clip.setLength (clip.getLength() - d, um); clip.setStart (e, um); }
    else clip.setLength (s - clip.getStart(), um);
}

void cropToRange (ProjectModel& p, ClipModel clip, double s, double e, double bpm)
{
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Crop clip");
    s = juce::jmax (s, clip.getStart()); e = juce::jmin (e, clip.getEnd());
    if (e <= s) return;
    const double d = s - clip.getStart();
    if (! clip.isReversed()) clip.setOffset (clip.getOffset() + beatsToSourceSeconds (clip, d, bpm), um);
    else clip.setOffset (clip.getOffset() + beatsToSourceSeconds (clip, clip.getEnd() - e, bpm), um);
    clip.setStart (s, um); clip.setLength (e - s, um);
}

void setReversed (ProjectModel& p, ClipModel clip, bool rev, double bpm)
{
    if (rev == clip.isReversed()) return;
    auto* um = &p.getUndoManager(); um->beginNewTransaction (rev ? "Reverse clip" : "Un-reverse clip");
    // keep the same source region: reversed offset marks the start of the region read backwards from its end
    clip.setReversed (rev, um);
    clip.getState().setProperty (ids::sourceEnd, clip.getOffset() + beatsToSourceSeconds (clip, clip.getLength(), bpm), um);
}

void crossfade (ProjectModel& p, ClipModel left, ClipModel right, double beats)
{
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Crossfade");
    const double overlap = left.getEnd() - right.getStart();
    if (overlap < beats) { left.setLength (left.getLength() + (beats - juce::jmax (0.0, overlap)), um); }
    left.setFadeOut (beats, um); right.setFadeIn (beats, um);
    left.setFadeShapes (left.getFadeInShape(), FadeShape::EqualPower, um);
    right.setFadeShapes (FadeShape::EqualPower, right.getFadeOutShape(), um);
}

std::vector<ClipModel> repeat (ProjectModel& p, ClipModel clip, int times)
{
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Repeat clip");
    std::vector<ClipModel> out { clip };
    auto parent = clip.getState().getParent();
    for (int i = 1; i <= times; ++i)
    {
        auto copy = clip.getState().createCopy();
        copy.setProperty (ids::id, ProjectModel::newId(), nullptr);
        ClipModel c (copy); c.setStart (clip.getStart() + i * clip.getLength(), nullptr);
        parent.appendChild (copy, um); out.push_back (c);
    }
    return out;
}

std::vector<ClipModel> sliceAt (ProjectModel& p, ClipModel clip, const std::vector<double>& beats, double bpm)
{
    p.getUndoManager().beginNewTransaction ("Slice clip");
    std::vector<ClipModel> out { clip };
    ClipModel cur = clip;
    for (double b : beats)
    {
        if (b <= cur.getStart() + 1e-4 || b >= cur.getEnd() - 1e-4) continue;
        auto r = splitNoTransaction (p, cur, b, bpm);
        if (! r.isValid()) continue;
        out.push_back (r); cur = r;
    }
    return out;
}

void matchToProjectBpm (ProjectModel& p, ClipModel clip, double sourceBpm)
{
    if (sourceBpm <= 0) return;
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Match clip to project BPM");
    const double bpm = p.getBpm();
    // preserve the source region currently covered by the clip
    const double srcSpan = beatsToSourceSeconds (clip, clip.getLength(), bpm);
    clip.setClipBpm (sourceBpm, um);
    clip.setSyncedToProject (true, um);
    if (clip.getStretchMode() == StretchMode::Repitch) clip.setStretchMode (StretchMode::Realtime, um);
    clip.setLength (sourceSecondsToBeats (clip, srcSpan, bpm), um);
}

void matchToKey (ProjectModel& p, ClipModel clip, int targetRoot)
{
    if (clip.getKeyRoot() < 0) return;
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Match clip key");
    clip.setPitch (key::shortestShift (clip.getKeyRoot(), targetRoot), 0, um);
}
} // namespace clipops

namespace trackops
{
TrackModel addTrack (ProjectModel& p, const juce::String& name, int index)
{
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Add track");
    auto tracks = p.tracks();
    auto t = TrackModel::create (name.isNotEmpty() ? name : "Track " + juce::String (tracks.getNumChildren() + 1), ui::colours::trackColour (tracks.getNumChildren()));
    tracks.addChild (t.getState(), index, um);
    return t;
}
void removeTrack (ProjectModel& p, TrackModel t) { auto* um = &p.getUndoManager(); um->beginNewTransaction ("Delete track"); p.tracks().removeChild (t.getState(), um); }
void moveTrack (ProjectModel& p, TrackModel t, int newIndex) { auto* um = &p.getUndoManager(); um->beginNewTransaction ("Move track"); p.tracks().moveChild (p.tracks().indexOf (t.getState()), newIndex, um); }
TrackModel duplicateTrack (ProjectModel& p, TrackModel t)
{
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Duplicate track");
    auto copy = t.getState().createCopy();
    copy.setProperty (ids::id, ProjectModel::newId(), nullptr);
    copy.setProperty (ids::name, t.getName() + " copy", nullptr);
    for (auto c : copy.getChildWithName (ids::CLIPS)) c.setProperty (ids::id, ProjectModel::newId(), nullptr);
    for (auto c : copy.getChildWithName (ids::EFFECTS)) c.setProperty (ids::id, ProjectModel::newId(), nullptr);
    p.tracks().addChild (copy, p.tracks().indexOf (t.getState()) + 1, um);
    return TrackModel (copy);
}
int indexOf (ProjectModel& p, const TrackModel& t) { return p.tracks().indexOf (t.getState()); }
TrackModel trackOfClip (ProjectModel&, const ClipModel& c) { return TrackModel (c.getTrackState()); }

ClipModel placeSource (ProjectModel& p, const juce::String& sourceId, const juce::String& name, double sourceSeconds, double startBeat, TrackModel track, double sourceBpm)
{
    auto* um = &p.getUndoManager();
    if (! track.isValid()) track = addTrack (p, name);
    const double bpm = p.getBpm();
    auto clip = ClipModel::create (sourceId, startBeat, sourceSeconds * bpm / 60.0, 0.0, name);
    if (sourceBpm > 0) { clip.setClipBpm (sourceBpm, nullptr); clip.setSyncedToProject (true, nullptr); clip.setLength (sourceSeconds * sourceBpm / 60.0, nullptr); }
    clip.setColour (track.getColour().darker (0.35f), nullptr);
    track.clips().appendChild (clip.getState(), um);
    return clip;
}
} // namespace trackops
} // namespace mashup
