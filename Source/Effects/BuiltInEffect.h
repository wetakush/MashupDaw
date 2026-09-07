#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Core/Identifiers.h"

namespace mashup
{
/** Base for all built-in effects: stereo in/out, parameters through an APVTS, state syncable with an
    EFFECT ValueTree node (PARAMS child holds paramId -> value). */
class BuiltInEffect : public juce::AudioProcessor
{
public:
    BuiltInEffect (const juce::String& typeName, juce::AudioProcessorValueTreeState::ParameterLayout layout);

    const juce::String getName() const override { return typeName; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return tailSeconds; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    bool isBusesLayoutSupported (const BusesLayout& l) const override { return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    float param (const juce::String& id) const noexcept { return raw.at (id)->load (std::memory_order_relaxed); }

    /** Message thread: pushes PARAMS from the model node into the processor. */
    void loadParamsFromNode (const juce::ValueTree& effectNode);
    /** Message thread: writes current parameter values to the node's PARAMS child (undoable). */
    void saveParamsToNode (juce::ValueTree effectNode, juce::UndoManager*) const;

protected:
    std::atomic<float>* rawPtr (const juce::String& id) { return apvts.getRawParameterValue (id); }
    double tailSeconds = 0.0;

private:
    juce::String typeName;
    juce::AudioProcessorValueTreeState apvts;
    std::map<juce::String, std::atomic<float>*> raw;
};

/** Helpers for building parameter layouts. */
namespace fx
{
    inline std::unique_ptr<juce::AudioParameterFloat> pf (const juce::String& id, const juce::String& name, float lo, float hi, float def, float skew = 1.0f, const juce::String& unit = {})
    {
        juce::NormalisableRange<float> r (lo, hi); r.setSkewForCentre (skew == 1.0f ? (lo + hi) * 0.5f : skew);
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID (id, 1), name, r, def, juce::AudioParameterFloatAttributes().withLabel (unit));
    }
    inline std::unique_ptr<juce::AudioParameterChoice> pc (const juce::String& id, const juce::String& name, const juce::StringArray& choices, int def)
    {
        return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID (id, 1), name, choices, def);
    }
    inline std::unique_ptr<juce::AudioParameterBool> pb (const juce::String& id, const juce::String& name, bool def)
    {
        return std::make_unique<juce::AudioParameterBool> (juce::ParameterID (id, 1), name, def);
    }
}
} // namespace mashup
