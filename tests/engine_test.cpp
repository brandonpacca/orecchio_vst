// Test d'integrazione del plugin senza DAW: istanzia il processore, carica la demo, simula il
// trasporto della DAW e quello interno, controlla l'uscita MIDI, il ripristino dello stato,
// l'export e salva uno screenshot dell'editor in engine_test_editor.png.
// Si compila con -DORECCHIO_ENGINE_TEST=ON (target orecchio_engine_test).
#include "../Source/PluginProcessor.h"
#include "../Source/Text.h"

#include <cstdio>
#include <map>



namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("%s  %s\n", ok ? "OK  " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

struct FakePlayHead : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        return p;
    }
    bool playing = false;
    double bpm = 90.0, ppq = 0.0;
};

bool waitFor (std::function<bool()> cond, int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (! cond() && juce::Time::getMillisecondCounter() < end)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    return cond();
}

struct Ev { juce::int64 time; int channel, note; bool on; };

// Esegue n blocchi e raccoglie gli eventi MIDI con il tempo assoluto in campioni.
std::vector<Ev> run (OrecchioProcessor& p, FakePlayHead& ph, int blocks, int blockSize, double rate,
                     juce::int64& clock, double& rms)
{
    std::vector<Ev> evs;
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    double sum = 0;
    for (int b = 0; b < blocks; ++b)
    {
        midi.clear();
        p.processBlock (buf, midi);
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOnOrOff())
                evs.push_back ({ clock + m.samplePosition, msg.getChannel(), msg.getNoteNumber(), msg.isNoteOn() });
        }
        for (int i = 0; i < blockSize; ++i) sum += (double) buf.getSample (0, i) * buf.getSample (0, i);
        clock += blockSize;
        if (ph.playing) ph.ppq += blockSize / rate * ph.bpm / 60.0;
    }
    rms = std::sqrt (sum / ((double) blocks * blockSize));
    return evs;
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double rate = 48000.0;
    const int bs = 480;

    std::unique_ptr<OrecchioProcessor> proc (dynamic_cast<OrecchioProcessor*> (createPluginFilter()));
    FakePlayHead ph;
    proc->setPlayHead (&ph);
    proc->setPlayConfigDetails (0, 2, rate, bs);
    proc->prepareToPlay (rate, bs);

    // --- analisi della demo ---
    proc->loadDemo();
    check (waitFor ([&] { return ! proc->isBusy() && proc->getAnalysis().valid; }, 20000), "analisi della demo completata");
    check (std::abs (proc->getBpm() - 90.0) < 0.05 && proc->getOffset() < 0.02,
           "tempo applicato: " + juce::String (proc->getBpm(), 2) + " BPM, primo battito " + juce::String (proc->getOffset(), 3));
    juce::String seq;
    for (auto& c : proc->getChords()) seq << txt (dsp::chordName (c)) << " ";
    check (seq.startsWith ("Am F C G Am F C G"), "accordi per battuta: " + seq);
    check (std::abs (proc->getProjectBeats() - 36.0) < 1e-9, "lunghezza progetto: " + juce::String (proc->getProjectBeats()) + " battiti");

    // --- suggerimenti e annulla ---
    proc->suggestChords();
    proc->suggestBass();
    int counts[3] = {};
    for (auto& n : proc->getNotes()) counts[n.track]++;
    check (counts[0] == 8 && counts[1] == 24, "suggerimenti: " + juce::String (counts[0]) + " note di basso, " + juce::String (counts[1]) + " di accordi");
    proc->undo();
    check (proc->getNotes().size() == 24, "annulla toglie il basso suggerito");
    proc->suggestBass();
    auto notes = proc->getNotes();
    notes.push_back ({ orecchio::kMelody, 76, 0.0, 1.0 });
    notes.push_back ({ orecchio::kMelody, 72, 1.0, 1.0 });
    proc->setNotes (notes);

    // --- trasporto della DAW, senza loop ---
    proc->apvts.getParameter ("loop")->setValueNotifyingHost (0.0f);
    ph.playing = true;
    ph.ppq = 0.0;
    juce::int64 clock = 0;
    double rms = 0;
    const double samplesPerBeat = rate * 60.0 / 90.0;
    auto evs = run (*proc, ph, (int) (33 * samplesPerBeat / bs), bs, rate, clock, rms);

    std::map<int, int> ons, offs;
    for (auto& e : evs) (e.on ? ons : offs)[e.channel]++;
    check (ons[1] == 8 && ons[2] == 24 && ons[3] == 2, "note-on per canale: basso " + juce::String (ons[1]) + ", accordi "
                                                          + juce::String (ons[2]) + ", melodia " + juce::String (ons[3]));
    check (ons[1] == offs[1] && ons[2] == offs[2] && ons[3] == offs[3], "ogni note-on ha il suo note-off");

    juce::int64 bar2Bass = -1, firstBass = -1;
    int firstBassNote = 0;
    for (auto& e : evs)
        if (e.on && e.channel == 1)
        {
            if (firstBass < 0) { firstBass = e.time; firstBassNote = e.note; }
            else if (bar2Bass < 0) bar2Bass = e.time;
        }
    check (firstBass == 0 && firstBassNote == 45, "primo basso: La2 al campione 0");
    check (std::abs ((double) bar2Bass - 4 * samplesPerBeat) <= 32, txt ("basso della 2ª battuta al campione ") + juce::String (bar2Bass)
                                                                 + " (atteso " + juce::String ((int) (4 * samplesPerBeat)) + ")");
    check (rms > 0.01, txt ("c'è audio in uscita (rms ") + juce::String (rms, 3) + ")");

    // --- salto del trasporto: niente note appese ---
    ph.ppq = 2.5;
    evs = run (*proc, ph, 50, bs, rate, clock, rms);
    ph.playing = false;
    evs = run (*proc, ph, 10, bs, rate, clock, rms);
    check (proc->getPlayheadBeat() > 2.5, "segue il salto della DAW");

    // --- trasporto interno con loop sulle prime 2 battute ---
    proc->setLoopRange (0.0, 8.0);
    proc->setPlayheadBeat (0.0);
    proc->togglePlay();
    clock = 0;
    evs = run (*proc, ph, (int) (3 * 8 * samplesPerBeat / bs) + 2, bs, rate, clock, rms);
    proc->togglePlay();
    auto tail = run (*proc, ph, 4, bs, rate, clock, rms);
    evs.insert (evs.end(), tail.begin(), tail.end());

    std::vector<juce::int64> bassOn;
    int onCount = 0, offCount = 0;
    for (auto& e : evs)
    {
        if (e.channel == 1 && e.on && e.note == 45) bassOn.push_back (e.time);
        (e.on ? onCount : offCount)++;
    }
    check (bassOn.size() >= 3, "loop: La2 ripetuto " + juce::String ((int) bassOn.size()) + " volte");
    if (bassOn.size() >= 2)
        check (std::abs ((double) (bassOn[1] - bassOn[0]) - 8 * samplesPerBeat) <= 64,
               "loop di 8 battiti: " + juce::String (bassOn[1] - bassOn[0]) + " campioni");
    check (onCount == offCount, "loop: nessuna nota appesa (" + juce::String (onCount) + " on, " + juce::String (offCount) + " off)");

    // --- export MIDI ---
    {
        const auto data = proc->makeMidiFile (-1);
        juce::MemoryInputStream in (data, false);
        juce::MidiFile mf;
        check (mf.readFrom (in) && mf.getNumTracks() == 4 && mf.getTimeFormat() == 480, "file MIDI tipo 1: 4 tracce, 480 PPQ");
        int midiOns = 0;
        for (int t = 1; t < mf.getNumTracks(); ++t)
            for (auto* e : *mf.getTrack (t)) midiOns += e->message.isNoteOn() ? 1 : 0;
        check (midiOns == (int) proc->getNotes().size(), "il file contiene tutte le " + juce::String (midiOns) + " note");
        const auto drag = proc->writeTempMidiForDrag();
        check (drag.existsAsFile() && drag.getSize() > 100, "file temporaneo per il trascinamento: " + drag.getFileName());
    }

    // --- stato del progetto ---
    juce::MemoryBlock state;
    proc->ui.setProperty ("track", 0, nullptr);
    proc->getStateInformation (state);
    {
        std::unique_ptr<OrecchioProcessor> copy (dynamic_cast<OrecchioProcessor*> (createPluginFilter()));
        copy->setBpm (120.0);
        copy->setStateInformation (state.getData(), (int) state.getSize());
        check (copy->getNotes() == proc->getNotes(), "stato: note ripristinate");
        check (std::abs (copy->getBpm() - proc->getBpm()) < 1e-9 && (int) copy->ui.getProperty ("track") == 0, "stato: tempo e interfaccia ripristinati");
        check (waitFor ([&] { return ! copy->isBusy() && copy->getAnalysis().valid; }, 20000) && std::abs (copy->getBpm() - 90.0) < 1e-9,
               "stato: demo rianalizzata senza toccare il tempo salvato");
    }

    // --- screenshot dell'editor ---
    proc->setPlayheadBeat (6.0);
    proc->ui.setProperty ("track", 2, nullptr);
    for (const auto& themeName : { "dark", "light" })
    {
        proc->ui.setProperty ("theme", themeName, nullptr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
        ed->setSize (1320, 780);
        waitFor ([] { return false; }, 300);
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto out = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String ("engine_test_editor_") + themeName + ".png");
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat png;
        check (os.openedOk() && png.writeImageToStream (img, os), "screenshot editor (" + juce::String (themeName) + "): " + out.getFullPathName());
    }

    proc->releaseResources();
    std::printf ("\n%s\n", failures ? "CI SONO ERRORI" : "tutto ok");
    return failures;
}
