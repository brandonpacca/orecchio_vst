#include "PluginEditor.h"
#include "Text.h"

using namespace orecchio;

namespace
{
juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

const char* kGridLabels[5] = { "1/16", "1/8", "1/4", "1/2", "1 battuta" };
} // namespace

// ---------------------------------------------------------------------------------------------
void MidiDragChip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (theme.accent.withAlpha (hover ? 0.28f : 0.14f));
    g.fillRoundedRectangle (r, 6.0f);
    juce::Path outline, border;
    outline.addRoundedRectangle (r, 6.0f);
    const float dashes[] = { 4.0f, 3.0f };
    juce::PathStrokeType (1.2f).createDashedStroke (border, outline, dashes, 2);
    g.setColour (theme.accent);
    g.fillPath (border);
    g.setColour (theme.ink);
    g.setFont (font (13.0f, true));
    g.drawText (txt ("Trascina il MIDI"), r, juce::Justification::centred);
}

void MidiDragChip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 5 || ! makeFile) return;
    dragging = true;
    const auto file = makeFile();
    if (! file.existsAsFile()) { dragging = false; return; }
    juce::Component::SafePointer<MidiDragChip> safe (this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                [safe] { if (safe != nullptr) safe->dragging = false; });
}

// ---------------------------------------------------------------------------------------------
OrecchioEditor::OrecchioEditor (OrecchioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), roll (p)
{
    setLookAndFeel (&look);

    auto setupButton = [this] (juce::TextButton& b, const char* text, const char* tip, bool toggle = false)
    {
        b.setButtonText (txt (text));
        if (tip != nullptr) b.setTooltip (txt (tip));
        b.setClickingTogglesState (toggle);
        b.setWantsKeyboardFocus (false);
        addAndMakeVisible (b);
    };
    auto setupLabel = [this] (juce::Label& l, const char* text, float size = 14.0f, bool bold = false)
    {
        l.setText (txt (text), juce::dontSendNotification);
        l.setFont (font (size, bold));
        addAndMakeVisible (l);
    };

    // --- riga 1: sample e tempo ---
    setupLabel (title, "Orecchio", 20.0f, true);
    setupButton (loadBtn, "Carica sample…", "Apri un file audio (WAV, AIFF, FLAC, MP3, OGG). Puoi anche trascinarlo sulla finestra.");
    setupButton (demoBtn, "Demo", "Genera un sample di prova a 90 BPM (Am–F–C–G)");
    setupLabel (sampleLabel, "Nessun sample", 13.0f);
    sampleLabel.setMinimumHorizontalScale (0.6f);
    setupLabel (bpmLabel, "BPM");
    bpmLabel.setJustificationType (juce::Justification::centredRight);
    setupButton (tapBtn, "Tap", "Batti a tempo per impostare il BPM");
    setupButton (detectBtn, "Rileva BPM", "Rimette il tempo e il primo battito trovati nell'analisi");
    setupLabel (offLabel, "1° battito");
    offLabel.setJustificationType (juce::Justification::centredRight);
    setupButton (playBtn, "Play", "Avvia o ferma (barra spaziatrice). Quando la DAW suona, il plugin la segue.");
    setupButton (loopBtn, "Loop", "Ripete la zona del loop: trascina sul righello per sceglierla", true);
    setupLabel (keyLabel, "Tonalità: –");
    keyLabel.setMinimumHorizontalScale (0.7f);
    setupButton (themeBtn, "Tema chiaro", nullptr);

    for (auto* s : { &bpmSlider, &offSlider })
    {
        s->setSliderStyle (juce::Slider::IncDecButtons);
        s->setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 62, 26);
        s->setWantsKeyboardFocus (false);
        addAndMakeVisible (*s);
    }
    bpmSlider.setRange (20.0, 300.0, 0.1);
    bpmSlider.setNumDecimalPlacesToDisplay (1);
    bpmSlider.onValueChange = [this] { proc.setBpm (bpmSlider.getValue()); };
    offSlider.setRange (0.0, 30.0, 0.005);
    offSlider.setNumDecimalPlacesToDisplay (3);
    offSlider.setTextValueSuffix (" s");
    offSlider.setTooltip (txt ("Secondi dall'inizio del file al primo battito della prima battuta"));
    offSlider.onValueChange = [this] { proc.setOffset (offSlider.getValue()); };

    loadBtn.onClick = [this] { chooseSample(); };
    demoBtn.onClick = [this] { proc.loadDemo(); };
    tapBtn.onClick = [this] { tap(); };
    detectBtn.onClick = [this] { proc.applyDetectedTempo(); };
    playBtn.onClick = [this] { proc.togglePlay(); };
    themeBtn.onClick = [this]
    {
        const bool light = proc.ui.getProperty ("theme", "dark").toString() == "light";
        proc.ui.setProperty ("theme", light ? "dark" : "light", nullptr);
        applyTheme();
    };

    // --- riga 2: tracce e strumenti di disegno ---
    for (int t = 0; t < kNumTracks; ++t)
    {
        setupButton (trackBtn[t], trackName (t), nullptr, true);
        trackBtn[t].setRadioGroupId (4242);
        trackBtn[t].setTooltip (txt ("Traccia su cui disegnare (tasto ") + juce::String (t + 1) + ")");
        trackBtn[t].onClick = [this, t] { if (trackBtn[t].getToggleState()) selectTrack (t); };
    }
    setupButton (scaleBtn, "Blocca scala", "Le note che disegni o sposti finiscono sempre sulla scala della tonalità", true);
    scaleBtn.onClick = [this] { proc.ui.setProperty ("scaleLock", scaleBtn.getToggleState(), nullptr); };

    for (auto* box : { &gridBox, &lenBox })
    {
        box->setWantsKeyboardFocus (false);
        addAndMakeVisible (*box);
    }
    for (int i = 0; i < 5; ++i)
    {
        gridBox.addItem (txt ("Griglia ") + txt (kGridLabels[i]), i + 1);
        lenBox.addItem (txt ("Nota ") + txt (kGridLabels[i]), i + 1);
    }
    gridBox.setTooltip (txt ("Passo della griglia per posizione e lunghezza"));
    lenBox.setTooltip (txt ("Lunghezza delle note nuove"));
    gridBox.onChange = [this] { proc.ui.setProperty ("grid", gridBox.getSelectedId() - 1, nullptr); roll.repaint(); };
    lenBox.onChange = [this] { proc.ui.setProperty ("len", lenBox.getSelectedId() - 1, nullptr); };

    setupButton (sugBassBtn, "Suggerisci basso", "Scrive una linea di basso leggendo le note gravi del sample");
    setupButton (sugChordsBtn, "Suggerisci accordi", "Scrive un accordo per battuta da quelli riconosciuti");
    setupButton (clearBtn, "Svuota traccia", "Cancella tutte le note della traccia attiva");
    setupButton (undoBtn, "Annulla", "Annulla l'ultima modifica (Ctrl/Cmd+Z)");
    sugBassBtn.onClick = [this] { proc.suggestBass(); };
    sugChordsBtn.onClick = [this] { proc.suggestChords(); };
    clearBtn.onClick = [this] { proc.clearTrack ((int) proc.ui.getProperty ("track", kMelody)); };
    undoBtn.onClick = [this] { proc.undo(); };

    // --- riga 3: vista, mixer, export ---
    contrastSlider.setSliderStyle (juce::Slider::LinearBar);
    contrastSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, true, 0, 0);
    contrastSlider.setRange (0.0, 1.0, 0.01);
    contrastSlider.textFromValueFunction = [] (double v) { return txt ("Contrasto ") + juce::String (juce::roundToInt (v * 100)) + "%"; };
    contrastSlider.setTooltip (txt ("Più alto = restano visibili solo le note più forti"));
    contrastSlider.onValueChange = [this]
    {
        proc.ui.setProperty ("contrast", contrastSlider.getValue(), nullptr);
        roll.rebuildHeatmap();
    };
    contrastSlider.setWantsKeyboardFocus (false);
    addAndMakeVisible (contrastSlider);

    setupButton (cleanBtn, "Riduci armonici", "Toglie dalla mappa le armoniche delle note più gravi", true);
    cleanBtn.onClick = [this]
    {
        proc.ui.setProperty ("clean", cleanBtn.getToggleState(), nullptr);
        roll.rebuildHeatmap();
    };

    const char* volIds[4] = { "volSample", "volBass", "volChords", "volMelody" };
    const char* volNames[4] = { "Sample", "Basso", "Accordi", "Melodia" };
    for (int i = 0; i < 4; ++i)
    {
        auto& s = volSlider[i];
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setTextBoxStyle (juce::Slider::TextBoxLeft, true, 0, 0);
        s.setWantsKeyboardFocus (false);
        addAndMakeVisible (s);
        volAtt[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, volIds[i], s);
        const juce::String name = txt (volNames[i]);
        s.textFromValueFunction = [name] (double v) { return name + " " + juce::String (juce::roundToInt (v * 100)) + "%"; };
        s.updateText();
        s.setTooltip (txt ("Volume ") + name.toLowerCase());
    }

    setupButton (midiOutBtn, "MIDI out", "Manda le note delle tracce all'uscita MIDI del plugin (canali 1, 2, 3)", true);
    setupButton (exportBtn, "Esporta MIDI…", "Salva un file con tutte le tracce più un file per traccia");
    exportBtn.onClick = [this] { exportMidi(); };
    loopAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "loop", loopBtn);
    midiOutAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "midiOut", midiOutBtn);
    loopBtn.onStateChange = [this] { roll.repaint(); };

    dragChip.makeFile = [this] { return proc.writeTempMidiForDrag(); };
    dragChip.setTooltip (txt ("Trascinalo su una traccia della DAW: arriva un file MIDI con basso, accordi e melodia"));
    addAndMakeVisible (dragChip);

    // --- piano roll e stato ---
    addAndMakeVisible (roll);
    setupLabel (statusLabel, "", 13.0f);
    setupLabel (hintLabel, "Clic: aggiungi · trascina: sposta o allunga · clic su una nota: cancella · Ctrl+rotella: zoom · Alt+rotella: altezza righe", 12.0f);
    hintLabel.setJustificationType (juce::Justification::centredRight);
    hintLabel.setMinimumHorizontalScale (0.5f);
    statusLabel.setMinimumHorizontalScale (0.7f);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (1180, 600, 3000, 2000);
    setSize ((int) proc.ui.getProperty ("w", 1320), (int) proc.ui.getProperty ("h", 780));

    applyTheme();
    proc.addChangeListener (this);
    syncFromProcessor();
    startTimerHz (30);
}

OrecchioEditor::~OrecchioEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------------------------
void OrecchioEditor::applyTheme()
{
    const bool light = proc.ui.getProperty ("theme", "dark").toString() == "light";
    theme = light ? Theme::makeLight() : Theme::makeDark();
    look.setTheme (theme);
    themeBtn.setButtonText (txt (light ? "Tema scuro" : "Tema chiaro"));

    for (int t = 0; t < kNumTracks; ++t)
    {
        trackBtn[t].setColour (juce::TextButton::buttonOnColourId, theme.track[t]);
        trackBtn[t].setColour (juce::TextButton::textColourOnId, theme.dark ? theme.bg : juce::Colours::white);
    }
    const juce::Colour volColours[4] = { theme.heat, theme.track[0], theme.track[1], theme.track[2] };
    for (int i = 0; i < 4; ++i)
    {
        volSlider[i].setColour (juce::Slider::trackColourId, volColours[i].withAlpha (0.45f));
        volSlider[i].setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }
    contrastSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    title.setColour (juce::Label::textColourId, theme.ink);
    sampleLabel.setColour (juce::Label::textColourId, theme.muted);
    hintLabel.setColour (juce::Label::textColourId, theme.muted);

    dragChip.theme = theme;
    roll.setTheme (theme);
    sendLookAndFeelChange();
    repaint();
}

void OrecchioEditor::selectTrack (int track)
{
    proc.ui.setProperty ("track", track, nullptr);
    proc.setActiveTrack (track);
    trackBtn[track].setToggleState (true, juce::dontSendNotification);
    roll.repaint();
}

void OrecchioEditor::syncFromProcessor()
{
    const auto& a = proc.getAnalysis();
    bpmSlider.setValue (proc.getBpm(), juce::dontSendNotification);
    offSlider.setValue (proc.getOffset(), juce::dontSendNotification);
    keyLabel.setText (txt ("Tonalità: ") + (a.valid ? txt (dsp::keyName (a.key)) : juce::String ("-")), juce::dontSendNotification);
    sampleLabel.setText (proc.getSampleName().isEmpty() ? txt ("Nessun sample") : proc.getSampleName(), juce::dontSendNotification);

    const bool ready = a.valid && ! proc.isBusy();
    sugBassBtn.setEnabled (ready);
    sugChordsBtn.setEnabled (ready);
    detectBtn.setEnabled (ready && a.tempo.valid);
    loadBtn.setEnabled (! proc.isBusy());
    demoBtn.setEnabled (! proc.isBusy());
    undoBtn.setEnabled (proc.canUndo());

    const int track = juce::jlimit (0, kNumTracks - 1, (int) proc.ui.getProperty ("track", kMelody));
    trackBtn[track].setToggleState (true, juce::dontSendNotification);
    proc.setActiveTrack (track);
    gridBox.setSelectedId ((int) proc.ui.getProperty ("grid", 1) + 1, juce::dontSendNotification);
    lenBox.setSelectedId ((int) proc.ui.getProperty ("len", 2) + 1, juce::dontSendNotification);
    scaleBtn.setToggleState ((bool) proc.ui.getProperty ("scaleLock", false), juce::dontSendNotification);
    cleanBtn.setToggleState ((bool) proc.ui.getProperty ("clean", true), juce::dontSendNotification);
    contrastSlider.setValue ((double) proc.ui.getProperty ("contrast", 0.5), juce::dontSendNotification);
    contrastSlider.updateText();

    const void* id = a.raw.v.empty() ? nullptr : a.raw.v.data();
    if (id != lastAnalysis)
    {
        lastAnalysis = id;
        roll.rebuildHeatmap();
    }
    roll.contentChanged();
}

void OrecchioEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    syncFromProcessor();
}

void OrecchioEditor::timerCallback()
{
    roll.updatePlayhead();

    const bool host = proc.isHostPlaying();
    playBtn.setButtonText (host ? txt ("DAW") : txt (proc.isPlaying() ? "Stop" : "Play"));
    playBtn.setToggleState (proc.isPlaying(), juce::dontSendNotification);

    juce::String status = proc.getStatus();
    const double hb = proc.getHostBpm();
    if (host && hb > 0 && std::abs (hb - proc.getBpm()) > 0.05 && proc.getAnalysis().valid)
        status = txt ("La DAW è a ") + juce::String (hb, 1) + txt (" BPM, il sample a ") + juce::String (proc.getBpm(), 1)
               + txt (": per restare a tempo il sample viene accelerato o rallentato, e cambia anche l'intonazione.");
    if (status != lastStatusShown)
    {
        lastStatusShown = status;
        statusLabel.setText (status, juce::dontSendNotification);
    }
}

// ---------------------------------------------------------------------------------------------
void OrecchioEditor::chooseSample()
{
    chooser = std::make_unique<juce::FileChooser> (txt ("Scegli un sample audio"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   proc.getFormatManager().getWildcardForAllFormats());
    juce::Component::SafePointer<OrecchioEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) safe->proc.loadSampleFile (f);
                          });
}

void OrecchioEditor::exportMidi()
{
    if (proc.getNotes().empty())
    {
        proc.setStatus (txt ("Non ci sono note da esportare: disegnale o usa i suggerimenti."));
        return;
    }
    chooser = std::make_unique<juce::FileChooser> (txt ("Esporta MIDI"),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Orecchio.mid"),
                                                   "*.mid");
    juce::Component::SafePointer<OrecchioEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              f = f.withFileExtension ("mid");

                              auto& p = safe->proc;
                              auto write = [] (const juce::File& file, const juce::MemoryBlock& data)
                              { return file.replaceWithData (data.getData(), data.getSize()); };

                              bool ok = write (f, p.makeMidiFile (-1));
                              int extra = 0;
                              for (int t = 0; t < kNumTracks; ++t)
                              {
                                  bool has = false;
                                  for (const auto& n : p.getNotes()) has = has || n.track == t;
                                  if (! has) continue;
                                  ok = write (f.getSiblingFile (f.getFileNameWithoutExtension() + "_" + trackSlug (t) + ".mid"),
                                              p.makeMidiFile (t)) && ok;
                                  ++extra;
                              }
                              p.setStatus (ok ? txt ("Salvato ") + f.getFileName() + txt (" più ") + juce::String (extra)
                                                    + txt (extra == 1 ? " file per la traccia" : " file, uno per traccia")
                                              : txt ("Non sono riuscito a scrivere il file MIDI in ") + f.getParentDirectory().getFullPathName());
                          });
}

void OrecchioEditor::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (! taps.empty() && now - taps.back() > 2.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 8) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        proc.setBpm (std::round (600.0 / avg) / 10.0);
        proc.setStatus (txt ("Tap: ") + juce::String (proc.getBpm(), 1) + txt (" BPM (") + juce::String ((int) taps.size()) + txt (" colpi)"));
    }
    else
    {
        proc.setStatus (txt ("Tap: continua a battere a tempo…"));
    }
}

// ---------------------------------------------------------------------------------------------
bool OrecchioEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey) { proc.togglePlay(); return true; }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { proc.undo(); return true; }
    if (! key.getModifiers().isAnyModifierKeyDown())
    {
        const auto c = key.getTextCharacter();
        if (c >= '1' && c <= '3') { selectTrack ((int) (c - '1')); return true; }
    }
    return false;
}

bool OrecchioEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (proc.getFormatManager().findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void OrecchioEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    repaint();
    for (const auto& f : files)
        if (proc.getFormatManager().findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
        {
            proc.loadSampleFile (juce::File (f));
            return;
        }
}

// ---------------------------------------------------------------------------------------------
void OrecchioEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme.bg);
    g.setColour (theme.panel);
    g.fillRect (0, 0, getWidth(), roll.getY() - 6);
    g.setColour (theme.line);
    g.fillRect (0, roll.getY() - 6, getWidth(), 1);
}

void OrecchioEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dragOver) return;
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    g.setColour (theme.bg.withAlpha (0.75f));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (theme.accent);
    g.drawRoundedRectangle (r, 12.0f, 2.0f);
    g.setFont (font (22.0f, true));
    g.drawText (txt ("Lascia qui il file audio"), r, juce::Justification::centred);
}

void OrecchioEditor::resized()
{
    proc.ui.setProperty ("w", getWidth(), nullptr);
    proc.ui.setProperty ("h", getHeight(), nullptr);

    auto area = getLocalBounds().reduced (8, 0);
    const int rowH = 30, gap = 6;

    // {componente, larghezza, larghezza minima}; componente nullptr = spaziatore (0 = elastico)
    struct Item { juce::Component* c; int w, min; };
    auto layoutRow = [] (juce::Rectangle<int> r, std::initializer_list<Item> items)
    {
        juce::FlexBox fb;
        fb.flexDirection = juce::FlexBox::Direction::row;
        fb.alignItems = juce::FlexBox::AlignItems::stretch;
        for (const auto& it : items)
        {
            if (it.c == nullptr)
            {
                fb.items.add (it.w > 0 ? juce::FlexItem().withWidth ((float) it.w) : juce::FlexItem().withFlex (1.0f));
                continue;
            }
            if (! it.c->isVisible()) continue;
            auto item = juce::FlexItem (*it.c).withMargin ({ 0, 3, 0, 3 });
            if (it.w <= 0) item = item.withFlex (1.0f, 1.0f).withMinWidth ((float) it.min);
            else           item = item.withWidth ((float) it.w).withFlex (0.0f, it.min < it.w ? 1.0f : 0.0f).withMinWidth ((float) it.min);
            fb.items.add (item);
        }
        fb.performLayout (r);
    };

    title.setVisible (getWidth() >= 1290);
    area.removeFromTop (gap + 2);
    layoutRow (area.removeFromTop (rowH), { { &title, 92, 92 }, { &loadBtn, 122, 100 }, { &demoBtn, 64, 60 }, { &sampleLabel, 0, 80 },
                                            { &bpmLabel, 38, 38 }, { &bpmSlider, 118, 118 }, { &tapBtn, 50, 46 }, { &detectBtn, 96, 84 },
                                            { &offLabel, 72, 72 }, { &offSlider, 128, 128 }, { nullptr, 6, 6 }, { &playBtn, 70, 62 },
                                            { &loopBtn, 58, 54 }, { &keyLabel, 170, 130 } });
    area.removeFromTop (gap);
    layoutRow (area.removeFromTop (rowH), { { &trackBtn[0], 84, 72 }, { &trackBtn[1], 84, 72 }, { &trackBtn[2], 84, 72 }, { nullptr, 10, 10 },
                                            { &scaleBtn, 112, 100 }, { &gridBox, 134, 112 }, { &lenBox, 124, 100 }, { nullptr, 10, 10 },
                                            { &sugBassBtn, 140, 126 }, { &sugChordsBtn, 150, 136 }, { &clearBtn, 118, 104 }, { &undoBtn, 86, 76 },
                                            { nullptr, 0, 0 }, { &themeBtn, 100, 92 } });
    area.removeFromTop (gap);
    layoutRow (area.removeFromTop (rowH), { { &contrastSlider, 150, 130 }, { &cleanBtn, 132, 120 }, { nullptr, 10, 10 },
                                            { &volSlider[0], 124, 108 }, { &volSlider[1], 124, 108 }, { &volSlider[2], 124, 108 },
                                            { &volSlider[3], 124, 108 }, { nullptr, 0, 0 }, { &midiOutBtn, 88, 84 },
                                            { &exportBtn, 128, 116 }, { &dragChip, 150, 136 } });
    area.removeFromTop (gap + 6);

    auto statusRow = getLocalBounds().removeFromBottom (24).reduced (10, 0);
    hintLabel.setBounds (statusRow.removeFromRight (juce::jmin (640, statusRow.getWidth() / 2)));
    statusLabel.setBounds (statusRow);
    area.removeFromBottom (26);

    roll.setBounds (getLocalBounds().withTop (area.getY()).withBottom (area.getBottom()));
}
