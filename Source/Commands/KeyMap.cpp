#include "KeyMap.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "CommandIDs.h"
#include "UI/Theme/Theme.h"

namespace mashup
{
KeyMap::KeyMap (juce::ApplicationProperties& p) : props (p) {}
KeyMap::~KeyMap() { save(); }

void KeyMap::setDefaults()
{
    auto& km = getMappings();
    km.clearAllKeyPresses();
    using KP = juce::KeyPress; const auto ctrl = juce::ModifierKeys::commandModifier; const auto shift = juce::ModifierKeys::shiftModifier; const auto alt = juce::ModifierKeys::altModifier;
    auto add = [&] (int id, KP kp) { km.addKeyPress (id, kp); };
    add (cmd::newProject, KP ('n', ctrl, 0));         add (cmd::openProject, KP ('o', ctrl, 0));
    add (cmd::saveProject, KP ('s', ctrl, 0));        add (cmd::saveProjectAs, KP ('s', ctrl | shift, 0));
    add (cmd::importAudio, KP ('i', ctrl, 0));        add (cmd::exportMix, KP ('e', ctrl, 0));
    add (cmd::exportStems, KP ('e', ctrl | shift, 0));
    add (cmd::undo, KP ('z', ctrl, 0));               add (cmd::redo, KP ('z', ctrl | shift, 0)); add (cmd::redo, KP ('y', ctrl, 0));
    add (cmd::cut, KP ('x', ctrl, 0));                add (cmd::copy, KP ('c', ctrl, 0));           add (cmd::paste, KP ('v', ctrl, 0));
    add (cmd::deleteSelection, KP (KP::deleteKey));   add (cmd::deleteSelection, KP (KP::backspaceKey));
    add (cmd::selectAll, KP ('a', ctrl, 0));          add (cmd::duplicate, KP ('d', ctrl, 0));
    add (cmd::split, KP ('s', 0, 0));
    add (cmd::playStop, KP (KP::spaceKey));           add (cmd::stop, KP (KP::numberPad0));  add (cmd::stop, KP (KP::escapeKey));
    add (cmd::record, KP ('r', 0, 0));                add (cmd::goToStart, KP (KP::homeKey));      add (cmd::goToStart, KP (KP::returnKey));
    add (cmd::toggleLoop, KP ('l', 0, 0));            add (cmd::setLoopToSelection, KP ('l', ctrl, 0));
    add (cmd::zoomIn, KP ('=', 0, 0));                add (cmd::zoomIn, KP ('+', 0, 0));            add (cmd::zoomOut, KP ('-', 0, 0));
    add (cmd::zoomToSelection, KP ('z', 0, 0));       add (cmd::zoomToFit, KP ('z', shift, 0));
    add (cmd::toggleBrowser, KP ('b', ctrl, 0));      add (cmd::toggleInspector, KP ('i', ctrl | shift, 0)); add (cmd::toggleBottom, KP ('m', ctrl, 0));
    add (cmd::addTrack, KP ('t', ctrl, 0));           add (cmd::muteTrack, KP ('m', 0, 0));         add (cmd::soloTrack, KP ('s', shift, 0));
    add (cmd::armTrack, KP ('r', shift, 0));
    add (cmd::toolSelect, KP ('1', 0, 0));            add (cmd::toolBlade, KP ('b', 0, 0));         add (cmd::toolBlade, KP ('2', 0, 0));
    add (cmd::toolAutomation, KP ('a', 0, 0));        add (cmd::toggleSnap, KP ('g', 0, 0));
    add (cmd::reverseClip, KP ('r', ctrl, 0));        add (cmd::loopClip, KP ('l', alt, 0));       add (cmd::muteClip, KP ('m', alt, 0));
    add (cmd::matchClipBpm, KP ('b', alt, 0));        add (cmd::matchClipKey, KP ('k', alt, 0));
    add (cmd::mashupAssistant, KP ('m', ctrl | shift, 0)); add (cmd::separateStems, KP ('d', ctrl | shift, 0)); add (cmd::vocalChopper, KP ('c', ctrl | shift, 0));
    add (cmd::bypassEffects, KP ('f', alt, 0));       add (cmd::tapTempo, KP ('t', 0, 0));          add (cmd::showMixer, KP ('x', alt, 0));
}

void KeyMap::applyDefaultsAndLoad()
{
    setDefaults();
    if (auto* settings = props.getUserSettings())
        if (auto xml = settings->getXmlValue ("keyMappings"))
            getMappings().restoreFromXml (*xml);
}

void KeyMap::save()
{
    if (auto* settings = props.getUserSettings())
        if (auto xml = getMappings().createXml (true)) { settings->setValue ("keyMappings", xml.get()); settings->saveIfNeeded(); }
}

void KeyMap::resetToDefaults() { setDefaults(); save(); }

void KeyMap::showEditor()
{
    auto* editor = new juce::KeyMappingEditorComponent (getMappings(), true);
    editor->setSize (520, 600);
    editor->setColours (ui::colours::panelBg, ui::colours::text);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (editor);
    o.dialogTitle = "Keyboard shortcuts";
    o.dialogBackgroundColour = ui::colours::panelBg;
    o.resizable = true; o.useNativeTitleBar = true;
    editorWindow.reset (o.create());
    editorWindow->setVisible (true);
}
} // namespace mashup
