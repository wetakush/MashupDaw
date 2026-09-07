#pragma once
#include <atomic>
namespace mashup
{
/** Global realtime-readable transport info for tempo-synced effects (written by the RenderGraph). */
struct EffectContext
{
    static std::atomic<double>& currentBpm() { static std::atomic<double> bpm { 120.0 }; return bpm; }
};
}
