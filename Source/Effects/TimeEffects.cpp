#include "TimeEffects.h"
#include "EffectContext.h"

namespace mashup
{
// ---- Delay -------------------------------------------------------------------------------------------------------------
DelayEffect::DelayEffect()
    : BuiltInEffect ("Delay", { fx::pb ("sync", "Tempo sync", true), fx::pc ("note", "Note", { "1/32", "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2", "1/1" }, 3),
                                fx::pf ("time", "Time", 1.0f, 2000.0f, 375.0f, 300.0f, "ms"), fx::pf ("feedback", "Feedback", 0.0f, 0.95f, 0.4f), fx::pb ("pingpong", "Ping-pong", true),
                                fx::pf ("hp", "Feedback HP", 20.0f, 2000.0f, 150.0f, 200.0f, "Hz"), fx::pf ("lp", "Feedback LP", 500.0f, 20000.0f, 6000.0f, 4000.0f, "Hz"), fx::pf ("mix", "Mix", 0.0f, 1.0f, 0.3f) })
{ tailSeconds = 4.0; }
void DelayEffect::prepareToPlay (double s, int bs)
{
    sr = s; for (auto& d : lines) { d.prepare ({ s, (juce::uint32) bs, 1 }); d.setMaximumDelayInSamples ((int) (s * 4.0)); }
    for (int c = 0; c < 2; ++c) { hp[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (s, 150.0f); lp[c].coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (s, 6000.0f); }
    timeSmoothed.reset (s, 0.05); reset();
}
void DelayEffect::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    const bool sync = param ("sync") > 0.5f, pingpong = param ("pingpong") > 0.5f;
    static const double noteBeats[] = { 0.125, 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0, 4.0 };
    double seconds = param ("time") * 0.001;
    if (sync) seconds = noteBeats[juce::jlimit (0, 8, (int) param ("note"))] * 60.0 / EffectContext::currentBpm().load();
    timeSmoothed.setTargetValue ((float) juce::jlimit (1.0, 3.99, seconds * sr) );
    const float fb = param ("feedback"), m = param ("mix");
    static float lastHp = -1, lastLp = -1; const float hpv = param ("hp"), lpv = param ("lp");
    if (hpv != lastHp) { lastHp = hpv; for (auto& f : hp) f.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, hpv); }
    if (lpv != lastLp) { lastLp = lpv; for (auto& f : lp) f.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, lpv); }
    float* l = b.getWritePointer (0); float* r = b.getWritePointer (1);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float d = timeSmoothed.getNextValue();
        lines[0].setDelay (d); lines[1].setDelay (d);
        const float dl = lines[0].popSample (0), dr = lines[1].popSample (0);
        float inL = l[i], inR = r[i];
        float fbL = lp[0].processSample (hp[0].processSample (pingpong ? dr : dl)) * fb;
        float fbR = lp[1].processSample (hp[1].processSample (pingpong ? dl : dr)) * fb;
        lines[0].pushSample (0, inL + fbL); lines[1].pushSample (0, (pingpong ? 0.0f : inR) + fbR + (pingpong ? 0.0f : 0.0f));
        if (pingpong) lines[1].pushSample (0, 0.0f), lines[1].popSample (0);   // keep line lengths equal (no-op)
        l[i] = inL * (1 - m) + dl * m; r[i] = inR * (1 - m) + dr * m;
    }
}

// ---- Reverb -------------------------------------------------------------------------------------------------------------
ReverbEffect::ReverbEffect()
    : BuiltInEffect ("Reverb", { fx::pf ("size", "Size", 0.0f, 1.0f, 0.6f), fx::pf ("damp", "Damping", 0.0f, 1.0f, 0.5f), fx::pf ("width", "Width", 0.0f, 1.0f, 1.0f),
                                 fx::pf ("predelay", "Pre-delay", 0.0f, 200.0f, 10.0f, 30.0f, "ms"), fx::pf ("mix", "Mix", 0.0f, 1.0f, 0.3f) })
{ tailSeconds = 6.0; }
void ReverbEffect::prepareToPlay (double s, int bs)
{
    sr = s; reverb.prepare ({ s, (juce::uint32) bs, 2 }); predelay.prepare ({ s, (juce::uint32) bs, 2 }); predelay.setMaximumDelayInSamples ((int) (s * 0.25)); dry.setSize (2, bs, false, true, true); reset();
}
void ReverbEffect::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    const int n = b.getNumSamples(); if (n > dry.getNumSamples()) return;
    juce::dsp::Reverb::Parameters p; p.roomSize = param ("size"); p.damping = param ("damp"); p.width = param ("width"); p.wetLevel = 1.0f; p.dryLevel = 0.0f; p.freezeMode = 0.0f;
    reverb.setParameters (p);
    const float m = param ("mix");
    for (int c = 0; c < 2; ++c) dry.copyFrom (c, 0, b, c, 0, n);
    predelay.setDelay ((float) (param ("predelay") * 0.001 * sr));
    for (int c = 0; c < 2; ++c) { float* d = b.getWritePointer (c); for (int i = 0; i < n; ++i) { predelay.pushSample (c, d[i]); d[i] = predelay.popSample (c); } }
    juce::dsp::AudioBlock<float> block (b); juce::dsp::ProcessContextReplacing<float> ctx (block);
    reverb.process (ctx);
    for (int c = 0; c < 2; ++c) { b.applyGain (c, 0, n, m); b.addFrom (c, 0, dry, c, 0, n, 1.0f - m); }
}

// ---- Chorus / Flanger / Phaser --------------------------------------------------------------------------------------------
ChorusEffect::ChorusEffect()
    : BuiltInEffect ("Chorus", { fx::pf ("rate", "Rate", 0.05f, 10.0f, 0.8f, 1.0f, "Hz"), fx::pf ("depth", "Depth", 0.0f, 1.0f, 0.35f), fx::pf ("delay", "Centre delay", 1.0f, 50.0f, 12.0f, 10.0f, "ms"), fx::pf ("feedback", "Feedback", -0.9f, 0.9f, 0.0f), fx::pf ("mix", "Mix", 0.0f, 1.0f, 0.5f) }) {}
void ChorusEffect::prepareToPlay (double s, int bs) { chorus.prepare ({ s, (juce::uint32) bs, 2 }); chorus.reset(); }
void ChorusEffect::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    chorus.setRate (param ("rate")); chorus.setDepth (param ("depth")); chorus.setCentreDelay (param ("delay")); chorus.setFeedback (param ("feedback")); chorus.setMix (param ("mix"));
    juce::dsp::AudioBlock<float> block (b); juce::dsp::ProcessContextReplacing<float> ctx (block); chorus.process (ctx);
}
FlangerEffect::FlangerEffect()
    : BuiltInEffect ("Flanger", { fx::pf ("rate", "Rate", 0.05f, 5.0f, 0.3f, 0.5f, "Hz"), fx::pf ("depth", "Depth", 0.0f, 1.0f, 0.7f), fx::pf ("delay", "Delay", 0.5f, 10.0f, 2.0f, 2.0f, "ms"), fx::pf ("feedback", "Feedback", -0.95f, 0.95f, 0.5f), fx::pf ("mix", "Mix", 0.0f, 1.0f, 0.5f) }) {}
void FlangerEffect::prepareToPlay (double s, int bs) { chorus.prepare ({ s, (juce::uint32) bs, 2 }); chorus.reset(); }
void FlangerEffect::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    chorus.setRate (param ("rate")); chorus.setDepth (param ("depth")); chorus.setCentreDelay (param ("delay")); chorus.setFeedback (param ("feedback")); chorus.setMix (param ("mix"));
    juce::dsp::AudioBlock<float> block (b); juce::dsp::ProcessContextReplacing<float> ctx (block); chorus.process (ctx);
}
PhaserEffect::PhaserEffect()
    : BuiltInEffect ("Phaser", { fx::pf ("rate", "Rate", 0.05f, 10.0f, 0.5f, 1.0f, "Hz"), fx::pf ("depth", "Depth", 0.0f, 1.0f, 0.6f), fx::pf ("centre", "Centre", 100.0f, 8000.0f, 1000.0f, 1000.0f, "Hz"), fx::pf ("feedback", "Feedback", -0.95f, 0.95f, 0.3f), fx::pf ("mix", "Mix", 0.0f, 1.0f, 0.5f) }) {}
void PhaserEffect::prepareToPlay (double s, int bs) { phaser.prepare ({ s, (juce::uint32) bs, 2 }); phaser.reset(); }
void PhaserEffect::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)
{
    phaser.setRate (param ("rate")); phaser.setDepth (param ("depth")); phaser.setCentreFrequency (param ("centre")); phaser.setFeedback (param ("feedback")); phaser.setMix (param ("mix"));
    juce::dsp::AudioBlock<float> block (b); juce::dsp::ProcessContextReplacing<float> ctx (block); phaser.process (ctx);
}
} // namespace mashup
