#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include "Core/Identifiers.h"

namespace mashup::analysis
{
/** Compact storage of time lists (beats, downbeats, transients) in a SOURCE node child as base64 doubles. */
inline void writeTimes (juce::ValueTree sourceNode, const juce::Identifier& childType, const std::vector<double>& times, juce::UndoManager* um)
{
    auto child = sourceNode.getOrCreateChildWithName (childType, um);
    juce::MemoryBlock mb (times.data(), times.size() * sizeof (double));
    child.setProperty (ids::data, mb.toBase64Encoding(), um);
}

inline std::vector<double> readTimes (const juce::ValueTree& sourceNode, const juce::Identifier& childType)
{
    std::vector<double> out;
    auto child = sourceNode.getChildWithName (childType);
    if (! child.isValid()) return out;
    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (child[ids::data].toString())) return out;
    out.resize (mb.getSize() / sizeof (double));
    std::memcpy (out.data(), mb.getData(), out.size() * sizeof (double));
    return out;
}

struct Phrase { double start = 0, end = 0; juce::String label; float confidence = 0; };

inline void writePhrases (juce::ValueTree sourceNode, const std::vector<Phrase>& phrases, juce::UndoManager* um)
{
    auto child = sourceNode.getOrCreateChildWithName (ids::PHRASES, um);
    child.removeAllChildren (um);
    for (const auto& p : phrases)
    {
        juce::ValueTree ph (ids::PHRASE);
        ph.setProperty (ids::start, p.start, nullptr); ph.setProperty (ids::end, p.end, nullptr);
        ph.setProperty (ids::label, p.label, nullptr); ph.setProperty (ids::confidence, p.confidence, nullptr);
        child.appendChild (ph, um);
    }
}

inline std::vector<Phrase> readPhrases (const juce::ValueTree& sourceNode)
{
    std::vector<Phrase> out;
    for (const auto& ph : sourceNode.getChildWithName (ids::PHRASES))
        out.push_back ({ (double) ph[ids::start], (double) ph[ids::end], ph[ids::label].toString(), (float) ph[ids::confidence] });
    return out;
}
} // namespace mashup::analysis
