#include <juce_core/juce_core.h>
#include "AudioEngine/GraphBuilder.h"
#include "Import/SourceLibrary.h"
#include "Effects/EffectFactory.h"
#include "Project/ProjectModel.h"
#include "Tracks/TrackModel.h"
#include "Clips/ClipOperations.h"
#include "Mixer/MixerOps.h"
#include "AudioEngine/RenderWorkers.h"
using namespace mashup;

/** Performance check: 50 tracks with stretched clips and an effect each must render faster than realtime. */
class StressTests : public juce::UnitTest
{
public:
    StressTests() : juce::UnitTest ("Stress", "Engine") {}
    void runTest() override
    {
        beginTest ("50 stretched tracks render faster than realtime");
        const double sr = 48000.0;
        juce::UndoManager um; ProjectModel model (um); juce::ThreadPool pool (1); SourceLibrary lib (model, pool); EffectFactory fx;
        juce::AudioBuffer<float> noise (2, (int) sr * 10); juce::Random rng (3);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < noise.getNumSamples(); ++i) noise.setSample (c, i, rng.nextFloat() * 0.4f - 0.2f);
        auto id = lib.addDecoded (juce::File(), std::move (noise), sr, "noise");
        for (int t = 0; t < 50; ++t)
        {
            auto track = trackops::addTrack (model, "T" + juce::String (t));
            auto clip = trackops::placeSource (model, id, "c", 10.0, 0.0, track, 100.0);   // synced 100 -> 120 bpm (stretched)
            clip.setStretchMode (t % 2 ? StretchMode::Realtime : StretchMode::Repitch, nullptr);
            mixerops::addEffect (model, track.effects(), t % 3 == 0 ? "eq" : t % 3 == 1 ? "compressor" : "filter");
        }
        GraphBuilder builder (model, lib, fx);
        auto g = builder.build (sr, 512, nullptr);
        expectEquals ((int) g->tracks.size(), 50);
        juce::AudioBuffer<float> out (2, 512);
        const double seconds = 5.0;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (juce::int64 pos = 0; pos < (juce::int64) (sr * seconds); pos += 512) { out.clear(); g->process (out, pos, 512); }
        const double elapsed = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        logMessage ("rendered " + juce::String (seconds, 1) + " s of 50 tracks (25 RubberBand) in " + juce::String (elapsed, 2) + " s  (" + juce::String (elapsed / seconds * 100.0, 1) + "% of realtime)");
        expectLessThan (elapsed, seconds);
        expectGreaterThan (g->masterPeakL.load(), 0.01f);

        beginTest ("parallel render workers produce the same result faster");
        juce::AudioBuffer<float> ref (2, 4096), par (2, 4096);
        g->notifyDiscontinuity(); ref.clear(); g->process (ref, 0, 4096);
        g->workers = std::make_shared<RenderWorkers> (juce::jlimit (1, 7, juce::SystemStats::getNumCpus() - 2));
        g->notifyDiscontinuity(); par.clear(); g->process (par, 0, 4096);
        // repitch tracks are deterministic; compare energy of both renders (RubberBand tracks are deterministic after reset too)
        double e1 = 0, e2 = 0; for (int i = 0; i < 4096; ++i) { e1 += ref.getSample (0, i) * ref.getSample (0, i); e2 += par.getSample (0, i) * par.getSample (0, i); }
        expectWithinAbsoluteError (e2 / juce::jmax (1e-9, e1), 1.0, 0.05);
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        for (juce::int64 pos = 0; pos < (juce::int64) (sr * seconds); pos += 512) { out.clear(); g->process (out, pos, 512); }
        const double elapsedPar = (juce::Time::getMillisecondCounterHiRes() - t1) / 1000.0;
        logMessage ("parallel (" + juce::String (g->workers->getNumThreads()) + " workers): " + juce::String (elapsedPar, 2) + " s  (" + juce::String (elapsedPar / seconds * 100.0, 1) + "% of realtime)");
        expectLessThan (elapsedPar, elapsed);
    }
};
static StressTests stressTests;
