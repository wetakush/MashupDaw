#pragma once
#include <juce_events/juce_events.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Project/Session.h"

namespace mashup
{
/** Turns transport record state into takes: starts/stops the engine's Recorder and, when a take finishes,
    writes a WAV into the project cache and places clips on every armed track. */
class RecordingController : private juce::Timer
{
public:
    explicit RecordingController (Session&);
    ~RecordingController() override;
private:
    void timerCallback() override;
    void takeFinished (juce::AudioBuffer<float>&& take, double sampleRate, juce::int64 timelineStart);
    Session& session;
    int takeCounter = 0;
};
}
