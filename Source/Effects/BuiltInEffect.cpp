#include "BuiltInEffect.h"

namespace mashup
{
BuiltInEffect::BuiltInEffect (const juce::String& type, juce::AudioProcessorValueTreeState::ParameterLayout layout)
    : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true).withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
      typeName (type), apvts (*this, nullptr, "PARAMS", std::move (layout))
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            raw[rp->paramID] = apvts.getRawParameterValue (rp->paramID);
}

void BuiltInEffect::getStateInformation (juce::MemoryBlock& mb)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, mb);
}

void BuiltInEffect::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size)) apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

void BuiltInEffect::loadParamsFromNode (const juce::ValueTree& node)
{
    auto params = node.getChildWithName (juce::Identifier ("PARAMS"));
    if (! params.isValid()) return;
    for (int i = 0; i < params.getNumProperties(); ++i)
    {
        auto id = params.getPropertyName (i).toString();
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) params[params.getPropertyName (i)]));
    }
}

void BuiltInEffect::saveParamsToNode (juce::ValueTree node, juce::UndoManager* um) const
{
    auto params = node.getOrCreateChildWithName (juce::Identifier ("PARAMS"), um);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            params.setProperty (rp->paramID, rp->convertFrom0to1 (rp->getValue()), um);
}
} // namespace mashup
