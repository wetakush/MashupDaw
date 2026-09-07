#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

namespace mashup
{
/** Encodes a float buffer to WAV / FLAC / MP3 / AAC through libavformat/libavcodec. Worker thread. */
class FFmpegEncoder
{
public:
    enum class Format { WAV, FLAC, MP3, AAC };
    struct Settings
    {
        Format format = Format::WAV;
        int sampleRate = 48000;     // output rate (resampled if different from the buffer's)
        int bitDepth = 24;          // WAV: 16/24/32(float); FLAC: 16/24
        int bitrateKbps = 320;      // MP3/AAC
        bool mono = false;
    };
    static const char* extensionFor (Format f) { switch (f) { case Format::WAV: return ".wav"; case Format::FLAC: return ".flac"; case Format::MP3: return ".mp3"; case Format::AAC: return ".m4a"; } return ".wav"; }

    /** Writes `audio` (any channel count 1..2, at `inputSampleRate`) to `file`. Returns an error string or empty. */
    static juce::String encode (const juce::AudioBuffer<float>& audio, double inputSampleRate, const juce::File& file, const Settings&,
                                const std::function<bool (float)>& progress = {});
};
} // namespace mashup
