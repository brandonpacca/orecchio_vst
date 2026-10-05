// Orecchio — motore DSP.
// Funzioni pure in C++17, senza dipendenze da JUCE: si testano da sole con tests/dsp_test.cpp.
#pragma once

#include <array>
#include <string>
#include <vector>

namespace dsp
{
constexpr int    kMidiLo = 28, kMidiHi = 96, kNumNotes = kMidiHi - kMidiLo + 1; // 69 note, Mi1–Do7
constexpr double kRate = 22050.0;    // frequenza di analisi
constexpr int    kFft = 8192;        // FFT principale
constexpr int    kBassFft = 16384;   // FFT lunga, solo per le note gravi
constexpr int    kBassSplit = 48;    // sotto il Do3 si usa la FFT lunga
constexpr int    kHop = 1102;        // ~50 ms

// Mappa delle note: frames × kNumNotes, una riga per frame. Il frame f è centrato a f*kHop campioni.
struct Spectrogram
{
    int frames = 0;
    std::vector<float> v;

    float  get (int f, int n) const { return v[(size_t) f * kNumNotes + (size_t) n]; }
    float& ref (int f, int n)       { return v[(size_t) f * kNumNotes + (size_t) n]; }
    static double frameSeconds()    { return kHop / kRate; }
};

struct Key   { int tonic = 0; bool minor = false; float score = 0; bool valid = false; };
struct Chord { int root = -1; bool minor = false; float score = 0; };          // root -1 = nessun accordo
struct Tempo { double bpm = 120.0, offset = 0.0; float confidence = 0; bool valid = false; };
struct NoteEvent { int midi; double start, len; };                             // start/len in battiti

// Analisi
std::vector<float> resample (const float* x, size_t n, double fromRate, double toRate);
Spectrogram analyze (const std::vector<float>& x22k);
Spectrogram cleanSpec (const Spectrogram& s);
std::array<float, 12> chromaOf (const Spectrogram& s, int f0, int f1, int midiMax = kMidiHi);
Key detectKey (const Spectrogram& s);
std::vector<Chord> detectChords (const Spectrogram& s, double bpm, double offset, double durationSec,
                                 int beatsPerBar, const Key& key);
Tempo detectTempo (const std::vector<float>& x22k);

// Teoria
bool inScale (int midi, const Key& key);
int  snapToScale (int midi, const Key& key);
std::array<int, 3> diatonicTriad (int rootMidi, const Key& key);
std::array<int, 3> chordPitchClasses (const Chord& c);
bool isDiatonic (int root, bool minor, const Key& key);
std::string noteName (int midi);       // "C4" (60 = C4)
std::string chordName (const Chord& c); // "Am"
std::string keyName (const Key& key);  // "La minore"

// Suggerimenti
std::vector<NoteEvent> suggestChords (const std::vector<Chord>& chords, int beatsPerBar);
std::vector<NoteEvent> suggestBass (const Spectrogram& clean, double bpm, double offset,
                                    const std::vector<Chord>& chords, int beatsPerBar);

// Sample sintetico di prova: 90 BPM, Am–F–C–G ripetuto due volte, mono.
std::vector<float> makeDemo (double sampleRate);
} // namespace dsp
