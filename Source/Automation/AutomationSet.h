#pragma once
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include "AutomationCurve.h"

namespace mashup
{
/** Immutable per-track automation snapshot used by the audio thread. */
struct AutomationSet
{
    AutomationCurve volume, pan, send[2];
    struct FxLane { juce::AudioProcessorParameter* param = nullptr; AutomationCurve curve; };
    std::vector<FxLane> fx;
    bool hasAny() const noexcept { return ! volume.isEmpty() || ! pan.isEmpty() || ! send[0].isEmpty() || ! send[1].isEmpty() || ! fx.empty(); }
};
}
