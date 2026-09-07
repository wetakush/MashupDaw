#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace mashup::ui
{
/** Interactive Camelot wheel: outer ring = major (B), inner ring = minor (A). Highlights the project key,
    compatible keys, and any number of marked keys (e.g. selected clips). Click selects a key. */
class CamelotWheel : public juce::Component
{
public:
    struct Mark { int root, mode; juce::Colour colour; juce::String label; };

    void setProjectKey (int root, int mode) { projRoot = root; projMode = mode; repaint(); }
    void setMarks (std::vector<Mark> m) { marks = std::move (m); repaint(); }
    std::function<void (int root, int mode)> onKeyClicked;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverRoot = -1; repaint(); }

private:
    bool hitTest (juce::Point<float> p, int& root, int& mode) const;
    juce::Rectangle<float> wheelBounds() const;
    int projRoot = -1, projMode = 0, hoverRoot = -1, hoverMode = 0;
    std::vector<Mark> marks;
};
}
