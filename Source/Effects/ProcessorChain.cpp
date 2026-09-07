#include "ProcessorChain.h"

namespace mashup
{
void ProcessorChain::prepare (double sampleRate, int maxBlockSize)
{
    for (auto& s : slots)
        if (s->processor)
        {
            s->processor->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
            s->processor->prepareToPlay (sampleRate, maxBlockSize);
        }
}

void ProcessorChain::process (juce::AudioBuffer<float>& stereo, int numSamples) noexcept
{
    if (slots.empty()) return;
    float* chans[2] = { stereo.getWritePointer (0), stereo.getWritePointer (1) };
    juce::AudioBuffer<float> block (chans, 2, numSamples);
    for (auto& s : slots)
    {
        if (! s->processor || s->bypass.load (std::memory_order_relaxed)) continue;
        midi.clear();
        s->processor->processBlock (block, midi);
    }
}

void ProcessorChain::reset() noexcept { for (auto& s : slots) if (s->processor) s->processor->reset(); }

int ProcessorChain::getLatencySamples() const noexcept
{
    int l = 0;
    for (auto& s : slots) if (s->processor && ! s->bypass) l += s->processor->getLatencySamples();
    return l;
}
}
