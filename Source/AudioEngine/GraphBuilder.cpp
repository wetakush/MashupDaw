#include "GraphBuilder.h"
#include "Project/ProjectModel.h"
#include "Tracks/TrackModel.h"
#include "Effects/ProcessorChain.h"
#include "Effects/EffectFactory.h"
#include "Effects/BuiltInEffect.h"
#include "Core/Log.h"
#include "Automation/AutomationCurve.h"

namespace mashup
{
GraphBuilder::GraphBuilder (ProjectModel& p, SourceLibrary& s, EffectFactory& e) : project (p), sources (s), effects (e) {}

void GraphBuilder::pushLive (ClipPlayer& player, const juce::ValueTree& node)
{
    ClipModel c (node);
    auto& l = player.live();
    l.gain.store ((float) c.getGain()); l.pan.store ((float) c.getPan()); l.rate.store ((float) c.getRate());
    l.pitchScale.store ((float) c.getPitchScale());
    l.formantScale.store ((float) std::pow (2.0, c.getFormant() / 12.0));
    l.fadeInBeats.store ((float) c.getFadeIn()); l.fadeOutBeats.store ((float) c.getFadeOut());
    l.mute.store (c.isMuted());
}

void GraphBuilder::pushLive (TrackRenderer& tr, const juce::ValueTree& node)
{
    TrackModel t (node);
    auto& l = tr.live();
    l.volume.set ((float) t.getVolume()); l.pan.set ((float) t.getPan()); l.inputGain.set ((float) t.getInputGain());
    l.mute.store (t.isMuted()); l.solo.store (t.isSolo()); l.phaseInvert.store (t.isPhaseInverted());
    float sends[2] = { 0, 0 };
    for (const auto& s : t.sends())
    {
        const int b = (int) s[ids::bus];
        if (b >= 0 && b < 2) sends[b] = (float) s[ids::level];
    }
    l.sendLevel[0].store (sends[0]); l.sendLevel[1].store (sends[1]);
}

std::shared_ptr<ClipPlayer> GraphBuilder::makeOrReuse (const juce::ValueTree& node, double sr, int mbs, const RenderGraph* previous)
{
    ClipModel c (node);
    ClipPlayer::Static st;
    st.clipId = c.getId();
    st.source = sources.get (c.getSourceId());
    st.startBeat = c.getStart(); st.lengthBeats = c.getLength(); st.offsetSeconds = c.getOffset();
    st.sourceEndSeconds = (double) node.getProperty (ids::sourceEnd, 0.0);
    st.reverse = c.isReversed(); st.loop = c.isLooped(); st.mode = c.getStretchMode();
    st.synced = c.isSyncedToProject(); st.clipBpm = c.getClipBpm() > 0 ? c.getClipBpm() : 120.0;
    st.fadeInShape = c.getFadeInShape(); st.fadeOutShape = c.getFadeOutShape();

    std::shared_ptr<ClipPlayer> player;
    if (auto it = cache.find (st.clipId); it != cache.end())
        if (auto existing = it->second.lock())
            if (existing->getStatic().sameAs (st) && previous != nullptr && previous->sampleRate == sr && previous->maxBlockSize >= mbs)
                player = existing;
    if (! player)
    {
        player = std::make_shared<ClipPlayer> (st, sr, mbs);
        cache[st.clipId] = player;
    }
    pushLive (*player, node);
    return player;
}

std::shared_ptr<ProcessorChain> GraphBuilder::buildChain (const juce::ValueTree& effectsNode, std::shared_ptr<ProcessorChain> previous, double sr, int mbs)
{
    if (! effectsNode.isValid() || effectsNode.getNumChildren() == 0) return nullptr;
    // reuse when the ordered list of effect ids/types is unchanged
    bool same = previous && (int) previous->slots.size() == effectsNode.getNumChildren();
    if (same)
        for (int i = 0; i < effectsNode.getNumChildren() && same; ++i)
            same = previous->slots[(size_t) i]->effectId == effectsNode.getChild (i)[ids::id].toString();
    if (same)
    {
        for (int i = 0; i < effectsNode.getNumChildren(); ++i)
        {
            auto node = effectsNode.getChild (i);
            previous->slots[(size_t) i]->bypass.store ((bool) node[ids::bypass]);
            if (auto* b = dynamic_cast<BuiltInEffect*> (previous->slots[(size_t) i]->processor.get())) b->loadParamsFromNode (node);
        }
        return previous;
    }
    auto chain = std::make_shared<ProcessorChain>();
    for (const auto& node : effectsNode)
    {
        auto slot = std::make_unique<ProcessorChain::Slot>();
        slot->effectId = node[ids::id].toString();
        slot->bypass.store ((bool) node[ids::bypass]);
        // move an existing processor with the same id if present
        if (previous)
            for (auto& ps : previous->slots)
                if (ps->effectId == slot->effectId && ps->processor) { slot->processor = std::move (ps->processor); break; }
        if (! slot->processor)
        {
            slot->processor = effects.create (node[ids::type].toString(), node[ids::pluginId].toString(), sr, mbs);
            if (slot->processor)
            {
                slot->processor->setPlayConfigDetails (2, 2, sr, mbs);
                if (auto* b = dynamic_cast<BuiltInEffect*> (slot->processor.get())) b->loadParamsFromNode (node);
                else if (node.hasProperty (ids::state))
                {
                    juce::MemoryBlock mb; mb.fromBase64Encoding (node[ids::state].toString());
                    if (mb.getSize() > 0) slot->processor->setStateInformation (mb.getData(), (int) mb.getSize());
                }
                slot->processor->prepareToPlay (sr, mbs);
            }
            else log ("Effect could not be created: " + node[ids::type].toString() + " " + node[ids::pluginId].toString());
        }
        else if (auto* b = dynamic_cast<BuiltInEffect*> (slot->processor.get())) b->loadParamsFromNode (node);
        chain->slots.push_back (std::move (slot));
    }
    return chain;
}

std::unique_ptr<RenderGraph> GraphBuilder::build (double sr, int mbs, const RenderGraph* previous)
{
    auto g = std::make_unique<RenderGraph> (sr, mbs);
    g->tempo = std::make_shared<TempoMap> (TempoMap::fromProject (project.getRoot()));
    if (previous && previous->sampleRate == sr) { g->masterVolume = previous->masterVolume; g->loudness = previous->loudness; }
    g->workers = workers;
    g->masterVolume->set ((float) project.master().getProperty (ids::volume, 1.0));

    for (const auto& tnode : project.tracks())
    {
        TrackModel t (tnode);
        std::shared_ptr<TrackRenderer> tr;
        if (previous && previous->sampleRate == sr && previous->maxBlockSize >= mbs)
            for (auto& p : previous->tracks) if (p->id == t.getId()) { tr = p; break; }
        std::shared_ptr<ProcessorChain> prevChain;
        if (tr)
        {
            // effect chain ownership: the renderer holds a unique_ptr; wrap it in a shared_ptr-less path by rebuilding
            // via buildChain with the renderer's existing chain (moved out below)
        }
        if (! tr) tr = std::make_shared<TrackRenderer> (t.getId(), sr, mbs);
        pushLive (*tr, tnode);

        std::vector<std::shared_ptr<ClipPlayer>> players;
        for (const auto& cnode : t.clips())
            players.push_back (makeOrReuse (cnode, sr, mbs, previous));

        // effects: reuse processors by id
        std::shared_ptr<ProcessorChain> prev;
        if (tr->getEffectChain()) prev = std::shared_ptr<ProcessorChain> (const_cast<ProcessorChain*> (tr->getEffectChain()), [] (ProcessorChain*) {});
        auto newChain = buildChain (t.effects(), prev, sr, mbs);

        // NOTE: the audio thread may be using `tr` right now; swapping its clip list/chain must be atomic w.r.t. the
        // callback. We therefore never mutate a renderer that a published graph references: if anything changed,
        // create a fresh renderer object that shares the players.
        const bool clipsChanged = players.size() != tr->clips.size() || ! std::equal (players.begin(), players.end(), tr->clips.begin());
        const bool chainChanged = newChain.get() != tr->getEffectChain();
        if (clipsChanged || chainChanged)
        {
            auto fresh = std::make_shared<TrackRenderer> (t.getId(), sr, mbs);
            pushLive (*fresh, tnode);
            fresh->live().volume.snap(); fresh->live().pan.snap(); fresh->live().inputGain.snap();
            fresh->clips = std::move (players);
            if (newChain)
            {
                if (newChain == prev)
                {
                    // move the chain out of the old renderer only after the old graph is retired -> instead build a
                    // new chain object sharing the processors (processors are unique_ptr; so re-create slots by moving)
                    auto moved = std::make_unique<ProcessorChain>();
                    for (auto& s : prev->slots)
                    {
                        auto slot = std::make_unique<ProcessorChain::Slot>();
                        slot->effectId = s->effectId; slot->bypass.store (s->bypass.load());
                        slot->processor = std::move (s->processor);
                        moved->slots.push_back (std::move (slot));
                    }
                    fresh->setEffectChain (std::move (moved));
                }
                else
                {
                    auto owned = std::make_unique<ProcessorChain>();
                    owned->slots = std::move (newChain->slots);
                    fresh->setEffectChain (std::move (owned));
                }
            }
            tr = fresh;
        }
        // automation snapshot (fx lanes bind to the renderer's live processors)
        {
            auto set = std::make_shared<AutomationSet>();
            for (const auto& lane : t.automation())
            {
                const auto param = lane[ids::param].toString();
                auto curve = AutomationCurve::fromLane (lane);
                if (curve.isEmpty()) continue;
                if (param == "volume") set->volume = curve; else if (param == "pan") set->pan = curve;
                else if (param == "send0") set->send[0] = curve; else if (param == "send1") set->send[1] = curve;
                else if (param.startsWith ("fx:") && tr->getEffectChain())
                {
                    const auto effectId = param.fromFirstOccurrenceOf ("fx:", false, false).upToFirstOccurrenceOf (":", false, false);
                    const auto paramId = param.fromLastOccurrenceOf (":", false, false);
                    for (auto& slot : tr->getEffectChain()->slots)
                        if (slot->effectId == effectId && slot->processor)
                            if (auto* b = dynamic_cast<BuiltInEffect*> (slot->processor.get()))
                                if (auto* prm = b->getAPVTS().getParameter (paramId)) set->fx.push_back ({ prm, curve });
                }
            }
            const bool changed = (tr->automation == nullptr) != (! set->hasAny()) || set->hasAny() || tr->automation;
            if (changed)
            {
                if (! tr->automation && ! set->hasAny()) {}
                else
                {
                    // renderer may be live: swap the shared_ptr (pointer store is atomic enough for our single reader)
                    std::atomic_thread_fence (std::memory_order_release);
                    tr->automation = set->hasAny() ? std::shared_ptr<const AutomationSet> (set) : nullptr;
                }
            }
            tr->automationPlayback.store ((AutomationMode) (int) tnode.getProperty ("automationMode", 0) != AutomationMode::Off);
        }
        g->tracks.push_back (tr);
        g->endBeat = juce::jmax (g->endBeat, t.getEndBeat());
    }

    // master + buses
    {
        std::shared_ptr<ProcessorChain> prevMaster = previous ? previous->masterChain : nullptr;
        g->masterChain = buildChain (project.master().getChildWithName (ids::EFFECTS), prevMaster, sr, mbs);
        auto buses = project.buses();
        for (int b = 0; b < 2 && b < buses.getNumChildren(); ++b)
        {
            auto bus = buses.getChild (b);
            if (previous && previous->sampleRate == sr) g->busVolume[b] = previous->busVolume[b];
            g->busVolume[b]->set ((float) bus.getProperty (ids::volume, 1.0));
            g->busChains[b] = buildChain (bus.getChildWithName (ids::EFFECTS), previous ? previous->busChains[b] : nullptr, sr, mbs);
        }
    }
    // drop dead cache entries
    for (auto it = cache.begin(); it != cache.end();) it = it->second.expired() ? cache.erase (it) : std::next (it);
    return g;
}

bool GraphBuilder::applyLiveChange (RenderGraph& graph, const juce::ValueTree& node, const juce::Identifier& prop)
{
    if (node.hasType (ids::TRACK))
    {
        static const juce::Identifier* liveProps[] = { &ids::volume, &ids::pan, &ids::mute, &ids::solo, &ids::phaseInvert, &ids::inputGain, &ids::name, &ids::color, &ids::height, &ids::arm };
        if (prop == juce::Identifier ("automationParam") || prop == juce::Identifier ("showAutomation")) return true;
        if (prop == juce::Identifier ("automationMode")) { for (auto& t : graph.tracks) if (t->id == node[ids::id].toString()) t->automationPlayback.store ((AutomationMode) (int) node.getProperty ("automationMode", 0) != AutomationMode::Off); return true; }
        for (auto* p : liveProps) if (prop == *p) { for (auto& t : graph.tracks) if (t->id == node[ids::id].toString()) { pushLive (*t, node); return true; } return true; }
        return false;
    }
    if (node.hasType (ids::SEND))
    {
        auto track = node.getParent().getParent();
        for (auto& t : graph.tracks) if (t->id == track[ids::id].toString()) { pushLive (*t, track); return true; }
        return false;
    }
    if (node.hasType (ids::CLIP))
    {
        static const juce::Identifier* liveProps[] = { &ids::gain, &ids::pan, &ids::mute, &ids::rate, &ids::pitchSemis, &ids::pitchCents, &ids::formant, &ids::fadeIn, &ids::fadeOut, &ids::name, &ids::color, &ids::keyRoot, &ids::keyMode };
        // rate is static for synced clips (affects mapping) but live otherwise; pitch is live unless Repitch (changes speed mapping)
        ClipModel c (node);
        bool live = false;
        for (auto* p : liveProps) if (prop == *p) live = true;
        if (! live) return false;
        if ((prop == ids::pitchSemis || prop == ids::pitchCents) && c.getStretchMode() == StretchMode::Repitch) live = true;   // still live: mapping is recomputed every block
        if (auto it = cache.find (c.getId()); it != cache.end()) if (auto pl = it->second.lock()) { pushLive (*pl, node); return true; }
        return false;
    }
    if (node.hasType (ids::MASTER) && prop == ids::volume) { graph.masterVolume->set ((float) node.getProperty (ids::volume, 1.0)); return true; }
    if (node.hasType (ids::BUS) && prop == ids::volume)
    {
        const int idx = node.getParent().indexOf (node);
        if (idx >= 0 && idx < 2) graph.busVolume[idx]->set ((float) node.getProperty (ids::volume, 1.0));
        return true;
    }
    if (node.hasType (juce::Identifier ("PARAMS")))
    {
        auto effect = node.getParent();
        const auto effectId = effect[ids::id].toString();
        auto apply = [&] (ProcessorChain* chain)
        {
            if (! chain) return false;
            for (auto& s : chain->slots)
                if (s->effectId == effectId)
                {
                    if (auto* b = dynamic_cast<BuiltInEffect*> (s->processor.get())) b->loadParamsFromNode (effect);
                    return true;
                }
            return false;
        };
        for (auto& t : graph.tracks) if (apply (t->getEffectChain())) return true;
        if (apply (graph.masterChain.get())) return true;
        for (auto& b : graph.busChains) if (apply (b.get())) return true;
        return false;
    }
    if (node.hasType (ids::EFFECT) && prop == ids::bypass)
    {
        const auto effectId = node[ids::id].toString();
        auto apply = [&] (ProcessorChain* chain) { if (! chain) return false; for (auto& s : chain->slots) if (s->effectId == effectId) { s->bypass.store ((bool) node[ids::bypass]); return true; } return false; };
        for (auto& t : graph.tracks) if (apply (t->getEffectChain())) return true;
        if (apply (graph.masterChain.get())) return true;
        for (auto& b : graph.busChains) if (apply (b.get())) return true;
        return false;
    }
    if (node.hasType (ids::PROJECT))
        return prop == ids::name || prop == ids::keyRoot || prop == ids::keyMode || prop == ids::loopStart || prop == ids::loopEnd || prop == ids::loopEnabled
            || prop == ids::zoom || prop == ids::scroll || prop == ids::snap || prop == ids::gridDivision || prop == ids::sampleRate;
    if (node.hasType (ids::SOURCE) || node.hasType (ids::MARKER) || node.hasType (ids::MARKERS) || node.hasType (ids::PHRASE) || node.hasType (ids::BEATS) || node.hasType (ids::DOWNBEATS) || node.hasType (ids::TRANSIENTS) || node.hasType (ids::PHRASES))
        return true;   // analysis metadata does not affect rendering
    return false;
}
} // namespace mashup
