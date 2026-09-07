#include "AudioEngine.h"
#include "Project/ProjectModel.h"
#include "Effects/EffectFactory.h"
#include "Effects/ProcessorChain.h"
#include "Core/Log.h"

namespace mashup
{
AudioEngine::AudioEngine (ProjectModel& p, SourceLibrary& s, EffectFactory& e, juce::ApplicationProperties& ap)
    : project (p), sources (s), effects (e), props (ap), builder (p, s, e)
{
    outBuffer.setSize (2, 8192, false, true, true);
    builder.setWorkers (std::make_shared<RenderWorkers> (juce::jlimit (0, 7, juce::SystemStats::getNumCpus() - 2)));
    project.getRoot().addListener (this);
    sources.addListener (this);
    deviceManager.addChangeListener (this);
}

AudioEngine::~AudioEngine()
{
    deviceManager.removeChangeListener (this);
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    sources.removeListener (this);
    project.getRoot().removeListener (this);
    cancelPendingUpdate();
}

juce::String AudioEngine::initialiseDevice()
{
    std::unique_ptr<juce::XmlElement> saved;
    if (auto* settings = props.getUserSettings()) saved = settings->getXmlValue ("audioDevice");
    juce::String err = deviceManager.initialise (2, 2, saved.get(), true);
    if (err.isNotEmpty() || deviceManager.getCurrentAudioDevice() == nullptr)
    {
        log ("Audio device init failed (" + err + "), retrying with defaults");
        err = deviceManager.initialise (0, 2, nullptr, true);
    }
    if (deviceManager.getCurrentAudioDevice() == nullptr)
        return err.isNotEmpty() ? err : "No audio output device available";
    deviceManager.addAudioCallback (this);
    return {};
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (auto* settings = props.getUserSettings())
        if (auto xml = deviceManager.createStateXml()) { settings->setValue ("audioDevice", xml.get()); settings->saveIfNeeded(); }
    listeners.call ([] (Listener& l) { l.deviceChanged(); });
}

int AudioEngine::getOutputLatencySamples() const
{
    if (auto* d = deviceManager.getCurrentAudioDevice()) return d->getOutputLatencyInSamples();
    return 0;
}

void AudioEngine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate.store (device->getCurrentSampleRate());
    blockSize.store (device->getCurrentBufferSizeSamples());
    if (outBuffer.getNumSamples() < device->getCurrentBufferSizeSamples()) outBuffer.setSize (2, device->getCurrentBufferSizeSamples() * 2, false, true, true);
    recorder.prepare (sampleRate.load(), device->getActiveInputChannels().countNumberOfSetBits());
    project.setSampleRate (sampleRate.load());
    builder.invalidateAll();
    juce::MessageManager::callAsync ([this] { rebuildGraph(); updateLoopFromProject(); });
}

void AudioEngine::audioDeviceStopped() {}
void AudioEngine::audioDeviceError (const juce::String& e) { log ("Audio device error: " + e); }

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* input, int numIn, float* const* output, int numOut, int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c) if (output[c]) juce::FloatVectorOperations::clear (output[c], numSamples);
    if (numOut <= 0 || numSamples <= 0 || numSamples > outBuffer.getNumSamples()) return;

    recorder.pushInput (input, numIn, numSamples);
    outBuffer.clear (0, numSamples);

    juce::int64 loc;
    bool discontinuity = false;
    if (transport.consumeLocate (loc)) discontinuity = true;

    if (auto g = graph.acquire())
    {
        if (transport.isPlaying())
        {
            juce::int64 pos = transport.getPosition();
            if (pos != lastCallbackEnd || discontinuity) g->notifyDiscontinuity();
            int done = 0;
            while (done < numSamples)
            {
                int n = numSamples - done;
                if (transport.isLoopEnabled())
                {
                    const juce::int64 le = transport.getLoopEnd(), ls = transport.getLoopStart();
                    if (pos >= le || pos < ls) { pos = ls; g->notifyDiscontinuity(); }
                    n = (int) std::min<juce::int64> (n, le - pos);
                }
                float* ptrs[2] = { outBuffer.getWritePointer (0, done), outBuffer.getWritePointer (1, done) };
                juce::AudioBuffer<float> view (ptrs, 2, n);
                g->process (view, pos, n);
                pos += n; done += n;
            }
            transport.setPositionRT (pos);
            lastCallbackEnd = pos;
        }
        else
        {
            lastCallbackEnd = -1;
            g->notifyDiscontinuity();
        }
    }

    renderAudition (outBuffer, numSamples);

    for (int c = 0; c < numOut; ++c)
        if (output[c]) juce::FloatVectorOperations::copy (output[c], outBuffer.getReadPointer (juce::jmin (c, 1)), numSamples);
}

void AudioEngine::renderAudition (juce::AudioBuffer<float>& out, int numSamples) noexcept
{
    auto a = audition.acquire();
    if (! a || a->finished || ! a->source) return;
    const auto& src = *a->source;
    const double step = src.sampleRate / sampleRate.load() * a->speed * (a->reverse ? -1.0 : 1.0);
    const int chans = src.getNumChannels();
    const double endS = a->endSamples > 0 ? a->endSamples : (double) src.getLengthSamples();
    float* l = out.getWritePointer (0); float* r = out.getWritePointer (1);
    double p = a->positionSamples;
    for (int i = 0; i < numSamples; ++i)
    {
        const juce::int64 idx = (juce::int64) p;
        if (idx >= src.getLengthSamples() || (! a->reverse && p >= endS) || (a->reverse && p < a->startBound) || p < 0) { a->finished = true; break; }
        const float frac = (float) (p - (double) idx);
        const float s0 = src.getSample (0, idx), s1 = src.getSample (0, idx + 1);
        const float v0 = s0 + (s1 - s0) * frac;
        float v1 = v0;
        if (chans > 1) { const float t0 = src.getSample (1, idx), t1 = src.getSample (1, idx + 1); v1 = t0 + (t1 - t0) * frac; }
        l[i] += v0 * a->gain; r[i] += v1 * a->gain;
        p += step;
    }
    a->positionSamples = p;
    a->positionSeconds.store (p / src.sampleRate, std::memory_order_relaxed);
}

void AudioEngine::startAudition (AudioSourcePtr source, double startSeconds, double gain, double endSeconds, bool reverse, double speed)
{
    auto a = std::make_unique<Audition>();
    a->source = std::move (source);
    const double sr = a->source ? a->source->sampleRate : 1.0;
    a->endSamples = endSeconds > startSeconds ? endSeconds * sr : 0.0;
    a->reverse = reverse; a->speed = juce::jlimit (0.1, 8.0, speed);
    a->positionSamples = reverse && a->endSamples > 0 ? a->endSamples - 1 : startSeconds * sr;
    a->startBound = reverse ? startSeconds * sr : 0.0;
    if (reverse) a->endSamples = 0;   // reverse plays down to the region start
    a->gain = (float) gain;
    audition.publish (std::move (a));
}
void AudioEngine::stopAudition() { audition.publish (nullptr); }
bool AudioEngine::isAuditioning() const noexcept { auto* a = audition.getMessageThreadView(); return a && ! a->finished && a->source; }
double AudioEngine::getAuditionPositionSeconds() const noexcept { auto* a = audition.getMessageThreadView(); return a ? a->positionSeconds.load() : 0.0; }

void AudioEngine::valueTreePropertyChanged (juce::ValueTree& node, const juce::Identifier& prop)
{
    if (node.hasType (ids::PROJECT) && (prop == ids::loopStart || prop == ids::loopEnd || prop == ids::loopEnabled)) { updateLoopFromProject(); return; }
    if (auto* g = graph.getMessageThreadView())
        if (builder.applyLiveChange (*g, node, prop)) return;
    scheduleRebuild();
}

void AudioEngine::handleAsyncUpdate()
{
    if (rebuildPending) { rebuildPending = false; rebuildGraph(); }
}

void AudioEngine::rebuildGraph()
{
    const juce::ScopedLock l (graphBuildLock);
    auto* prev = graph.getMessageThreadView();
    auto g = builder.build (sampleRate.load(), juce::jmax (blockSize.load(), 64), prev);
    graph.publish (std::move (g));
    listeners.call ([] (Listener& l2) { l2.graphRebuilt(); });
}

std::unique_ptr<RenderGraph> AudioEngine::buildOfflineGraph (double sr, int bs)
{
    GraphBuilder offline (project, sources, effects);
    return offline.build (sr, bs, nullptr);
}

void AudioEngine::renderOffline (RenderGraph& g, juce::AudioBuffer<float>& out, juce::int64 startSample, int numSamples, int bs)
{
    out.clear();
    g.notifyDiscontinuity();
    int done = 0;
    while (done < numSamples)
    {
        const int n = juce::jmin (bs, numSamples - done);
        float* ptrs[2] = { out.getWritePointer (0, done), out.getWritePointer (1, done) };
        juce::AudioBuffer<float> view (ptrs, 2, n);
        g.process (view, startSample + done, n);
        done += n;
    }
}

void AudioEngine::locateBeat (double beat)
{
    TempoMap tm = TempoMap::fromProject (project.getRoot());
    locateSeconds (tm.beatToTime (beat));
}

double AudioEngine::getPositionBeat() const
{
    if (auto* g = graph.getMessageThreadView()) return g->tempo->timeToBeat (getPositionSeconds());
    return TempoMap::fromProject (project.getRoot()).timeToBeat (getPositionSeconds());
}

void AudioEngine::updateLoopFromProject()
{
    TempoMap tm = TempoMap::fromProject (project.getRoot());
    const double sr = sampleRate.load();
    transport.setLoop ((juce::int64) std::llround (tm.beatToTime (project.getLoopStart()) * sr),
                       (juce::int64) std::llround (tm.beatToTime (project.getLoopEnd()) * sr), project.isLoopEnabled());
}

float AudioEngine::readMasterPeak (int ch) noexcept
{
    if (auto* g = graph.getMessageThreadView()) return (ch == 0 ? g->masterPeakL : g->masterPeakR).exchange (0.0f);
    return 0.0f;
}

float AudioEngine::readTrackPeak (const juce::String& trackId, int ch) noexcept
{
    if (auto* g = graph.getMessageThreadView())
        for (auto& t : g->tracks) if (t->id == trackId) return t->readAndResetPeak (ch);
    return 0.0f;
}
} // namespace mashup
