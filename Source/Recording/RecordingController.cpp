#include "RecordingController.h"
#include "AudioEngine/AudioEngine.h"
#include "Import/SourceLibrary.h"
#include "Clips/ClipOperations.h"
#include "Timeline/TempoMap.h"
#include "Analysis/AnalysisService.h"
#include "Core/Log.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace mashup
{
RecordingController::RecordingController (Session& s) : session (s)
{
    session.getAudioEngine().getRecorder().onTakeFinished = [this] (juce::AudioBuffer<float>&& take, double sr, juce::int64 start) { takeFinished (std::move (take), sr, start); };
    startTimerHz (30);
}
RecordingController::~RecordingController() { stopTimer(); session.getAudioEngine().getRecorder().onTakeFinished = nullptr; }

void RecordingController::timerCallback()
{
    auto& engine = session.getAudioEngine();
    auto& t = engine.getTransport();
    auto& rec = engine.getRecorder();
    bool anyArmed = false;
    for (const auto& tr : session.getProject().tracks()) if (TrackModel (tr).isArmed()) anyArmed = true;
    const bool shouldRecord = t.isPlaying() && t.isRecording() && anyArmed;
    if (shouldRecord && ! rec.isRecording()) rec.start (t.getPosition());
    else if (! shouldRecord && rec.isRecording()) rec.stop();
}

void RecordingController::takeFinished (juce::AudioBuffer<float>&& take, double sr, juce::int64 start)
{
    auto dir = session.getCacheDirectory().getChildFile ("recordings"); dir.createDirectory();
    juce::File file;
    do { file = dir.getChildFile ("take-" + juce::String (++takeCounter).paddedLeft ('0', 3) + ".wav"); } while (file.existsAsFile());
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream());
        if (! os) { log ("Recording: cannot write " + file.getFullPathName()); return; }
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (os.get(), sr, (unsigned) take.getNumChannels(), 24, {}, 0));
        if (! writer) return;
        os.release();
        writer->writeFromAudioSampleBuffer (take, 0, take.getNumSamples());
    }
    auto& p = session.getProject();
    const double seconds = take.getNumSamples() / sr;
    const auto name = file.getFileNameWithoutExtension();
    const auto id = session.getSourceLibrary().addDecoded (file, std::move (take), sr, name);
    TempoMap tm = TempoMap::fromProject (p.getRoot());
    const double startBeat = tm.timeToBeat (start / session.getAudioEngine().getSampleRate());
    p.getUndoManager().beginNewTransaction ("Record take");
    for (auto tr : p.tracks())
    {
        TrackModel track (tr);
        if (! track.isArmed()) continue;
        auto clip = trackops::placeSource (p, id, name, seconds, startBeat, track);
        clip.setStretchMode (StretchMode::Repitch, nullptr);
    }
    session.getAnalysis().analyseSource (id);
    log ("Recorded take " + name + " (" + juce::String (seconds, 1) + " s)");
}
} // namespace mashup
