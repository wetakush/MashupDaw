#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Analysis/BpmDetector.h"
#include "Analysis/KeyDetector.h"
#include "Analysis/TransientDetector.h"
#include "Analysis/PhraseDetector.h"
#include "DSP/Loudness.h"
#include "DSP/Stretcher.h"
#include "Waveform/WaveformCache.h"
#include "Core/MusicalKey.h"
using namespace mashup;

/** Synthesises a "song": kick on every beat, snare on 2/4, hat 8ths, plus a chord progression in a given key. */
static juce::AudioBuffer<float> makeSong (double sr, double bpm, double seconds, int keyRoot, bool minor, int beatsPerBar = 4, double firstBeatOffset = 0.0)
{
    juce::AudioBuffer<float> b (1, (int) (sr * seconds)); b.clear();
    float* d = b.getWritePointer (0);
    const double beat = 60.0 / bpm;
    juce::Random rng (42);
    // drums
    int beatIndex = 0;
    for (double t = firstBeatOffset; t < seconds; t += beat, ++beatIndex)
    {
        const int s = (int) (t * sr);
        for (int i = 0; i < (int) (0.12 * sr) && s + i < b.getNumSamples(); ++i)
        {
            const double tt = i / sr;
            const double f = 150.0 * std::exp (-tt * 30.0) + 45.0;
            d[s + i] += (float) (0.9 * std::sin (2 * juce::MathConstants<double>::pi * f * tt) * std::exp (-tt * 18.0));   // kick
        }
        if (beatIndex % beatsPerBar == 0)   // accent the downbeat: extra low tom + crash-ish noise
            for (int i = 0; i < (int) (0.3 * sr) && s + i < b.getNumSamples(); ++i) d[s + i] += (float) (0.4 * rng.nextFloat() * std::exp (-i / sr * 12.0));
        if (beatIndex % beatsPerBar == 1 || beatIndex % beatsPerBar == 3)
            for (int i = 0; i < (int) (0.15 * sr) && s + i < b.getNumSamples(); ++i) d[s + i] += (float) (0.5 * (rng.nextFloat() * 2 - 1) * std::exp (-i / sr * 25.0));
        const int hat = (int) ((t + beat / 2) * sr);
        for (int i = 0; i < (int) (0.04 * sr) && hat + i < b.getNumSamples(); ++i) d[hat + i] += (float) (0.2 * (rng.nextFloat() * 2 - 1) * std::exp (-i / sr * 80.0));
    }
    // chords: I - IV - V - I (major) or i - VI - III - VII (minor), one chord per bar, sawtooth-ish with harmonics
    const int majorDeg[4] = { 0, 5, 7, 0 }; const int minorDeg[4] = { 0, 8, 3, 10 };
    const double bar = beat * beatsPerBar;
    int barIdx = 0;
    for (double t = firstBeatOffset; t < seconds; t += bar, ++barIdx)
    {
        const int root = (keyRoot + (minor ? minorDeg[barIdx % 4] : majorDeg[barIdx % 4])) % 12;
        // chord quality of each degree inside the key
        const bool chordMinor = minor ? (barIdx % 4 == 0) : false;
        const int notes[3] = { root, root + (chordMinor ? 3 : 4), root + 7 };
        const int s0 = (int) (t * sr), s1 = std::min (b.getNumSamples(), (int) ((t + bar) * sr));
        for (int n = 0; n < 3; ++n)
        {
            const double f = 440.0 * std::pow (2.0, (notes[n] - 9 - 12) / 12.0);   // around octave 3
            for (int s = s0; s < s1; ++s)
            {
                const double tt = (s - s0) / sr;
                double v = 0; for (int h = 1; h <= 5; ++h) v += std::sin (2 * juce::MathConstants<double>::pi * f * h * s / sr) / h;
                d[s] += (float) (0.12 * v * std::min (1.0, tt * 20.0));
            }
        }
    }
    // normalise
    float mx = 0; for (int i = 0; i < b.getNumSamples(); ++i) mx = std::max (mx, std::abs (d[i]));
    b.applyGain (0.9f / mx);
    return b;
}

class AnalysisTests : public juce::UnitTest
{
public:
    AnalysisTests() : juce::UnitTest ("Analysis", "Analysis") {}
    void runTest() override
    {
        const double sr = 44100.0;
        beginTest ("BPM detection on synthetic 128 bpm");
        {
            auto song = makeSong (sr, 128.0, 30.0, 9, true, 4, 0.25);
            analysis::BpmDetector det;
            auto r = det.analyse (song.getReadPointer (0), song.getNumSamples(), sr);
            logMessage ("detected bpm " + juce::String (r.bpm, 2) + " conf " + juce::String (r.confidence, 2));
            expectWithinAbsoluteError (r.bpm, 128.0, 0.6);
            expect (r.beats.size() > 50);
            // beat phase should line up with the true beats (offset 0.25 s)
            double err = 0; int cnt = 0;
            for (double bt : r.beats) { if (bt < 1.0 || bt > 28.0) continue; const double rel = std::fmod (bt - 0.25 + 60.0 / 128.0 / 2, 60.0 / 128.0) - 60.0 / 128.0 / 2; err += std::abs (rel); ++cnt; }
            logMessage ("mean beat phase error " + juce::String (err / cnt * 1000.0, 1) + " ms");
            expectLessThan (err / cnt, 0.03);
            // downbeats on bar starts (every 4 beats from 0.25 s)
            expect (! r.downbeats.empty());
            int good = 0;
            for (double db : r.downbeats) { const double bars = (db - 0.25) / (240.0 / 128.0); if (std::abs (bars - std::round (bars)) < 0.05) ++good; }
            logMessage ("downbeats on bar: " + juce::String (good) + "/" + juce::String ((int) r.downbeats.size()));
            expectGreaterThan (good, (int) r.downbeats.size() * 3 / 4);
        }
        beginTest ("BPM detection on 95 bpm");
        {
            auto song = makeSong (sr, 95.0, 30.0, 0, false);
            analysis::BpmDetector det;
            auto r = det.analyse (song.getReadPointer (0), song.getNumSamples(), sr);
            logMessage ("detected bpm " + juce::String (r.bpm, 2));
            expect (std::abs (r.bpm - 95.0) < 0.6 || std::abs (r.bpm - 190.0) < 1.2);
        }
        beginTest ("key detection");
        {
            for (auto [root, minor] : std::vector<std::pair<int, bool>> { { 9, true }, { 0, false }, { 6, true }, { 7, false } })
            {
                auto song = makeSong (sr, 120.0, 24.0, root, minor);
                analysis::KeyDetector kd;
                auto r = kd.analyse (song.getReadPointer (0), song.getNumSamples(), sr);
                logMessage ("key: expected " + juce::String (root) + (minor ? "m" : "") + " got " + juce::String (r.root) + (r.mode ? "m" : "") + " conf " + juce::String (r.confidence, 2));
                // accept the exact key or its relative major/minor
                int relRoot, relMode; key::relative (root, minor ? 1 : 0, relRoot, relMode);
                expect ((r.root == root && r.mode == (minor ? 1 : 0)) || (r.root == relRoot && r.mode == relMode));
            }
        }
        beginTest ("transients");
        {
            auto song = makeSong (sr, 120.0, 10.0, 0, false);
            auto tr = analysis::TransientDetector::detect (song.getReadPointer (0), song.getNumSamples(), sr);
            logMessage ("transients: " + juce::String ((int) tr.size()));
            expect (tr.size() >= 30 && tr.size() <= 60);   // 20 beats + 20 hats approx
        }
        beginTest ("phrases");
        {
            auto song = makeSong (sr, 120.0, 64.0, 0, false);
            // make bars 8..15 louder to create a "chorus"
            float* d = song.getWritePointer (0);
            for (int i = (int) (16.0 * sr); i < (int) (32.0 * sr); ++i) d[i] *= 0.5f;
            std::vector<double> downbeats; for (double t = 0; t < 64.0; t += 2.0) downbeats.push_back (t);
            auto ph = analysis::PhraseDetector::detect (song.getReadPointer (0), song.getNumSamples(), sr, downbeats, 120.0);
            logMessage ("phrases: " + juce::String ((int) ph.size()));
            for (auto& p : ph) logMessage ("  " + p.label + " " + juce::String (p.start, 1) + "-" + juce::String (p.end, 1) + " conf " + juce::String (p.confidence, 2));
            expect (ph.size() >= 2);
            bool boundaryNear16 = false, boundaryNear32 = false;
            for (auto& p : ph) { if (std::abs (p.start - 16.0) < 2.1) boundaryNear16 = true; if (std::abs (p.start - 32.0) < 2.1) boundaryNear32 = true; }
            expect (boundaryNear16 && boundaryNear32);
        }
        beginTest ("loudness");
        {
            juce::AudioBuffer<float> sine (2, (int) (sr * 5));
            for (int i = 0; i < sine.getNumSamples(); ++i) { const float v = (float) std::sin (2 * juce::MathConstants<double>::pi * 1000.0 * i / sr) * 0.5f; sine.setSample (0, i, v); sine.setSample (1, i, v); }
            double lufs, tp, lra; dsp::LoudnessMeter::analyse (sine, sr, lufs, tp, lra);
            logMessage ("1 kHz stereo -6 dBFS sine: " + juce::String (lufs, 2) + " LUFS");
            // BS.1770: a -6 dBFS 1 kHz sine in both channels reads about -6.0 LUFS (K-weighting ~0 dB at 1 kHz, +3 dB for two channels, -0.691 offset => -9.0+3.0 ≈ -6.0)
            expectWithinAbsoluteError (lufs, -6.0, 0.7);
            expectWithinAbsoluteError (tp, 0.5, 0.01);
        }
        beginTest ("offline stretch changes length, keeps pitch");
        {
            juce::AudioBuffer<float> sine (1, (int) sr * 2);
            for (int i = 0; i < sine.getNumSamples(); ++i) sine.setSample (0, i, (float) std::sin (2 * juce::MathConstants<double>::pi * 440.0 * i / sr));
            auto out = dsp::stretchOffline (sine, sr, 1.5, 1.0, StretchMode::HighQuality);
            expectWithinAbsoluteError ((double) out.getNumSamples() / sine.getNumSamples(), 1.5, 0.02);
            // zero-crossing based frequency estimate of the middle of the output
            int zc = 0; const int a = out.getNumSamples() / 3, bnd = 2 * out.getNumSamples() / 3;
            for (int i = a + 1; i < bnd; ++i) if (out.getSample (0, i - 1) < 0 && out.getSample (0, i) >= 0) ++zc;
            const double freq = zc / ((bnd - a) / sr);
            logMessage ("stretched freq " + juce::String (freq, 1));
            expectWithinAbsoluteError (freq, 440.0, 5.0);
        }
        beginTest ("waveform cache levels agree");
        {
            juce::AudioBuffer<float> noise (2, 300000); juce::Random r (1);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < noise.getNumSamples(); ++i) noise.setSample (c, i, r.nextFloat() * 2 - 1);
            auto src = std::make_shared<AudioSource> ("n", juce::File(), std::move (noise), 48000.0);
            WaveformCache cache (src);
            expect (cache.build());
            auto fine = cache.summarise (0, 0, 65536), coarse = cache.summarise (0, 0, 65536 * 4);
            expectGreaterThan (fine.max, 0.9f); expectLessThan (fine.min, -0.9f);
            expectWithinAbsoluteError (fine.rms, 0.577f, 0.02f); expectWithinAbsoluteError (coarse.rms, 0.577f, 0.02f);
            std::vector<WaveformCache::Bin> px; cache.render (0, 0, 300000, 100, px);
            expectEquals ((int) px.size(), 100);
            auto raw = cache.summarise (0, 100, 200);   // below base bin -> raw path
            expectGreaterThan (raw.max, 0.5f);
            auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mashup_test.wfc");
            expect (cache.saveTo (tmp));
            WaveformCache loaded (src); expect (loaded.loadFrom (tmp)); expect (loaded.isReady());
            expectWithinAbsoluteError (loaded.summarise (0, 0, 65536).rms, fine.rms, 1e-6f);
            tmp.deleteFile();
        }
    }
};
static AnalysisTests analysisTests;
