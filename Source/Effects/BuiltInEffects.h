#pragma once
#include "EffectFactory.h"

namespace mashup
{
/** Registers every built-in effect type with the factory. Types (strings used in EFFECT.type):
    "filter", "eq", "compressor", "limiter", "gate", "saturation", "distortion", "delay", "reverb",
    "chorus", "flanger", "phaser", "widener". */
void registerAllBuiltInEffects (EffectFactory&);
}
