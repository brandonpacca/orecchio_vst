#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PianoRoll.h"
#include "PluginProcessor.h"
#include "Theme.h"

// Riquadro da cui trascinare il MIDI direttamente su una traccia della DAW.
class MidiDragChip : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    std::function<juce::File()> makeFile;
    Theme theme = Theme::makeDark();

    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

private:
    bool hover = false, dragging = false;
};

class OrecchioEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    explicit OrecchioEditor (OrecchioProcessor&);
    ~OrecchioEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void syncFromProcessor();
    void applyTheme();
    void selectTrack (int track);
    void chooseSample();
    void exportMidi();
    void tap();

    OrecchioProcessor& proc;
    OrecchioLook look;
    Theme theme = Theme::makeDark();
    juce::TooltipWindow tooltips { this, 700 };

    PianoRoll roll;
    juce::Label title, sampleLabel, bpmLabel, offLabel, keyLabel, statusLabel, hintLabel;
    juce::TextButton loadBtn, demoBtn, tapBtn, detectBtn, playBtn, loopBtn, themeBtn;
    juce::Slider bpmSlider, offSlider, contrastSlider;
    juce::TextButton trackBtn[orecchio::kNumTracks];
    juce::TextButton scaleBtn, sugBassBtn, sugChordsBtn, clearBtn, undoBtn, cleanBtn, midiOutBtn, exportBtn;
    juce::ComboBox gridBox, lenBox;
    juce::Slider volSlider[4];
    MidiDragChip dragChip;

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volAtt[4];
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAtt, midiOutAtt;
    std::unique_ptr<juce::FileChooser> chooser;

    std::vector<double> taps;
    const void* lastAnalysis = nullptr;
    bool dragOver = false;
    juce::String lastStatusShown;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OrecchioEditor)
};
