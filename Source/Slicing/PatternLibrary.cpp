#include "PatternLibrary.h"
#include "Slicer.h"
#include "Project/ProjectModel.h"
#include "Clips/ClipOperations.h"
#include "Tracks/TrackModel.h"
#include "Analysis/AnalysisData.h"

namespace mashup
{
namespace
{
    double barLen (ProjectModel& p) { return p.getTimeSigNumerator() * 4.0 / p.getTimeSigDenominator(); }
    juce::UndoManager* um (ProjectModel& p) { return &p.getUndoManager(); }

    /** Keeps bars matching keep(barIndex) inside the clip, deletes the others. */
    std::vector<ClipModel> keepBars (ProjectModel& p, ClipModel c, const std::function<bool (int)>& keep, const char* name)
    {
        um (p)->beginNewTransaction (name);
        const double bl = barLen (p), bpm = p.getBpm();
        auto cuts = slicer::byDivision (c, bl, bpm);
        // slice at bar boundaries relative to the clip start (pattern bars start where the clip starts)
        std::vector<double> rel; for (double b = c.getStart() + bl; b < c.getEnd() - 1e-6; b += bl) rel.push_back (b);
        auto parts = clipops::sliceAt (p, c, rel, bpm);
        std::vector<ClipModel> out;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (keep ((int) i)) out.push_back (parts[i]);
            else parts[i].getState().getParent().removeChild (parts[i].getState(), um (p));
        }
        return out;
    }

    std::vector<ClipModel> cropBars (ProjectModel& p, ClipModel c, int bars)
    {
        um (p)->beginNewTransaction (juce::String (bars) + " bars");
        const double len = bars * barLen (p);
        if (c.getLength() > len) c.setLength (len, um (p));
        return { c };
    }

    std::vector<ClipModel> stutter (ProjectModel& p, ClipModel c, double sliceBeats, int repeats)
    {
        um (p)->beginNewTransaction ("Stutter");
        const double bpm = p.getBpm();
        // take the first `sliceBeats` of each beat and repeat it `repeats` times to fill the beat
        auto beats = slicer::byDivision (c, 1.0, bpm);
        auto parts = clipops::sliceAt (p, c, beats, bpm);
        std::vector<ClipModel> out;
        for (auto& part : parts)
        {
            const double start = part.getStart(), len = part.getLength();
            if (len <= sliceBeats + 1e-6) { out.push_back (part); continue; }
            part.setLength (sliceBeats, um (p));
            out.push_back (part);
            auto parent = part.getState().getParent();
            for (int r = 1; r < repeats && start + (r + 1) * sliceBeats <= start + len + 1e-6; ++r)
            {
                auto copy = part.getState().createCopy(); copy.setProperty (ids::id, ProjectModel::newId(), nullptr);
                ClipModel cc (copy); cc.setStart (start + r * sliceBeats, nullptr);
                parent.appendChild (copy, um (p)); out.push_back (cc);
            }
        }
        return out;
    }

    std::vector<ClipModel> gate (ProjectModel& p, ClipModel c, double stepBeats)
    {
        um (p)->beginNewTransaction ("Gate");
        auto parts = clipops::sliceAt (p, c, slicer::byDivision (c, stepBeats, p.getBpm()), p.getBpm());
        std::vector<ClipModel> out;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            if (i % 2 == 0) { parts[i].setFadeOut (juce::jmin (stepBeats * 0.25, parts[i].getLength()), um (p)); out.push_back (parts[i]); }
            else parts[i].getState().getParent().removeChild (parts[i].getState(), um (p));
        }
        return out;
    }

    std::vector<ClipModel> reverseSections (ProjectModel& p, ClipModel c, int everyBars)
    {
        um (p)->beginNewTransaction ("Reverse sections");
        const double bl = barLen (p), bpm = p.getBpm();
        std::vector<double> rel; for (double b = c.getStart() + bl; b < c.getEnd() - 1e-6; b += bl) rel.push_back (b);
        auto parts = clipops::sliceAt (p, c, rel, bpm);
        for (size_t i = 0; i < parts.size(); ++i)
            if ((int) (i / (size_t) everyBars) % 2 == 1)
            {
                parts[i].setReversed (true, um (p));
                parts[i].getState().setProperty (ids::sourceEnd, parts[i].getOffset() + clipops::beatsToSourceSeconds (parts[i], parts[i].getLength(), bpm), um (p));
            }
        return parts;
    }

    std::vector<ClipModel> keepPhrases (ProjectModel& p, ClipModel c, const std::function<bool (const juce::String&)>& keepLabel, const char* name)
    {
        um (p)->beginNewTransaction (name);
        auto src = ProjectModel::findByIdIn (p.sources(), ids::SOURCE, c.getSourceId());
        auto phrases = analysis::readPhrases (src);
        if (phrases.empty()) return { c };
        const double bpm = p.getBpm();
        std::vector<double> cuts; for (const auto& ph : phrases) cuts.push_back (ph.start);
        auto beats = slicer::atSourceTimes (c, cuts, bpm, 0.5);
        auto parts = clipops::sliceAt (p, c, beats, bpm);
        std::vector<ClipModel> out;
        for (auto& part : parts)
        {
            // which phrase does this part start in?
            const double srcT = part.getOffset() + 1e-3;
            juce::String label = "section";
            for (const auto& ph : phrases) if (srcT >= ph.start - 1e-3 && srcT < ph.end) label = ph.label;
            if (keepLabel (label)) out.push_back (part);
            else part.getState().getParent().removeChild (part.getState(), um (p));
        }
        return out;
    }

    std::vector<ClipModel> chopped (ProjectModel& p, ClipModel c)
    {
        um (p)->beginNewTransaction ("Chopped vocal");
        const double bpm = p.getBpm();
        auto parts = clipops::sliceAt (p, c, slicer::atTransients (p, c, 0.25), bpm);
        // shuffle deterministically (rotate every group of 4 by 1) and shorten each slice to 1/8 with a short fade
        std::vector<ClipModel> out;
        for (size_t i = 0; i < parts.size(); ++i)
        {
            auto& part = parts[i];
            const size_t group = i / 4, idx = i % 4;
            const size_t target = group * 4 + ((idx + 1) % 4);
            if (target < parts.size() && target != i)
            {
                // swap start positions with the target clip
                const double a = part.getStart(), b = parts[target].getStart();
                (void) a; (void) b;
            }
            part.setFadeIn (juce::jmin (0.02, part.getLength() * 0.2), um (p));
            part.setFadeOut (juce::jmin (0.05, part.getLength() * 0.3), um (p));
            out.push_back (part);
        }
        // rotate groups of 4 by moving the first of each group to the end of the group
        for (size_t g = 0; g + 3 < out.size(); g += 4)
        {
            const double s0 = out[g].getStart();
            for (size_t k = g; k < g + 3; ++k) out[k].setStart (out[k + 1].getStart(), um (p));
            out[g + 3].setStart (s0, um (p));
        }
        return out;
    }

    std::vector<ClipModel> soloStem (ProjectModel& p, ClipModel c, const juce::String& stemType, const char* name)
    {
        um (p)->beginNewTransaction (name);
        // mute every other track that holds a stem of the same original; unmute the requested stem
        auto src = ProjectModel::findByIdIn (p.sources(), ids::SOURCE, c.getSourceId());
        const juce::String origin = src.hasProperty (ids::stemOf) ? src[ids::stemOf].toString() : src[ids::id].toString();
        for (auto t : p.tracks())
        {
            TrackModel track (t);
            bool related = false, isWanted = false;
            for (auto cn : track.clips())
            {
                auto s2 = ProjectModel::findByIdIn (p.sources(), ids::SOURCE, cn[ids::sourceId].toString());
                const juce::String o2 = s2.hasProperty (ids::stemOf) ? s2[ids::stemOf].toString() : s2[ids::id].toString();
                if (o2 == origin) { related = true; if (s2[ids::stemType].toString() == stemType || (stemType == "instrumental" && s2.hasProperty (ids::stemType) && s2[ids::stemType].toString() != "vocals")) isWanted = true; }
            }
            if (related) track.setMuted (! isWanted, um (p));
        }
        return { c };
    }
}

const std::vector<Pattern>& PatternLibrary::all()
{
    static const std::vector<Pattern> patterns = {
        { "Full vocal", "Stems", "Unmute the vocal stem, mute the other stems of the same song", [] (ProjectModel& p, ClipModel c) { return soloStem (p, c, "vocals", "Full vocal"); } },
        { "Drum-only", "Stems", "Only the drums stem of this song", [] (ProjectModel& p, ClipModel c) { return soloStem (p, c, "drums", "Drum-only"); } },
        { "Bass-only", "Stems", "Only the bass stem of this song", [] (ProjectModel& p, ClipModel c) { return soloStem (p, c, "bass", "Bass-only"); } },
        { "Instrumental-only", "Stems", "Everything except vocals", [] (ProjectModel& p, ClipModel c) { return soloStem (p, c, "instrumental", "Instrumental-only"); } },
        { "Chorus only", "Phrases", "Keep only chorus sections", [] (ProjectModel& p, ClipModel c) { return keepPhrases (p, c, [] (const juce::String& l) { return l == "chorus"; }, "Chorus only"); } },
        { "Verse only", "Phrases", "Keep only verse sections", [] (ProjectModel& p, ClipModel c) { return keepPhrases (p, c, [] (const juce::String& l) { return l == "verse"; }, "Verse only"); } },
        { "Hook", "Phrases", "First chorus, first 4 bars", [] (ProjectModel& p, ClipModel c) { auto r = keepPhrases (p, c, [] (const juce::String& l) { return l == "chorus"; }, "Hook"); if (r.empty()) return r; for (size_t i = 1; i < r.size(); ++i) r[i].getState().getParent().removeChild (r[i].getState(), um (p)); r.resize (1); if (r[0].getLength() > 4 * barLen (p)) r[0].setLength (4 * barLen (p), um (p)); return r; } },
        { "Intro", "Phrases", "Keep the intro", [] (ProjectModel& p, ClipModel c) { return keepPhrases (p, c, [] (const juce::String& l) { return l == "intro"; }, "Intro"); } },
        { "Outro", "Phrases", "Keep the outro", [] (ProjectModel& p, ClipModel c) { return keepPhrases (p, c, [] (const juce::String& l) { return l == "outro"; }, "Outro"); } },
        { "4 bars", "Length", "Crop to 4 bars", [] (ProjectModel& p, ClipModel c) { return cropBars (p, c, 4); } },
        { "8 bars", "Length", "Crop to 8 bars", [] (ProjectModel& p, ClipModel c) { return cropBars (p, c, 8); } },
        { "16 bars", "Length", "Crop to 16 bars", [] (ProjectModel& p, ClipModel c) { return cropBars (p, c, 16); } },
        { "32 bars", "Length", "Crop to 32 bars", [] (ProjectModel& p, ClipModel c) { return cropBars (p, c, 32); } },
        { "Every 2 bars", "Rhythmic", "Keep the first 2 bars of every 4", [] (ProjectModel& p, ClipModel c) { return keepBars (p, c, [] (int i) { return i % 4 < 2; }, "Every 2 bars"); } },
        { "Every 4 bars", "Rhythmic", "Keep the first 4 bars of every 8", [] (ProjectModel& p, ClipModel c) { return keepBars (p, c, [] (int i) { return i % 8 < 4; }, "Every 4 bars"); } },
        { "Alternate bars", "Rhythmic", "Keep every other bar", [] (ProjectModel& p, ClipModel c) { return keepBars (p, c, [] (int i) { return i % 2 == 0; }, "Alternate bars"); } },
        { "Reverse sections", "Rhythmic", "Reverse every second bar", [] (ProjectModel& p, ClipModel c) { return reverseSections (p, c, 1); } },
        { "Stutter 1/16", "Glitch", "Repeat the first 1/16 of each beat", [] (ProjectModel& p, ClipModel c) { return stutter (p, c, 0.25, 4); } },
        { "Stutter 1/32", "Glitch", "Repeat the first 1/32 of each beat", [] (ProjectModel& p, ClipModel c) { return stutter (p, c, 0.125, 8); } },
        { "Repeat x2", "Glitch", "Repeat the clip twice", [] (ProjectModel& p, ClipModel c) { return clipops::repeat (p, c, 1); } },
        { "Repeat x4", "Glitch", "Repeat the clip four times", [] (ProjectModel& p, ClipModel c) { return clipops::repeat (p, c, 3); } },
        { "Gated 1/8", "Glitch", "Mute every other 1/8", [] (ProjectModel& p, ClipModel c) { return gate (p, c, 0.5); } },
        { "Gated 1/16", "Glitch", "Mute every other 1/16", [] (ProjectModel& p, ClipModel c) { return gate (p, c, 0.25); } },
        { "Chopped vocal", "Glitch", "Slice at transients, rotate groups of 4, short fades", [] (ProjectModel& p, ClipModel c) { return chopped (p, c); } },
    };
    return patterns;
}

const Pattern* PatternLibrary::find (const juce::String& name) { for (auto& p : all()) if (p.name == name) return &p; return nullptr; }
std::vector<juce::String> PatternLibrary::categories() { std::vector<juce::String> c; for (auto& p : all()) if (std::find (c.begin(), c.end(), p.category) == c.end()) c.push_back (p.category); return c; }
} // namespace mashup
