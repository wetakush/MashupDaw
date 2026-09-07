#include "FFmpegDecoder.h"
extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace mashup
{
namespace
{
    struct FormatCtx { AVFormatContext* p = nullptr; ~FormatCtx() { if (p) avformat_close_input (&p); } };
    struct CodecCtx  { AVCodecContext* p = nullptr;  ~CodecCtx()  { if (p) avcodec_free_context (&p); } };
    struct Swr       { SwrContext* p = nullptr;      ~Swr()       { if (p) swr_free (&p); } };
    struct Frame     { AVFrame* p = av_frame_alloc(); ~Frame()    { av_frame_free (&p); } };
    struct Packet    { AVPacket* p = av_packet_alloc(); ~Packet() { av_packet_free (&p); } };

    juce::String avErr (int e) { char buf[256]; av_strerror (e, buf, sizeof (buf)); return juce::String (buf); }

    bool openInput (const juce::File& file, FormatCtx& fmt, CodecCtx& codec, int& streamIndex, juce::String& error)
    {
        int r = avformat_open_input (&fmt.p, file.getFullPathName().toRawUTF8(), nullptr, nullptr);
        if (r < 0) { error = "avformat_open_input: " + avErr (r); return false; }
        r = avformat_find_stream_info (fmt.p, nullptr);
        if (r < 0) { error = "avformat_find_stream_info: " + avErr (r); return false; }
        const AVCodec* dec = nullptr;
        streamIndex = av_find_best_stream (fmt.p, AVMEDIA_TYPE_AUDIO, -1, -1, &dec, 0);
        if (streamIndex < 0 || dec == nullptr) { error = "no audio stream"; return false; }
        codec.p = avcodec_alloc_context3 (dec);
        if (! codec.p) { error = "avcodec_alloc_context3 failed"; return false; }
        r = avcodec_parameters_to_context (codec.p, fmt.p->streams[streamIndex]->codecpar);
        if (r < 0) { error = "avcodec_parameters_to_context: " + avErr (r); return false; }
        codec.p->pkt_timebase = fmt.p->streams[streamIndex]->time_base;
        r = avcodec_open2 (codec.p, dec, nullptr);
        if (r < 0) { error = "avcodec_open2: " + avErr (r); return false; }
        if (codec.p->ch_layout.nb_channels <= 0) { error = "no channels"; return false; }
        return true;
    }

    void fillInfo (FormatCtx& fmt, CodecCtx& codec, int streamIndex, FFmpegDecoder::Info& info)
    {
        info.sampleRate = codec.p->sample_rate;
        info.channels = codec.p->ch_layout.nb_channels;
        info.codecName = avcodec_get_name (codec.p->codec_id);
        auto* st = fmt.p->streams[streamIndex];
        if (st->duration > 0)
            info.estimatedLength = (juce::int64) (st->duration * av_q2d (st->time_base) * info.sampleRate);
        else if (fmt.p->duration > 0)
            info.estimatedLength = (juce::int64) ((double) fmt.p->duration / AV_TIME_BASE * info.sampleRate);
        if (auto* t = av_dict_get (fmt.p->metadata, "title", nullptr, 0)) info.title = t->value;
        if (auto* a = av_dict_get (fmt.p->metadata, "artist", nullptr, 0)) info.artist = a->value;
    }
}

bool FFmpegDecoder::probe (const juce::File& file, Info& out, juce::String& error)
{
    FormatCtx fmt; CodecCtx codec; int idx = 0;
    if (! openInput (file, fmt, codec, idx, error)) return false;
    fillInfo (fmt, codec, idx, out);
    return true;
}

FFmpegDecoder::Result FFmpegDecoder::decode (const juce::File& file, double targetSampleRate, const std::function<bool (float)>& progress)
{
    Result res;
    FormatCtx fmt; CodecCtx codec; int idx = 0;
    if (! openInput (file, fmt, codec, idx, res.error)) return res;
    fillInfo (fmt, codec, idx, res.info);

    const int outChannels = juce::jlimit (1, 2, res.info.channels);   // mono or stereo; downmix anything wider
    const int outRate = targetSampleRate > 0 ? (int) targetSampleRate : codec.p->sample_rate;

    Swr swr;
    AVChannelLayout outLayout;
    if (outChannels == 1) outLayout = AV_CHANNEL_LAYOUT_MONO; else outLayout = AV_CHANNEL_LAYOUT_STEREO;
    int r = swr_alloc_set_opts2 (&swr.p, &outLayout, AV_SAMPLE_FMT_FLTP, outRate,
                                 &codec.p->ch_layout, codec.p->sample_fmt, codec.p->sample_rate, 0, nullptr);
    if (r < 0 || (r = swr_init (swr.p)) < 0) { res.error = "swr init: " + avErr (r); return res; }

    // grow in chunks to avoid quadratic reallocation
    juce::int64 capacity = std::max<juce::int64> (1 << 16, (juce::int64) (res.info.estimatedLength * (double) outRate / res.info.sampleRate) + 4096);
    juce::AudioBuffer<float> out (outChannels, (int) capacity);
    juce::int64 written = 0;

    Frame frame; Packet pkt;
    std::vector<float*> tmpPlanes ((size_t) outChannels);
    juce::AudioBuffer<float> tmp (outChannels, 8192);

    auto pushConverted = [&] (const uint8_t** inData, int inSamples) -> bool
    {
        const int maxOut = swr_get_out_samples (swr.p, inSamples);
        if (maxOut > tmp.getNumSamples()) tmp.setSize (outChannels, maxOut, false, false, true);
        for (int c = 0; c < outChannels; ++c) tmpPlanes[(size_t) c] = tmp.getWritePointer (c);
        const int got = swr_convert (swr.p, (uint8_t**) tmpPlanes.data(), maxOut, inData, inSamples);
        if (got < 0) return false;
        if (written + got > capacity)
        {
            capacity = juce::jmax (capacity * 2, written + got + 4096);
            out.setSize (outChannels, (int) capacity, true, false, true);
        }
        for (int c = 0; c < outChannels; ++c)
            out.copyFrom (c, (int) written, tmp, c, 0, got);
        written += got;
        return true;
    };

    const double totalDur = fmt.p->duration > 0 ? (double) fmt.p->duration / AV_TIME_BASE : 0.0;
    int progressCounter = 0;

    while ((r = av_read_frame (fmt.p, pkt.p)) >= 0)
    {
        if (pkt.p->stream_index != idx) { av_packet_unref (pkt.p); continue; }
        r = avcodec_send_packet (codec.p, pkt.p);
        av_packet_unref (pkt.p);
        if (r < 0 && r != AVERROR (EAGAIN)) { res.error = "send_packet: " + avErr (r); return res; }
        while ((r = avcodec_receive_frame (codec.p, frame.p)) >= 0)
        {
            if (! pushConverted ((const uint8_t**) frame.p->extended_data, frame.p->nb_samples)) { res.error = "swr_convert failed"; return res; }
            if (progress && (++progressCounter & 31) == 0 && totalDur > 0)
            {
                const double t = frame.p->pts != AV_NOPTS_VALUE ? frame.p->pts * av_q2d (fmt.p->streams[idx]->time_base) : 0.0;
                if (! progress ((float) juce::jlimit (0.0, 1.0, t / totalDur))) { res.error = "cancelled"; return res; }
            }
            av_frame_unref (frame.p);
        }
    }
    // flush decoder
    avcodec_send_packet (codec.p, nullptr);
    while (avcodec_receive_frame (codec.p, frame.p) >= 0)
    {
        pushConverted ((const uint8_t**) frame.p->extended_data, frame.p->nb_samples);
        av_frame_unref (frame.p);
    }
    // flush resampler
    pushConverted (nullptr, 0);

    out.setSize (outChannels, (int) written, true, true, true);
    res.audio = std::move (out);
    res.info.sampleRate = outRate;
    res.info.channels = outChannels;
    res.info.estimatedLength = written;
    res.ok = written > 0;
    if (! res.ok && res.error.isEmpty()) res.error = "file contains no audio samples";
    return res;
}

bool FFmpegDecoder::isSupportedExtension (const juce::String& e)
{
    static const juce::StringArray exts { "wav", "aif", "aiff", "flac", "mp3", "m4a", "aac", "ogg", "opus", "wma", "mp4", "mkv", "webm", "mov", "caf", "alac", "wv", "ape" };
    return exts.contains (e.trimCharactersAtStart (".").toLowerCase());
}

juce::String FFmpegDecoder::getSupportedWildcards()
{
    return "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.aac;*.ogg;*.opus;*.wma;*.mp4;*.mkv;*.webm;*.mov;*.caf;*.wv;*.ape";
}
} // namespace mashup
