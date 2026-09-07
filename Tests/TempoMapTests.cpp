#include <juce_core/juce_core.h>
#include "Timeline/TempoMap.h"
using namespace mashup;

class TempoMapTests : public juce::UnitTest
{
public:
    TempoMapTests() : juce::UnitTest ("TempoMap", "Timeline") {}
    void runTest() override
    {
        beginTest ("constant tempo round trip");
        TempoMap tm (120.0);
        expectWithinAbsoluteError (tm.beatToTime (4.0), 2.0, 1e-9);
        expectWithinAbsoluteError (tm.timeToBeat (2.0), 4.0, 1e-9);
        auto bb = tm.beatToBarsBeats (5.5);
        expectEquals (bb.bar, 2); expectEquals (bb.beat, 2); expectWithinAbsoluteError (bb.tick, 0.5, 1e-9);
        expectWithinAbsoluteError (tm.barsBeatsToBeat (2, 2, 0.5), 5.5, 1e-9);

        beginTest ("tempo changes");
        TempoMap tm2 ({ { 0.0, 120.0 }, { 8.0, 60.0 } }, 4, 4);
        expectWithinAbsoluteError (tm2.beatToTime (8.0), 4.0, 1e-9);
        expectWithinAbsoluteError (tm2.beatToTime (12.0), 8.0, 1e-9);
        expectWithinAbsoluteError (tm2.timeToBeat (8.0), 12.0, 1e-9);
        expectWithinAbsoluteError (tm2.bpmAtBeat (9.0), 60.0, 1e-9);
        for (double b = 0; b < 20; b += 0.37) expectWithinAbsoluteError (tm2.timeToBeat (tm2.beatToTime (b)), b, 1e-9);

        beginTest ("6/8 bars");
        TempoMap tm3 ({ { 0.0, 100.0 } }, 6, 8);
        expectWithinAbsoluteError (tm3.beatsPerBar(), 3.0, 1e-9);
        expectEquals (tm3.beatToBarsBeats (3.0).bar, 2);
        expectEquals (tm3.beatToBarsBeats (3.0).beat, 1);
        expectEquals (tm3.beatToBarsBeats (0.5).beat, 2);

        beginTest ("snap");
        expectWithinAbsoluteError (TempoMap::snapBeat (3.13, 0.25), 3.25, 1e-9);
        expectWithinAbsoluteError (TempoMap::floorBeat (3.99, 1.0), 3.0, 1e-9);
    }
};
static TempoMapTests tempoMapTests;
