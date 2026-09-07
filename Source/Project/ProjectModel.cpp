#include "ProjectModel.h"

namespace mashup
{
ProjectModel::ProjectModel (juce::UndoManager& um) : undo (um) { createDefault(); }

void ProjectModel::createDefault (const juce::String& name)
{
    root = juce::ValueTree (ids::PROJECT);
    root.setProperty (ids::version, 1, nullptr);
    root.setProperty (ids::name, name, nullptr);
    root.setProperty (ids::bpm, 120.0, nullptr);
    root.setProperty (ids::timeSigNum, 4, nullptr);
    root.setProperty (ids::timeSigDen, 4, nullptr);
    root.setProperty (ids::keyRoot, 0, nullptr);
    root.setProperty (ids::keyMode, 1, nullptr);
    root.setProperty (ids::sampleRate, 48000.0, nullptr);
    ensureSections();
    undo.clearUndoHistory();
}

void ProjectModel::replaceWith (const juce::ValueTree& newRoot)
{
    jassert (newRoot.hasType (ids::PROJECT));
    root = newRoot;
    ensureSections();
    undo.clearUndoHistory();
}

void ProjectModel::ensureSections()
{
    for (auto* t : { &ids::TEMPOMAP, &ids::MARKERS, &ids::SOURCES, &ids::TRACKS, &ids::MASTER, &ids::BUSES })
        if (! root.getChildWithName (*t).isValid())
            root.appendChild (juce::ValueTree (*t), nullptr);

    auto master = root.getChildWithName (ids::MASTER);
    if (! master.hasProperty (ids::volume)) master.setProperty (ids::volume, 1.0, nullptr);
    if (! master.getChildWithName (ids::EFFECTS).isValid()) master.appendChild (juce::ValueTree (ids::EFFECTS), nullptr);

    auto buses = root.getChildWithName (ids::BUSES);
    if (buses.getNumChildren() == 0)
    {
        for (auto* n : { "Reverb", "Delay" })
        {
            juce::ValueTree b (ids::BUS);
            b.setProperty (ids::id, newId(), nullptr);
            b.setProperty (ids::name, n, nullptr);
            b.setProperty (ids::type, juce::String (n).toLowerCase(), nullptr);
            b.setProperty (ids::volume, 1.0, nullptr);
            b.appendChild (juce::ValueTree (ids::EFFECTS), nullptr);
            buses.appendChild (b, nullptr);
        }
    }
}

juce::String ProjectModel::newId() { return juce::Uuid().toDashedString().substring (0, 8) + juce::String::toHexString ((int) (juce::Time::getHighResolutionTicks() & 0xffff)); }

juce::ValueTree ProjectModel::findById (const juce::Identifier& type, const juce::String& id) const { return findByIdIn (root, type, id); }

juce::ValueTree ProjectModel::findByIdIn (const juce::ValueTree& parent, const juce::Identifier& type, const juce::String& id)
{
    if (parent.hasType (type) && parent[ids::id].toString() == id) return parent;
    for (const auto& c : parent)
    {
        auto r = findByIdIn (c, type, id);
        if (r.isValid()) return r;
    }
    return {};
}
} // namespace mashup
