#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

namespace mashup
{
/** Decodes any FFmpeg-readable file into a float AudioBuffer (planar, native sample rate).
    Worker-thread only. */
class FFmpegDecoder
{
public:
    struct Info
    {
        double sampleRate = 0;
        int channels = 0;
        juce::int64 estimatedLength = 0;   // may be 0 if unknown
        juce::String codecName;
        juce::String title, artist;
    };

    struct Result
    {
        bool ok = false;
        juce::String error;
        Info info;
        juce::AudioBuffer<float> audio;
    };

    /** Reads only the header. */
    static bool probe (const juce::File&, Info& out, juce::String& error);

    /** Decodes the whole file. `targetSampleRate` <= 0 keeps the native rate. Progress callback receives 0..1 and
        may return false to cancel. */
    static Result decode (const juce::File&, double targetSampleRate = 0.0,
                          const std::function<bool (float)>& progress = {});

    static bool isSupportedExtension (const juce::String& ext);
    static juce::String getSupportedWildcards();
};
} // namespace mashup
