#pragma once
#include <vector>
#include <functional>
#include "Clips/ClipModel.h"

namespace mashup
{
class ProjectModel;

/** Beat-grid-synchronous arrangement patterns applied to a clip (cut/mute/repeat/reverse operations). */
struct Pattern
{
    juce::String name, category, description;
    /** Applies to the clip inside one undo transaction; returns the clips that now make up the result. */
    std::function<std::vector<ClipModel> (ProjectModel&, ClipModel)> apply;
};

class PatternLibrary
{
public:
    static const std::vector<Pattern>& all();
    static const Pattern* find (const juce::String& name);
    static std::vector<juce::String> categories();
};
} // namespace mashup
