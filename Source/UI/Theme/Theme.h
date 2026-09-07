#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace mashup::ui
{
/** Dark professional palette shared by every component. */
namespace colours
{
    inline const juce::Colour windowBg      { 0xff16181c };
    inline const juce::Colour panelBg       { 0xff1e2126 };
    inline const juce::Colour panelBgAlt    { 0xff23272d };
    inline const juce::Colour headerBg      { 0xff2a2f36 };
    inline const juce::Colour border        { 0xff0e0f12 };
    inline const juce::Colour separator     { 0xff33383f };
    inline const juce::Colour text          { 0xffd7dbe0 };
    inline const juce::Colour textDim       { 0xff8b929c };
    inline const juce::Colour accent        { 0xff1ec8aa };
    inline const juce::Colour accentDim     { 0xff138a76 };
    inline const juce::Colour warning       { 0xffe0a72f };
    inline const juce::Colour danger        { 0xffd9503f };
    inline const juce::Colour record        { 0xffe0453a };
    inline const juce::Colour playhead      { 0xfff4f6f8 };
    inline const juce::Colour gridBar       { 0xff3b414a };
    inline const juce::Colour gridBeat      { 0xff2b3037 };
    inline const juce::Colour gridSub       { 0xff242830 };
    inline const juce::Colour selection     { 0x4020c0a0 };
    inline const juce::Colour loopRegion    { 0x3030a0ff };
    inline const juce::Colour waveform      { 0xff5ccfb8 };
    inline const juce::Colour waveformRms   { 0xff8fe6d5 };
    inline const juce::Colour clipBg        { 0xff2c4c48 };
    inline const juce::Colour clipBgSel     { 0xff3f6f69 };
    inline const juce::Colour meterLow      { 0xff2ec27e };
    inline const juce::Colour meterMid      { 0xffe0c04a };
    inline const juce::Colour meterHigh     { 0xffe04a3a };
    inline const juce::Colour knob          { 0xff3a4049 };

    /** Track colour palette (cycled for new tracks). */
    inline const juce::Colour trackPalette[] = {
        juce::Colour (0xff1ec8aa), juce::Colour (0xffe0a72f), juce::Colour (0xff5a8dee), juce::Colour (0xffd95d8a),
        juce::Colour (0xff9b6bea), juce::Colour (0xff62b84f), juce::Colour (0xffe07a3a), juce::Colour (0xff3fb6d9) };
    inline juce::Colour trackColour (int index) { return trackPalette[(size_t) (juce::jmax (0, index) % 8)]; }
}

class Theme : public juce::LookAndFeel_V4
{
public:
    Theme();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float min, float max,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&, bool, bool) override;
    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool mouseOver, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getTextButtonFont (juce::TextButton&, int h) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    void fillResizableWindowBackground (juce::Graphics&, int, int, const juce::BorderSize<int>&, juce::ResizableWindow&) override;

    static juce::Font mono (float size);
    static juce::Font ui (float size, bool bold = false);
};

/** Draws a panel header strip with a title. */
void drawPanelHeader (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title);

} // namespace mashup::ui
