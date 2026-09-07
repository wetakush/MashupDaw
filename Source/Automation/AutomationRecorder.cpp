#include "AutomationRecorder.h"
#include "AudioEngine/AudioEngine.h"
#include "Tracks/TrackModel.h"
#include "Mixer/MixerOps.h"
#include "Automation/AutomationCurve.h"

namespace mashup
{
AutomationRecorder::AutomationRecorder (Session& s) : session (s) { startTimerHz (30); }
AutomationRecorder::~AutomationRecorder() { stopTimer(); }

void AutomationRecorder::touchBegin (const juce::String& t, const juce::String& p) { touching[t + "|" + p]++; }
void AutomationRecorder::touchEnd (const juce::String& t, const juce::String& p) { auto& c = touching[t + "|" + p]; c = juce::jmax (0, c - 1); }
bool AutomationRecorder::isRecordingLane (const juce::String& t, const juce::String& p) const { auto it = lanes.find (t + "|" + p); return it != lanes.end() && it->second.active; }

void AutomationRecorder::timerCallback()
{
    auto& engine = session.getAudioEngine();
    const bool playing = engine.getTransport().isPlaying();
    auto& p = session.getProject();
    const double beat = engine.getPositionBeat();
    static const char* params[] = { "volume", "pan", "send0", "send1" };
    for (auto tn : p.tracks())
    {
        TrackModel track (tn);
        const auto mode = (AutomationMode) (int) tn.getProperty ("automationMode", 0);
        const juce::String tid = track.getId();
        for (const char* param : params)
        {
            auto& st = lanes[tid + "|" + param];
            float value = 0;
            if (juce::String (param) == "volume") value = AutomationCurve::volumeToNorm (track.getVolume());
            else if (juce::String (param) == "pan") value = AutomationCurve::panToNorm (track.getPan());
            else value = (float) mixerops::getSend (tn, param[4] - '0');
            const bool changed = st.lastValue >= 0 && std::abs (value - st.lastValue) > 1e-4;
            const bool touched = touching[tid + "|" + param] > 0;
            bool write = false;
            if (playing && mode != AutomationMode::Read && mode != AutomationMode::Off)
            {
                if (mode == AutomationMode::Write) write = true;
                else if (mode == AutomationMode::Touch) { if (changed || touched) { st.active = true; st.idleTicks = 0; } else if (st.active && ++st.idleTicks > 8) st.active = false; write = st.active; }
                else if (mode == AutomationMode::Latch) { if (changed || touched) st.active = true; write = st.active; }
            }
            else st.active = false;
            if (write)
            {
                auto lane = AutomationCurve::getOrCreateLane (tn, param, nullptr);
                auto curve = AutomationCurve::fromLane (lane);
                if (st.lastBeat >= 0 && beat > st.lastBeat) curve.removePointsInRange (st.lastBeat + 1e-6, beat);
                if (changed || st.lastBeat < 0 || beat - st.lastBeat >= 0.25 || ! curve.getPoints().empty()) curve.addPoint (beat, value);
                curve.writeToLane (lane, nullptr);   // recording is not an undoable edit per tick (the model listener still rebuilds the graph)
                st.lastBeat = beat;
            }
            else st.lastBeat = -1;
            st.lastValue = value;
        }
    }
    if (wasPlaying && ! playing) for (auto& [k, st] : lanes) { st.active = false; st.lastBeat = -1; }
    wasPlaying = playing;
}
} // namespace mashup
