#include "TrackModel.h"
#include "Project/ProjectModel.h"

namespace mashup
{
TrackModel TrackModel::create (const juce::String& name, juce::Colour colour)
{
    juce::ValueTree v (ids::TRACK);
    v.setProperty (ids::id, ProjectModel::newId(), nullptr);
    v.setProperty (ids::name, name, nullptr);
    v.setProperty (ids::color, colour.toString(), nullptr);
    v.setProperty (ids::volume, 1.0, nullptr);
    v.setProperty (ids::pan, 0.0, nullptr);
    v.setProperty (ids::height, 80, nullptr);
    v.appendChild (juce::ValueTree (ids::CLIPS), nullptr);
    v.appendChild (juce::ValueTree (ids::EFFECTS), nullptr);
    juce::ValueTree sends (ids::SENDS);
    v.appendChild (sends, nullptr);
    v.appendChild (juce::ValueTree (ids::AUTOMATION), nullptr);
    return TrackModel (v);
}

double TrackModel::getEndBeat() const
{
    double e = 0.0;
    for (const auto& c : clips()) e = juce::jmax (e, ClipModel (c).getEnd());
    return e;
}
}
