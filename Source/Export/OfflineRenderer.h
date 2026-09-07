#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <set>
#include "FFmpegEncoder.h"

namespace mashup
{
class Session;

/** Renders the project (or a subset of tracks) offline through a private RenderGraph. Worker thread. */
class OfflineRenderer
{
public:
    struct Options
    {
        double startBeat = 0, endBeat = 0;         // endBeat <= startBeat => whole project (+ tail)
        double sampleRate = 48000;
        std::set<juce::String> onlyTracks;         // empty = all
        bool normalizePeak = false; double peakDbfs = -1.0;
        bool normalizeLoudness = false; double targetLufs = -14.0;
        double tailSeconds = 2.0;
    };
    struct Result { juce::AudioBuffer<float> audio; double sampleRate = 48000; double lufs = -100, peak = 0; juce::String error; };

    static Result render (Session&, const Options&, const std::function<bool (float)>& progress = {});

    /** Convenience: render + encode. Returns error string or empty. */
    static juce::String renderToFile (Session&, const Options&, const juce::File&, const FFmpegEncoder::Settings&, const std::function<bool (float)>& progress = {});
};
} // namespace mashup
