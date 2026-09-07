#include "MashupAssistant.h"
#include "Project/ProjectModel.h"
#include "Clips/ClipOperations.h"
#include "Core/MusicalKey.h"
#include <cmath>
#include <algorithm>

namespace mashup
{
MashupAssistant::Part MashupAssistant::partFromSource (const juce::ValueTree& s)
{
    Part p;
    p.name = s[ids::name].toString();
    p.bpm = (double) s.getProperty (ids::bpm, 0.0);
    p.firstDownbeat = (double) s.getProperty (ids::firstDownbeat, 0.0);
    p.lengthSeconds = (double) s[ids::lengthSamples] / juce::jmax (1.0, (double) s[ids::sampleRate]);
    p.keyRoot = (int) s.getProperty (ids::keyRoot, -1); p.keyMode = (int) s.getProperty (ids::keyMode, 0);
    p.phrases = analysis::readPhrases (s);
    p.downbeats = analysis::readTimes (s, ids::DOWNBEATS);
    return p;
}

static double stretchPenalty (double speed, bool isVocal)
{
    const double semis = std::abs (std::log2 (speed)) * 12.0;   // stretch expressed like semitones of tempo change
    return semis * (isVocal ? 4.0 : 2.5);
}
static double pitchPenalty (int semis, bool isVocal)
{
    const int a = std::abs (semis);
    double pen = a * (isVocal ? 5.0 : 3.0);
    if (a > 3) pen += (a - 3) * (isVocal ? 6.0 : 3.0);   // large shifts sound artificial
    return pen;
}

std::vector<MashupAssistant::Candidate> MashupAssistant::propose (const Part& v, const Part& i)
{
    std::vector<Candidate> out;
    if (v.bpm <= 0 || i.bpm <= 0) return out;

    // tempo options: instrumental tempo, vocal tempo, geometric mean, and double/half relations
    struct Tempo { double bpm; const char* label; };
    std::vector<Tempo> tempos { { i.bpm, "instrumental tempo" }, { v.bpm, "vocal tempo" }, { std::sqrt (v.bpm * i.bpm), "meet in the middle" } };
    if (v.bpm * 2 < i.bpm * 1.25 && v.bpm * 2 > i.bpm * 0.8) tempos.push_back ({ i.bpm, "vocal at double time" });
    if (v.bpm / 2 < i.bpm * 1.25 && v.bpm / 2 > i.bpm * 0.8) tempos.push_back ({ i.bpm, "vocal at half time" });

    // key options
    struct KeyOpt { int vs, is; int root, mode; const char* label; };
    std::vector<KeyOpt> keys;
    const bool haveKeys = v.keyRoot >= 0 && i.keyRoot >= 0;
    if (! haveKeys) keys.push_back ({ 0, 0, i.keyRoot >= 0 ? i.keyRoot : v.keyRoot, i.keyRoot >= 0 ? i.keyMode : v.keyMode, "keys unknown - no pitch change" });
    else
    {
        const int compat = key::compatibility (v.keyRoot, v.keyMode, i.keyRoot, i.keyMode);
        if (compat <= 1) keys.push_back ({ 0, 0, i.keyRoot, i.keyMode, compat == 0 ? "same key" : "compatible keys (Camelot neighbours)" });
        // move vocal to instrumental's key (same mode target root), or to its relative
        int rel, relMode; key::relative (i.keyRoot, i.keyMode, rel, relMode);
        const int targetForVocal = (v.keyMode == i.keyMode) ? i.keyRoot : rel;   // keep the vocal's mode: land on the relative key when modes differ
        const int shiftV = key::shortestShift (v.keyRoot, targetForVocal);
        if (shiftV != 0) keys.push_back ({ shiftV, 0, i.keyRoot, i.keyMode, "shift vocal to match instrumental" });
        int relV, relVMode; key::relative (v.keyRoot, v.keyMode, relV, relVMode);
        const int targetForInstr = (v.keyMode == i.keyMode) ? v.keyRoot : relV;
        const int shiftI = key::shortestShift (i.keyRoot, targetForInstr);
        if (shiftI != 0) keys.push_back ({ 0, shiftI, (i.keyRoot + shiftI + 12) % 12, i.keyMode, "shift instrumental to match vocal" });
        // split the difference when both shifts are large
        if (std::abs (shiftV) >= 3)
        {
            const int half = shiftV > 0 ? shiftV / 2 : -((-shiftV) / 2);
            keys.push_back ({ half, -(shiftV - half), (i.keyRoot - (shiftV - half) + 24) % 12, i.keyMode, "split the shift between both" });
        }
        // energy boost: +1 Camelot step (perfect fifth) if it means a smaller shift
        for (int alt : { 7, -7 }) { const int s2 = key::shortestShift (v.keyRoot, (targetForVocal + alt + 12) % 12); if (s2 != 0 && std::abs (s2) < std::abs (shiftV)) keys.push_back ({ s2, 0, i.keyRoot, i.keyMode, "vocal a fifth away (energy boost)" }); }
    }

    for (const auto& t : tempos)
    {
        double vSpeed = t.bpm / v.bpm, iSpeed = t.bpm / i.bpm;
        juce::String tempoText;
        if (juce::String (t.label) == "vocal at double time") vSpeed = t.bpm / (v.bpm * 2.0);
        if (juce::String (t.label) == "vocal at half time") vSpeed = t.bpm / (v.bpm * 0.5);
        for (const auto& k : keys)
        {
            Candidate c;
            c.targetBpm = std::round (t.bpm * 10.0) / 10.0;
            c.vocalSpeed = vSpeed; c.instrSpeed = iSpeed; c.vocalSemis = k.vs; c.instrSemis = k.is; c.resultRoot = k.root; c.resultMode = k.mode;
            double score = 100.0 - stretchPenalty (vSpeed, true) - stretchPenalty (iSpeed, false) - pitchPenalty (k.vs, true) - pitchPenalty (k.is, false);
            if (std::abs (t.bpm - std::round (t.bpm)) > 0.05) score -= 2.0;
            c.score = juce::jlimit (0.0, 100.0, score);
            auto pct = [] (double s) { const double p = (s - 1.0) * 100.0; return juce::String (p >= 0 ? "+" : "") + juce::String (p, 1) + "%"; };
            c.tempoText = "Project " + juce::String (c.targetBpm, 1) + " BPM (" + t.label + "): vocal " + pct (vSpeed) + ", instrumental " + pct (iSpeed);
            auto st = [] (int s) { return juce::String (s > 0 ? "+" : "") + juce::String (s) + " st"; };
            c.keyText = juce::String (k.label) + ": vocal " + st (k.vs) + ", instrumental " + st (k.is) + (k.root >= 0 ? "  -> " + key::name (k.root, k.mode) + " (" + key::camelot (k.root, k.mode) + ")" : juce::String());
            c.summary = juce::String (c.targetBpm, 1) + " BPM, vocal " + st (k.vs) + ", instr " + st (k.is);
            out.push_back (c);
        }
    }
    std::sort (out.begin(), out.end(), [] (const Candidate& a, const Candidate& b) { return a.score > b.score; });
    // remove duplicates (same numbers)
    std::vector<Candidate> unique;
    for (auto& c : out)
    {
        bool dup = false;
        for (auto& u : unique) if (std::abs (u.targetBpm - c.targetBpm) < 0.05 && u.vocalSemis == c.vocalSemis && u.instrSemis == c.instrSemis && std::abs (u.vocalSpeed - c.vocalSpeed) < 1e-6) dup = true;
        if (! dup) unique.push_back (c);
    }
    return unique;
}

double MashupAssistant::phraseAnchor (const Part& p, bool preferChorus)
{
    // first phrase that is not intro; prefer chorus when asked
    if (preferChorus) for (const auto& ph : p.phrases) if (ph.label == "chorus") return ph.start;
    for (const auto& ph : p.phrases) if (ph.label != "intro") return ph.start;
    return p.firstDownbeat;
}

double MashupAssistant::alignedVocalStart (const ClipModel& vocal, const ClipModel& instr, const Part& vp, const Part& ip, double bpm, bool alignPhrases)
{
    // anchors in source seconds
    const double vAnchorSrc = alignPhrases ? phraseAnchor (vp, false) : vp.firstDownbeat;
    const double iAnchorSrc = alignPhrases ? phraseAnchor (ip, false) : ip.firstDownbeat;
    // instrumental anchor on the timeline (beats)
    const double iAnchorBeat = instr.getStart() + clipops::sourceSecondsToBeats (instr, iAnchorSrc - instr.getOffset(), bpm);
    // vocal anchor offset from its clip start (beats)
    const double vAnchorBeats = clipops::sourceSecondsToBeats (vocal, vAnchorSrc - vocal.getOffset(), bpm);
    // snap the instrumental anchor to the nearest bar to keep grid alignment
    const double bpb = 4.0;
    double target = iAnchorBeat - vAnchorBeats;
    // move by whole bars so the vocal downbeat coincides with an instrumental downbeat
    const double iDownbeatBeat = instr.getStart() + clipops::sourceSecondsToBeats (instr, ip.firstDownbeat - instr.getOffset(), bpm);
    const double vDownbeatBeats = clipops::sourceSecondsToBeats (vocal, vp.firstDownbeat - vocal.getOffset(), bpm);
    const double phase = std::fmod (((target + vDownbeatBeats) - iDownbeatBeat), bpb);
    target -= phase; if (phase > bpb / 2) target += bpb;
    return juce::jmax (0.0, target);
}

void MashupAssistant::apply (ProjectModel& p, ClipModel vocal, ClipModel instr, const Part& vp, const Part& ip, const Candidate& c, bool alignPhrases)
{
    auto* um = &p.getUndoManager();
    um->beginNewTransaction ("Mashup: " + c.summary);
    p.setBpm (c.targetBpm);
    if (c.resultRoot >= 0) p.setKey (c.resultRoot, c.resultMode);
    const double bpm = p.getBpm();
    auto syncClip = [&] (ClipModel clip, double sourceBpm, double speedOverride, int semis)
    {
        const double span = clipops::beatsToSourceSeconds (clip, clip.getLength(), bpm);
        // speedOverride encodes double/half-time: effective source bpm = target / speed
        const double effBpm = std::abs (speedOverride) > 1e-9 ? c.targetBpm / speedOverride : sourceBpm;
        clip.setClipBpm (effBpm, um);
        clip.setSyncedToProject (true, um);
        if (clip.getStretchMode() == StretchMode::Repitch) clip.setStretchMode (StretchMode::Realtime, um);
        clip.setLength (clipops::sourceSecondsToBeats (clip, span, bpm), um);
        clip.setPitch (semis, 0, um);
        if (clip.getKeyRoot() < 0 && sourceBpm > 0) {}
    };
    syncClip (vocal, vp.bpm, c.vocalSpeed, c.vocalSemis);
    syncClip (instr, ip.bpm, c.instrSpeed, c.instrSemis);
    if (vocal.getStretchMode() != StretchMode::Vocal) vocal.setStretchMode (StretchMode::Vocal, um);
    vocal.setKey (vp.keyRoot, vp.keyMode, um); instr.setKey (ip.keyRoot, ip.keyMode, um);
    // alignment: instrumental stays, vocal moves
    const double start = alignedVocalStart (vocal, instr, vp, ip, bpm, alignPhrases);
    vocal.setStart (start, um);
}
} // namespace mashup
