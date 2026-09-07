#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include "Core/Identifiers.h"
#include "Clips/ClipModel.h"

namespace mashup
{
/** Typed facade over a TRACK ValueTree. */
class TrackModel
{
public:
    TrackModel() = default;
    explicit TrackModel (juce::ValueTree v) : state (std::move (v)) {}
    static TrackModel create (const juce::String& name, juce::Colour colour);

    bool isValid() const { return state.hasType (ids::TRACK); }
    juce::ValueTree getState() const { return state; }
    juce::String getId() const { return state[ids::id]; }
    juce::String getName() const { return state.getProperty (ids::name, "Track"); }
    juce::Colour getColour() const { return juce::Colour::fromString (state.getProperty (ids::color, "ff1ec8aa").toString()); }
    double getVolume() const { return (double) state.getProperty (ids::volume, 1.0); }   // linear gain
    double getPan() const    { return (double) state.getProperty (ids::pan, 0.0); }
    bool isMuted() const     { return (bool) state.getProperty (ids::mute, false); }
    bool isSolo() const      { return (bool) state.getProperty (ids::solo, false); }
    bool isArmed() const     { return (bool) state.getProperty (ids::arm, false); }
    bool isPhaseInverted() const { return (bool) state.getProperty (ids::phaseInvert, false); }
    double getInputGain() const  { return (double) state.getProperty (ids::inputGain, 1.0); }
    int getHeight() const    { return (int) state.getProperty (ids::height, 80); }
    static constexpr int automationLaneHeight = 56;
    bool isAutomationShown() const { return (bool) state.getProperty ("showAutomation", false); }
    juce::String getAutomationParam() const { return state.getProperty ("automationParam", "volume").toString(); }
    int getAutomationMode() const { return (int) state.getProperty ("automationMode", 0); }
    /** Height of the clip lane plus the automation lane when shown. */
    int getTotalHeight() const { return getHeight() + (isAutomationShown() ? automationLaneHeight : 0); }
    void setAutomationShown (bool b, juce::UndoManager* um) { state.setProperty ("showAutomation", b, um); }
    void setAutomationParam (const juce::String& p, juce::UndoManager* um) { state.setProperty ("automationParam", p, um); }
    void setAutomationMode (int m, juce::UndoManager* um) { state.setProperty ("automationMode", m, um); }
    juce::String getStemType() const { return state.getProperty (ids::stemType, "").toString(); }

    void setName (const juce::String& n, juce::UndoManager* um) { state.setProperty (ids::name, n, um); }
    void setColour (juce::Colour c, juce::UndoManager* um) { state.setProperty (ids::color, c.toString(), um); }
    void setVolume (double v, juce::UndoManager* um) { state.setProperty (ids::volume, juce::jlimit (0.0, 4.0, v), um); }
    void setPan (double p, juce::UndoManager* um)    { state.setProperty (ids::pan, juce::jlimit (-1.0, 1.0, p), um); }
    void setMuted (bool m, juce::UndoManager* um)    { state.setProperty (ids::mute, m, um); }
    void setSolo (bool s, juce::UndoManager* um)     { state.setProperty (ids::solo, s, um); }
    void setArmed (bool a, juce::UndoManager* um)    { state.setProperty (ids::arm, a, um); }
    void setPhaseInverted (bool p, juce::UndoManager* um) { state.setProperty (ids::phaseInvert, p, um); }
    void setInputGain (double g, juce::UndoManager* um) { state.setProperty (ids::inputGain, g, um); }
    void setHeight (int h, juce::UndoManager* um)    { state.setProperty (ids::height, juce::jlimit (32, 400, h), um); }

    juce::ValueTree clips() const      { return state.getChildWithName (ids::CLIPS); }
    juce::ValueTree effects() const    { return state.getChildWithName (ids::EFFECTS); }
    juce::ValueTree sends() const      { return state.getChildWithName (ids::SENDS); }
    juce::ValueTree automation() const { return state.getChildWithName (ids::AUTOMATION); }
    int getNumClips() const { return clips().getNumChildren(); }
    ClipModel getClip (int i) const { return ClipModel (clips().getChild (i)); }

    /** Adds a clip and returns it. */
    ClipModel addClip (ClipModel clip, juce::UndoManager* um) { clips().appendChild (clip.getState(), um); return clip; }

    /** End of the last clip in beats (0 if empty). */
    double getEndBeat() const;

private:
    juce::ValueTree state;
};
} // namespace mashup
