#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include "Core/Identifiers.h"

namespace mashup
{
/** How a clip's audio is fitted to the timeline. */
enum class StretchMode { Repitch = 0, Realtime, HighQuality, Vocal, Percussive };
inline const char* stretchModeName (StretchMode m)
{
    switch (m) { case StretchMode::Repitch: return "Repitch"; case StretchMode::Realtime: return "Realtime";
                 case StretchMode::HighQuality: return "High Quality"; case StretchMode::Vocal: return "Vocal"; case StretchMode::Percussive: return "Percussive"; }
    return "?";
}
enum class FadeShape { Linear = 0, EqualPower, Exponential, SCurve };

/** Typed facade over a CLIP ValueTree. Positions/lengths are in beats (quarter notes), source offset in seconds. */
class ClipModel
{
public:
    ClipModel() = default;
    explicit ClipModel (juce::ValueTree v) : state (std::move (v)) {}
    static ClipModel create (const juce::String& sourceId, double startBeat, double lengthBeats, double offsetSeconds, const juce::String& name);

    bool isValid() const { return state.hasType (ids::CLIP); }
    juce::ValueTree getState() const { return state; }
    juce::String getId() const { return state[ids::id]; }
    juce::String getSourceId() const { return state[ids::sourceId]; }
    juce::String getName() const { return state.getProperty (ids::name, "clip"); }

    double getStart() const   { return (double) state.getProperty (ids::start, 0.0); }
    double getLength() const  { return (double) state.getProperty (ids::length, 4.0); }
    double getEnd() const     { return getStart() + getLength(); }
    double getOffset() const  { return (double) state.getProperty (ids::offset, 0.0); }        // seconds into the source
    double getGain() const    { return (double) state.getProperty (ids::gain, 1.0); }
    double getPan() const     { return (double) state.getProperty (ids::pan, 0.0); }
    bool   isMuted() const    { return (bool) state.getProperty (ids::mute, false); }
    bool   isReversed() const { return (bool) state.getProperty (ids::reverse, false); }
    bool   isLooped() const   { return (bool) state.getProperty (ids::loop, false); }
    double getFadeIn() const  { return (double) state.getProperty (ids::fadeIn, 0.0); }        // beats
    double getFadeOut() const { return (double) state.getProperty (ids::fadeOut, 0.0); }
    FadeShape getFadeInShape() const  { return (FadeShape) (int) state.getProperty (ids::fadeInShape, 0); }
    FadeShape getFadeOutShape() const { return (FadeShape) (int) state.getProperty (ids::fadeOutShape, 0); }
    int    getPitchSemis() const { return (int) state.getProperty (ids::pitchSemis, 0); }
    int    getPitchCents() const { return (int) state.getProperty (ids::pitchCents, 0); }
    double getFormant() const    { return (double) state.getProperty (ids::formant, 0.0); }   // semitones of formant shift
    double getPitchScale() const { return std::pow (2.0, (getPitchSemis() + getPitchCents() / 100.0) / 12.0); }
    StretchMode getStretchMode() const { return (StretchMode) (int) state.getProperty (ids::stretchMode, (int) StretchMode::Realtime); }
    double getRate() const       { return (double) state.getProperty (ids::rate, 1.0); }      // playback speed multiplier
    double getClipBpm() const    { return (double) state.getProperty (ids::clipBpm, 0.0); }   // 0 = unknown
    bool   isSyncedToProject() const { return (bool) state.getProperty (ids::syncToProject, false) && getClipBpm() > 0.0; }
    int    getKeyRoot() const    { return (int) state.getProperty (ids::keyRoot, -1); }
    int    getKeyMode() const    { return (int) state.getProperty (ids::keyMode, 0); }
    juce::Colour getColour() const { return juce::Colour::fromString (state.getProperty (ids::color, "ff2c4c48").toString()); }

    void setName (const juce::String& n, juce::UndoManager* um) { state.setProperty (ids::name, n, um); }
    void setStart (double b, juce::UndoManager* um)  { state.setProperty (ids::start, juce::jmax (0.0, b), um); }
    void setLength (double b, juce::UndoManager* um) { state.setProperty (ids::length, juce::jmax (1.0e-4, b), um); }
    void setOffset (double s, juce::UndoManager* um) { state.setProperty (ids::offset, juce::jmax (0.0, s), um); }
    void setGain (double g, juce::UndoManager* um)   { state.setProperty (ids::gain, g, um); }
    void setPan (double p, juce::UndoManager* um)    { state.setProperty (ids::pan, juce::jlimit (-1.0, 1.0, p), um); }
    void setMuted (bool m, juce::UndoManager* um)    { state.setProperty (ids::mute, m, um); }
    void setReversed (bool r, juce::UndoManager* um) { state.setProperty (ids::reverse, r, um); }
    void setLooped (bool l, juce::UndoManager* um)   { state.setProperty (ids::loop, l, um); }
    void setFadeIn (double b, juce::UndoManager* um) { state.setProperty (ids::fadeIn, juce::jlimit (0.0, getLength(), b), um); }
    void setFadeOut (double b, juce::UndoManager* um){ state.setProperty (ids::fadeOut, juce::jlimit (0.0, getLength(), b), um); }
    void setFadeShapes (FadeShape in, FadeShape out, juce::UndoManager* um) { state.setProperty (ids::fadeInShape, (int) in, um); state.setProperty (ids::fadeOutShape, (int) out, um); }
    void setPitch (int semis, int cents, juce::UndoManager* um) { state.setProperty (ids::pitchSemis, juce::jlimit (-48, 48, semis), um); state.setProperty (ids::pitchCents, juce::jlimit (-100, 100, cents), um); }
    void setFormant (double f, juce::UndoManager* um) { state.setProperty (ids::formant, f, um); }
    void setStretchMode (StretchMode m, juce::UndoManager* um) { state.setProperty (ids::stretchMode, (int) m, um); }
    void setRate (double r, juce::UndoManager* um)   { state.setProperty (ids::rate, juce::jlimit (0.1, 8.0, r), um); }
    void setClipBpm (double b, juce::UndoManager* um){ state.setProperty (ids::clipBpm, b, um); }
    void setSyncedToProject (bool s, juce::UndoManager* um) { state.setProperty (ids::syncToProject, s, um); }
    void setKey (int root, int mode, juce::UndoManager* um) { state.setProperty (ids::keyRoot, root, um); state.setProperty (ids::keyMode, mode, um); }
    void setColour (juce::Colour c, juce::UndoManager* um) { state.setProperty (ids::color, c.toString(), um); }

    /** Speed of source playback relative to output time at the given project bpm. */
    double getPlaybackSpeed (double projectBpm) const
    {
        double speed = isSyncedToProject() ? projectBpm / getClipBpm() : getRate();
        if (getStretchMode() == StretchMode::Repitch) speed *= getPitchScale();
        return speed;
    }
    /** Seconds of source audio consumed by the clip at a constant project bpm. */
    double getSourceSpan (double projectBpm) const { return getLength() * 60.0 / projectBpm * getPlaybackSpeed (projectBpm); }

    juce::ValueTree getTrackState() const { return state.getParent().getParent(); }

private:
    juce::ValueTree state;
};
} // namespace mashup
