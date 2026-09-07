#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <vector>
#include "Clips/ClipModel.h"
#include "Analysis/AnalysisData.h"

namespace mashup
{
class ProjectModel;

/** Proposes and applies tempo/key/alignment plans for a vocal + instrumental pair. */
class MashupAssistant
{
public:
    struct Part
    {
        juce::String name;
        double bpm = 0, firstDownbeat = 0, lengthSeconds = 0;
        int keyRoot = -1, keyMode = 0;
        std::vector<analysis::Phrase> phrases;
        std::vector<double> downbeats;
    };

    struct Candidate
    {
        double targetBpm = 120;
        double vocalSpeed = 1, instrSpeed = 1;          // playback speed factors after stretching
        int vocalSemis = 0, instrSemis = 0;             // pitch shifts
        int resultRoot = -1, resultMode = 0;            // resulting key of the mashup
        double score = 0;                               // higher is better (0..100)
        juce::String tempoText, keyText, summary;
    };

    static Part partFromSource (const juce::ValueTree& sourceNode);

    /** Ranked list of candidates (best first). */
    static std::vector<Candidate> propose (const Part& vocal, const Part& instrumental);

    /** Applies a candidate: project bpm/key, clip sync + pitch, and aligns the vocal's downbeat/phrase start to the
        instrumental's. Wrapped in one undo transaction named "Mashup: <summary>". */
    static void apply (ProjectModel&, ClipModel vocal, ClipModel instrumental, const Part& vocalPart, const Part& instrPart,
                       const Candidate&, bool alignPhrases);

    /** Beat position where the vocal clip should start so that its anchor (first downbeat or first main phrase)
        lands on the instrumental's anchor. */
    static double alignedVocalStart (const ClipModel& vocal, const ClipModel& instr, const Part& vocalPart, const Part& instrPart,
                                     double projectBpm, bool alignPhrases);

private:
    static double phraseAnchor (const Part&, bool preferChorus);
};
} // namespace mashup
