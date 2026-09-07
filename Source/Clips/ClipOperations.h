#pragma once
#include "ClipModel.h"
#include "Tracks/TrackModel.h"

namespace mashup
{
class ProjectModel;

/** Undoable editing operations on clips. Every function starts its own undo transaction unless noted. */
namespace clipops
{
    /** Splits at an absolute beat; returns the right-hand clip (invalid if beat is outside). */
    ClipModel split (ProjectModel&, ClipModel clip, double beat, double projectBpm);
    /** Trims clip start to `beat` keeping audio in place. */
    void trimStart (ProjectModel&, ClipModel clip, double beat, double projectBpm);
    void trimEnd (ProjectModel&, ClipModel clip, double beat);
    ClipModel duplicate (ProjectModel&, ClipModel clip, double atBeat, TrackModel targetTrack);
    void remove (ProjectModel&, ClipModel clip);
    void move (ProjectModel&, ClipModel clip, double newStart, TrackModel newTrack);
    /** Cuts out [startBeat,endBeat) from the clip (may produce two clips). */
    void deleteRange (ProjectModel&, ClipModel clip, double startBeat, double endBeat, double projectBpm);
    /** Keeps only [startBeat,endBeat). */
    void cropToRange (ProjectModel&, ClipModel clip, double startBeat, double endBeat, double projectBpm);
    void setReversed (ProjectModel&, ClipModel clip, bool reversed, double projectBpm);
    /** Makes overlapping neighbours crossfade over `beats` by adding fades. */
    void crossfade (ProjectModel&, ClipModel left, ClipModel right, double beats);
    /** Repeats the clip N times back-to-back. */
    std::vector<ClipModel> repeat (ProjectModel&, ClipModel clip, int times);
    /** Cuts a clip into slices at the given absolute beats (sorted). Returns all resulting clips in order. */
    std::vector<ClipModel> sliceAt (ProjectModel&, ClipModel clip, const std::vector<double>& beats, double projectBpm);
    /** Sets clipBpm+sync so that the clip plays in time with the project (stretch). */
    void matchToProjectBpm (ProjectModel&, ClipModel clip, double sourceBpm);
    /** Sets pitch so that the clip's key matches the target key (semitones -6..+6). */
    void matchToKey (ProjectModel&, ClipModel clip, int targetRoot);

    /** Beats occupied by `seconds` of source audio when played by `clip` at `projectBpm`. */
    double sourceSecondsToBeats (const ClipModel& clip, double seconds, double projectBpm);
    double beatsToSourceSeconds (const ClipModel& clip, double beats, double projectBpm);

    /** Non-transactional helpers used by compound operations. */
    ClipModel splitNoTransaction (ProjectModel&, ClipModel clip, double beat, double projectBpm);
}

namespace trackops
{
    TrackModel addTrack (ProjectModel&, const juce::String& name, int insertIndex = -1);
    void removeTrack (ProjectModel&, TrackModel);
    void moveTrack (ProjectModel&, TrackModel, int newIndex);
    TrackModel duplicateTrack (ProjectModel&, TrackModel);
    /** Imports a source onto a (new or given) track at `startBeat`; clip length derived from the source. */
    ClipModel placeSource (ProjectModel&, const juce::String& sourceId, const juce::String& sourceName, double sourceSeconds,
                           double startBeat, TrackModel track, double sourceBpm = 0.0);
    int indexOf (ProjectModel&, const TrackModel&);
    TrackModel trackOfClip (ProjectModel&, const ClipModel&);
}
} // namespace mashup
