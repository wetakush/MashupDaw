#pragma once
#include <juce_core/juce_core.h>
namespace mashup
{
/** Message/worker thread logging. NEVER call from the audio thread. */
inline void log (const juce::String& s) { juce::Logger::writeToLog (s); }
}
