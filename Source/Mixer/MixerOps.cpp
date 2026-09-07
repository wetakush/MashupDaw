#include "MixerOps.h"

namespace mashup::mixerops
{
juce::ValueTree addEffect (ProjectModel& p, juce::ValueTree list, const juce::String& type, const juce::String& pluginId, const juce::String& name)
{
    auto* um = &p.getUndoManager(); um->beginNewTransaction ("Add effect");
    juce::ValueTree e (ids::EFFECT);
    e.setProperty (ids::id, ProjectModel::newId(), nullptr);
    e.setProperty (ids::type, type, nullptr);
    if (pluginId.isNotEmpty()) e.setProperty (ids::pluginId, pluginId, nullptr);
    e.setProperty (ids::name, name.isNotEmpty() ? name : type, nullptr);
    e.setProperty (ids::bypass, false, nullptr);
    list.appendChild (e, um);
    return e;
}
void removeEffect (ProjectModel& p, juce::ValueTree e) { auto* um = &p.getUndoManager(); um->beginNewTransaction ("Remove effect"); e.getParent().removeChild (e, um); }
void moveEffect (ProjectModel& p, juce::ValueTree e, int idx) { auto* um = &p.getUndoManager(); um->beginNewTransaction ("Move effect"); auto parent = e.getParent(); parent.moveChild (parent.indexOf (e), idx, um); }
void setBypass (ProjectModel& p, juce::ValueTree e, bool b) { auto* um = &p.getUndoManager(); um->beginNewTransaction ("Bypass effect"); e.setProperty (ids::bypass, b, um); }

void setSend (ProjectModel& p, juce::ValueTree track, int bus, double level)
{
    auto* um = &p.getUndoManager();
    auto sends = track.getOrCreateChildWithName (ids::SENDS, um);
    for (auto s : sends) if ((int) s[ids::bus] == bus) { s.setProperty (ids::level, level, um); return; }
    juce::ValueTree s (ids::SEND); s.setProperty (ids::bus, bus, nullptr); s.setProperty (ids::level, level, nullptr); sends.appendChild (s, um);
}
double getSend (const juce::ValueTree& track, int bus)
{
    for (const auto& s : track.getChildWithName (ids::SENDS)) if ((int) s[ids::bus] == bus) return (double) s[ids::level];
    return 0.0;
}
juce::String effectDisplayName (const juce::ValueTree& e) { return e.getProperty (ids::name, e[ids::type]).toString(); }
}
