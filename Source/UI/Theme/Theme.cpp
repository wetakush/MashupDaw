#include "Theme.h"

namespace mashup::ui
{
using namespace colours;

Theme::Theme()
{
    setColour (juce::ResizableWindow::backgroundColourId, windowBg);
    setColour (juce::DocumentWindow::backgroundColourId, windowBg);
    setColour (juce::TextButton::buttonColourId, headerBg);
    setColour (juce::TextButton::buttonOnColourId, accentDim);
    setColour (juce::TextButton::textColourOffId, text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TextEditor::backgroundColourId, windowBg);
    setColour (juce::TextEditor::textColourId, text);
    setColour (juce::TextEditor::outlineColourId, separator);
    setColour (juce::TextEditor::focusedOutlineColourId, accent);
    setColour (juce::TextEditor::highlightColourId, accentDim);
    setColour (juce::ComboBox::backgroundColourId, windowBg);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::outlineColourId, separator);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, panelBgAlt);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDim);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::PopupMenu::headerTextColourId, textDim);
    setColour (juce::Slider::backgroundColourId, windowBg);
    setColour (juce::Slider::thumbColourId, accent);
    setColour (juce::Slider::trackColourId, accentDim);
    setColour (juce::Slider::rotarySliderFillColourId, accent);
    setColour (juce::Slider::rotarySliderOutlineColourId, knob);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxBackgroundColourId, windowBg);
    setColour (juce::Slider::textBoxOutlineColourId, separator);
    setColour (juce::ScrollBar::thumbColourId, separator);
    setColour (juce::ScrollBar::trackColourId, windowBg);
    setColour (juce::TabbedComponent::backgroundColourId, panelBg);
    setColour (juce::TabbedComponent::outlineColourId, border);
    setColour (juce::TabbedButtonBar::tabOutlineColourId, border);
    setColour (juce::TabbedButtonBar::frontOutlineColourId, border);
    setColour (juce::TabbedButtonBar::tabTextColourId, textDim);
    setColour (juce::TabbedButtonBar::frontTextColourId, text);
    setColour (juce::ListBox::backgroundColourId, panelBg);
    setColour (juce::ListBox::textColourId, text);
    setColour (juce::TreeView::backgroundColourId, panelBg);
    setColour (juce::TreeView::linesColourId, separator);
    setColour (juce::TreeView::selectedItemBackgroundColourId, accentDim.withAlpha (0.5f));
    setColour (juce::AlertWindow::backgroundColourId, panelBgAlt);
    setColour (juce::AlertWindow::textColourId, text);
    setColour (juce::AlertWindow::outlineColourId, separator);
    setColour (juce::ProgressBar::backgroundColourId, windowBg);
    setColour (juce::ProgressBar::foregroundColourId, accent);
    setColour (juce::TooltipWindow::backgroundColourId, headerBg);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, separator);
    setColour (juce::FileBrowserComponent::currentPathBoxBackgroundColourId, windowBg);
    setColour (juce::FileBrowserComponent::currentPathBoxTextColourId, text);
    setColour (juce::FileBrowserComponent::filenameBoxBackgroundColourId, windowBg);
    setColour (juce::FileBrowserComponent::filenameBoxTextColourId, text);
    setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, accentDim.withAlpha (0.5f));
    setColour (juce::DirectoryContentsDisplayComponent::textColourId, text);
    setColour (juce::ToggleButton::textColourId, text);
    setColour (juce::ToggleButton::tickColourId, accent);
    setColour (juce::ToggleButton::tickDisabledColourId, textDim);
    setColour (juce::GroupComponent::outlineColourId, separator);
    setColour (juce::GroupComponent::textColourId, textDim);
    setColour (juce::TableHeaderComponent::backgroundColourId, headerBg);
    setColour (juce::TableHeaderComponent::textColourId, text);
    setColour (juce::TableHeaderComponent::outlineColourId, border);
    setColour (juce::CaretComponent::caretColourId, accent);
}

juce::Font Theme::mono (float size) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain)); }
juce::Font Theme::ui (float size, bool bold) { return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), size, bold ? juce::Font::bold : juce::Font::plain)); }

juce::Font Theme::getTextButtonFont (juce::TextButton&, int h) { return ui (juce::jmin (13.0f, h * 0.6f)); }
juce::Font Theme::getLabelFont (juce::Label& l) { return l.getFont().withHeight (juce::jmin (l.getFont().getHeight(), 13.0f)); }
juce::Font Theme::getComboBoxFont (juce::ComboBox&) { return ui (12.0f); }
juce::Font Theme::getPopupMenuFont() { return ui (13.0f); }

void Theme::fillResizableWindowBackground (juce::Graphics& g, int, int, const juce::BorderSize<int>&, juce::ResizableWindow&)
{
    g.fillAll (windowBg);
}

void Theme::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = bg;
    if (down) c = c.brighter (0.25f); else if (over) c = c.brighter (0.1f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (border);
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
}

void Theme::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (2, 0), juce::Justification::centred, false);
}

void Theme::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat();
    const float box = juce::jmin (14.0f, r.getHeight() - 4.0f);
    auto boxR = juce::Rectangle<float> (2.0f, (r.getHeight() - box) * 0.5f, box, box);
    g.setColour (windowBg.brighter (over ? 0.15f : 0.0f).brighter (down ? 0.1f : 0.0f));
    g.fillRoundedRectangle (boxR, 2.0f);
    g.setColour (separator);
    g.drawRoundedRectangle (boxR, 2.0f, 1.0f);
    if (b.getToggleState())
    {
        g.setColour (accent);
        g.fillRoundedRectangle (boxR.reduced (3.0f), 1.5f);
    }
    g.setColour (b.isEnabled() ? text : textDim);
    g.setFont (ui (12.0f));
    g.drawText (b.getButtonText(), r.withTrimmedLeft ((int) box + 7).toNearestInt(), juce::Justification::centredLeft);
}

void Theme::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto r = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    auto c = juce::Rectangle<float> (size, size).withCentre (r.getCentre());
    const float angle = start + pos * (end - start);
    const float lw = juce::jmax (2.0f, size * 0.08f);
    juce::Path bgArc; bgArc.addCentredArc (c.getCentreX(), c.getCentreY(), size / 2 - lw, size / 2 - lw, 0, start, end, true);
    g.setColour (knob);
    g.strokePath (bgArc, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    juce::Path fg;
    // bipolar knobs (pan) fill from centre
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    const float zero = bipolar ? start + (float) ((0.0 - s.getMinimum()) / (s.getMaximum() - s.getMinimum())) * (end - start) : start;
    fg.addCentredArc (c.getCentreX(), c.getCentreY(), size / 2 - lw, size / 2 - lw, 0, juce::jmin (zero, angle), juce::jmax (zero, angle), true);
    g.setColour (s.isEnabled() ? accent : textDim);
    g.strokePath (fg, juce::PathStrokeType (lw, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (headerBg);
    g.fillEllipse (c.reduced (lw * 2.2f));
    juce::Point<float> p1 = c.getCentre().getPointOnCircumference (size * 0.18f, angle);
    juce::Point<float> p2 = c.getCentre().getPointOnCircumference (size / 2 - lw * 2.4f, angle);
    g.setColour (text);
    g.drawLine ({ p1, p2 }, 2.0f);
}

void Theme::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style, juce::Slider& s)
{
    const bool vertical = style == juce::Slider::LinearVertical || style == juce::Slider::LinearBarVertical;
    if (vertical)
    {
        auto track = juce::Rectangle<float> ((float) x + w * 0.5f - 3.0f, (float) y, 6.0f, (float) h);
        g.setColour (windowBg); g.fillRoundedRectangle (track, 3.0f);
        g.setColour (accentDim.withAlpha (0.7f)); g.fillRoundedRectangle (track.withTop (pos), 3.0f);
        g.setColour (separator); g.drawRoundedRectangle (track, 3.0f, 1.0f);
        const float tw = juce::jmin ((float) w - 4.0f, 26.0f);
        auto thumb = juce::Rectangle<float> ((float) x + w * 0.5f - tw * 0.5f, pos - 7.0f, tw, 14.0f);
        g.setColour (s.isEnabled() ? headerBg.brighter (0.5f) : headerBg); g.fillRoundedRectangle (thumb, 3.0f);
        g.setColour (border); g.drawRoundedRectangle (thumb, 3.0f, 1.0f);
        g.setColour (accent); g.fillRect (thumb.reduced (4.0f, 0.0f).withHeight (2.0f).withY (thumb.getCentreY() - 1.0f));
    }
    else
    {
        auto track = juce::Rectangle<float> ((float) x, (float) y + h * 0.5f - 2.0f, (float) w, 4.0f);
        g.setColour (windowBg); g.fillRoundedRectangle (track, 2.0f);
        g.setColour (accentDim); g.fillRoundedRectangle (track.withWidth (pos - (float) x), 2.0f);
        auto thumb = juce::Rectangle<float> (pos - 5.0f, (float) y + 2.0f, 10.0f, (float) h - 4.0f);
        g.setColour (headerBg.brighter (0.4f)); g.fillRoundedRectangle (thumb, 2.0f);
    }
}

void Theme::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);
    juce::Path p;
    const float ax = (float) w - 12.0f, ay = h * 0.5f;
    p.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
    g.setColour (textDim);
    g.fillPath (p);
}

int Theme::getTabButtonBestWidth (juce::TabBarButton& b, int) { return juce::jmax (70, (int) ui (12.0f).getStringWidthFloat (b.getButtonText()) + 24); }

void Theme::drawTabButton (juce::TabBarButton& b, juce::Graphics& g, bool over, bool)
{
    auto r = b.getLocalBounds();
    const bool front = b.isFrontTab();
    g.setColour (front ? panelBg : (over ? headerBg.brighter (0.05f) : headerBg));
    g.fillRect (r);
    if (front) { g.setColour (accent); g.fillRect (r.removeFromTop (2)); }
    g.setColour (border);
    g.drawVerticalLine (r.getRight() - 1, (float) r.getY(), (float) r.getBottom());
    g.setColour (front ? text : textDim);
    g.setFont (ui (12.0f, front));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

void Theme::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int thumbStart, int thumbSize, bool over, bool)
{
    g.setColour (windowBg);
    g.fillRect (x, y, w, h);
    juce::Rectangle<int> thumb = vertical ? juce::Rectangle<int> (x + 2, thumbStart, w - 4, thumbSize)
                                          : juce::Rectangle<int> (thumbStart, y + 2, thumbSize, h - 4);
    g.setColour (over ? separator.brighter (0.3f) : separator);
    g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
}

void Theme::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (panelBgAlt);
    g.setColour (separator);
    g.drawRect (0, 0, w, h);
}

void drawPanelHeader (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title)
{
    g.setColour (headerBg);
    g.fillRect (area);
    g.setColour (border);
    g.drawHorizontalLine (area.getBottom() - 1, (float) area.getX(), (float) area.getRight());
    g.setColour (textDim);
    g.setFont (Theme::ui (11.0f, true));
    g.drawText (title.toUpperCase(), area.reduced (8, 0), juce::Justification::centredLeft);
}

} // namespace mashup::ui
