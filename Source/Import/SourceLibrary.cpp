#include "SourceLibrary.h"
#include "FFmpegDecoder.h"
#include "Project/ProjectModel.h"
#include "Core/Log.h"

namespace mashup
{
class SourceLibrary::LoadJob : public juce::ThreadPoolJob
{
public:
    LoadJob (SourceLibrary& o, juce::String id_, juce::File f) : ThreadPoolJob ("load " + f.getFileName()), owner (o), id (std::move (id_)), file (std::move (f)) {}

    JobStatus runJob() override
    {
        auto result = FFmpegDecoder::decode (file, 0.0, [this] (float p)
        {
            owner.postEvent ({ id, true, {}, p, true });
            return ! shouldExit();
        });
        if (shouldExit()) return jobHasFinished;
        if (result.ok)
        {
            auto src = std::make_shared<AudioSource> (id, file, std::move (result.audio), result.info.sampleRate);
            {
                std::lock_guard<std::mutex> l (owner.mapLock);
                owner.sources[id] = src;
                owner.loading.erase (id);
            }
            owner.postEvent ({ id, true, {}, 1.0f, false });
        }
        else
        {
            { std::lock_guard<std::mutex> l (owner.mapLock); owner.loading.erase (id); }
            owner.postEvent ({ id, false, result.error, 0.0f, false });
        }
        return jobHasFinished;
    }
private:
    SourceLibrary& owner;
    juce::String id;
    juce::File file;
};

SourceLibrary::SourceLibrary (ProjectModel& p, juce::ThreadPool& tp) : project (p), pool (tp) {}
SourceLibrary::~SourceLibrary() { cancelPendingUpdate(); }

void SourceLibrary::postEvent (Event e)
{
    { std::lock_guard<std::mutex> l (eventLock); pendingEvents.push_back (std::move (e)); }
    triggerAsyncUpdate();
}

juce::String SourceLibrary::importFile (const juce::File& file)
{
    auto sources_ = project.sources();
    for (const auto& s : sources_)
        if (juce::File (s[ids::path].toString()) == file)
        {
            if (! get (s[ids::id]) && ! isLoading (s[ids::id])) startLoad (s);
            return s[ids::id];
        }

    juce::ValueTree node (ids::SOURCE);
    node.setProperty (ids::id, ProjectModel::newId(), nullptr);
    node.setProperty (ids::path, file.getFullPathName(), nullptr);
    node.setProperty (ids::name, file.getFileNameWithoutExtension(), nullptr);
    FFmpegDecoder::Info info; juce::String err;
    if (FFmpegDecoder::probe (file, info, err))
    {
        node.setProperty (ids::sampleRate, info.sampleRate, nullptr);
        node.setProperty (ids::channels, info.channels, nullptr);
        node.setProperty (ids::lengthSamples, info.estimatedLength, nullptr);
    }
    sources_.appendChild (node, &project.getUndoManager());
    startLoad (node);
    return node[ids::id];
}

juce::String SourceLibrary::addDecoded (const juce::File& file, juce::AudioBuffer<float>&& audio, double sampleRate,
                                        const juce::String& displayName, const juce::ValueTree& extraProps)
{
    juce::ValueTree node (ids::SOURCE);
    const auto id = ProjectModel::newId();
    node.setProperty (ids::id, id, nullptr);
    node.setProperty (ids::path, file.getFullPathName(), nullptr);
    node.setProperty (ids::name, displayName, nullptr);
    node.setProperty (ids::sampleRate, sampleRate, nullptr);
    node.setProperty (ids::channels, audio.getNumChannels(), nullptr);
    node.setProperty (ids::lengthSamples, (juce::int64) audio.getNumSamples(), nullptr);
    if (extraProps.isValid())
        for (int i = 0; i < extraProps.getNumProperties(); ++i)
        {
            auto n = extraProps.getPropertyName (i);
            node.setProperty (n, extraProps[n], nullptr);
        }
    project.sources().appendChild (node, &project.getUndoManager());
    auto src = std::make_shared<AudioSource> (id, file, std::move (audio), sampleRate);
    { std::lock_guard<std::mutex> l (mapLock); sources[id] = src; }
    postEvent ({ id, true, {}, 1.0f, false });
    return id;
}

void SourceLibrary::loadMissing()
{
    for (const auto& s : project.sources())
        if (! get (s[ids::id]) && ! isLoading (s[ids::id]))
            startLoad (s);
}

void SourceLibrary::startLoad (const juce::ValueTree& node)
{
    const juce::String id = node[ids::id];
    juce::File file (node[ids::path].toString());
    if (! file.existsAsFile())
    {
        postEvent ({ id, false, "file not found: " + file.getFullPathName(), 0.0f, false });
        return;
    }
    { std::lock_guard<std::mutex> l (mapLock); loading[id] = 0.0f; }
    pool.addJob (new LoadJob (*this, id, file), true);
}

AudioSourcePtr SourceLibrary::get (const juce::String& id) const
{
    std::lock_guard<std::mutex> l (mapLock);
    auto it = sources.find (id);
    return it == sources.end() ? nullptr : it->second;
}
bool SourceLibrary::isLoading (const juce::String& id) const { std::lock_guard<std::mutex> l (mapLock); return loading.count (id) > 0; }
float SourceLibrary::getProgress (const juce::String& id) const { std::lock_guard<std::mutex> l (mapLock); auto it = loading.find (id); return it == loading.end() ? 1.0f : it->second; }

void SourceLibrary::removeSource (const juce::String& id) { std::lock_guard<std::mutex> l (mapLock); sources.erase (id); }
void SourceLibrary::clear() { pool.removeAllJobs (true, 2000); std::lock_guard<std::mutex> l (mapLock); sources.clear(); loading.clear(); }

void SourceLibrary::handleAsyncUpdate()
{
    std::vector<Event> events;
    { std::lock_guard<std::mutex> l (eventLock); events.swap (pendingEvents); }
    for (auto& e : events)
    {
        if (e.isProgress)
        {
            { std::lock_guard<std::mutex> l (mapLock); if (loading.count (e.id)) loading[e.id] = e.progress; }
            listeners.call ([&] (Listener& l) { l.sourceProgress (e.id, e.progress); });
        }
        else if (e.ok)
        {
            // record real decoded length in the model
            auto node = ProjectModel::findByIdIn (project.sources(), ids::SOURCE, e.id);
            if (auto src = get (e.id); node.isValid() && src)
            {
                node.setProperty (ids::lengthSamples, src->getLengthSamples(), nullptr);
                node.setProperty (ids::sampleRate, src->sampleRate, nullptr);
                node.setProperty (ids::channels, src->getNumChannels(), nullptr);
            }
            listeners.call ([&] (Listener& l) { l.sourceLoaded (e.id); });
        }
        else
        {
            log ("Source load failed: " + e.error);
            listeners.call ([&] (Listener& l) { l.sourceFailed (e.id, e.error); });
        }
    }
}
} // namespace mashup
