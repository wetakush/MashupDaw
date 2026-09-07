#pragma once
#include <juce_events/juce_events.h>
#include <juce_data_structures/juce_data_structures.h>
#include <map>
#include <mutex>
#include "AudioSource.h"

namespace mashup
{
class ProjectModel;

/** Registry of decoded audio sources, keyed by the SOURCE id in the project model.
    Files are decoded asynchronously on the thread pool; listeners are notified on the message thread. */
class SourceLibrary : private juce::AsyncUpdater
{
public:
    SourceLibrary (ProjectModel&, juce::ThreadPool&);
    ~SourceLibrary() override;

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void sourceLoaded (const juce::String& sourceId) = 0;
        virtual void sourceFailed (const juce::String& sourceId, const juce::String& error) {}
        virtual void sourceProgress (const juce::String& /*sourceId*/, float /*progress*/) {}
    };
    void addListener (Listener* l)    { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    /** Adds a SOURCE node to the project (if not already there for this file) and starts decoding.
        Returns the source id. Message thread. */
    juce::String importFile (const juce::File&);

    /** Registers already-decoded audio (e.g. a separated stem written to `file`). */
    juce::String addDecoded (const juce::File& file, juce::AudioBuffer<float>&& audio, double sampleRate,
                             const juce::String& displayName, const juce::ValueTree& extraProps = {});

    /** Starts (re)loading every SOURCE in the project that is not yet decoded. */
    void loadMissing();

    /** Message thread or audio thread: returns the decoded source or nullptr while it is still loading. */
    AudioSourcePtr get (const juce::String& sourceId) const;
    bool isLoading (const juce::String& sourceId) const;
    float getProgress (const juce::String& sourceId) const;

    void removeSource (const juce::String& sourceId);
    void clear();

private:
    void startLoad (const juce::ValueTree& sourceNode);
    struct Event;
    void postEvent (Event);
    void handleAsyncUpdate() override;

    struct Event { juce::String id; bool ok; juce::String error; float progress; bool isProgress; };
    friend class LoadJob;

    ProjectModel& project;
    juce::ThreadPool& pool;
    mutable std::mutex mapLock;   // message thread + workers; never touched by the audio thread (engine keeps its own shared_ptrs)
    std::map<juce::String, AudioSourcePtr> sources;
    std::map<juce::String, float> loading;
    std::vector<Event> pendingEvents;
    std::mutex eventLock;
    juce::ListenerList<Listener> listeners;
    class LoadJob;
};
} // namespace mashup
