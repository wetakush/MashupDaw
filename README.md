# MashupDaw

A desktop DAW (C++20 / JUCE 8) specialised in building mashups and remixes from finished tracks:
import vocals / instrumentals / acapellas / stems, get BPM, beat grid, downbeats, key and phrases
automatically, separate stems with Demucs (GPU), match tempo and key, arrange on a multitrack timeline
with real waveforms, mix with built-in effects or VST3 plugins, automate, and export.

## Features

* **Realtime engine** — lock-free model → `RenderGraph` publishing, sample-accurate clip scheduling,
  loop, parallel track rendering on realtime worker threads, per-clip Rubber Band (R3) time-stretch /
  pitch-shift with *Repitch / Realtime / High Quality / Vocal / Percussive* modes, formant control.
* **Import / export via FFmpeg** — any container/codec in; WAV / FLAC / MP3 / AAC out with sample rate,
  bit depth, bitrate, mono/stereo, peak normalisation and LUFS loudness target; master, per-track or
  stem-group export.
* **Analysis** — BPM (autocorrelation + dynamic-programming beat tracking, sample-accurate phase),
  downbeats, key (harmonic chroma + Krumhansl/Temperley profiles, Camelot notation), EBU R128 loudness,
  transients, phrase structure (intro / verse / pre-chorus / chorus / bridge / outro with confidence).
  Everything is editable by hand in the Analysis tab.
* **Timeline** — unlimited tracks, multiresolution waveform cache (peaks + RMS, disk-cached), zoom, snap,
  adaptive grid, markers, tempo changes, split / trim / move / duplicate / crossfade / fades / reverse /
  loop / clip gain & pan, blade tool, drag & drop from the browser or the OS, loop region, time selection.
* **Stem separation** — Demucs (htdemucs / htdemucs_ft / 6-stem / mdx_extra) in a Python worker with
  CUDA and CPU fallback, progress + cancel, batch queue; stems land on new tracks aligned with the original.
  *Extract acapella* / *Extract instrumental* shortcuts.
* **Mashup Assistant** — pick a vocal and an instrumental clip, get ranked tempo/key plans (stretch and
  pitch penalties, Camelot compatibility, half/double time, relative keys), apply with one click, A/B.
* **Slicing & patterns** — beat divisions, transients, phrases; pattern library (chorus only, hook,
  4/8/16/32 bars, every 2 bars, alternate bars, reverse sections, stutter, gate, repeat, chopped vocal,
  drum/bass/instrumental-only ...). **Vocal Chopper** with pads, drag-to-rearrange, keyboard and MIDI trigger.
* **Mixer** — volume, pan, mute, solo, phase, input gain, reverb/delay sends, effect chains per track, buses
  and master, LUFS/true-peak metering. Built-in effects: parametric EQ, filter, compressor, limiter, gate,
  saturation, distortion, delay (tempo-synced), reverb, chorus, flanger, phaser, stereo widener. VST3 hosting.
* **Automation** — volume, pan, sends and built-in effect parameters; Read / Write / Touch / Latch.
* **Project files** — `.mashup` (zip: project XML + waveform caches), relative source paths, autosave,
  crash recovery, command-based undo/redo across every edit.
* **Shortcuts** — Space, R, S, B, M, A, Z, Ctrl+Z / Ctrl+Shift+Z, Ctrl+C/V/X, Ctrl+S, Ctrl+E ... all
  editable (File → Keyboard shortcuts).

## Building (Linux)

Dependencies: CMake ≥ 3.22, a C++20 compiler, FFmpeg (libavformat/avcodec/avutil/swresample),
Rubber Band ≥ 3, FFTW3 (single precision), ALSA/JACK, X11/freetype/GL (JUCE), `uv` or Python 3.10–3.12
for stem separation.

```sh
git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git third_party/JUCE
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
./build/MashupDaw_artefacts/Release/MashupDaw            # optionally pass audio files or a .mashup
./build/Tests/MashupTests_artefacts/Release/MashupTests  # or: ctest --test-dir build
Scripts/setup_stems_env.sh                               # once: PyTorch + Demucs (CUDA wheels; --cpu for CPU only)
```

`-DMASHUP_ENABLE_LTO=ON` enables link-time optimisation for release packages; `cpack --config build/CPackConfig.cmake`
builds a tarball; `ninja -C build install` installs the binary, scripts, desktop file and icon.

Windows/macOS: the code has no Linux-only dependencies apart from the PipeWire workaround in `Main.cpp`;
point the `Find*.cmake` modules at your FFmpeg / Rubber Band / FFTW builds.

## Developer flags

`--screenshot=<png> [--delay=<ms>]` renders the main window and quits; `--tab=<name>` opens a bottom tab;
`--separate` runs stem separation on the first imported source; `--export=<file>` renders the project;
`--save=<file.mashup>` saves; `--autolane` shows automation lanes. `mashup_analyse <file>` prints the analysis
of a file. See `docs/ARCHITECTURE.md` for the design.

## Layout

```
Source/AudioEngine   realtime engine, render graph, clip players, transport, recorder, worker threads
Source/DSP           Rubber Band wrapper, FFTW wrapper, loudness meters
Source/Analysis      BPM / beat / downbeat, key, transients, phrases, analysis service
Source/StemSeparation Demucs worker driver, stem placement
Source/Mashup        Mashup Assistant, Camelot helpers (Core/MusicalKey.h)
Source/Slicing       slicer, pattern library
Source/Effects       built-in effects, effect factory, processor chains
Source/Plugins       VST3 host
Source/Automation    curves, recorder
Source/Mixer         mixer model operations
Source/Project       model, project files, session, controller (autosave / recovery)
Source/Import|Export FFmpeg decode/encode, offline renderer
Source/Timeline|Tracks|Clips|Waveform  model facades, tempo map, waveform cache
Source/UI            workspace, transport, browser, timeline, inspector, mixer, chopper, mashup, stems, analysis
Scripts              demucs_worker.py, setup_stems_env.sh
Tests                juce::UnitTest suite (engine, analysis, timeline ops, project files, stress)
```
