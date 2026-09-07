#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace mashup
{
/** .mashup project files: a ZIP containing project.xml plus optional waveform caches. Audio is referenced,
    never embedded. Paths are stored absolute and relative to the project so projects can be moved. */
struct ProjectFile
{
    static constexpr const char* extension = ".mashup";

    /** Saves `root`. `cacheDir` (may be empty) is scanned for waveforms/*.wfc to embed. */
    static juce::Result save (const juce::ValueTree& root, const juce::File& file, const juce::File& cacheDir);

    /** Loads project.xml; extracts embedded caches into `cacheDir` (if given) and fixes up source paths. */
    static juce::Result load (const juce::File& file, juce::ValueTree& outRoot, const juce::File& cacheDir);

    /** Plain XML variant used by autosave (fast, no zip). */
    static juce::Result saveXml (const juce::ValueTree& root, const juce::File& file);
    static juce::Result loadXml (const juce::File& file, juce::ValueTree& outRoot);

    /** Rewrites SOURCE paths so they point at existing files (absolute first, then relative to the project). */
    static void resolveSourcePaths (juce::ValueTree& root, const juce::File& projectFile);
};
} // namespace mashup
