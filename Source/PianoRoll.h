#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "Theme.h"

// Piano roll: tastiera a sinistra, righello (battute, accordi, loop) in alto, mappa delle note
// come sfondo e le tre tracce disegnabili sopra. Un solo componente che disegna solo la parte
// visibile, come il canvas sticky della versione web.
class PianoRoll : public juce::Component,
                  private juce::ScrollBar::Listener
{
public:
    explicit PianoRoll (OrecchioProcessor&);
    ~PianoRoll() override;

    void setTheme (const Theme&);
    void rebuildHeatmap();   // nuova analisi, contrasto o riduzione armonici cambiati
    void contentChanged();   // note, tempo o loop cambiati
    void updatePlayhead();   // chiamato dal timer dell'editor

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr double kGridValues[5] = { 0.25, 0.5, 1.0, 2.0, 4.0 };

private:
    enum class Mode { None, Move, Resize, NewNote, Ruler };
    static constexpr int kKeyW = 58, kRulerH = 36, kBar = 12;

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void updateScrollbars();
    void paintRuler (juce::Graphics&);
    void paintKeyboard (juce::Graphics&);
    void paintEmptyState (juce::Graphics&, juce::Rectangle<int>);

    juce::Rectangle<int> gridArea() const;
    float  beatToX (double beat) const  { return (float) (kKeyW + beat * ppb - scrollX); }
    double xToBeat (float x) const       { return (x - kKeyW + scrollX) / ppb; }
    float  midiToY (int midi) const      { return (float) (kRulerH + (dsp::kMidiHi - midi) * rowH - scrollY); }
    int    yToMidi (float y) const       { return dsp::kMidiHi - (int) std::floor ((y - kRulerH + scrollY) / rowH); }
    juce::Rectangle<float> noteRect (const orecchio::Note&) const;
    int noteAt (juce::Point<float>) const;

    int activeTrack() const;
    double gridBeats() const;
    double noteLenBeats() const;
    bool scaleLock() const;
    dsp::Key currentKey() const;

    OrecchioProcessor& proc;
    Theme theme = Theme::makeDark();
    juce::ScrollBar hbar { false }, vbar { true };
    juce::Image heat;

    double ppb = 48.0;     // pixel per battito
    int rowH = 14;
    double scrollX = 0.0, scrollY = 0.0;
    bool initialScrollDone = false;
    double lastPlayhead = -1.0;

    Mode mode = Mode::None;
    std::vector<orecchio::Note> before, work;
    std::vector<int> newIndices;
    orecchio::Note orig;
    int dragIndex = -1, downMidi = 0;
    double downBeat = 0.0, newStart = 0.0, rulerDownBeat = 0.0;
    bool moved = false, loopDragged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRoll)
};
