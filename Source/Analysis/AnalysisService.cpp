#include "AnalysisService.h"
#include "Project/ProjectModel.h"
#include "Clips/ClipModel.h"
#include "Tracks/TrackModel.h"
#include "DSP/Loudness.h"
#include "Analysis/BpmDetector.h"
#include "Analysis/KeyDetector.h"
#include "Analysis/TransientDetector.h"
#include "Analysis/PhraseDetector.h"
#include "Core/Log.h"

namespace mashup
{
class AnalysisService::Job : public juce::ThreadPoolJob
{
public:
    Job (AnalysisService& o, juce::String id_, AudioSourcePtr src) : ThreadPoolJob ("analyse " + id_), owner (o), id (std::move (id_)), source (std::move (src)) {}
    JobStatus runJob() override
    {
        auto r = analyseBuffer (source->buffer, source->sampleRate, [this] (float p) { { std::lock_guard<std::mutex> l (owner.lock); owner.progress[id] = p; } return ! shouldExit(); });
        r.sourceId = id;
        { std::lock_guard<std::mutex> l (owner.lock); owner.running.erase (id); owner.progress.erase (id); if (! shouldExit()) owner.finished.push_back (std::move (r)); }
        owner.triggerAsyncUpdate();
        return jobHasFinished;
    }
private:
    AnalysisService& owner; juce::String id; AudioSourcePtr source;
};

AnalysisService::AnalysisService (ProjectModel& p, SourceLibrary& s, juce::ThreadPool& tp) : project (p), sources (s), pool (tp) { sources.addListener (this); }
AnalysisService::~AnalysisService() { sources.removeListener (this); cancelPendingUpdate(); }

void AnalysisService::analyseSource (const juce::String& id, bool force)
{
    auto node = ProjectModel::findByIdIn (project.sources(), ids::SOURCE, id);
    if (! force && node.isValid() && (bool) node.getProperty (ids::analysed, false)) return;
    { std::lock_guard<std::mutex> l (lock); if (running.count (id)) return; }
    if (sources.get (id)) startJob (id);
    else { std::lock_guard<std::mutex> l (lock); pending.insert (id); }
}

void AnalysisService::sourceLoaded (const juce::String& id)
{
    bool go = false;
    { std::lock_guard<std::mutex> l (lock); go = pending.erase (id) > 0; }
    if (! go)
    {
        auto node = ProjectModel::findByIdIn (project.sources(), ids::SOURCE, id);
        go = node.isValid() && ! (bool) node.getProperty (ids::analysed, false);
    }
    if (go) startJob (id);
}

void AnalysisService::startJob (const juce::String& id)
{
    auto src = sources.get (id);
    if (! src) return;
    { std::lock_guard<std::mutex> l (lock); running.insert (id); progress[id] = 0.0f; }
    pool.addJob (new Job (*this, id, src), true);
    sendChangeMessage();
}

bool AnalysisService::isAnalysing (const juce::String& id) const { std::lock_guard<std::mutex> l (lock); return running.count (id) || pending.count (id); }
float AnalysisService::getProgress (const juce::String& id) const { std::lock_guard<std::mutex> l (lock); auto it = progress.find (id); return it == progress.end() ? 0.0f : it->second; }

AnalysisService::Result AnalysisService::analyseBuffer (const juce::AudioBuffer<float>& buffer, double sr, const std::function<bool (float)>& progress)
{
    Result r;
    auto report = [&] (float p) { return progress ? progress (p) : true; };
    dsp::LoudnessMeter::analyse (buffer, sr, r.lufs, r.truePeak, r.lra);
    if (! report (0.1f)) return r;

    // mono mix for rhythm/key analysis
    juce::AudioBuffer<float> mono (1, buffer.getNumSamples());
    mono.clear();
    for (int c = 0; c < buffer.getNumChannels(); ++c) mono.addFrom (0, 0, buffer, c, 0, buffer.getNumSamples(), 1.0f / buffer.getNumChannels());

    analysis::BpmDetector bpm;
    auto beatResult = bpm.analyse (mono.getReadPointer (0), mono.getNumSamples(), sr, [&] (float p) { return report (0.1f + 0.45f * p); });
    r.bpm = beatResult.bpm; r.bpmConfidence = beatResult.confidence; r.beats = beatResult.beats; r.downbeats = beatResult.downbeats;
    r.firstDownbeat = beatResult.downbeats.empty() ? (beatResult.beats.empty() ? 0.0 : beatResult.beats.front()) : beatResult.downbeats.front();
    if (! report (0.55f)) return r;

    analysis::KeyDetector kd;
    auto keyResult = kd.analyse (mono.getReadPointer (0), mono.getNumSamples(), sr);
    r.keyRoot = keyResult.root; r.keyMode = keyResult.mode; r.keyConfidence = keyResult.confidence;
    if (! report (0.75f)) return r;

    r.transients = analysis::TransientDetector::detect (mono.getReadPointer (0), mono.getNumSamples(), sr);
    if (! report (0.85f)) return r;

    r.phrases = analysis::PhraseDetector::detect (mono.getReadPointer (0), mono.getNumSamples(), sr, r.downbeats, r.bpm);
    report (1.0f);
    return r;
}

void AnalysisService::handleAsyncUpdate()
{
    std::vector<Result> done;
    { std::lock_guard<std::mutex> l (lock); done.swap (finished); }
    for (auto& r : done) applyResult (r);
    sendChangeMessage();
}

void AnalysisService::applyResult (const Result& r)
{
    auto node = ProjectModel::findByIdIn (project.sources(), ids::SOURCE, r.sourceId);
    if (! node.isValid()) return;
    // analysis results are not undoable edits: write without the undo manager
    node.setProperty (ids::bpm, r.bpm, nullptr);
    node.setProperty (ids::bpmConfidence, r.bpmConfidence, nullptr);
    node.setProperty (ids::keyRoot, r.keyRoot, nullptr);
    node.setProperty (ids::keyMode, r.keyMode, nullptr);
    node.setProperty (ids::keyConfidence, r.keyConfidence, nullptr);
    node.setProperty (ids::lufs, r.lufs, nullptr);
    node.setProperty (ids::peak, r.truePeak, nullptr);
    node.setProperty (ids::firstDownbeat, r.firstDownbeat, nullptr);
    analysis::writeTimes (node, ids::BEATS, r.beats, nullptr);
    analysis::writeTimes (node, ids::DOWNBEATS, r.downbeats, nullptr);
    analysis::writeTimes (node, ids::TRANSIENTS, r.transients, nullptr);
    analysis::writePhrases (node, r.phrases, nullptr);
    node.setProperty (ids::analysed, true, nullptr);

    // propagate to clips that have no bpm/key of their own yet
    for (auto t : project.tracks())
        for (auto c : TrackModel (t).clips())
        {
            ClipModel clip (c);
            if (clip.getSourceId() != r.sourceId) continue;
            if (clip.getClipBpm() <= 0.0 && r.bpm > 0) clip.setClipBpm (r.bpm, nullptr);
            if (clip.getKeyRoot() < 0 && r.keyRoot >= 0) clip.setKey (r.keyRoot, r.keyMode, nullptr);
        }
    log ("Analysed " + node[ids::name].toString() + ": " + juce::String (r.bpm, 1) + " bpm, key " + juce::String (r.keyRoot) + "/" + juce::String (r.keyMode) + ", " + juce::String (r.lufs, 1) + " LUFS");
}
} // namespace mashup
