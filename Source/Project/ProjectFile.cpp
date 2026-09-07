#include "ProjectFile.h"
#include "Core/Identifiers.h"
#include "Core/Log.h"

namespace mashup
{
static void storeRelativePaths (juce::ValueTree& root, const juce::File& projectFile)
{
    for (auto s : root.getChildWithName (ids::SOURCES))
    {
        juce::File f (s[ids::path].toString());
        if (f != juce::File()) s.setProperty ("relPath", f.getRelativePathFrom (projectFile.getParentDirectory()), nullptr);
    }
}

void ProjectFile::resolveSourcePaths (juce::ValueTree& root, const juce::File& projectFile)
{
    for (auto s : root.getChildWithName (ids::SOURCES))
    {
        juce::File abs (s[ids::path].toString());
        if (abs.existsAsFile()) continue;
        const auto rel = s["relPath"].toString();
        if (rel.isNotEmpty())
        {
            auto f = projectFile.getParentDirectory().getChildFile (rel);
            if (f.existsAsFile()) { s.setProperty (ids::path, f.getFullPathName(), nullptr); continue; }
        }
        // last resort: same file name next to the project
        auto sibling = projectFile.getSiblingFile (abs.getFileName());
        if (sibling.existsAsFile()) s.setProperty (ids::path, sibling.getFullPathName(), nullptr);
    }
}

juce::Result ProjectFile::saveXml (const juce::ValueTree& root, const juce::File& file)
{
    auto xml = root.createXml();
    if (! xml) return juce::Result::fail ("could not serialise project");
    juce::TemporaryFile tmp (file);
    if (! xml->writeTo (tmp.getFile())) return juce::Result::fail ("could not write " + tmp.getFile().getFullPathName());
    if (! tmp.overwriteTargetFileWithTemporary()) return juce::Result::fail ("could not replace " + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result ProjectFile::loadXml (const juce::File& file, juce::ValueTree& out)
{
    auto xml = juce::XmlDocument::parse (file);
    if (! xml) return juce::Result::fail ("not a valid project XML: " + file.getFullPathName());
    out = juce::ValueTree::fromXml (*xml);
    if (! out.hasType (ids::PROJECT)) return juce::Result::fail ("not a MashupDaw project");
    return juce::Result::ok();
}

juce::Result ProjectFile::save (const juce::ValueTree& rootIn, const juce::File& file, const juce::File& cacheDir)
{
    auto root = rootIn.createCopy();
    storeRelativePaths (root, file);
    auto xml = root.createXml();
    if (! xml) return juce::Result::fail ("could not serialise project");
    juce::ZipFile::Builder zip;
    juce::MemoryOutputStream xmlData;
    xml->writeTo (xmlData);
    juce::MemoryBlock xmlBlock (xmlData.getData(), xmlData.getDataSize());
    zip.addEntry (new juce::MemoryInputStream (xmlBlock, false), 6, "project.xml", juce::Time::getCurrentTime());
    // embed waveform caches for sources that exist in the project
    if (cacheDir.isDirectory())
        for (auto s : root.getChildWithName (ids::SOURCES))
        {
            auto wfc = cacheDir.getChildFile ("waveforms").getChildFile (s[ids::id].toString() + ".wfc");
            if (wfc.existsAsFile()) zip.addFile (wfc, 1, "waveforms/" + wfc.getFileName());
        }
    juce::TemporaryFile tmp (file);
    {
        juce::FileOutputStream os (tmp.getFile());
        if (! os.openedOk()) return juce::Result::fail ("could not open " + tmp.getFile().getFullPathName());
        double progress = 0;
        if (! zip.writeToStream (os, &progress)) return juce::Result::fail ("could not write zip");
    }
    if (! tmp.overwriteTargetFileWithTemporary()) return juce::Result::fail ("could not replace " + file.getFullPathName());
    return juce::Result::ok();
}

juce::Result ProjectFile::load (const juce::File& file, juce::ValueTree& out, const juce::File& cacheDir)
{
    if (! file.existsAsFile()) return juce::Result::fail ("file not found: " + file.getFullPathName());
    juce::ZipFile zip (file);
    if (zip.getNumEntries() == 0)
    {
        // maybe a plain XML autosave
        auto r = loadXml (file, out);
        if (r.wasOk()) resolveSourcePaths (out, file);
        return r;
    }
    auto* entry = zip.getEntry ("project.xml");
    if (! entry) return juce::Result::fail ("project.xml missing in " + file.getFileName());
    std::unique_ptr<juce::InputStream> is (zip.createStreamForEntry (*entry));
    if (! is) return juce::Result::fail ("could not read project.xml");
    auto xml = juce::XmlDocument::parse (is->readEntireStreamAsString());
    if (! xml) return juce::Result::fail ("project.xml is not valid XML");
    out = juce::ValueTree::fromXml (*xml);
    if (! out.hasType (ids::PROJECT)) return juce::Result::fail ("not a MashupDaw project");
    resolveSourcePaths (out, file);
    if (cacheDir != juce::File())
    {
        auto wfDir = cacheDir.getChildFile ("waveforms"); wfDir.createDirectory();
        for (int i = 0; i < zip.getNumEntries(); ++i)
        {
            auto* e = zip.getEntry (i);
            if (! e->filename.startsWith ("waveforms/")) continue;
            auto target = wfDir.getChildFile (e->filename.fromFirstOccurrenceOf ("/", false, false));
            if (target.existsAsFile()) continue;
            std::unique_ptr<juce::InputStream> es (zip.createStreamForEntry (i));
            if (es) { juce::FileOutputStream os (target); if (os.openedOk()) os.writeFromInputStream (*es, -1); }
        }
    }
    return juce::Result::ok();
}
} // namespace mashup
