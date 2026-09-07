#include <juce_core/juce_core.h>
#include "AudioEngine/RenderGraph.h"
#include "AudioEngine/ClipPlayer.h"
#include "AudioEngine/GraphBuilder.h"
#include "Import/SourceLibrary.h"
#include "Effects/EffectFactory.h"
#include "Project/ProjectModel.h"
#include "Tracks/TrackModel.h"
#include "Core/MusicalKey.h"
using namespace mashup;

static AudioSourcePtr makeSine (double sr, double freq, double seconds, const juce::String& id = "src")
{
    juce::AudioBuffer<float> b (2, (int) (sr * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = (float) std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / sr);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return std::make_shared<AudioSource> (id, juce::File(), std::move (b), sr);
}

static double rms (const juce::AudioBuffer<float>& b, int ch, int start, int n)
{
    double s = 0; for (int i = start; i < start + n; ++i) s += b.getSample (ch, i) * b.getSample (ch, i); return std::sqrt (s / n);
}

class EngineTests : public juce::UnitTest
{
public:
    EngineTests() : juce::UnitTest ("AudioEngine", "Engine") {}

    void runTest() override
    {
        const double sr = 48000.0;
        beginTest ("clip player renders sample-accurately (repitch mode)");
        {
            ClipPlayer::Static st;
            st.clipId = "c"; st.source = makeSine (sr, 1000.0, 2.0);
            st.startBeat = 2.0; st.lengthBeats = 2.0; st.mode = StretchMode::Repitch;
            ClipPlayer p (st, sr, 512);
            juce::AudioBuffer<float> out (2, 512);
            // at 120 bpm beat 2 = 1.0s = sample 48000. Render a block spanning 47744..48256
            out.clear();
            ClipPlayer::Segment seg { 47744 / sr, 47744 / sr * 2.0, 120.0, 512, sr };
            p.render (out, seg);
            expectWithinAbsoluteError (rms (out, 0, 0, 200), 0.0, 1e-6);   // before the clip
            expectGreaterThan (rms (out, 0, 300, 200), 0.5);              // inside the clip
            expect (std::abs (out.getSample (0, 255)) < 1e-6 && std::abs (out.getSample (0, 256)) < 0.14); // starts exactly at sample 256 (sin(0)=0)
        }

        beginTest ("stretched clip keeps duration and level");
        {
            ClipPlayer::Static st;
            st.clipId = "c2"; st.source = makeSine (sr, 440.0, 4.0);
            st.startBeat = 0.0; st.lengthBeats = 8.0; st.mode = StretchMode::Realtime; st.synced = true; st.clipBpm = 100.0;
            ClipPlayer p (st, sr, 512);
            // project at 120 bpm: speed = 1.2 -> 8 beats = 4s output consumes 4.8s (source 4s -> silence in the tail)
            juce::AudioBuffer<float> out (2, 512);
            double energyEarly = 0, energyLate = 0; int nEarly = 0, nLate = 0;
            for (juce::int64 pos = 0; pos < (juce::int64) (4.0 * sr); pos += 512)
            {
                out.clear();
                ClipPlayer::Segment seg { pos / sr, pos / sr * 2.0, 120.0, 512, sr };
                p.render (out, seg);
                const double r = rms (out, 0, 0, 512);
                if (pos > sr * 0.5 && pos < sr * 3.0) { energyEarly += r; ++nEarly; }
                if (pos > sr * 3.6) { energyLate += r; ++nLate; }
            }
            expectGreaterThan (energyEarly / nEarly, 0.4);
            expectLessThan (energyLate / nLate, 0.05);
        }

        beginTest ("graph builder: track/clip model -> rendered audio, mute/solo/volume");
        {
            juce::UndoManager um; ProjectModel model (um);
            juce::ThreadPool pool (1);
            SourceLibrary lib (model, pool);
            EffectFactory fx;
            auto src = makeSine (sr, 440.0, 2.0, "s1");
            auto id = lib.addDecoded (juce::File(), juce::AudioBuffer<float> (src->buffer), sr, "sine");
            auto t1 = TrackModel::create ("A", juce::Colours::red);
            auto t2 = TrackModel::create ("B", juce::Colours::blue);
            model.tracks().appendChild (t1.getState(), nullptr); model.tracks().appendChild (t2.getState(), nullptr);
            t1.addClip (ClipModel::create (id, 0.0, 4.0, 0.0, "a"), nullptr).setStretchMode (StretchMode::Repitch, nullptr);
            t2.addClip (ClipModel::create (id, 0.0, 4.0, 0.0, "b"), nullptr).setStretchMode (StretchMode::Repitch, nullptr);
            GraphBuilder builder (model, lib, fx);
            auto g = builder.build (sr, 256, nullptr);
            expectEquals ((int) g->tracks.size(), 2);
            juce::AudioBuffer<float> out (2, 4096); out.clear();
            g->process (out, 0, 4096);
            const double both = rms (out, 0, 1024, 2048);
            expectWithinAbsoluteError (both, 2.0 / std::sqrt (2.0), 0.05);   // two unity sines summed

            t2.setMuted (true, nullptr);
            auto g2 = builder.build (sr, 256, g.get());
            expect (g2->tracks[0] == g->tracks[0]);   // untouched renderer reused
            out.clear(); g2->notifyDiscontinuity(); g2->process (out, 0, 4096);
            expectWithinAbsoluteError (rms (out, 0, 2048, 1024), 1.0 / std::sqrt (2.0), 0.05);

            t1.setSolo (true, nullptr); t2.setMuted (false, nullptr);
            auto g3 = builder.build (sr, 256, g2.get());
            out.clear(); g3->notifyDiscontinuity(); g3->process (out, 0, 4096);
            expectWithinAbsoluteError (rms (out, 0, 2048, 1024), 1.0 / std::sqrt (2.0), 0.05);

            t1.setSolo (false, nullptr); t1.setVolume (0.5, nullptr);
            auto g4 = builder.build (sr, 256, g3.get());
            for (auto& t : g4->tracks) { t->live().volume.snap(); }
            out.clear(); g4->notifyDiscontinuity(); g4->process (out, 0, 4096);
            expectWithinAbsoluteError (rms (out, 0, 2048, 1024), 1.5 / std::sqrt (2.0), 0.05);
        }

        beginTest ("tempo change inside a block is split");
        {
            juce::UndoManager um; ProjectModel model (um);
            juce::ThreadPool pool (1); SourceLibrary lib (model, pool); EffectFactory fx;
            auto src = makeSine (sr, 440.0, 8.0, "s1");
            auto id = lib.addDecoded (juce::File(), juce::AudioBuffer<float> (src->buffer), sr, "sine");
            juce::ValueTree tempo (ids::TEMPO); tempo.setProperty (ids::beat, 4.0, nullptr); tempo.setProperty (ids::bpm, 60.0, nullptr);
            model.tempoMap().appendChild (tempo, nullptr);
            auto t = TrackModel::create ("A", juce::Colours::red); model.tracks().appendChild (t.getState(), nullptr);
            auto c = t.addClip (ClipModel::create (id, 4.0, 4.0, 0.0, "a"), nullptr); c.setStretchMode (StretchMode::Repitch, nullptr);
            GraphBuilder builder (model, lib, fx);
            auto g = builder.build (sr, 1024, nullptr);
            // beat 4 at 120 bpm = 2.0 s = sample 96000. Block 95500..96524 crosses the tempo marker and clip start.
            juce::AudioBuffer<float> out (2, 1024); out.clear();
            g->process (out, 95500, 1024);
            expectWithinAbsoluteError (rms (out, 0, 0, 400), 0.0, 1e-6);
            expectGreaterThan (rms (out, 0, 600, 400), 0.5);
        }

        beginTest ("camelot");
        expectEquals (key::camelot (0, 0), juce::String ("8B"));
        expectEquals (key::camelot (9, 1), juce::String ("8A"));
        expectEquals (key::camelot (6, 1), juce::String ("11A"));   // F# minor
        expectEquals (key::camelot (9, 0), juce::String ("11B"));   // A major
        expectEquals (key::compatibility (1, 1, 6, 1), 1);          // C#m + F#m
        expectEquals (key::compatibility (0, 0, 9, 1), 1);          // C + Am
        int r, m; expect (key::parse ("F#m", r, m)); expectEquals (r, 6); expectEquals (m, 1);
        expect (key::parse ("Bb", r, m)); expectEquals (r, 10); expectEquals (m, 0);
        expect (key::parse ("8A", r, m)); expectEquals (r, 9); expectEquals (m, 1);
        expectEquals (key::shortestShift (6, 8), 2);
        expectEquals (key::shortestShift (9, 0), 3);
    }
};
static EngineTests engineTests;
