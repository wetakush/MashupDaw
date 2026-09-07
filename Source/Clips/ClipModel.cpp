#include "ClipModel.h"
#include "Project/ProjectModel.h"

namespace mashup
{
ClipModel ClipModel::create (const juce::String& sourceId, double startBeat, double lengthBeats, double offsetSeconds, const juce::String& name)
{
    juce::ValueTree v (ids::CLIP);
    v.setProperty (ids::id, ProjectModel::newId(), nullptr);
    v.setProperty (ids::sourceId, sourceId, nullptr);
    v.setProperty (ids::name, name, nullptr);
    v.setProperty (ids::start, startBeat, nullptr);
    v.setProperty (ids::length, lengthBeats, nullptr);
    v.setProperty (ids::offset, offsetSeconds, nullptr);
    v.setProperty (ids::gain, 1.0, nullptr);
    v.setProperty (ids::stretchMode, (int) StretchMode::Realtime, nullptr);
    return ClipModel (v);
}
}
