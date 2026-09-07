#include "StemPlacer.h"
#include "Import/SourceLibrary.h"
#include "Clips/ClipOperations.h"
#include "Analysis/AnalysisService.h"
#include "Analysis/AnalysisData.h"
#include "UI/Theme/Theme.h"
#include "Core/Log.h"

namespace mashup
{
StemPlacer::StemPlacer (Session& s) : session (s)
{
    session.getStemSeparation().onJobFinished = [this] (const StemSeparationService::Job& j) { place (j); };
}
StemPlacer::~StemPlacer() { session.getStemSeparation().onJobFinished = nullptr; }

void StemPlacer::place (const StemSeparationService::Job& job)
{
    auto& p = session.getProject(); auto* um = &p.getUndoManager();
    auto& lib = session.getSourceLibrary();
    auto original = ProjectModel::findByIdIn (p.sources(), ids::SOURCE, job.sourceId);
    if (! original.isValid()) return;
    um->beginNewTransaction ("Add separated stems");

    // clips referencing the original, with their tracks
    struct Placement { ClipModel clip; TrackModel track; };
    std::vector<Placement> placements;
    for (auto t : p.tracks()) for (auto c : TrackModel (t).clips()) { ClipModel clip (c); if (clip.getSourceId() == job.sourceId) placements.push_back ({ clip, TrackModel (t) }); }

    static const std::pair<const char*, int> order[] = { { "vocals", 0 }, { "drums", 1 }, { "bass", 2 }, { "other", 3 }, { "guitar", 4 }, { "piano", 5 }, { "no_vocals", 6 } };
    std::vector<std::pair<juce::String, juce::File>> stems;
    for (auto& [name, idx] : order) if (job.files.count (name)) stems.push_back ({ name, job.files.at (name) });
    for (auto& [name, f] : job.files) { bool known = false; for (auto& s : stems) if (s.first == name) known = true; if (! known) stems.push_back ({ name, f }); }

    int insertIndex = placements.empty() ? -1 : trackops::indexOf (p, placements.front().track) + 1;
    for (auto& [stemName, file] : stems)
    {
        if (! file.existsAsFile()) { log ("stem file missing: " + file.getFullPathName()); continue; }
        const auto displayName = (stemName == "no_vocals" ? "instrumental" : stemName);
        auto id = lib.importFile (file);
        auto node = ProjectModel::findByIdIn (p.sources(), ids::SOURCE, id);
        node.setProperty (ids::name, original[ids::name].toString() + " - " + displayName, nullptr);
        node.setProperty (ids::stemOf, job.sourceId, nullptr);
        node.setProperty (ids::stemType, displayName, nullptr);
        // stems share the original's rhythm/key analysis
        for (auto* prop : { &ids::bpm, &ids::bpmConfidence, &ids::keyRoot, &ids::keyMode, &ids::keyConfidence, &ids::firstDownbeat })
            if (original.hasProperty (*prop)) node.setProperty (*prop, original[*prop], nullptr);
        for (auto* child : { &ids::BEATS, &ids::DOWNBEATS, &ids::PHRASES })
            if (auto c = original.getChildWithName (*child); c.isValid()) node.appendChild (c.createCopy(), nullptr);
        node.setProperty (ids::analysed, (bool) original.getProperty (ids::analysed, false), nullptr);
        if (! (bool) node[ids::analysed]) session.getAnalysis().analyseSource (id);

        auto track = trackops::addTrack (p, node[ids::name].toString(), insertIndex);
        if (insertIndex >= 0) ++insertIndex;
        track.getState().setProperty (ids::stemType, displayName, um);
        const int colourIdx = stemName == "vocals" ? 3 : stemName == "drums" ? 1 : stemName == "bass" ? 2 : 4;
        track.setColour (ui::colours::trackPalette[colourIdx], um);
        if (placements.empty())
        {
            const double seconds = (double) original[ids::lengthSamples] / juce::jmax (1.0, (double) original[ids::sampleRate]);
            trackops::placeSource (p, id, node[ids::name].toString(), seconds, 0.0, track, (double) original.getProperty (ids::bpm, 0.0));
        }
        else
            for (auto& pl : placements)
            {
                auto copy = pl.clip.getState().createCopy();
                copy.setProperty (ids::id, ProjectModel::newId(), nullptr);
                copy.setProperty (ids::sourceId, id, nullptr);
                copy.setProperty (ids::name, pl.clip.getName() + " - " + displayName, nullptr);
                copy.setProperty (ids::color, track.getColour().darker (0.35f).toString(), nullptr);
                track.clips().appendChild (copy, um);
            }
    }
    if (session.getStemSeparation().muteOriginalAfterSeparation)
        for (auto& pl : placements) pl.track.setMuted (true, um);
    log ("Placed " + juce::String ((int) stems.size()) + " stems for " + job.sourceName);
}
} // namespace mashup
