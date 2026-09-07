#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace mashup
{
/** Owns the ApplicationCommandManager and the persisted, user-editable key mapping. */
class KeyMap
{
public:
    explicit KeyMap (juce::ApplicationProperties&);
    ~KeyMap();

    juce::ApplicationCommandManager& getCommandManager() { return commandManager; }
    juce::KeyPressMappingSet& getMappings() { return *commandManager.getKeyMappings(); }

    /** Call after the command target has registered all commands. Loads saved mappings on top of the defaults. */
    void applyDefaultsAndLoad();
    void save();
    void resetToDefaults();

    /** Shows the JUCE key mapping editor in a window. */
    void showEditor();

private:
    void setDefaults();
    juce::ApplicationProperties& props;
    juce::ApplicationCommandManager commandManager;
    std::unique_ptr<juce::DialogWindow> editorWindow;
};
} // namespace mashup
