#pragma once
#include <juce_events/juce_events.h>
#include <map>
#include "Project/Session.h"

namespace mashup
{
/** Implements Write / Touch / Latch: while the transport plays, watches each track's volume/pan/sends and writes
    the values the user changes into automation lanes at the playhead. Read mode plays lanes back (engine). */
class AutomationRecorder : private juce::Timer
{
public:
    explicit AutomationRecorder (Session&);
    ~AutomationRecorder() override;
    /** Called by UI controls when the user starts/stops touching a parameter (Touch mode). */
    void touchBegin (const juce::String& trackId, const juce::String& param);
    void touchEnd (const juce::String& trackId, const juce::String& param);
    bool isRecordingLane (const juce::String& trackId, const juce::String& param) const;
private:
    void timerCallback() override;
    struct LaneState { float lastValue = -1; double lastBeat = -1; bool active = false; int idleTicks = 0; };
    Session& session;
    std::map<juce::String, LaneState> lanes;   // key trackId|param
    std::map<juce::String, int> touching;
    bool wasPlaying = false;
};
}
