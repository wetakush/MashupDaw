#include "FFmpegEncoder.h"
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
    juce::String avErr (int e) { char buf[256]; av_strerror (e, buf, sizeof (buf)); return juce::String (buf); }
    struct Cleanup
    {
        AVFormatContext* fmt = nullptr; AVCodecContext* codec = nullptr; SwrContext* swr = nullptr; AVFrame* frame = nullptr; AVPacket* pkt = nullptr; bool opened = false;
        ~Cleanup()
        {
            if (frame) av_frame_free (&frame); if (pkt) av_packet_free (&pkt); if (swr) swr_free (&swr); if (codec) avcodec_free_context (&codec);
            if (fmt) { if (opened && ! (fmt->oformat->flags & AVFMT_NOFILE)) avio_closep (&fmt->pb); avformat_free_context (fmt); }
        }
    };
    bool supportsFormat (const AVCodec* c, AVSampleFormat f)
    {
        const enum AVSampleFormat* fmts = nullptr; int n = 0;
       #if LIBAVCODEC_VERSION_MAJOR >= 61
        if (avcodec_get_supported_config (nullptr, c, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void**) &fmts, &n) < 0 || ! fmts) return false;
        for (int i = 0; i < n; ++i) if (fmts[i] == f) return true;
        return false;
       #else
        for (const enum AVSampleFormat* p = c->sample_fmts; p && *p != AV_SAMPLE_FMT_NONE; ++p) if (*p == f) return true;
        return false;
       #endif
    }
}

juce::String FFmpegEncoder::encode (const juce::AudioBuffer<float>& audio, double inRate, const juce::File& file, const Settings& s, const std::function<bool (float)>& progress)
{
    Cleanup c;
    const char* formatName = s.format == Format::WAV ? "wav" : s.format == Format::FLAC ? "flac" : s.format == Format::MP3 ? "mp3" : "ipod";
    int r = avformat_alloc_output_context2 (&c.fmt, nullptr, formatName, file.getFullPathName().toRawUTF8());
    if (r < 0 || ! c.fmt) return "avformat_alloc_output_context2: " + avErr (r);

    AVCodecID codecId = AV_CODEC_ID_NONE;
    switch (s.format)
    {
        case Format::WAV: codecId = s.bitDepth >= 32 ? AV_CODEC_ID_PCM_F32LE : s.bitDepth == 24 ? AV_CODEC_ID_PCM_S24LE : AV_CODEC_ID_PCM_S16LE; break;
        case Format::FLAC: codecId = AV_CODEC_ID_FLAC; break;
        case Format::MP3: codecId = AV_CODEC_ID_MP3; break;
        case Format::AAC: codecId = AV_CODEC_ID_AAC; break;
    }
    const AVCodec* enc = s.format == Format::MP3 ? avcodec_find_encoder_by_name ("libmp3lame") : nullptr;
    if (! enc) enc = avcodec_find_encoder (codecId);
    if (! enc) return "no encoder available for this format";
    AVStream* stream = avformat_new_stream (c.fmt, nullptr);
    if (! stream) return "avformat_new_stream failed";
    c.codec = avcodec_alloc_context3 (enc);
    const int outCh = s.mono ? 1 : juce::jmin (2, audio.getNumChannels());
    if (outCh == 1) c.codec->ch_layout = AV_CHANNEL_LAYOUT_MONO; else c.codec->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    c.codec->sample_rate = s.sampleRate;
    c.codec->time_base = { 1, s.sampleRate };
    // pick a sample format the encoder supports
    AVSampleFormat wanted = AV_SAMPLE_FMT_FLTP;
    if (s.format == Format::WAV) wanted = s.bitDepth >= 32 ? AV_SAMPLE_FMT_FLT : s.bitDepth == 24 ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S16;
    else if (s.format == Format::FLAC) wanted = s.bitDepth == 24 ? AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S16;
    if (! supportsFormat (enc, wanted))
        for (auto alt : { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_S32P })
            if (supportsFormat (enc, alt)) { wanted = alt; break; }
    c.codec->sample_fmt = wanted;
    if (s.format == Format::FLAC && s.bitDepth == 24) c.codec->bits_per_raw_sample = 24;
    if (s.format == Format::MP3 || s.format == Format::AAC) c.codec->bit_rate = (int64_t) s.bitrateKbps * 1000;
    if (c.fmt->oformat->flags & AVFMT_GLOBALHEADER) c.codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if ((r = avcodec_open2 (c.codec, enc, nullptr)) < 0) return "avcodec_open2: " + avErr (r);
    if ((r = avcodec_parameters_from_context (stream->codecpar, c.codec)) < 0) return "avcodec_parameters_from_context: " + avErr (r);
    stream->time_base = c.codec->time_base;
    if (! (c.fmt->oformat->flags & AVFMT_NOFILE))
    {
        if ((r = avio_open (&c.fmt->pb, file.getFullPathName().toRawUTF8(), AVIO_FLAG_WRITE)) < 0) return "avio_open: " + avErr (r);
        c.opened = true;
    }
    if ((r = avformat_write_header (c.fmt, nullptr)) < 0) return "avformat_write_header: " + avErr (r);

    // resampler: planar float input at inRate -> encoder format/rate
    AVChannelLayout inLayout; if (audio.getNumChannels() == 1) inLayout = AV_CHANNEL_LAYOUT_MONO; else inLayout = AV_CHANNEL_LAYOUT_STEREO;
    if ((r = swr_alloc_set_opts2 (&c.swr, &c.codec->ch_layout, c.codec->sample_fmt, c.codec->sample_rate, &inLayout, AV_SAMPLE_FMT_FLTP, (int) inRate, 0, nullptr)) < 0 || (r = swr_init (c.swr)) < 0)
        return "swr init: " + avErr (r);

    c.frame = av_frame_alloc(); c.pkt = av_packet_alloc();
    const int frameSize = c.codec->frame_size > 0 ? c.codec->frame_size : 4096;
    c.frame->format = c.codec->sample_fmt; c.frame->sample_rate = c.codec->sample_rate; c.frame->nb_samples = frameSize;
    av_channel_layout_copy (&c.frame->ch_layout, &c.codec->ch_layout);
    if ((r = av_frame_get_buffer (c.frame, 0)) < 0) return "av_frame_get_buffer: " + avErr (r);

    int64_t pts = 0;
    auto flushPackets = [&] (bool final) -> int
    {
        int rr = avcodec_send_frame (c.codec, final ? nullptr : c.frame);
        if (rr < 0) return rr;
        while ((rr = avcodec_receive_packet (c.codec, c.pkt)) >= 0)
        {
            c.pkt->stream_index = stream->index;
            av_packet_rescale_ts (c.pkt, c.codec->time_base, stream->time_base);
            rr = av_interleaved_write_frame (c.fmt, c.pkt);
            av_packet_unref (c.pkt);
            if (rr < 0) return rr;
        }
        return (rr == AVERROR (EAGAIN) || rr == AVERROR_EOF) ? 0 : rr;
    };

    const int inCh = juce::jmin (2, audio.getNumChannels());
    const int total = audio.getNumSamples();
    int pos = 0;
    const int inChunk = 4096;
    while (pos < total)
    {
        const int n = juce::jmin (inChunk, total - pos);
        const uint8_t* inPtrs[2] = { (const uint8_t*) audio.getReadPointer (0, pos), (const uint8_t*) audio.getReadPointer (inCh > 1 ? 1 : 0, pos) };
        // convert into the frame in encoder-frame-sized pieces
        int inLeft = n; bool first = true;
        while (inLeft > 0 || swr_get_out_samples (c.swr, 0) >= frameSize)
        {
            if ((r = av_frame_make_writable (c.frame)) < 0) return "av_frame_make_writable: " + avErr (r);
            const int got = swr_convert (c.swr, c.frame->data, frameSize, first ? inPtrs : nullptr, first ? inLeft : 0);
            first = false; inLeft = 0;
            if (got < 0) return "swr_convert: " + avErr (got);
            if (got == 0) break;
            if (got < frameSize && pos + n < total) { /* not enough for a full frame: swr keeps the remainder queued only when we pass fewer out samples; simplest: pad handled at flush */ }
            c.frame->nb_samples = got; c.frame->pts = pts; pts += got;
            if ((r = flushPackets (false)) < 0) return "encode: " + avErr (r);
            c.frame->nb_samples = frameSize;
            if (got < frameSize) break;
        }
        pos += n;
        if (progress && ! progress ((float) pos / total)) return "cancelled";
    }
    // drain resampler
    for (;;)
    {
        if ((r = av_frame_make_writable (c.frame)) < 0) break;
        const int got = swr_convert (c.swr, c.frame->data, frameSize, nullptr, 0);
        if (got <= 0) break;
        c.frame->nb_samples = got; c.frame->pts = pts; pts += got;
        if ((r = flushPackets (false)) < 0) return "encode: " + avErr (r);
        c.frame->nb_samples = frameSize;
    }
    if ((r = flushPackets (true)) < 0) return "flush: " + avErr (r);
    if ((r = av_write_trailer (c.fmt)) < 0) return "av_write_trailer: " + avErr (r);
    return {};
}
} // namespace mashup
