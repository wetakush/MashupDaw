#include "OfflineRenderer.h"
#include "Project/Session.h"
#include "AudioEngine/AudioEngine.h"
#include "Timeline/TempoMap.h"
#include "DSP/Loudness.h"
#include "Tracks/TrackModel.h"

namespace mashup
{
OfflineRenderer::Result OfflineRenderer::render (Session& session, const Options& o, const std::function<bool (float)>& progress)
{
    Result res; res.sampleRate = o.sampleRate;
    auto& engine = session.getAudioEngine();
    std::unique_ptr<RenderGraph> graph;
    double startSec = 0, endSec = 0;
    {
        // graph building touches the model -> message thread
        juce::WaitableEvent done;
        juce::MessageManager::callAsync ([&]
        {
            graph = engine.buildOfflineGraph (o.sampleRate, 4096);
            TempoMap tm = TempoMap::fromProject (session.getProject().getRoot());
            const double endBeat = o.endBeat > o.startBeat ? o.endBeat : graph->endBeat;
            startSec = tm.beatToTime (o.startBeat); endSec = tm.beatToTime (endBeat) + (o.endBeat > o.startBeat ? 0.0 : o.tailSeconds);
            if (! o.onlyTracks.empty())
                for (auto& t : graph->tracks) { const bool on = o.onlyTracks.count (t->id) > 0; t->live().mute.store (! on); t->live().solo.store (false); }
            for (auto& t : graph->tracks) { t->live().volume.snap(); t->live().pan.snap(); t->live().inputGain.snap(); }
            graph->masterVolume->snap();
            done.signal();
        });
        if (! done.wait (10000) || ! graph) { res.error = "could not build render graph"; return res; }
    }
    const juce::int64 startSample = (juce::int64) std::llround (startSec * o.sampleRate);
    const juce::int64 numSamples = std::max<juce::int64> (1, (juce::int64) std::llround ((endSec - startSec) * o.sampleRate));
    if (numSamples > (juce::int64) 1 << 31) { res.error = "render too long"; return res; }
    res.audio.setSize (2, (int) numSamples); res.audio.clear();
    graph->notifyDiscontinuity();
    const int block = 4096;
    for (juce::int64 pos = 0; pos < numSamples; pos += block)
    {
        const int n = (int) std::min<juce::int64> (block, numSamples - pos);
        float* ptrs[2] = { res.audio.getWritePointer (0, (int) pos), res.audio.getWritePointer (1, (int) pos) };
        juce::AudioBuffer<float> view (ptrs, 2, n);
        graph->process (view, startSample + pos, n);
        if (progress && ! progress (0.9f * (float) pos / (float) numSamples)) { res.error = "cancelled"; return res; }
    }
    double lra;
    dsp::LoudnessMeter::analyse (res.audio, o.sampleRate, res.lufs, res.peak, lra);
    if (o.normalizeLoudness && res.lufs > -90)
    {
        const float g = juce::Decibels::decibelsToGain ((float) (o.targetLufs - res.lufs));
        res.audio.applyGain (g); res.peak *= g; res.lufs = o.targetLufs;
    }
    if (o.normalizePeak && res.peak > 1e-6)
    {
        const float g = juce::Decibels::decibelsToGain ((float) o.peakDbfs) / (float) res.peak;
        if (! o.normalizeLoudness || g < 1.0f) { res.audio.applyGain (g); res.lufs += juce::Decibels::gainToDecibels (g); res.peak *= g; }
    }
    if (progress) progress (0.95f);
    return res;
}

juce::String OfflineRenderer::renderToFile (Session& s, const Options& o, const juce::File& file, const FFmpegEncoder::Settings& enc, const std::function<bool (float)>& progress)
{
    auto r = render (s, o, [&] (float p) { return progress ? progress (p * 0.8f) : true; });
    if (r.error.isNotEmpty()) return r.error;
    file.getParentDirectory().createDirectory();
    return FFmpegEncoder::encode (r.audio, r.sampleRate, file, enc, [&] (float p) { return progress ? progress (0.8f + 0.2f * p) : true; });
}
} // namespace mashup
