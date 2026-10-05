#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Text.h"

using namespace orecchio;

namespace
{
double posMod (double a, double m)
{
    const double r = std::fmod (a, m);
    return r < 0 ? r + m : r;
}

inline float sampleAt (const float* d, int n, int i) { return (i >= 0 && i < n) ? d[i] : 0.0f; }

// interpolazione di Hermite a 4 punti
inline float hermite (const float* d, int n, double p)
{
    const int i = (int) std::floor (p);
    const float t = (float) (p - i);
    const float xm1 = sampleAt (d, n, i - 1), x0 = sampleAt (d, n, i), x1 = sampleAt (d, n, i + 1), x2 = sampleAt (d, n, i + 2);
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

constexpr double kMaxSampleSeconds = 300.0;
} // namespace

OrecchioProcessor::OrecchioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formats.registerBasicFormats();

    volSample        = apvts.getRawParameterValue ("volSample");
    volTrack[kBass]   = apvts.getRawParameterValue ("volBass");
    volTrack[kChords] = apvts.getRawParameterValue ("volChords");
    volTrack[kMelody] = apvts.getRawParameterValue ("volMelody");
    loopParam        = apvts.getRawParameterValue ("loop");
    midiOutParam     = apvts.getRawParameterValue ("midiOut");

    audioNotes.reserve (8192);
}

OrecchioProcessor::~OrecchioProcessor()
{
    pool.removeAllJobs (true, 10000);
}

juce::AudioProcessorValueTreeState::ParameterLayout OrecchioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto volume = [&p] (const char* id, const char* name, float def)
    {
        p.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                                  juce::NormalisableRange<float> (0.0f, 1.0f), def));
    };
    volume ("volSample", "Volume sample", 0.8f);
    volume ("volBass",   "Volume basso", 0.7f);
    volume ("volChords", "Volume accordi", 0.6f);
    volume ("volMelody", "Volume melodia", 0.7f);
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "loop", 1 }, "Loop", true));
    p.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "midiOut", 1 }, "Uscita MIDI", true));
    return { p.begin(), p.end() };
}

void OrecchioProcessor::prepareToPlay (double sampleRate, int)
{
    rate = sampleRate;
    synth.prepare (sampleRate);
    numActive = 0;
    wasPlaying = false;
}

bool OrecchioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

// ---------------------------------------------------------------------------------------------
// Caricamento e analisi
void OrecchioProcessor::startAnalysis (std::function<bool (juce::AudioBuffer<float>&, double&, juce::String&)> loader,
                                       juce::String name, juce::String path, bool demo, bool useDetectedTempo)
{
    const int gen = ++loadGeneration;
    busy = true;
    status = txt ("Analisi in corso: ") + name + txt ("…");
    sendChangeMessage();

    juce::WeakReference<OrecchioProcessor> weak (this);
    pool.addJob ([weak, gen, loader, name, path, demo, useDetectedTempo]
    {
        auto buffer = std::make_shared<juce::AudioBuffer<float>>();
        auto result = std::make_shared<Analysis>();
        double fileRate = 44100.0;
        juce::String error;
        const bool ok = loader (*buffer, fileRate, error);

        if (ok)
        {
            const int n = buffer->getNumSamples(), chs = buffer->getNumChannels();
            std::vector<float> mono ((size_t) n, 0.0f);
            for (int ch = 0; ch < chs; ++ch)
            {
                const float* d = buffer->getReadPointer (ch);
                for (int i = 0; i < n; ++i) mono[(size_t) i] += d[i] / (float) chs;
            }
            const auto x = dsp::resample (mono.data(), mono.size(), fileRate, dsp::kRate);
            result->raw = dsp::analyze (x);
            result->clean = dsp::cleanSpec (result->raw);
            result->key = dsp::detectKey (result->clean);
            result->tempo = dsp::detectTempo (x);
            result->duration = n / fileRate;
            result->valid = true;
        }

        juce::MessageManager::callAsync ([weak, gen, ok, error, buffer, result, fileRate, name, path, demo, useDetectedTempo]
        {
            auto* self = weak.get();
            if (self == nullptr || gen != self->loadGeneration) return;

            self->busy = false;
            if (! ok)
            {
                self->status = error;
                self->sendChangeMessage();
                return;
            }

            auto fresh = std::make_unique<juce::AudioBuffer<float>> (std::move (*buffer));
            {
                const juce::SpinLock::ScopedLockType sl (self->sampleLock);
                std::swap (self->sample, fresh);
                self->sampleFileRate = fileRate;
            }
            fresh.reset(); // il vecchio sample si libera qui, fuori dal lock

            self->analysis = std::move (*result);
            self->sampleName = name;
            self->samplePath = path;
            self->sampleIsDemo = demo;

            const auto& a = self->analysis;
            if (useDetectedTempo && a.tempo.valid)
            {
                self->bpm = a.tempo.bpm;
                self->offset = a.tempo.offset;
            }
            self->recomputeChords();
            self->updateProjectBeats();

            if (useDetectedTempo)
            {
                self->loopStart = 0.0;
                self->loopEnd = self->projectBeats.load();
                self->setPlayheadBeat (0.0);
                self->status = a.tempo.valid
                    ? juce::String::formatted ("%.1f BPM, ", a.tempo.bpm) + txt ("primo battito a ")
                          + juce::String (a.tempo.offset, 3) + " s, " + txt (dsp::keyName (a.key))
                          + txt (". Se il tempo non torna, correggilo a mano o con Tap.")
                    : txt ("Sample caricato. Imposta BPM e primo battito.");
            }
            else
            {
                self->status = txt ("Progetto ripristinato: ") + name;
            }
            self->sendChangeMessage();
        });
    });
}

void OrecchioProcessor::loadSampleFile (const juce::File& file, bool useDetectedTempo)
{
    auto* fm = &formats;
    startAnalysis ([fm, file] (juce::AudioBuffer<float>& buf, double& rateOut, juce::String& err)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (fm->createReaderFor (file));
        if (reader == nullptr)
        {
            err = txt ("Non riesco a leggere ") + file.getFileName() + txt (": formato non supportato o file mancante.");
            return false;
        }
        const auto maxLen = (juce::int64) (reader->sampleRate * kMaxSampleSeconds);
        const int len = (int) juce::jmin (reader->lengthInSamples, maxLen);
        const int chs = (int) juce::jlimit (1u, 2u, reader->numChannels);
        if (len <= 0)
        {
            err = txt ("Il file è vuoto: ") + file.getFileName();
            return false;
        }
        buf.setSize (chs, len);
        reader->read (&buf, 0, len, 0, true, chs > 1);
        rateOut = reader->sampleRate;
        return true;
    }, file.getFileName(), file.getFullPathName(), false, useDetectedTempo);
}

void OrecchioProcessor::loadDemo (bool useDetectedTempo)
{
    startAnalysis ([] (juce::AudioBuffer<float>& buf, double& rateOut, juce::String&)
    {
        const auto y = dsp::makeDemo (44100.0);
        buf.setSize (1, (int) y.size());
        buf.copyFrom (0, 0, y.data(), (int) y.size());
        rateOut = 44100.0;
        return true;
    }, txt ("Demo · 90 BPM · Am–F–C–G"), {}, true, useDetectedTempo);
}

void OrecchioProcessor::recomputeChords()
{
    if (! analysis.valid) { chords.clear(); return; }
    chords = dsp::detectChords (analysis.clean, bpm.load(), offset.load(), analysis.duration,
                                getBeatsPerBar(), analysis.key);
}

void OrecchioProcessor::updateProjectBeats()
{
    double beats = 0.0;
    if (analysis.valid) beats = (analysis.duration - offset.load()) * bpm.load() / 60.0;
    for (const auto& n : notes) beats = juce::jmax (beats, n.end());

    const int bpb = getBeatsPerBar();
    const double bars = juce::jmax (4.0, std::ceil (beats / bpb - 1e-6));
    projectBeats = bars * bpb;

    if (loopEnd.load() - loopStart.load() < 0.25)
    {
        loopStart = 0.0;
        loopEnd = projectBeats.load();
    }
}

// ---------------------------------------------------------------------------------------------
// Tempo
void OrecchioProcessor::setBpm (double newBpm)
{
    bpm = juce::jlimit (20.0, 300.0, newBpm);
    recomputeChords();
    updateProjectBeats();
    sendChangeMessage();
}

void OrecchioProcessor::setOffset (double seconds)
{
    offset = juce::jmax (0.0, seconds);
    recomputeChords();
    updateProjectBeats();
    sendChangeMessage();
}

void OrecchioProcessor::applyDetectedTempo()
{
    if (! analysis.valid || ! analysis.tempo.valid)
    {
        status = txt ("Non ho abbastanza audio per rilevare il tempo.");
        sendChangeMessage();
        return;
    }
    bpm = analysis.tempo.bpm;
    offset = analysis.tempo.offset;
    recomputeChords();
    updateProjectBeats();
    status = juce::String::formatted ("Tempo rilevato: %.1f BPM, primo battito %.3f s", analysis.tempo.bpm, analysis.tempo.offset);
    sendChangeMessage();
}

// ---------------------------------------------------------------------------------------------
// Note
void OrecchioProcessor::publishNotes()
{
    {
        const juce::SpinLock::ScopedLockType sl (notesLock);
        sharedNotes = notes;
    }
    notesDirty = true;
}

void OrecchioProcessor::setNotes (std::vector<Note> newNotes)
{
    notes = std::move (newNotes);
    publishNotes();
    updateProjectBeats();
    sendChangeMessage();
}

void OrecchioProcessor::commitEdit (const std::vector<Note>& before)
{
    if (before == notes) return;
    undoStack.push_back (before);
    if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
    sendChangeMessage();
}

bool OrecchioProcessor::undo()
{
    if (undoStack.empty()) return false;
    notes = std::move (undoStack.back());
    undoStack.pop_back();
    publishNotes();
    updateProjectBeats();
    status = txt ("Modifica annullata.");
    sendChangeMessage();
    return true;
}

void OrecchioProcessor::clearTrack (int track)
{
    const auto before = notes;
    std::vector<Note> kept;
    for (const auto& n : notes)
        if (n.track != track) kept.push_back (n);
    setNotes (std::move (kept));
    commitEdit (before);
}

void OrecchioProcessor::suggestBass()
{
    if (! analysis.valid) { status = txt ("Prima carica un sample."); sendChangeMessage(); return; }
    const auto before = notes;
    std::vector<Note> next;
    for (const auto& n : notes)
        if (n.track != kBass) next.push_back (n);
    const auto sug = dsp::suggestBass (analysis.clean, bpm.load(), offset.load(), chords, getBeatsPerBar());
    for (const auto& e : sug) next.push_back ({ kBass, e.midi, e.start, e.len });
    setNotes (std::move (next));
    commitEdit (before);
    status = juce::String ((int) sug.size()) + txt (" note di basso suggerite. Annulla per tornare indietro.");
    sendChangeMessage();
}

void OrecchioProcessor::suggestChords()
{
    if (! analysis.valid) { status = txt ("Prima carica un sample."); sendChangeMessage(); return; }
    const auto before = notes;
    std::vector<Note> next;
    for (const auto& n : notes)
        if (n.track != kChords) next.push_back (n);
    const auto sug = dsp::suggestChords (chords, getBeatsPerBar());
    for (const auto& e : sug) next.push_back ({ kChords, e.midi, e.start, e.len });
    setNotes (std::move (next));
    commitEdit (before);
    status = juce::String ((int) sug.size() / 3) + txt (" accordi suggeriti, uno per battuta. Annulla per tornare indietro.");
    sendChangeMessage();
}

// ---------------------------------------------------------------------------------------------
// Trasporto
void OrecchioProcessor::togglePlay()
{
    if (hostPlaying.load())
    {
        status = txt ("Il plugin segue il trasporto della DAW: avvia e ferma da lì.");
        sendChangeMessage();
        return;
    }
    internalPlaying = ! internalPlaying.load();
}

void OrecchioProcessor::setPlayheadBeat (double beat)
{
    seekBeat = juce::jmax (0.0, beat);
    seekPending = true;
    playheadBeat = seekBeat.load();
}

void OrecchioProcessor::setLoopRange (double startBeat, double endBeat)
{
    loopStart = juce::jmax (0.0, juce::jmin (startBeat, endBeat));
    loopEnd = juce::jmax (startBeat, endBeat);
    if (auto* p = apvts.getParameter ("loop")) p->setValueNotifyingHost (1.0f);
    sendChangeMessage();
}

void OrecchioProcessor::previewNote (int track, int midi)
{
    const auto scope = previewFifo.write (1);
    if (scope.blockSize1 > 0)      previewBuf[(size_t) scope.startIndex1] = { track, midi };
    else if (scope.blockSize2 > 0) previewBuf[(size_t) scope.startIndex2] = { track, midi };
}

// ---------------------------------------------------------------------------------------------
// Audio
void OrecchioProcessor::allNotesOff (juce::MidiBuffer& midi, int samplePos)
{
    const bool midiOut = midiOutParam->load() > 0.5f;
    for (int i = 0; i < numActive; ++i)
    {
        synth.noteOff (active[(size_t) i].track, active[(size_t) i].midi);
        if (midiOut) midi.addEvent (juce::MidiMessage::noteOff (active[(size_t) i].track + 1, active[(size_t) i].midi), samplePos);
    }
    numActive = 0;
}

void OrecchioProcessor::processEvents (double b0, double b1, juce::MidiBuffer& midi, int samplePos, bool midiOut)
{
    for (int i = numActive - 1; i >= 0; --i)
        if (active[(size_t) i].end < b1)
        {
            const auto a = active[(size_t) i];
            synth.noteOff (a.track, a.midi);
            if (midiOut) midi.addEvent (juce::MidiMessage::noteOff (a.track + 1, a.midi), samplePos);
            active[(size_t) i] = active[(size_t) --numActive];
        }

    for (const auto& n : audioNotes)
        if (n.start >= b0 && n.start < b1 && n.len > 0)
        {
            const float vel = n.track == kChords ? 0.7f : 0.8f;
            synth.noteOn (n.track, n.midi, vel);
            if (midiOut) midi.addEvent (juce::MidiMessage::noteOn (n.track + 1, n.midi, vel), samplePos);
            if (numActive < (int) active.size()) active[(size_t) numActive++] = { n.track, n.midi, n.end() };
        }
}

void OrecchioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // MIDI in ingresso: suona sulla traccia attiva, per provare idee con una tastiera.
    const int track = activeTrack.load();
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())       synth.noteOn (track, m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) synth.noteOff (track, m.getNoteNumber());
    }
    midi.clear();

    if (notesDirty.load())
    {
        const juce::SpinLock::ScopedTryLockType tl (notesLock);
        if (tl.isLocked())
        {
            audioNotes.clear();
            const size_t count = juce::jmin (sharedNotes.size(), audioNotes.capacity());
            audioNotes.insert (audioNotes.end(), sharedNotes.begin(), sharedNotes.begin() + (std::ptrdiff_t) count);
            notesDirty = false;
        }
    }

    {
        const auto scope = previewFifo.read (previewFifo.getNumReady());
        const int autoOff = (int) (rate * 0.35);
        for (int i = 0; i < scope.blockSize1; ++i)
            synth.noteOn (previewBuf[(size_t) (scope.startIndex1 + i)].track, previewBuf[(size_t) (scope.startIndex1 + i)].midi, 0.8f, autoOff);
        for (int i = 0; i < scope.blockSize2; ++i)
            synth.noteOn (previewBuf[(size_t) (scope.startIndex2 + i)].track, previewBuf[(size_t) (scope.startIndex2 + i)].midi, 0.8f, autoOff);
    }

    // --- trasporto ---
    bool hostPlay = false;
    double ppq = 0.0, tempoHost = 0.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) tempoHost = *b;
            if (auto p = pos->getPpqPosition()) { ppq = *p; hostPlay = pos->getIsPlaying(); }
        }
    hostPlaying = hostPlay;
    hostBpm = tempoHost;

    const double sbpm = bpm.load(), off = offset.load(), projEnd = projectBeats.load();
    const double ls = loopStart.load(), le = loopEnd.load();
    const bool loopOn = loopParam->load() > 0.5f && le - ls >= 0.25;
    const bool midiOut = midiOutParam->load() > 0.5f;

    float gains[kNumTracks];
    for (int t = 0; t < kNumTracks; ++t) gains[t] = volTrack[t]->load();
    const float sampleGain = volSample->load();

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    bool playing = false;
    double tempo = sbpm;

    if (hostPlay)
    {
        internalPlaying = false;
        seekPending = false;
        tempo = tempoHost > 0 ? tempoHost : sbpm;
        const double target = loopOn ? ls + posMod (ppq, le - ls) : ppq;
        double dist = std::abs (target - currentBeat);
        if (loopOn) dist = juce::jmin (dist, (le - ls) - dist);
        if (! wasPlaying || dist > 0.01)
        {
            if (wasPlaying) allNotesOff (midi, 0);
            currentBeat = target;
        }
        playing = true;
    }
    else
    {
        if (seekPending.exchange (false))
        {
            if (wasPlaying) allNotesOff (midi, 0);
            currentBeat = seekBeat.load();
        }
        if (internalPlaying.load())
        {
            if (! wasPlaying)
            {
                if (loopOn && (currentBeat < ls || currentBeat >= le)) currentBeat = ls;
                if (! loopOn && currentBeat >= projEnd) currentBeat = 0.0;
            }
            playing = true;
        }
    }

    if (! playing)
    {
        if (wasPlaying) allNotesOff (midi, 0);
        wasPlaying = false;
        playheadBeat = currentBeat;
        synth.render (L, R, numSamples, gains);
        return;
    }
    wasPlaying = true;

    const juce::SpinLock::ScopedTryLockType sampleTl (sampleLock);
    const juce::AudioBuffer<float>* smp = sampleTl.isLocked() ? sample.get() : nullptr;
    const double fileRate = sampleFileRate;
    const double bps = tempo / 60.0 / rate;   // battiti per campione d'uscita
    const double secPerBeat = 60.0 / sbpm;    // secondi di sample per battito

    int pos = 0, guard = 0;
    double eventsFrom = -1.0; // dopo un giro del loop gli eventi ripartono esattamente dall'inizio
    while (pos < numSamples && ++guard < 100000)
    {
        int len = juce::jmin (32, numSamples - pos);
        const double b0 = currentBeat;
        double b1 = b0 + len * bps;
        bool wrap = false;

        if (loopOn && b1 >= le)
        {
            const int toWrap = (int) std::ceil ((le - b0) / bps);
            if (toWrap <= 0)
            {
                allNotesOff (midi, pos);
                currentBeat = (b0 - le < le - ls) ? ls + juce::jmax (0.0, b0 - le) : ls;
                eventsFrom = ls;
                continue;
            }
            len = juce::jmin (len, toWrap);
            b1 = b0 + len * bps;
            wrap = b1 >= le;
        }

        processEvents (eventsFrom >= 0.0 ? eventsFrom : b0, wrap ? le : b1, midi, pos, midiOut);
        eventsFrom = -1.0;

        // il sample è agganciato ai battiti: se la DAW ha un tempo diverso viene accelerato o rallentato
        if (smp != nullptr && sampleGain > 0.0001f)
        {
            const int n = smp->getNumSamples();
            const float* dl = smp->getReadPointer (0);
            const float* dr = smp->getNumChannels() > 1 ? smp->getReadPointer (1) : dl;
            const double step = bps * secPerBeat * fileRate;
            double sp = (off + b0 * secPerBeat) * fileRate;
            for (int i = 0; i < len; ++i, sp += step)
            {
                if (sp < -2.0 || sp >= n + 2.0) continue;
                L[pos + i] += hermite (dl, n, sp) * sampleGain;
                if (R != nullptr) R[pos + i] += hermite (dr, n, sp) * sampleGain;
            }
        }
        synth.render (L + pos, R != nullptr ? R + pos : nullptr, len, gains);

        pos += len;
        currentBeat = b1;
        if (wrap)
        {
            allNotesOff (midi, juce::jmin (pos, numSamples - 1));
            currentBeat = ls + (b1 - le);
            eventsFrom = ls;
        }
        else if (! loopOn && ! hostPlay && currentBeat >= projEnd)
        {
            allNotesOff (midi, juce::jmin (pos, numSamples - 1));
            internalPlaying = false;
            wasPlaying = false;
            currentBeat = 0.0;
            if (pos < numSamples)
                synth.render (L + pos, R != nullptr ? R + pos : nullptr, numSamples - pos, gains);
            break;
        }
    }

    playheadBeat = currentBeat;
}

// ---------------------------------------------------------------------------------------------
// MIDI
juce::MemoryBlock OrecchioProcessor::makeMidiFile (int onlyTrack) const
{
    constexpr int ppqn = 480;
    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppqn);

    juce::MidiMessageSequence meta;
    meta.addEvent (juce::MidiMessage::textMetaEvent (3, "Orecchio"), 0);
    meta.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm.load())), 0);
    meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent (getBeatsPerBar(), 4), 0);
    file.addTrack (meta);

    auto tick = [] (double beat) { return (double) std::lround (beat * ppqn); };
    for (int t = 0; t < kNumTracks; ++t)
    {
        if (onlyTrack >= 0 && t != onlyTrack) continue;
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, trackName (t)), 0);
        // prima tutti i note-off: a parità di tick finiscono prima dei note-on
        for (const auto& n : notes)
            if (n.track == t) seq.addEvent (juce::MidiMessage::noteOff (t + 1, n.midi).withTimeStamp (tick (n.end())));
        for (const auto& n : notes)
            if (n.track == t) seq.addEvent (juce::MidiMessage::noteOn (t + 1, n.midi, (juce::uint8) (t == kChords ? 90 : 100)).withTimeStamp (tick (n.start)));
        seq.sort();
        file.addTrack (seq);
    }

    juce::MemoryOutputStream out;
    file.writeTo (out, 1);
    return out.getMemoryBlock();
}

juce::File OrecchioProcessor::writeTempMidiForDrag() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Orecchio");
    dir.createDirectory();
    juce::String base = sampleIsDemo || sampleName.isEmpty() ? juce::String ("Demo")
                                                            : sampleName.upToLastOccurrenceOf (".", false, false);
    auto file = dir.getChildFile (juce::File::createLegalFileName (base + " - Orecchio.mid"));
    const auto data = makeMidiFile (-1);
    file.replaceWithData (data.getData(), data.getSize());
    return file;
}

// ---------------------------------------------------------------------------------------------
// Stato del progetto (salvato dalla DAW)
void OrecchioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree st ("ORECCHIO");
    st.setProperty ("version", 1, nullptr);
    st.setProperty ("bpm", bpm.load(), nullptr);
    st.setProperty ("offset", offset.load(), nullptr);
    st.setProperty ("loopStart", loopStart.load(), nullptr);
    st.setProperty ("loopEnd", loopEnd.load(), nullptr);
    st.setProperty ("samplePath", samplePath, nullptr);
    st.setProperty ("demo", sampleIsDemo, nullptr);

    juce::String ns;
    for (const auto& n : notes)
        ns << n.track << ',' << n.midi << ',' << juce::String (n.start, 6) << ',' << juce::String (n.len, 6) << ';';
    st.setProperty ("notes", ns, nullptr);

    st.appendChild (apvts.copyState(), nullptr);
    st.appendChild (ui.createCopy(), nullptr);

    if (auto xml = st.createXml()) copyXmlToBinary (*xml, destData);
}

void OrecchioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    const auto st = juce::ValueTree::fromXml (*xml);
    if (! st.hasType ("ORECCHIO")) return;

    if (auto p = st.getChildWithName (apvts.state.getType()); p.isValid()) apvts.replaceState (p);
    if (auto u = st.getChildWithName ("UI"); u.isValid()) ui.copyPropertiesFrom (u, nullptr);

    bpm = (double) st.getProperty ("bpm", 90.0);
    offset = (double) st.getProperty ("offset", 0.0);
    loopStart = (double) st.getProperty ("loopStart", 0.0);
    loopEnd = (double) st.getProperty ("loopEnd", 16.0);

    std::vector<Note> restored;
    for (const auto& tok : juce::StringArray::fromTokens (st.getProperty ("notes").toString(), ";", ""))
    {
        const auto f = juce::StringArray::fromTokens (tok, ",", "");
        if (f.size() != 4) continue;
        Note n { juce::jlimit (0, kNumTracks - 1, f[0].getIntValue()), juce::jlimit (0, 127, f[1].getIntValue()),
                 f[2].getDoubleValue(), f[3].getDoubleValue() };
        if (n.len > 0) restored.push_back (n);
    }
    notes = std::move (restored);
    undoStack.clear();
    publishNotes();

    const juce::String path = st.getProperty ("samplePath").toString();
    if ((bool) st.getProperty ("demo", false))
        loadDemo (false);
    else if (path.isNotEmpty() && juce::File (path).existsAsFile())
        loadSampleFile (juce::File (path), false);
    else if (path.isNotEmpty())
        status = txt ("Sample non trovato: ") + path + txt (". Le note ci sono, ricarica il file per vedere la mappa.");

    updateProjectBeats();
    sendChangeMessage();
}

juce::AudioProcessorEditor* OrecchioProcessor::createEditor()
{
    return new OrecchioEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OrecchioProcessor();
}
