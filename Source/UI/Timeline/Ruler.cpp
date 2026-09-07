#include "Ruler.h"
#include "UI/Theme/Theme.h"
#include "Timeline/TempoMap.h"
#include "AudioEngine/AudioEngine.h"

namespace mashup::ui
{
Ruler::Ruler (Session& s, TimelineViewState& v) : session (s), view (v) { view.addChangeListener (this); }
Ruler::~Ruler() { view.removeChangeListener (this); }

void Ruler::paint (juce::Graphics& g)
{
    g.fillAll (colours::headerBg);
    auto& p = session.getProject();
    TempoMap tm = TempoMap::fromProject (p.getRoot());
    const double bpb = tm.beatsPerBar();
    const int w = getWidth(), h = getHeight();

    // loop region
    if (p.getLoopEnd() > p.getLoopStart())
    {
        const int x0 = (int) view.beatToX (p.getLoopStart()), x1 = (int) view.beatToX (p.getLoopEnd());
        g.setColour (p.isLoopEnabled() ? colours::accent.withAlpha (0.35f) : colours::textDim.withAlpha (0.25f));
        g.fillRect (x0, 0, juce::jmax (1, x1 - x0), 8);
        g.setColour (p.isLoopEnabled() ? colours::accent : colours::textDim);
        g.fillRect (x0, 0, 2, 8); g.fillRect (x1 - 2, 0, 2, 8);
    }
    // time selection
    if (view.hasTimeSelection)
    {
        const int x0 = (int) view.beatToX (view.timeSelStart), x1 = (int) view.beatToX (view.timeSelEnd);
        g.setColour (colours::selection); g.fillRect (x0, 8, juce::jmax (1, x1 - x0), h - 8);
    }

    // bar lines: choose a bar step so that labels don't collide
    const double pixelsPerBar = bpb * view.pixelsPerBeat;
    int barStep = 1; while (pixelsPerBar * barStep < 50) barStep *= 2;
    const double firstBar = std::floor (view.scrollBeat / bpb);
    g.setFont (Theme::mono (11.0f));
    for (double bar = firstBar; ; bar += 1.0)
    {
        const double beat = bar * bpb;
        const int x = (int) view.beatToX (beat);
        if (x > w) break;
        const bool labelled = ((int) bar % barStep) == 0;
        g.setColour (labelled ? colours::text : colours::textDim);
        g.drawVerticalLine (x, labelled ? (float) (h - 14) : (float) (h - 8), (float) h);
        if (labelled) g.drawText (juce::String ((int) bar + 1), x + 3, h - 16, 60, 14, juce::Justification::centredLeft);
        // beat ticks when zoomed in
        if (view.pixelsPerBeat >= 12.0)
        {
            g.setColour (colours::textDim.withAlpha (0.6f));
            for (int b = 1; b < (int) bpb; ++b) g.drawVerticalLine ((int) view.beatToX (beat + b), (float) (h - 5), (float) h);
        }
    }
    // markers
    g.setFont (Theme::ui (10.0f, true));
    for (const auto& m : p.markers())
    {
        const int x = (int) view.beatToX ((double) m[ids::beat]);
        if (x < -100 || x > w) continue;
        auto c = juce::Colour::fromString (m.getProperty (ids::color, "ffe0a72f").toString());
        g.setColour (c);
        juce::Path flag; flag.addTriangle ((float) x, 8.0f, (float) x + 8.0f, 12.0f, (float) x, 16.0f);
        g.fillPath (flag);
        g.drawText (m[ids::name].toString(), x + 10, 6, 120, 12, juce::Justification::centredLeft);
    }
    // playhead
    const int px = (int) view.beatToX (session.getAudioEngine().getPositionBeat());
    g.setColour (colours::playhead);
    juce::Path tri; tri.addTriangle ((float) px - 6, 8.0f, (float) px + 6, 8.0f, (float) px, 16.0f);
    g.fillPath (tri);
    g.setColour (colours::border);
    g.drawHorizontalLine (h - 1, 0.0f, (float) w);
}

void Ruler::mouseDown (const juce::MouseEvent& e)
{
    auto& p = session.getProject();
    TempoMap tm = TempoMap::fromProject (p.getRoot());
    const double beat = view.xToBeat (e.x);
    if (e.mods.isPopupMenu()) { showMenu (e); return; }
    dragStartBeat = beat;
    // marker hit?
    for (auto m : p.markers())
        if (std::abs (view.beatToX ((double) m[ids::beat]) - e.x) < 6 && e.y < 18) { drag = Drag::Marker; draggedMarker = m; session.getUndoManager().beginNewTransaction ("Move marker"); return; }
    if (e.y < 10 && p.getLoopEnd() > p.getLoopStart())
    {
        const double lx0 = view.beatToX (p.getLoopStart()), lx1 = view.beatToX (p.getLoopEnd());
        if (std::abs (e.x - lx0) < 6) { drag = Drag::LoopStart; return; }
        if (std::abs (e.x - lx1) < 6) { drag = Drag::LoopEnd; return; }
        if (e.x > lx0 && e.x < lx1) { drag = Drag::LoopMove; loopMoveOffset = beat - p.getLoopStart(); return; }
    }
    drag = Drag::Selection;
    session.getAudioEngine().locateBeat (view.snap (beat, tm.beatsPerBar()));
}

void Ruler::mouseDrag (const juce::MouseEvent& e)
{
    auto& p = session.getProject();
    TempoMap tm = TempoMap::fromProject (p.getRoot());
    const double bpb = tm.beatsPerBar();
    const double beat = view.snap (view.xToBeat (e.x), bpb);
    switch (drag)
    {
        case Drag::Selection:
            if (std::abs (e.getDistanceFromDragStartX()) > 3)
            {
                view.setTimeSelection (view.snap (dragStartBeat, bpb), beat);
                p.setLoop (view.timeSelStart, view.timeSelEnd, p.isLoopEnabled());
            }
            break;
        case Drag::LoopStart: p.setLoop (juce::jmin (beat, p.getLoopEnd() - 0.25), p.getLoopEnd(), p.isLoopEnabled()); break;
        case Drag::LoopEnd:   p.setLoop (p.getLoopStart(), juce::jmax (beat, p.getLoopStart() + 0.25), p.isLoopEnabled()); break;
        case Drag::LoopMove:  { const double len = p.getLoopEnd() - p.getLoopStart(); const double s = juce::jmax (0.0, view.snap (view.xToBeat (e.x) - loopMoveOffset, bpb)); p.setLoop (s, s + len, p.isLoopEnabled()); break; }
        case Drag::Marker:    if (draggedMarker.isValid()) draggedMarker.setProperty (ids::beat, beat, &session.getUndoManager()); break;
        default: break;
    }
    repaint();
}

void Ruler::mouseUp (const juce::MouseEvent&) { drag = Drag::None; draggedMarker = {}; }

void Ruler::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto& p = session.getProject();
    if (e.y < 10) { p.setLoop (p.getLoopStart(), p.getLoopEnd(), ! p.isLoopEnabled()); repaint(); }
}

void Ruler::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) view.setZoom (view.pixelsPerBeat * std::pow (1.15, w.deltaY * 10.0), e.x);
    else view.setScrollBeat (view.scrollBeat - (w.deltaY + w.deltaX) * 40.0 / view.pixelsPerBeat * 4.0);
}

void Ruler::showMenu (const juce::MouseEvent& e)
{
    auto& p = session.getProject();
    TempoMap tm = TempoMap::fromProject (p.getRoot());
    const double beat = view.snap (view.xToBeat (e.x), tm.beatsPerBar());
    juce::PopupMenu m;
    m.addItem (1, "Add marker here");
    m.addItem (2, "Set loop to selection", view.hasTimeSelection);
    m.addItem (3, "Add tempo change here...");
    juce::ValueTree hitMarker;
    for (auto mk : p.markers()) if (std::abs (view.beatToX ((double) mk[ids::beat]) - e.x) < 8) hitMarker = mk;
    if (hitMarker.isValid()) { m.addSeparator(); m.addItem (10, "Rename marker..."); m.addItem (11, "Delete marker"); }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }), [this, beat, hitMarker] (int r)
    {
        auto& proj = session.getProject(); auto* um = &session.getUndoManager();
        if (r == 1)
        {
            um->beginNewTransaction ("Add marker");
            juce::ValueTree mk (ids::MARKER);
            mk.setProperty (ids::id, ProjectModel::newId(), nullptr); mk.setProperty (ids::beat, beat, nullptr);
            mk.setProperty (ids::name, "Marker " + juce::String (proj.markers().getNumChildren() + 1), nullptr);
            proj.markers().appendChild (mk, um);
        }
        else if (r == 2) proj.setLoop (view.timeSelStart, view.timeSelEnd, true);
        else if (r == 3)
        {
            auto* w = new juce::AlertWindow ("Tempo change", "BPM from beat " + juce::String (beat, 2) + ":", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("bpm", juce::String (proj.getBpm(), 2));
            w->addButton ("OK", 1); w->addButton ("Cancel", 0);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, beat] (int res)
            {
                if (res == 1)
                {
                    const double bpm = w->getTextEditorContents ("bpm").getDoubleValue();
                    if (bpm >= 20 && bpm <= 400)
                    {
                        auto& pr = session.getProject(); auto* u = &session.getUndoManager(); u->beginNewTransaction ("Add tempo change");
                        juce::ValueTree t (ids::TEMPO); t.setProperty (ids::beat, beat, nullptr); t.setProperty (ids::bpm, bpm, nullptr);
                        pr.tempoMap().appendChild (t, u);
                    }
                }
            }), true);
        }
        else if (r == 10 && hitMarker.isValid())
        {
            auto* w = new juce::AlertWindow ("Rename marker", "", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", hitMarker[ids::name].toString());
            w->addButton ("OK", 1); w->addButton ("Cancel", 0);
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, hitMarker] (int res) mutable
            {
                if (res == 1) { session.getUndoManager().beginNewTransaction ("Rename marker"); hitMarker.setProperty (ids::name, w->getTextEditorContents ("name"), &session.getUndoManager()); }
            }), true);
        }
        else if (r == 11 && hitMarker.isValid()) { um->beginNewTransaction ("Delete marker"); proj.markers().removeChild (hitMarker, um); }
        repaint();
    });
}
} // namespace mashup::ui
