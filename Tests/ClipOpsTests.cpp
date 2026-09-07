#include <juce_core/juce_core.h>
#include "Project/ProjectModel.h"
#include "Project/ProjectFile.h"
#include "Clips/ClipOperations.h"
#include "Slicing/Slicer.h"
#include "Slicing/PatternLibrary.h"
#include "Automation/AutomationCurve.h"
#include "Analysis/AnalysisData.h"
#include "AudioEngine/ClipPlayer.h"
using namespace mashup;

class ClipOpsTests : public juce::UnitTest
{
public:
    ClipOpsTests() : juce::UnitTest ("ClipOperations", "Timeline") {}

    void runTest() override
    {
        juce::UndoManager um; ProjectModel p (um);
        auto track = trackops::addTrack (p, "T");
        // 10 s source at 120 bpm -> 20 beats
        auto clip = trackops::placeSource (p, "src", "clip", 10.0, 4.0, track);
        clip.setStretchMode (StretchMode::Repitch, nullptr);
        expectWithinAbsoluteError (clip.getLength(), 20.0, 1e-9);

        beginTest ("split keeps audio continuous");
        um.beginNewTransaction();
        auto right = clipops::split (p, clip, 12.0, 120.0);
        expect (right.isValid());
        expectWithinAbsoluteError (clip.getLength(), 8.0, 1e-9);
        expectWithinAbsoluteError (right.getStart(), 12.0, 1e-9);
        expectWithinAbsoluteError (right.getOffset(), 4.0, 1e-9);   // 8 beats @120 = 4 s
        expectEquals (track.getNumClips(), 2);
        expect (um.undo());
        expectEquals (track.getNumClips(), 1);
        expectWithinAbsoluteError (clip.getLength(), 20.0, 1e-9);
        expect (um.redo());
        expectEquals (track.getNumClips(), 2);

        beginTest ("trim start moves offset");
        clipops::trimStart (p, clip, 6.0, 120.0);
        expectWithinAbsoluteError (clip.getStart(), 6.0, 1e-9);
        expectWithinAbsoluteError (clip.getOffset(), 1.0, 1e-9);
        expectWithinAbsoluteError (clip.getLength(), 6.0, 1e-9);
        um.undo();

        beginTest ("delete range and crop");
        um.beginNewTransaction();
        clipops::deleteRange (p, clip, 6.0, 8.0, 120.0);      // clip 4..12 -> 4..6 + 8..12
        expectEquals (track.getNumClips(), 3);
        um.undo();
        expectEquals (track.getNumClips(), 2);
        clipops::cropToRange (p, clip, 5.0, 9.0, 120.0);
        expectWithinAbsoluteError (clip.getStart(), 5.0, 1e-9); expectWithinAbsoluteError (clip.getLength(), 4.0, 1e-9); expectWithinAbsoluteError (clip.getOffset(), 0.5, 1e-9);
        um.undo();

        beginTest ("reverse keeps the same source region");
        clipops::setReversed (p, clip, true, 120.0);
        expect (clip.isReversed());
        expectWithinAbsoluteError ((double) clip.getState()[ids::sourceEnd], 4.0, 1e-9);   // offset 0 + 8 beats = 4 s
        auto rr = clipops::split (p, clip, 8.0, 120.0);       // reversed clip 4..12 split at 8: left reads 2..4 s, right reads 0..2 s
        expectWithinAbsoluteError (clip.getOffset(), 2.0, 1e-9);
        expectWithinAbsoluteError ((double) rr.getState()[ids::sourceEnd], 2.0, 1e-9);
        um.undo(); um.undo();

        beginTest ("slice and repeat");
        auto parts = slicer::slice (p, clip, slicer::byDivision (clip, 2.0, 120.0));
        expectEquals ((int) parts.size(), 4);
        for (size_t i = 1; i < parts.size(); ++i) expectWithinAbsoluteError (parts[i].getStart(), parts[i - 1].getEnd(), 1e-9);
        um.undo();
        auto reps = clipops::repeat (p, clip, 2);
        expectEquals ((int) reps.size(), 3);
        expectWithinAbsoluteError (reps[2].getStart(), 20.0, 1e-9);
        um.undo();

        beginTest ("match to project bpm changes length, keeps source span");
        clipops::matchToProjectBpm (p, clip, 100.0);   // source at 100 bpm, project 120: plays faster -> shorter on the timeline
        expect (clip.isSyncedToProject());
        expectWithinAbsoluteError (clip.getLength(), 4.0 * 100.0 / 60.0, 1e-6);   // 4 s of source = 6.667 beats at 100 bpm
        expectWithinAbsoluteError (clip.getPlaybackSpeed (120.0), 1.2, 1e-9);
        um.undo();

        beginTest ("patterns: alternate bars, gate");
        {
            auto* pat = PatternLibrary::find ("Alternate bars");
            expect (pat != nullptr);
            auto out = pat->apply (p, clip);          // 8 beats = 2 bars -> keep bar 0 only
            expectEquals ((int) out.size(), 1);
            expectWithinAbsoluteError (out[0].getLength(), 4.0, 1e-9);
            um.undo();
            auto* gate = PatternLibrary::find ("Gated 1/8");
            auto g = gate->apply (p, clip);           // 8 beats / 0.5 = 16 pieces, every other kept
            expectEquals ((int) g.size(), 8);
            um.undo();
        }

        beginTest ("automation curve");
        {
            AutomationCurve c;
            c.addPoint (0.0, 0.0f); c.addPoint (4.0, 1.0f);
            expectWithinAbsoluteError (c.valueAt (2.0), 0.5f, 1e-6f);
            expectWithinAbsoluteError (c.valueAt (-1.0), 0.0f, 1e-6f);
            expectWithinAbsoluteError (c.valueAt (9.0), 1.0f, 1e-6f);
            auto lane = AutomationCurve::getOrCreateLane (track.getState(), "volume", nullptr);
            c.writeToLane (lane, nullptr);
            auto back = AutomationCurve::fromLane (lane);
            expectEquals ((int) back.getPoints().size(), 2);
            expectWithinAbsoluteError (AutomationCurve::normToVolume (AutomationCurve::volumeToNorm (0.5)), 0.5, 1e-6);
        }

        beginTest ("project file round trip");
        {
            analysis::writeTimes (juce::ValueTree(), ids::BEATS, {}, nullptr);   // no-op on invalid tree must not crash
            auto src = juce::ValueTree (ids::SOURCE); src.setProperty (ids::id, "src", nullptr); src.setProperty (ids::path, "/nonexistent/a.wav", nullptr);
            analysis::writeTimes (src, ids::BEATS, { 0.5, 1.0, 1.5 }, nullptr);
            p.sources().appendChild (src, nullptr);
            auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mashup_test_project.mashup");
            auto r = ProjectFile::save (p.getRoot(), tmp, juce::File());
            expect (r.wasOk(), r.getErrorMessage());
            juce::ValueTree loaded;
            r = ProjectFile::load (tmp, loaded, juce::File());
            expect (r.wasOk(), r.getErrorMessage());
            expect (loaded.hasType (ids::PROJECT));
            expectEquals (loaded.getChildWithName (ids::TRACKS).getNumChildren(), 1);
            auto beats = analysis::readTimes (loaded.getChildWithName (ids::SOURCES).getChild (0), ids::BEATS);
            expectEquals ((int) beats.size(), 3); expectWithinAbsoluteError (beats[2], 1.5, 1e-12);
            expectWithinAbsoluteError ((double) loaded[ids::bpm], p.getBpm(), 1e-9);
            tmp.deleteFile();
        }

        beginTest ("pitch shift changes frequency, not duration");
        {
            const double sr = 48000.0;
            juce::AudioBuffer<float> b (2, (int) sr * 3);
            for (int i = 0; i < b.getNumSamples(); ++i) { const float v = (float) std::sin (2 * M_PI * 220.0 * i / sr); b.setSample (0, i, v); b.setSample (1, i, v); }
            ClipPlayer::Static st; st.clipId = "x"; st.source = std::make_shared<AudioSource> ("x", juce::File(), std::move (b), sr);
            st.startBeat = 0; st.lengthBeats = 4; st.mode = StretchMode::HighQuality;
            ClipPlayer player (st, sr, 512);
            player.live().pitchScale.store (2.0f);   // +12 st
            juce::AudioBuffer<float> out (2, 512);
            int zc = 0; int counted = 0;
            for (juce::int64 pos = 0; pos < (juce::int64) sr * 2; pos += 512)
            {
                out.clear();
                ClipPlayer::Segment seg { pos / sr, pos / sr * 2.0, 120.0, 512, sr };
                player.render (out, seg);
                if (pos > sr * 0.5) { for (int i = 1; i < 512; ++i) if (out.getSample (0, i - 1) < 0 && out.getSample (0, i) >= 0) ++zc; counted += 512; }
            }
            const double freq = zc / (counted / sr);
            logMessage ("pitched frequency " + juce::String (freq, 1));
            expectWithinAbsoluteError (freq, 440.0, 12.0);
        }
    }
};
static ClipOpsTests clipOpsTests;
