#include "BuiltInEffects.h"
#include "FilterEffect.h"
#include "DynamicsEffects.h"
#include "ToneEffects.h"
#include "TimeEffects.h"

namespace mashup
{
void registerAllBuiltInEffects (EffectFactory& f)
{
    f.registerBuiltIn ("eq", "EQ / Tone", [] { return std::make_unique<ParametricEQEffect>(); });
    f.registerBuiltIn ("filter", "EQ / Tone", [] { return std::make_unique<FilterEffect>(); });
    f.registerBuiltIn ("saturation", "EQ / Tone", [] { return std::make_unique<SaturationEffect>(); });
    f.registerBuiltIn ("distortion", "EQ / Tone", [] { return std::make_unique<DistortionEffect>(); });
    f.registerBuiltIn ("widener", "EQ / Tone", [] { return std::make_unique<StereoWidenerEffect>(); });
    f.registerBuiltIn ("compressor", "Dynamics", [] { return std::make_unique<CompressorEffect>(); });
    f.registerBuiltIn ("limiter", "Dynamics", [] { return std::make_unique<LimiterEffect>(); });
    f.registerBuiltIn ("gate", "Dynamics", [] { return std::make_unique<GateEffect>(); });
    f.registerBuiltIn ("delay", "Time / Modulation", [] { return std::make_unique<DelayEffect>(); });
    f.registerBuiltIn ("reverb", "Time / Modulation", [] { return std::make_unique<ReverbEffect>(); });
    f.registerBuiltIn ("chorus", "Time / Modulation", [] { return std::make_unique<ChorusEffect>(); });
    f.registerBuiltIn ("flanger", "Time / Modulation", [] { return std::make_unique<FlangerEffect>(); });
    f.registerBuiltIn ("phaser", "Time / Modulation", [] { return std::make_unique<PhaserEffect>(); });
}
}
