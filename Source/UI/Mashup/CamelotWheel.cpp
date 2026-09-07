#include "CamelotWheel.h"
#include "UI/Theme/Theme.h"
#include "Core/MusicalKey.h"

namespace mashup::ui
{
juce::Rectangle<float> CamelotWheel::wheelBounds() const
{
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    const float s = juce::jmin (r.getWidth(), r.getHeight());
    return juce::Rectangle<float> (s, s).withCentre (r.getCentre());
}

static void keyForCamelotNumber (int number, int mode, int& root)
{
    for (int r = 0; r < 12; ++r) if (key::camelotNumber (r, mode) == number) { root = r; return; }
    root = 0;
}

void CamelotWheel::paint (juce::Graphics& g)
{
    auto b = wheelBounds();
    const float R = b.getWidth() * 0.5f, rMid = R * 0.66f, rIn = R * 0.32f;
    const auto c = b.getCentre();
    g.setFont (Theme::ui (juce::jmax (9.0f, R * 0.11f), true));
    for (int n = 1; n <= 12; ++n)
    {
        // Camelot number n sits at clock position n (12 at top)
        const float a0 = juce::MathConstants<float>::twoPi * (n - 1 - 0.5f) / 12.0f, a1 = a0 + juce::MathConstants<float>::twoPi / 12.0f;
        for (int mode = 0; mode < 2; ++mode)   // 0 = major (outer), 1 = minor (inner)
        {
            int root; keyForCamelotNumber (n, mode, root);
            const float ro = mode == 0 ? R : rMid, ri = mode == 0 ? rMid : rIn;
            juce::Path seg;
            seg.addCentredArc (c.x, c.y, ro, ro, 0, a0, a1, true);
            seg.addCentredArc (c.x, c.y, ri, ri, 0, a1, a0, false);
            seg.closeSubPath();
            const int compat = projRoot >= 0 ? key::compatibility (projRoot, projMode, root, mode) : 3;
            juce::Colour fill = juce::Colour::fromHSV ((n - 1) / 12.0f, 0.55f, mode == 0 ? 0.55f : 0.40f, 1.0f);
            if (projRoot >= 0)
            {
                if (compat == 0) fill = colours::accent;
                else if (compat == 1) fill = fill.brighter (0.25f);
                else if (compat == 2) fill = fill.withSaturation (0.35f);
                else fill = fill.withSaturation (0.12f).withBrightness (0.28f);
            }
            if (hoverRoot == root && hoverMode == mode) fill = fill.brighter (0.3f);
            g.setColour (fill); g.fillPath (seg);
            g.setColour (colours::border); g.strokePath (seg, juce::PathStrokeType (1.0f));
            const float am = (a0 + a1) * 0.5f, rm = (ro + ri) * 0.5f;
            const auto pos = c.getPointOnCircumference (rm, am);
            g.setColour (compat == 3 && projRoot >= 0 ? colours::textDim : colours::text);
            g.drawText (juce::String (n) + (mode == 0 ? "B" : "A") + "\n" + key::name (root, mode), juce::Rectangle<float> (46, 26).withCentre (pos), juce::Justification::centred);
            for (const auto& m : marks)
                if (m.root == root && m.mode == mode)
                {
                    g.setColour (m.colour);
                    g.drawEllipse (juce::Rectangle<float> (rm * 0.0f + (ro - ri) * 0.9f, (ro - ri) * 0.9f).withCentre (pos), 2.5f);
                }
        }
    }
    g.setColour (colours::panelBg); g.fillEllipse (juce::Rectangle<float> (rIn * 2, rIn * 2).withCentre (c));
    g.setColour (colours::text); g.setFont (Theme::ui (juce::jmax (10.0f, R * 0.13f), true));
    g.drawText (projRoot >= 0 ? key::name (projRoot, projMode) + "\n" + key::camelot (projRoot, projMode) : "-", juce::Rectangle<float> (rIn * 1.8f, rIn * 1.4f).withCentre (c), juce::Justification::centred);
}

bool CamelotWheel::hitTest (juce::Point<float> p, int& root, int& mode) const
{
    auto b = wheelBounds(); const float R = b.getWidth() * 0.5f; const auto c = b.getCentre();
    const float d = p.getDistanceFrom (c);
    if (d < R * 0.32f || d > R) return false;
    mode = d > R * 0.66f ? 0 : 1;
    float ang = std::atan2 (p.x - c.x, c.y - p.y);   // 0 at top, clockwise
    if (ang < 0) ang += juce::MathConstants<float>::twoPi;
    const int n = ((int) std::floor (ang / juce::MathConstants<float>::twoPi * 12.0f + 0.5f)) % 12 + 1;
    keyForCamelotNumber (n, mode, root);
    return true;
}

void CamelotWheel::mouseDown (const juce::MouseEvent& e) { int r, m; if (hitTest (e.position, r, m) && onKeyClicked) onKeyClicked (r, m); }
void CamelotWheel::mouseMove (const juce::MouseEvent& e) { int r, m; if (hitTest (e.position, r, m)) { hoverRoot = r; hoverMode = m; } else hoverRoot = -1; repaint(); }
} // namespace mashup::ui
