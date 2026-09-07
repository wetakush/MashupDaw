// Developer tool: mashup_analyse <audio file> — prints the analysis a source would get inside the DAW.
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "Import/FFmpegDecoder.h"
#include "Analysis/AnalysisService.h"
#include "Core/MusicalKey.h"

int main (int argc, char** argv)
{
    if (argc < 2) { std::cout << "usage: mashup_analyse <file>\n"; return 1; }
    juce::ScopedJuceInitialiser_GUI init;
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto res = mashup::FFmpegDecoder::decode (juce::File (juce::String::fromUTF8 (argv[1])));
    if (! res.ok) { std::cout << "decode failed: " << res.error << "\n"; return 1; }
    std::cout << "decoded " << res.audio.getNumSamples() << " samples @ " << res.info.sampleRate << " Hz, " << res.info.channels << " ch in " << (juce::Time::getMillisecondCounterHiRes() - t0) << " ms\n";
    t0 = juce::Time::getMillisecondCounterHiRes();
    auto r = mashup::AnalysisService::analyseBuffer (res.audio, res.info.sampleRate);
    std::cout << "analysed in " << (juce::Time::getMillisecondCounterHiRes() - t0) << " ms\n";
    std::cout << "BPM: " << r.bpm << " (confidence " << r.bpmConfidence << ")\n";
    std::cout << "Key: " << mashup::key::name (r.keyRoot, r.keyMode) << " / Camelot " << mashup::key::camelot (r.keyRoot, r.keyMode) << " (confidence " << r.keyConfidence << ")\n";
    std::cout << "Loudness: " << r.lufs << " LUFS, true peak " << juce::Decibels::gainToDecibels (r.truePeak) << " dBFS, LRA " << r.lra << "\n";
    std::cout << "Beats: " << r.beats.size() << ", first downbeat " << r.firstDownbeat << " s, transients " << r.transients.size() << "\n";
    std::cout << "Phrases:\n";
    for (auto& p : r.phrases) std::cout << "  " << p.start << " - " << p.end << "  " << p.label << " (" << (int) (p.confidence * 100) << "%)\n";
    return 0;
}
