#pragma once
#include <juce_core/juce_core.h>

namespace mashup
{
/** Key helpers: pitch classes, modes, names and Camelot codes. root 0 == C. mode 0 == major, 1 == minor. */
namespace key
{
    inline const char* const noteNames[12]      = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    inline const char* const noteNamesFlat[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

    inline juce::String name (int root, int mode, bool shortForm = true)
    {
        if (root < 0) return "-";
        root = ((root % 12) + 12) % 12;
        return juce::String (noteNames[root]) + (mode == 1 ? (shortForm ? "m" : " minor") : (shortForm ? "" : " major"));
    }

    /** Camelot wheel: number 1..12 + 'A' (minor) / 'B' (major). */
    inline juce::String camelot (int root, int mode)
    {
        if (root < 0) return "-";
        // major: C=8B, G=9B, D=10B, A=11B, E=12B, B=1B, F#=2B, C#=3B, G#=4B, D#=5B, A#=6B, F=7B
        static const int majorNum[12] = { 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 1 };
        int n = majorNum[((root % 12) + 12) % 12];
        if (mode == 1) n = ((n - 3 - 1 + 12) % 12) + 1;   // relative minor is 3 semitones down => same number, letter A
        // Note: relative minor of C major (8B) is A minor (8A). Compute directly instead:
        static const int minorNum[12] = { 5, 12, 7, 2, 9, 4, 11, 6, 1, 8, 3, 10 };
        n = mode == 1 ? minorNum[((root % 12) + 12) % 12] : majorNum[((root % 12) + 12) % 12];
        return juce::String (n) + (mode == 1 ? "A" : "B");
    }

    inline int camelotNumber (int root, int mode) { return camelot (root, mode).dropLastCharacters (1).getIntValue(); }

    /** Parses "Am", "A minor", "F#m", "Bb", "8A" ... Returns false if unrecognised. */
    inline bool parse (const juce::String& text, int& root, int& mode)
    {
        auto s = text.trim();
        if (s.isEmpty()) return false;
        // Camelot
        if (s.length() <= 3 && (s.endsWithChar ('A') || s.endsWithChar ('B')) && s.dropLastCharacters (1).containsOnly ("0123456789"))
        {
            const int n = s.dropLastCharacters (1).getIntValue();
            if (n < 1 || n > 12) return false;
            mode = s.endsWithChar ('A') ? 1 : 0;
            for (int r = 0; r < 12; ++r) if (camelot (r, mode) == s) { root = r; return true; }
            return false;
        }
        int r = -1; int i = 0;
        switch (s[0]) { case 'C': r = 0; break; case 'D': r = 2; break; case 'E': r = 4; break; case 'F': r = 5; break; case 'G': r = 7; break; case 'A': r = 9; break; case 'B': r = 11; break; default: return false; }
        i = 1;
        if (i < s.length() && (s[i] == '#')) { r = (r + 1) % 12; ++i; }
        else if (i < s.length() && (s[i] == 'b')) { r = (r + 11) % 12; ++i; }
        auto rest = s.substring (i).trim().toLowerCase();
        mode = (rest.startsWith ("m") && ! rest.startsWith ("maj")) ? 1 : 0;
        root = r;
        return true;
    }

    /** Semitone distance (-6..+6) that moves `fromRoot` to `toRoot`. */
    inline int shortestShift (int fromRoot, int toRoot)
    {
        int d = ((toRoot - fromRoot) % 12 + 12) % 12;
        return d > 6 ? d - 12 : d;
    }

    /** Relative key (Am <-> C). */
    inline void relative (int root, int mode, int& outRoot, int& outMode)
    {
        outMode = 1 - mode;
        outRoot = mode == 0 ? (root + 9) % 12 : (root + 3) % 12;
    }

    /** Harmonic compatibility on the Camelot wheel: 0 = identical, 1 = adjacent/relative (compatible),
        2 = energy boost (+2/-2 or relative +1), 3 = clash. */
    inline int compatibility (int rootA, int modeA, int rootB, int modeB)
    {
        if (rootA < 0 || rootB < 0) return 3;
        const int na = camelotNumber (rootA, modeA), nb = camelotNumber (rootB, modeB);
        int d = std::abs (na - nb); d = juce::jmin (d, 12 - d);
        if (modeA == modeB) { if (d == 0) return 0; if (d == 1) return 1; if (d == 2) return 2; return 3; }
        if (d == 0) return 1;                   // relative major/minor
        if (d == 1) return 2;                   // diagonal mix
        return 3;
    }
}
} // namespace mashup
