#include "PianoRoll.h"
#include "Text.h"

using namespace orecchio;

namespace
{
bool isBlackKey (int midi)
{
    const int p = midi % 12;
    return p == 1 || p == 3 || p == 6 || p == 8 || p == 10;
}

juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}
} // namespace

PianoRoll::PianoRoll (OrecchioProcessor& p) : proc (p)
{
    addAndMakeVisible (hbar);
    addAndMakeVisible (vbar);
    hbar.addListener (this);
    vbar.addListener (this);
    hbar.setAutoHide (false);
    vbar.setAutoHide (false);
    ppb = (double) proc.ui.getProperty ("ppb", 48.0);
    rowH = (int) proc.ui.getProperty ("rowH", 14);
    setWantsKeyboardFocus (true); // i tasti non gestiti risalgono all'editor
    rebuildHeatmap();
}

PianoRoll::~PianoRoll()
{
    hbar.removeListener (this);
    vbar.removeListener (this);
}

// ---------------------------------------------------------------------------------------------
int PianoRoll::activeTrack() const { return juce::jlimit (0, kNumTracks - 1, (int) proc.ui.getProperty ("track", kMelody)); }
double PianoRoll::gridBeats() const { return kGridValues[juce::jlimit (0, 4, (int) proc.ui.getProperty ("grid", 1))]; }
double PianoRoll::noteLenBeats() const { return kGridValues[juce::jlimit (0, 4, (int) proc.ui.getProperty ("len", 2))]; }
bool PianoRoll::scaleLock() const { return (bool) proc.ui.getProperty ("scaleLock", false); }
dsp::Key PianoRoll::currentKey() const { return proc.getAnalysis().valid ? proc.getAnalysis().key : dsp::Key {}; }

juce::Rectangle<int> PianoRoll::gridArea() const
{
    return { kKeyW, kRulerH, juce::jmax (0, getWidth() - kKeyW - kBar), juce::jmax (0, getHeight() - kRulerH - kBar) };
}

juce::Rectangle<float> PianoRoll::noteRect (const Note& n) const
{
    const float x0 = beatToX (n.start), x1 = beatToX (n.end());
    return { x0, midiToY (n.midi), juce::jmax (3.0f, x1 - x0), (float) rowH };
}

int PianoRoll::noteAt (juce::Point<float> p) const
{
    const auto& ns = proc.getNotes();
    const int t = activeTrack();
    for (int i = (int) ns.size() - 1; i >= 0; --i)
        if (ns[(size_t) i].track == t && noteRect (ns[(size_t) i]).contains (p))
            return i;
    return -1;
}

// ---------------------------------------------------------------------------------------------
void PianoRoll::setTheme (const Theme& t)
{
    theme = t;
    rebuildHeatmap();
}

void PianoRoll::rebuildHeatmap()
{
    const auto& a = proc.getAnalysis();
    if (! a.valid || a.raw.frames == 0)
    {
        heat = {};
        repaint();
        return;
    }

    const auto& s = (bool) proc.ui.getProperty ("clean", true) ? a.clean : a.raw;

    // riferimento al 99,5° percentile, così un picco isolato non spegne tutto il resto
    std::vector<float> vals;
    vals.reserve (s.v.size());
    for (float v : s.v)
        if (v > 0) vals.push_back (v);
    float ref = 1.0f;
    if (! vals.empty())
    {
        const size_t k = juce::jmin (vals.size() - 1, (size_t) ((double) vals.size() * 0.995));
        std::nth_element (vals.begin(), vals.begin() + (std::ptrdiff_t) k, vals.end());
        ref = juce::jmax (1e-9f, vals[k]);
    }

    const float contrast = (float) (double) proc.ui.getProperty ("contrast", 0.5);
    const float gamma = 0.6f + contrast * 2.4f;

    heat = juce::Image (juce::Image::ARGB, s.frames, dsp::kNumNotes, true);
    juce::Image::BitmapData bd (heat, juce::Image::BitmapData::writeOnly);
    for (int f = 0; f < s.frames; ++f)
        for (int n = 0; n < dsp::kNumNotes; ++n)
        {
            const float v = std::pow (juce::jlimit (0.0f, 1.0f, s.get (f, n) / ref), gamma);
            bd.setPixelColour (f, dsp::kNumNotes - 1 - n, theme.heat.withAlpha (v * 0.92f));
        }
    repaint();
}

void PianoRoll::contentChanged()
{
    updateScrollbars();
    repaint();
}

void PianoRoll::updatePlayhead()
{
    const double b = proc.getPlayheadBeat();
    if (std::abs (b - lastPlayhead) < 1e-6) return;

    const auto ga = gridArea();
    const float x = beatToX (b);
    if (proc.isPlaying() && mode == Mode::None && (x > ga.getRight() - 24 || x < ga.getX()))
    {
        scrollX = juce::jmax (0.0, b * ppb - ga.getWidth() * 0.1);
        updateScrollbars();
        repaint();
    }
    else
    {
        repaint ((int) beatToX (lastPlayhead) - 6, 0, 12, getHeight());
        repaint ((int) x - 6, 0, 12, getHeight());
    }
    lastPlayhead = b;
}

void PianoRoll::resized()
{
    hbar.setBounds (kKeyW, getHeight() - kBar, juce::jmax (0, getWidth() - kKeyW - kBar), kBar);
    vbar.setBounds (getWidth() - kBar, kRulerH, kBar, juce::jmax (0, getHeight() - kRulerH - kBar));
    if (! initialScrollDone && getHeight() > 0)
    {
        scrollY = (dsp::kMidiHi - 79) * rowH; // in alto il Sol5
        initialScrollDone = true;
    }
    updateScrollbars();
}

void PianoRoll::updateScrollbars()
{
    const auto ga = gridArea();
    const double contentW = (proc.getProjectBeats() + 2 * proc.getBeatsPerBar()) * ppb;
    const double contentH = (double) dsp::kNumNotes * rowH;
    scrollX = juce::jlimit (0.0, juce::jmax (0.0, contentW - ga.getWidth()), scrollX);
    scrollY = juce::jlimit (0.0, juce::jmax (0.0, contentH - ga.getHeight()), scrollY);
    hbar.setRangeLimits (0.0, contentW, juce::dontSendNotification);
    hbar.setCurrentRange (scrollX, ga.getWidth(), juce::dontSendNotification);
    vbar.setRangeLimits (0.0, contentH, juce::dontSendNotification);
    vbar.setCurrentRange (scrollY, ga.getHeight(), juce::dontSendNotification);
}

void PianoRoll::scrollBarMoved (juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &hbar) scrollX = newRangeStart;
    else              scrollY = newRangeStart;
    repaint();
}

// ---------------------------------------------------------------------------------------------
void PianoRoll::paint (juce::Graphics& g)
{
    const auto ga = gridArea();
    const auto& a = proc.getAnalysis();
    const auto key = currentKey();
    const double bpm = proc.getBpm(), off = proc.getOffset();
    const int bpb = proc.getBeatsPerBar();
    const int active = activeTrack();

    g.fillAll (theme.roll);

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (ga);

        // righe: le note fuori scala sono più scure
        const int topMidi = juce::jmin (dsp::kMidiHi, yToMidi ((float) ga.getY()));
        const int botMidi = juce::jmax (dsp::kMidiLo, yToMidi ((float) ga.getBottom()));
        for (int m = botMidi; m <= topMidi; ++m)
        {
            const juce::Colour c = key.valid ? (dsp::inScale (m, key) ? theme.roll : theme.off)
                                             : (isBlackKey (m) ? theme.rollAlt : theme.roll);
            g.setColour (c);
            g.fillRect ((float) ga.getX(), midiToY (m), (float) ga.getWidth(), (float) rowH);
            if (m % 12 == 0)
            {
                g.setColour (theme.grid);
                g.fillRect ((float) ga.getX(), midiToY (m) + rowH - 1.0f, (float) ga.getWidth(), 1.0f);
            }
        }

        // mappa delle note del sample
        if (heat.isValid())
        {
            const double fs = dsp::Spectrogram::frameSeconds();
            const double x0 = beatToX ((-0.5 * fs - off) * bpm / 60.0);
            const double w = heat.getWidth() * fs * bpm / 60.0 * ppb;
            g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
            g.drawImageTransformed (heat, juce::AffineTransform::scale ((float) (w / heat.getWidth()), (float) rowH)
                                              .translated ((float) x0, midiToY (dsp::kMidiHi)));
        }

        // griglia
        const double firstBeat = juce::jmax (0.0, std::floor (xToBeat ((float) ga.getX())));
        const double lastBeat = xToBeat ((float) ga.getRight()) + 1;
        const double grid = gridBeats();
        if (grid < 1.0 && grid * ppb >= 7)
        {
            g.setColour (theme.grid.withAlpha (0.45f));
            for (double b = firstBeat; b <= lastBeat; b += grid)
                if (std::abs (b - std::round (b)) > 1e-6)
                    g.fillRect (beatToX (b), (float) ga.getY(), 1.0f, (float) ga.getHeight());
        }
        for (int b = (int) firstBeat; b <= (int) lastBeat; ++b)
        {
            g.setColour (b % bpb == 0 ? theme.bar : theme.grid);
            g.fillRect (beatToX (b), (float) ga.getY(), 1.0f, (float) ga.getHeight());
        }

        // fuori dal loop si scurisce un po'
        if (proc.apvts.getRawParameterValue ("loop")->load() > 0.5f)
        {
            g.setColour (theme.off.withAlpha (0.35f));
            const float ls = beatToX (proc.getLoopStart()), le = beatToX (proc.getLoopEnd());
            if (ls > ga.getX()) g.fillRect ((float) ga.getX(), (float) ga.getY(), ls - ga.getX(), (float) ga.getHeight());
            if (le < ga.getRight()) g.fillRect (le, (float) ga.getY(), ga.getRight() - le, (float) ga.getHeight());
        }

        // note: prima le tracce non attive, sbiadite, poi quella attiva
        const auto& ns = proc.getNotes();
        for (int pass = 0; pass < 2; ++pass)
            for (const auto& n : ns)
            {
                const bool isActive = n.track == active;
                if (isActive != (pass == 1)) continue;
                auto r = noteRect (n).reduced (0.5f, 1.0f);
                if (! r.intersects (ga.toFloat())) continue;
                const auto c = theme.track[n.track];
                g.setColour (c.withAlpha (isActive ? 0.95f : 0.32f));
                g.fillRoundedRectangle (r, 2.5f);
                if (isActive)
                {
                    g.setColour (c.darker (0.6f));
                    g.drawRoundedRectangle (r, 2.5f, 1.0f);
                    if (r.getWidth() > 34 && rowH >= 12)
                    {
                        g.setColour (theme.dark ? theme.bg : juce::Colours::white);
                        g.setFont (font (10.0f));
                        g.drawText (txt (dsp::noteName (n.midi)), r.reduced (3.0f, 0.0f), juce::Justification::centredLeft, false);
                    }
                }
            }

        // cursore di riproduzione
        g.setColour (theme.play);
        g.fillRect (beatToX (proc.getPlayheadBeat()) - 1.0f, (float) ga.getY(), 2.0f, (float) ga.getHeight());

        if (! a.valid && proc.getNotes().empty())
            paintEmptyState (g, ga);
    }

    paintRuler (g);
    paintKeyboard (g);

    g.setColour (theme.panel);
    g.fillRect (0, 0, kKeyW, kRulerH);
    g.fillRect (getWidth() - kBar, getHeight() - kBar, kBar, kBar);
    g.setColour (theme.track[active]);
    g.fillRoundedRectangle (8.0f, 11.0f, 14.0f, 14.0f, 3.0f);
    g.setColour (theme.muted);
    g.setFont (font (10.0f));
    g.drawText (txt (trackName (active)), 0, 24, kKeyW, 12, juce::Justification::centred);
    g.setColour (theme.line);
    g.fillRect (0, kRulerH - 1, getWidth(), 1);
    g.fillRect (kKeyW - 1, 0, 1, getHeight());
}

void PianoRoll::paintEmptyState (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto box = area.withSizeKeepingCentre (juce::jmin (460, area.getWidth() - 40), 92);
    g.setColour (theme.panel.withAlpha (0.92f));
    g.fillRoundedRectangle (box.toFloat(), 10.0f);
    g.setColour (theme.line);
    g.drawRoundedRectangle (box.toFloat(), 10.0f, 1.0f);
    g.setColour (theme.ink);
    g.setFont (font (17.0f, true));
    g.drawText (txt ("Trascina qui un file audio"), box.removeFromTop (48).withTrimmedTop (14), juce::Justification::centred);
    g.setColour (theme.muted);
    g.setFont (font (13.0f));
    g.drawText (txt ("oppure usa «Carica sample» o «Demo». Puoi anche disegnare subito."), box.withTrimmedBottom (14),
                juce::Justification::centred);
}

void PianoRoll::paintRuler (juce::Graphics& g)
{
    const juce::Rectangle<int> rr (kKeyW, 0, getWidth() - kKeyW, kRulerH);
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (rr);
    g.setColour (theme.panel);
    g.fillRect (rr);

    const int bpb = proc.getBeatsPerBar();
    const double barPx = bpb * ppb;

    if (proc.apvts.getRawParameterValue ("loop")->load() > 0.5f)
    {
        const float ls = beatToX (proc.getLoopStart()), le = beatToX (proc.getLoopEnd());
        g.setColour (theme.accent.withAlpha (0.22f));
        g.fillRect (ls, 0.0f, le - ls, 16.0f);
        g.setColour (theme.accent);
        g.fillRect (ls, 0.0f, 2.0f, 16.0f);
        g.fillRect (le - 2.0f, 0.0f, 2.0f, 16.0f);
    }

    const int firstBar = juce::jmax (0, (int) std::floor (xToBeat ((float) kKeyW) / bpb));
    const int lastBar = (int) std::ceil (xToBeat ((float) getWidth()) / bpb);
    const int labelEvery = barPx >= 40 ? 1 : (barPx >= 20 ? 2 : 4);
    const auto& chords = proc.getChords();

    for (int bar = firstBar; bar <= lastBar; ++bar)
    {
        const float x = beatToX (bar * bpb);
        g.setColour (theme.bar);
        g.fillRect (x, 0.0f, 1.0f, (float) kRulerH);
        for (int k = 1; k < bpb && ppb >= 10; ++k)
        {
            g.setColour (theme.grid);
            g.fillRect (beatToX (bar * bpb + k), 11.0f, 1.0f, 5.0f);
        }
        if (bar % labelEvery == 0)
        {
            g.setColour (theme.muted);
            g.setFont (font (11.0f));
            g.drawText (juce::String (bar + 1), (int) x + 4, 1, 40, 14, juce::Justification::centredLeft, false);
        }
        if (bar < (int) chords.size() && chords[(size_t) bar].root >= 0 && barPx >= 26)
        {
            g.setColour (theme.ink);
            g.setFont (font (12.5f, true));
            g.drawText (txt (dsp::chordName (chords[(size_t) bar])), (int) x + 4, 17, (int) barPx - 6, 17,
                        juce::Justification::centredLeft, true);
        }
    }

    const float px = beatToX (proc.getPlayheadBeat());
    juce::Path tri;
    tri.addTriangle (px - 5.0f, 0.0f, px + 5.0f, 0.0f, px, 7.0f);
    g.setColour (theme.play);
    g.fillPath (tri);
}

void PianoRoll::paintKeyboard (juce::Graphics& g)
{
    const juce::Rectangle<int> kr (0, kRulerH, kKeyW, getHeight() - kRulerH);
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (kr);

    const auto key = currentKey();
    const juce::Colour white = theme.dark ? juce::Colour (0xffd9dce3) : juce::Colours::white;
    const juce::Colour black = theme.dark ? juce::Colour (0xff272d3b) : juce::Colour (0xff3a4255);
    g.setColour (white);
    g.fillRect (kr);

    const int topMidi = juce::jmin (dsp::kMidiHi, yToMidi ((float) kRulerH));
    const int botMidi = juce::jmax (dsp::kMidiLo, yToMidi ((float) getHeight()));
    for (int m = botMidi; m <= topMidi; ++m)
    {
        const float y = midiToY (m);
        if (isBlackKey (m))
        {
            g.setColour (black);
            g.fillRect (0.0f, y, kKeyW * 0.6f, (float) rowH);
        }
        g.setColour (juce::Colour (0x22000000));
        g.fillRect (0.0f, y + rowH - 1.0f, (float) kKeyW, 1.0f);
        if (key.valid && dsp::inScale (m, key))
        {
            g.setColour (theme.accent.withAlpha (m % 12 == key.tonic ? 0.95f : 0.45f));
            g.fillRect (kKeyW - 5.0f, y + 1.0f, 3.0f, rowH - 2.0f);
        }
        if (m % 12 == 0 && rowH >= 9)
        {
            g.setColour (juce::Colour (0xff3a4255));
            g.setFont (font (10.0f));
            g.drawText (txt (dsp::noteName (m)), 0, (int) y, kKeyW - 8, rowH, juce::Justification::centredRight, false);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Mouse: clic su vuoto = aggiungi (triade sulla traccia Accordi), trascina = allunga;
// trascina una nota = sposta, dal bordo destro = allunga; clic su una nota = cancella.
void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    const auto ga = gridArea();
    mode = Mode::None;
    moved = loopDragged = false;

    if (p.y < kRulerH && p.x >= kKeyW)
    {
        mode = Mode::Ruler;
        rulerDownBeat = juce::jmax (0.0, xToBeat (p.x));
        return;
    }
    if (p.x < kKeyW && p.y >= kRulerH)
    {
        const int m = yToMidi (p.y);
        if (m >= dsp::kMidiLo && m <= dsp::kMidiHi) proc.previewNote (activeTrack(), m);
        return;
    }
    if (! ga.toFloat().contains (p)) return;

    before = proc.getNotes();
    work = before;
    const int hit = noteAt (p);

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            work.erase (work.begin() + hit);
            proc.setNotes (work);
            proc.commitEdit (before);
        }
        return;
    }

    if (hit >= 0)
    {
        dragIndex = hit;
        orig = work[(size_t) hit];
        downBeat = xToBeat (p.x);
        downMidi = yToMidi (p.y);
        const float x0 = beatToX (orig.start), x1 = beatToX (orig.end());
        mode = (p.x >= x1 - 6.0f && x1 - x0 > 12.0f) ? Mode::Resize : Mode::Move;
        return;
    }

    const auto key = currentKey();
    const int track = activeTrack();
    const double g = gridBeats();
    newStart = juce::jmax (0.0, std::floor (xToBeat (p.x) / g) * g);
    int midi = juce::jlimit (dsp::kMidiLo, dsp::kMidiHi, yToMidi (p.y));
    if (scaleLock()) midi = dsp::snapToScale (midi, key);

    std::vector<int> pitches { midi };
    if (track == kChords)
    {
        const auto tri = dsp::diatonicTriad (midi, key);
        pitches = { tri[0], tri[1], tri[2] };
    }

    newIndices.clear();
    for (int m : pitches)
    {
        if (m < 0 || m > 127) continue;
        work.push_back ({ track, m, newStart, noteLenBeats() });
        newIndices.push_back ((int) work.size() - 1);
        proc.previewNote (track, m);
    }
    proc.setNotes (work);
    mode = Mode::NewNote;
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position;
    switch (mode)
    {
        case Mode::Ruler:
        {
            if (e.getDistanceFromDragStart() < 4) return;
            const int bpb = proc.getBeatsPerBar();
            const double b = juce::jmax (0.0, xToBeat (p.x));
            const double s = std::floor (juce::jmin (rulerDownBeat, b) / bpb) * bpb;
            double en = std::ceil (juce::jmax (rulerDownBeat, b) / bpb) * bpb;
            if (en - s < bpb) en = s + bpb;
            proc.setLoopRange (s, en);
            loopDragged = true;
            repaint();
            return;
        }
        case Mode::Move:
        {
            if (! moved && e.getDistanceFromDragStart() < 4) return;
            moved = true;
            const double g = gridBeats();
            const double dBeat = std::round ((xToBeat (p.x) - downBeat) / g) * g;
            int m = juce::jlimit (dsp::kMidiLo, dsp::kMidiHi, orig.midi + (yToMidi (p.y) - downMidi));
            if (scaleLock()) m = dsp::snapToScale (m, currentKey());
            auto& n = work[(size_t) dragIndex];
            const int prevMidi = n.midi;
            n.start = juce::jmax (0.0, orig.start + dBeat);
            n.midi = m;
            if (m != prevMidi) proc.previewNote (n.track, m);
            proc.setNotes (work);
            return;
        }
        case Mode::Resize:
        {
            const double g = gridBeats();
            auto& n = work[(size_t) dragIndex];
            n.len = juce::jmax (g, std::round (xToBeat (p.x) / g) * g - n.start);
            proc.setNotes (work);
            return;
        }
        case Mode::NewNote:
        {
            if (e.getDistanceFromDragStart() < 4) return;
            const double g = gridBeats();
            const double len = juce::jmax (g, std::ceil (xToBeat (p.x) / g) * g - newStart);
            for (int i : newIndices) work[(size_t) i].len = len;
            proc.setNotes (work);
            return;
        }
        case Mode::None:
        default:
            return;
    }
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    if (mode == Mode::Ruler && ! loopDragged)
    {
        const double g = gridBeats();
        proc.setPlayheadBeat (std::floor (rulerDownBeat / g) * g);
    }
    if (mode == Mode::Move && ! moved && dragIndex >= 0 && dragIndex < (int) work.size())
    {
        work.erase (work.begin() + dragIndex);
        proc.setNotes (work);
    }
    if (mode == Mode::Move || mode == Mode::Resize || mode == Mode::NewNote)
        proc.commitEdit (before);

    mode = Mode::None;
    dragIndex = -1;
    repaint();
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (p.y < kRulerH || p.x < kKeyW) { setMouseCursor (juce::MouseCursor::PointingHandCursor); return; }
    const int hit = noteAt (p);
    if (hit < 0) { setMouseCursor (juce::MouseCursor::NormalCursor); return; }
    const auto r = noteRect (proc.getNotes()[(size_t) hit]);
    setMouseCursor (p.x >= r.getRight() - 6.0f && r.getWidth() > 12.0f ? juce::MouseCursor::LeftRightResizeCursor
                                                                         : juce::MouseCursor::DraggingHandCursor);
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto ga = gridArea();
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        const double anchor = xToBeat (e.position.x);
        ppb = juce::jlimit (8.0, 480.0, ppb * std::pow (1.2, (double) w.deltaY * 3.0));
        scrollX = anchor * ppb - (e.position.x - ga.getX());
        proc.ui.setProperty ("ppb", ppb, nullptr);
    }
    else if (e.mods.isAltDown())
    {
        const double anchorRow = (e.position.y - kRulerH + scrollY) / rowH;
        rowH = juce::jlimit (8, 28, rowH + (w.deltaY > 0 ? 1 : -1));
        scrollY = anchorRow * rowH - (e.position.y - kRulerH);
        proc.ui.setProperty ("rowH", rowH, nullptr);
    }
    else if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        scrollX -= (std::abs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY) * 300.0;
    }
    else
    {
        scrollY -= w.deltaY * 200.0;
    }
    updateScrollbars();
    repaint();
}
