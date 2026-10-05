// Test del motore DSP senza JUCE:
//   g++ -std=c++17 -O2 tests/dsp_test.cpp Source/DSP.cpp -o dsp_test && ./dsp_test
// Usa la demo (90 BPM, Am–F–C–G) e controlla tempo, tonalità, accordi e suggerimenti.
#include "../Source/DSP.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;
static void check (bool ok, const std::string& what)
{
    std::printf ("%s  %s\n", ok ? "OK  " : "FAIL", what.c_str());
    if (! ok) ++failures;
}

int main()
{
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();

    const auto demo = dsp::makeDemo (44100.0);
    const auto x = dsp::resample (demo.data(), demo.size(), 44100.0, dsp::kRate);
    const double duration = (double) x.size() / dsp::kRate;
    check (std::abs ((double) x.size() - demo.size() / 2.0) < 2, "ricampionamento 44100 → 22050");

    const auto raw = dsp::analyze (x);
    const auto clean = dsp::cleanSpec (raw);
    check (raw.frames == (int) ((x.size() + dsp::kHop - 1) / dsp::kHop), "numero di frame");

    // La4 (69) attacca sul 3° battito della prima battuta: la sua energia deve crescere rispetto
    // al 2° battito, quando suona il Do5.
    {
        auto frameAt = [] (double t) { return (int) std::lround (t / dsp::Spectrogram::frameSeconds()); };
        const double beat = 60.0 / 90.0;
        const float a4b2 = clean.get (frameAt (1 * beat + 0.15), 69 - dsp::kMidiLo);
        const float a4b3 = clean.get (frameAt (2 * beat + 0.15), 69 - dsp::kMidiLo);
        const float c5b2 = clean.get (frameAt (1 * beat + 0.15), 72 - dsp::kMidiLo);
        const float c5b3 = clean.get (frameAt (2 * beat + 0.15), 72 - dsp::kMidiLo);
        check (a4b3 > 1.5f * a4b2 && c5b2 > 1.5f * c5b3, "melodia: Do5 → La4 tra 2° e 3° battito");
    }
    // Basso: Fa2 (41) nella seconda battuta, sotto la soglia della FFT lunga
    {
        const int f = (int) std::lround ((4 * 60.0 / 90.0 + 0.3) / dsp::Spectrogram::frameSeconds());
        int best = 0; float bv = -1;
        for (int m = dsp::kMidiLo; m <= 47; ++m)
            if (clean.get (f, m - dsp::kMidiLo) > bv) { bv = clean.get (f, m - dsp::kMidiLo); best = m; }
        check (best == 41, "basso grave: Fa2 nella 2ª battuta (trovato " + dsp::noteName (best) + ")");
    }

    const auto key = dsp::detectKey (clean);
    check (key.valid && ((key.tonic == 9 && key.minor) || (key.tonic == 0 && ! key.minor)),
           "tonalità: " + dsp::keyName (key));

    const auto tempo = dsp::detectTempo (x);
    char buf[128];
    std::snprintf (buf, sizeof buf, "tempo: %.2f BPM, primo battito %.3f s (confidenza %.1f)", tempo.bpm, tempo.offset, tempo.confidence);
    check (tempo.valid && std::abs (tempo.bpm - 90.0) < 0.3, buf);
    check (std::abs (tempo.offset) < 0.03 || std::abs (tempo.offset - 60.0 / 90.0) < 0.03, "primo battito vicino all'inizio");

    const auto chords = dsp::detectChords (clean, 90.0, 0.0, duration, 4, key);
    std::string seq;
    for (auto& c : chords) seq += dsp::chordName (c) + " ";
    check (chords.size() >= 8 && seq.rfind ("Am F C G Am F C G", 0) == 0, "accordi: " + seq);

    const auto sc = dsp::suggestChords (chords, 4);
    check (sc.size() == 3 * 8, "suggerisci accordi: " + std::to_string (sc.size()) + " note");

    const auto sb = dsp::suggestBass (clean, 90.0, 0.0, chords, 4);
    std::string bass;
    for (auto& n : sb) bass += dsp::noteName (n.midi) + "x" + std::to_string ((int) n.len) + " ";
    check (sb.size() >= 8 && dsp::noteName (sb[0].midi) == "A2" && dsp::noteName (sb[1].midi) == "F2", "suggerisci basso: " + bass);

    const auto tri = dsp::diatonicTriad (62, key); // Re: in La minore → Re minore
    check (tri[1] - tri[0] == 3 && tri[2] - tri[0] == 7, "triade diatonica su Re = minore");

    const double ms = std::chrono::duration<double, std::milli> (clock::now() - t0).count();
    std::printf ("\n%.1f s di audio analizzati in %.0f ms — %s\n", duration, ms, failures ? "CI SONO ERRORI" : "tutto ok");
    return failures;
}
