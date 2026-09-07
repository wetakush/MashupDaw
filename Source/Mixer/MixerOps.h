#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include "Project/ProjectModel.h"

namespace mashup::mixerops
{
/** Adds an EFFECT node (type = built-in type name or "vst3" with pluginId) to an EFFECTS list. */
juce::ValueTree addEffect (ProjectModel&, juce::ValueTree effectsList, const juce::String& type, const juce::String& pluginId = {}, const juce::String& displayName = {});
void removeEffect (ProjectModel&, juce::ValueTree effect);
void moveEffect (ProjectModel&, juce::ValueTree effect, int newIndex);
void setBypass (ProjectModel&, juce::ValueTree effect, bool);
/** Send level (0..1) of a track to bus index 0 (reverb) / 1 (delay). */
void setSend (ProjectModel&, juce::ValueTree track, int bus, double level);
double getSend (const juce::ValueTree& track, int bus);
juce::String effectDisplayName (const juce::ValueTree& effect);
}
