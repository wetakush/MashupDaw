#pragma once
#include <juce_core/juce_core.h>

// All ValueTree type/property identifiers in one place so the model schema is discoverable.
namespace mashup::ids
{
#define MASHUP_ID(name) inline const juce::Identifier name (#name);

    // node types
    MASHUP_ID (PROJECT)  MASHUP_ID (TEMPOMAP) MASHUP_ID (TEMPO)   MASHUP_ID (MARKERS) MASHUP_ID (MARKER)
    MASHUP_ID (SOURCES)  MASHUP_ID (SOURCE)   MASHUP_ID (BEATS)   MASHUP_ID (DOWNBEATS) MASHUP_ID (TRANSIENTS)
    MASHUP_ID (PHRASES)  MASHUP_ID (PHRASE)   MASHUP_ID (TRACKS)  MASHUP_ID (TRACK)   MASHUP_ID (CLIPS)
    MASHUP_ID (CLIP)     MASHUP_ID (EFFECTS)  MASHUP_ID (EFFECT)  MASHUP_ID (SENDS)   MASHUP_ID (SEND)
    MASHUP_ID (AUTOMATION) MASHUP_ID (LANE)   MASHUP_ID (POINT)   MASHUP_ID (MASTER)  MASHUP_ID (BUSES)
    MASHUP_ID (BUS)      MASHUP_ID (WARP)     MASHUP_ID (WARPMARKER) MASHUP_ID (SLICES) MASHUP_ID (SLICE)
    MASHUP_ID (SELECTION)

    // properties
    MASHUP_ID (id)  MASHUP_ID (name) MASHUP_ID (color) MASHUP_ID (path) MASHUP_ID (sampleRate) MASHUP_ID (channels)
    MASHUP_ID (lengthSamples) MASHUP_ID (bpm) MASHUP_ID (bpmConfidence) MASHUP_ID (keyRoot) MASHUP_ID (keyMode)
    MASHUP_ID (keyConfidence) MASHUP_ID (lufs) MASHUP_ID (peak) MASHUP_ID (data) MASHUP_ID (firstDownbeat)
    MASHUP_ID (analysed) MASHUP_ID (beat) MASHUP_ID (time) MASHUP_ID (start) MASHUP_ID (end) MASHUP_ID (label)
    MASHUP_ID (confidence) MASHUP_ID (volume) MASHUP_ID (pan) MASHUP_ID (mute) MASHUP_ID (solo) MASHUP_ID (arm)
    MASHUP_ID (phaseInvert) MASHUP_ID (inputGain) MASHUP_ID (height) MASHUP_ID (sourceId) MASHUP_ID (length)
    MASHUP_ID (offset) MASHUP_ID (gain) MASHUP_ID (fadeIn) MASHUP_ID (fadeOut) MASHUP_ID (fadeInShape)
    MASHUP_ID (fadeOutShape) MASHUP_ID (pitchSemis) MASHUP_ID (pitchCents) MASHUP_ID (formant) MASHUP_ID (stretchMode)
    MASHUP_ID (rate) MASHUP_ID (clipBpm) MASHUP_ID (reverse) MASHUP_ID (loop) MASHUP_ID (warpEnabled)
    MASHUP_ID (syncToProject) MASHUP_ID (type) MASHUP_ID (pluginId) MASHUP_ID (bypass) MASHUP_ID (state)
    MASHUP_ID (bus) MASHUP_ID (level) MASHUP_ID (param) MASHUP_ID (mode) MASHUP_ID (value) MASHUP_ID (curve)
    MASHUP_ID (timeSigNum) MASHUP_ID (timeSigDen) MASHUP_ID (loopStart) MASHUP_ID (loopEnd) MASHUP_ID (loopEnabled)
    MASHUP_ID (sourceTime) MASHUP_ID (beatTime) MASHUP_ID (role) MASHUP_ID (version) MASHUP_ID (stemOf)
    MASHUP_ID (stemType) MASHUP_ID (sourceStart) MASHUP_ID (sourceEnd) MASHUP_ID (playbackPitch) MASHUP_ID (gate)
    MASHUP_ID (repeat) MASHUP_ID (order) MASHUP_ID (midiNote) MASHUP_ID (masterVolume) MASHUP_ID (index)
    MASHUP_ID (armed) MASHUP_ID (enabled) MASHUP_ID (locked) MASHUP_ID (snap) MASHUP_ID (gridDivision)
    MASHUP_ID (zoom) MASHUP_ID (scroll) MASHUP_ID (playhead)

#undef MASHUP_ID
}
