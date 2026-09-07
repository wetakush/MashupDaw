#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace mashup::cmd
{
enum IDs
{
    // file
    newProject = 0x1000, openProject, saveProject, saveProjectAs, importAudio, exportMix, exportStems, audioSettings, editShortcuts, pluginManager,
    // edit
    undo = 0x2000, redo, cut, copy, paste, deleteSelection, selectAll, duplicate, split, splitAtSelection,
    // transport
    playStop = 0x3000, stop, record, goToStart, toggleLoop, setLoopToSelection, rewind, forward,
    // view
    zoomIn = 0x4000, zoomOut, zoomToFit, zoomToSelection, toggleBrowser, toggleInspector, toggleBottom, showMixer, showChopper, showMashupAssistant, showAnalysis, toggleFollow,
    // track
    addTrack = 0x5000, deleteTrack, muteTrack, soloTrack, armTrack, duplicateTrack,
    // clip / tools
    toolSelect = 0x6000, toolBlade, toolAutomation, toggleSnap, matchClipBpm, matchClipKey, reverseClip, loopClip, muteClip, normalizeClip,
    sliceBeats, sliceTransients, slicePhrases, cropToSelection, crossfadeSelected,
    // tools
    mashupAssistant = 0x7000, separateStems, extractAcapella, extractInstrumental, analyseSelected, vocalChopper, tapTempo, bypassEffects, abCompare
};
}
