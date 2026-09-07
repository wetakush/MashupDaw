#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <map>
#include "RenderGraph.h"
#include "Import/SourceLibrary.h"

namespace mashup
{
class ProjectModel;
class EffectFactory;

/** Compiles the project ValueTree into a RenderGraph, reusing TrackRenderers/ClipPlayers/ProcessorChains
    whose static configuration has not changed. Message thread only. */
class GraphBuilder
{
public:
    GraphBuilder (ProjectModel&, SourceLibrary&, EffectFactory&);

    std::unique_ptr<RenderGraph> build (double sampleRate, int maxBlockSize, const RenderGraph* previous);

    /** Updates only the live atomics of an existing graph for a property change. Returns false if a rebuild is required. */
    bool applyLiveChange (RenderGraph& graph, const juce::ValueTree& node, const juce::Identifier& property);

    void invalidateAll() { cache.clear(); }
    void setWorkers (std::shared_ptr<RenderWorkers> w) { workers = std::move (w); }

private:
    std::shared_ptr<ClipPlayer> makeOrReuse (const juce::ValueTree& clipNode, double sampleRate, int maxBlockSize, const RenderGraph* previous);
    static void pushLive (ClipPlayer&, const juce::ValueTree& clipNode);
    static void pushLive (TrackRenderer&, const juce::ValueTree& trackNode);
    std::shared_ptr<ProcessorChain> buildChain (const juce::ValueTree& effectsNode, std::shared_ptr<ProcessorChain> previous, double sr, int mbs);

    ProjectModel& project;
    SourceLibrary& sources;
    EffectFactory& effects;
    std::map<juce::String, std::weak_ptr<ClipPlayer>> cache;
    std::shared_ptr<RenderWorkers> workers;
};
} // namespace mashup
