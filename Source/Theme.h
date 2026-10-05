// Palette ripresa dai token CSS della versione web (:root e tema scuro).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Model.h"

struct Theme
{
    juce::Colour bg, panel, ink, muted, line, roll, rollAlt, off, grid, bar, heat, accent, play;
    juce::Colour track[orecchio::kNumTracks];
    bool dark = true;

    static Theme makeDark()
    {
        Theme t;
        t.dark = true;
        t.bg = juce::Colour (0xff141925);  t.panel = juce::Colour (0xff1c2230);
        t.ink = juce::Colour (0xffece8df); t.muted = juce::Colour (0xff9aa1b2);
        t.line = juce::Colour (0xff2d3445); t.roll = juce::Colour (0xff1a202d);
        t.rollAlt = juce::Colour (0xff161b27); t.off = juce::Colour (0xff121621);
        t.grid = juce::Colour (0xff262d3c); t.bar = juce::Colour (0xff3f4859);
        t.heat = juce::Colour (255, 212, 138); t.accent = juce::Colour (0xffffd48a);
        t.play = juce::Colour (0xffff8d68);
        t.track[orecchio::kBass] = juce::Colour (0xff52c7b9);
        t.track[orecchio::kChords] = juce::Colour (0xffb597ea);
        t.track[orecchio::kMelody] = juce::Colour (0xffff8d68);
        return t;
    }

    static Theme makeLight()
    {
        Theme t;
        t.dark = false;
        t.bg = juce::Colour (0xffe9ecf1);  t.panel = juce::Colour (0xfffbfbfc);
        t.ink = juce::Colour (0xff1b2232); t.muted = juce::Colour (0xff5f6779);
        t.line = juce::Colour (0xffd3d8e1); t.roll = juce::Colour (0xfff6f7f9);
        t.rollAlt = juce::Colour (0xffeceff4); t.off = juce::Colour (0xffdfe3ea);
        t.grid = juce::Colour (0xffcfd5df); t.bar = juce::Colour (0xff9aa3b4);
        t.heat = juce::Colour (38, 52, 98); t.accent = juce::Colour (0xff2b3a67);
        t.play = juce::Colour (0xffd9572f);
        t.track[orecchio::kBass] = juce::Colour (0xff1a9488);
        t.track[orecchio::kChords] = juce::Colour (0xff7f57c4);
        t.track[orecchio::kMelody] = juce::Colour (0xffd9572f);
        return t;
    }
};

class OrecchioLook : public juce::LookAndFeel_V4
{
public:
    void setTheme (const Theme& t)
    {
        theme = t;
        using namespace juce;
        setColour (ResizableWindow::backgroundColourId, t.bg);
        setColour (TextButton::buttonColourId, t.panel);
        setColour (TextButton::buttonOnColourId, t.accent);
        setColour (TextButton::textColourOffId, t.ink);
        setColour (TextButton::textColourOnId, t.dark ? t.bg : t.panel);
        setColour (ComboBox::backgroundColourId, t.panel);
        setColour (ComboBox::textColourId, t.ink);
        setColour (ComboBox::outlineColourId, t.line);
        setColour (ComboBox::arrowColourId, t.muted);
        setColour (PopupMenu::backgroundColourId, t.panel);
        setColour (PopupMenu::textColourId, t.ink);
        setColour (PopupMenu::highlightedBackgroundColourId, t.accent);
        setColour (PopupMenu::highlightedTextColourId, t.dark ? t.bg : t.panel);
        setColour (Label::textColourId, t.ink);
        setColour (Slider::backgroundColourId, t.panel);
        setColour (Slider::trackColourId, t.accent.withAlpha (0.35f));
        setColour (Slider::thumbColourId, t.accent);
        setColour (Slider::textBoxTextColourId, t.ink);
        setColour (Slider::textBoxBackgroundColourId, t.panel);
        setColour (Slider::textBoxOutlineColourId, t.line);
        setColour (TextEditor::backgroundColourId, t.panel);
        setColour (TextEditor::textColourId, t.ink);
        setColour (TextEditor::highlightColourId, t.accent.withAlpha (0.3f));
        setColour (CaretComponent::caretColourId, t.ink);
        setColour (ScrollBar::thumbColourId, t.bar);
        setColour (ToggleButton::textColourId, t.ink);
        setColour (ToggleButton::tickColourId, t.accent);
        setColour (ToggleButton::tickDisabledColourId, t.muted);
        setColour (AlertWindow::backgroundColourId, t.panel);
        setColour (AlertWindow::textColourId, t.ink);
        setColour (FileChooserDialogBox::titleTextColourId, t.ink);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto c = backgroundColour;
        if (b.getToggleState()) c = b.findColour (juce::TextButton::buttonOnColourId);
        if (down) c = c.contrasting (0.15f);
        else if (highlighted) c = c.contrasting (0.06f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme.line);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) height * 0.55f)));
    }

    const Theme& getTheme() const { return theme; }

private:
    Theme theme = Theme::makeDark();
};
