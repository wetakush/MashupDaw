#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "Core/Identifiers.h"

namespace mashup
{
/** Thin typed facade over the root PROJECT ValueTree. All mutation goes through the UndoManager. */
class ProjectModel
{
public:
    explicit ProjectModel (juce::UndoManager& um);

    void createDefault (const juce::String& name = "Untitled");
    void replaceWith (const juce::ValueTree& newRoot);

    /** Returns a reference to the long-lived root so that ValueTree listeners registered through it stay alive
        (JUCE listeners belong to the ValueTree wrapper object). replaceWith() redirects them to the new tree. */
    juce::ValueTree& getRoot() noexcept { return root; }
    juce::ValueTree getRoot() const noexcept { return root; }
    juce::UndoManager& getUndoManager() noexcept { return undo; }

    // top level sections (created on demand)
    juce::ValueTree tracks() const   { return root.getChildWithName (ids::TRACKS); }
    juce::ValueTree sources() const  { return root.getChildWithName (ids::SOURCES); }
    juce::ValueTree markers() const  { return root.getChildWithName (ids::MARKERS); }
    juce::ValueTree tempoMap() const { return root.getChildWithName (ids::TEMPOMAP); }
    juce::ValueTree master() const   { return root.getChildWithName (ids::MASTER); }
    juce::ValueTree buses() const    { return root.getChildWithName (ids::BUSES); }

    // project-wide properties
    double getBpm() const               { return (double) root.getProperty (ids::bpm, 120.0); }
    void   setBpm (double bpm)          { root.setProperty (ids::bpm, juce::jlimit (20.0, 400.0, bpm), &undo); }
    int    getTimeSigNumerator() const  { return (int) root.getProperty (ids::timeSigNum, 4); }
    int    getTimeSigDenominator() const{ return (int) root.getProperty (ids::timeSigDen, 4); }
    void   setTimeSignature (int num, int den) { root.setProperty (ids::timeSigNum, num, &undo); root.setProperty (ids::timeSigDen, den, &undo); }
    int    getKeyRoot() const           { return (int) root.getProperty (ids::keyRoot, 0); }   // 0..11, C=0
    int    getKeyMode() const           { return (int) root.getProperty (ids::keyMode, 0); }   // 0 = major, 1 = minor
    void   setKey (int root_, int mode) { root.setProperty (ids::keyRoot, root_, &undo); root.setProperty (ids::keyMode, mode, &undo); }
    double getSampleRate() const        { return (double) root.getProperty (ids::sampleRate, 48000.0); }
    void   setSampleRate (double sr)    { root.setProperty (ids::sampleRate, sr, nullptr); }
    juce::String getName() const        { return root.getProperty (ids::name, "Untitled"); }
    void   setName (const juce::String& n) { root.setProperty (ids::name, n, nullptr); }

    bool   isLoopEnabled() const        { return (bool) root.getProperty (ids::loopEnabled, false); }
    double getLoopStart() const         { return (double) root.getProperty (ids::loopStart, 0.0); }
    double getLoopEnd() const           { return (double) root.getProperty (ids::loopEnd, 0.0); }
    void   setLoop (double start, double end, bool enabled)
    {
        root.setProperty (ids::loopStart, start, nullptr); root.setProperty (ids::loopEnd, end, nullptr);
        root.setProperty (ids::loopEnabled, enabled, nullptr);
    }

    /** Generates a new unique id string for tracks/clips/sources. */
    static juce::String newId();

    juce::ValueTree findById (const juce::Identifier& type, const juce::String& id) const;
    static juce::ValueTree findByIdIn (const juce::ValueTree& parent, const juce::Identifier& type, const juce::String& id);

private:
    void ensureSections();
    juce::UndoManager& undo;
    juce::ValueTree root;
};
} // namespace mashup
