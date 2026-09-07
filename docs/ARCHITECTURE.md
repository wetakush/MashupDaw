# MashupDaw — Architecture

## Stack
- C++20, JUCE 8 (add_subdirectory third_party/JUCE), CMake ≥ 3.22, Ninja.
- FFmpeg (libavformat/avcodec/swresample) — import (any container/codec) and export (WAV/FLAC/MP3/AAC).
- Rubber Band 4 (R3 engine) — realtime and offline time-stretch / pitch-shift. Selectable option presets:
  Realtime, HighQuality, Vocal, Percussive.
- FFTW3f — spectral analysis (BPM, key, transients, phrases). juce::dsp used for IIR/convolution.
- VST3 hosting via juce_audio_processors.
- Stem separation: `Scripts/demucs_worker.py` (PyTorch/Demucs htdemucs, CUDA with CPU fallback) driven from C++ over
  stdin/stdout JSON lines (progress / cancel / result). Set up by `Scripts/setup_stems_env.sh`.

## Layering (dependency direction: top → bottom only)
```
UI ─────────────────────────────┐
Commands (key map, app cmds)    │
Mashup / Slicing / Export       │   message thread
Project (model, undo, io)       │
Analysis / StemSeparation ──────┤   worker threads (juce::ThreadPool)
AudioEngine ────────────────────┤   audio thread (realtime-safe)
DSP / Core                      │   pure, thread-agnostic
```

## Data model
Everything persistent lives in ONE `juce::ValueTree` (root type `PROJECT`), edited through `juce::UndoManager`.
Every edit is therefore undoable and serializable. Compound operations (split, slice, mashup apply, ...)
are wrapped in `UndoManager::beginNewTransaction`. Non-ValueTree side effects (e.g. waveform cache files)
are idempotent and recomputed from the model.

```
PROJECT {name, sampleRate, bpm, timeSigNum, timeSigDen, keyRoot, keyMode, loopStart, loopEnd, loopEnabled}
  TEMPOMAP  { TEMPO {beat, bpm} ... }
  MARKERS   { MARKER {time, name, color} }
  SOURCES   { SOURCE {id, path, sampleRate, channels, lengthSamples, bpm, keyRoot, keyMode, lufs, peak,
                       BEATS{...} DOWNBEATS{...} TRANSIENTS{...} PHRASES{ PHRASE{start,end,label,confidence} }} }
  TRACKS    { TRACK {id, name, color, volume, pan, mute, solo, arm, phaseInvert, inputGain, height}
                 CLIPS { CLIP {id, sourceId, start, length, offset, gain, pan, mute, fadeIn, fadeOut,
                                pitchSemis, pitchCents, stretchMode, rate, clipBpm, keyRoot, keyMode,
                                reverse, loop, warpEnabled, name, color} }
                 EFFECTS { EFFECT {type, pluginId, bypass, state} }
                 SENDS { SEND {bus, level} }
                 AUTOMATION { LANE {param, mode} { POINT {time, value, curve} } } }
  MASTER    { EFFECTS {...} volume }
  BUSES     { BUS {name, type=reverb|delay} EFFECTS {...} }
```

## Realtime engine
- `AudioEngine` owns `juce::AudioDeviceManager` and is the single `AudioIODeviceCallback`.
- The message thread compiles the model into an immutable `RenderGraph` (tracks → clip players → fx chains →
  sends → master). The graph is published to the audio thread through a lock-free pointer swap
  (`RealtimeSwap<T>`); retired graphs are deleted on the message thread.
- Decoded audio (`AudioSource`, float, immutable) is shared via `std::shared_ptr` and never freed on audio thread.
- Each `ClipPlayer` owns a pre-allocated `RubberBandStretcher` (realtime mode) and output ring buffer;
  `process()` is allocation-free. Stretchers are created/resized on the message thread before publishing.
- Parameter changes (volume/pan/mute/...) travel through `RealtimeParameter` atomics; commands through a SPSC
  `juce::AbstractFifo`-based `CommandQueue`.
- Metering: audio thread writes atomics; UI polls at 30 Hz.
- Forbidden in the callback: allocation, locks, file IO, logging, ValueTree access.
- Automation: `GraphBuilder` snapshots each track's lanes into an immutable `AutomationSet`; `TrackRenderer`
  evaluates the curves at the start of every segment (volume/pan/sends via atomics, built-in effect parameters via
  `AudioProcessorParameter::setValue`). While a lane is being recorded (Write/Touch/Latch) playback of that
  track's automation is disabled so the fader and the curve do not fight.
- Clips whose speed/pitch/formant are all at unity bypass Rubber Band entirely (plain interpolation).

## Threads
- Message thread: model, UI, graph compilation.
- Audio thread: `RenderGraph::process`; tracks are rendered in parallel by `RenderWorkers` (realtime-priority
  worker threads, lock-free job counter; the audio thread participates and spins until all tracks are done),
  then summed with the send buses and the master chain.
- `juce::ThreadPool` (N = cores): decode, waveform cache build, analysis, offline stretch, export.
- Stem separation: external process + reader thread.

## Waveform
`WaveformCache` = mip-map of min/max/RMS per block at 256, 1024, 4096, 16384, 65536 samples/bin, built once
per source in the background; renderers pick the closest level ≥ desired px density and downsample on the fly.
Cache stored beside the project (`<project>.mashupcache/<sourceId>.wfc`).

## Persisted model additions
`TRACK` also carries `showAutomation`, `automationParam`, `automationMode`, `stemType`;
`SOURCE` carries `stemOf`/`stemType` for separated stems, `firstDownbeat`, `analysed`, `relPath`;
`CLIP` carries `sourceEnd` (loop/reverse region end in source seconds), `formant`, `syncToProject`;
`EFFECT` carries `PARAMS` (built-in parameter values) or `state` (base64 plugin state);
`LANE {param}` holds `POINT {beat, value(0..1), curve}` children.

## Stem separation protocol
`Scripts/demucs_worker.py --job job.json` prints JSON lines: `progress {value, stage}`, `info {device, model}`,
`done {files}`, `error {message}`. `StemSeparationService` runs one job at a time on a worker thread, parses the
lines, posts updates to the message thread and kills the process to cancel. `StemPlacer` imports the resulting
WAVs, copies the original's analysis onto them and mirrors the original clips on new tracks.

## Directory map
Source/Core          RealtimeSwap, SpscQueue, Identifiers, MusicalTime, Result, Log
Source/AudioEngine   AudioEngine, RenderGraph, TrackRenderer, ClipPlayer, MasterBus, Metering, BufferPool
Source/DSP           Stretcher (RubberBand), FFT (FFTW), Filters, Onset, Chroma, Loudness, Resampler
Source/Timeline      TempoMap, Grid/Snap, Markers, TimelineModel helpers
Source/Tracks        TrackModel
Source/Clips         ClipModel, ClipOperations (split/trim/duplicate/fades/crossfade)
Source/Waveform      WaveformCache, WaveformBuilder, WaveformRenderer
Source/Analysis      Analyzer, BpmDetector, BeatTracker, KeyDetector, LoudnessAnalyzer, TransientDetector, PhraseDetector
Source/StemSeparation StemSeparator, DemucsBackend
Source/Mashup        MashupAssistant, Camelot, KeyCompatibility
Source/Mixer         MixerModel helpers (sends, buses)
Source/Effects       Built-in processors (EQ, Comp, Limiter, Gate, Sat, Dist, Delay, Reverb, Chorus, Flanger, Phaser, Filter, Widener), EffectFactory
Source/Plugins       PluginHost (VST3 scanning/instantiation)
Source/Automation    AutomationCurve, AutomationRecorder
Source/Project       ProjectModel, ProjectFile (.mashup), Autosave, CrashRecovery
Source/Commands      CommandIDs, KeyMap
Source/Import        FFmpegDecoder, AudioImporter
Source/Export        OfflineRenderer, FFmpegEncoder, StemExporter
Source/Slicing       Slicer, PatternLibrary, VocalChopperModel
Source/UI            MainWindow, Workspace, Transport, Browser, Timeline, Inspector, Mixer, Chopper, Mashup, Theme
Tests                juce::UnitTest based console runner (ctest)
Scripts              demucs_worker.py, setup_stems_env.sh
