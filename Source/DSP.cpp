#include "DSP.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace dsp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
int    pc (int midi)       { return ((midi % 12) + 12) % 12; }

// FFT complessa radix-2, iterativa, in place.
struct FFT
{
    explicit FFT (int size) : n (size)
    {
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        rev.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
        cosT.resize ((size_t) n / 2);
        sinT.resize ((size_t) n / 2);
        for (int i = 0; i < n / 2; ++i)
        {
            cosT[(size_t) i] = (float) std::cos (2.0 * kPi * i / n);
            sinT[(size_t) i] = (float) -std::sin (2.0 * kPi * i / n);
        }
    }

    void forward (std::vector<float>& re, std::vector<float>& im) const
    {
        for (int i = 0; i < n; ++i)
            if (i < rev[(size_t) i])
            {
                std::swap (re[(size_t) i], re[(size_t) rev[(size_t) i]]);
                std::swap (im[(size_t) i], im[(size_t) rev[(size_t) i]]);
            }

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2, step = n / len;
            for (int i = 0; i < n; i += len)
                for (int j = 0; j < half; ++j)
                {
                    const float wr = cosT[(size_t) (j * step)], wi = sinT[(size_t) (j * step)];
                    const size_t a = (size_t) (i + j), b = a + (size_t) half;
                    const float xr = re[b] * wr - im[b] * wi;
                    const float xi = re[b] * wi + im[b] * wr;
                    re[b] = re[a] - xr;  im[b] = im[a] - xi;
                    re[a] += xr;         im[a] += xi;
                }
        }
    }

    int n;
    std::vector<int> rev;
    std::vector<float> cosT, sinT;
};

std::vector<float> hann (int n, double& sum)
{
    std::vector<float> w ((size_t) n);
    sum = 0;
    for (int i = 0; i < n; ++i)
    {
        w[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / n));
        sum += w[(size_t) i];
    }
    return w;
}

const int kMajorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
const int kMinorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };

int scaleDegree (int midi, const Key& key) // −1 se fuori scala
{
    const int rel = pc (midi - key.tonic);
    const int* steps = key.minor ? kMinorSteps : kMajorSteps;
    for (int i = 0; i < 7; ++i)
        if (steps[i] == rel) return i;
    return -1;
}
} // namespace

// ---------------------------------------------------------------------------------------------
// Ricampionamento con sinc finestrato (Blackman), tabella precalcolata.
std::vector<float> resample (const float* x, size_t n, double fromRate, double toRate)
{
    if (n == 0 || fromRate <= 0 || toRate <= 0) return {};
    if (std::abs (fromRate - toRate) < 1e-6) return std::vector<float> (x, x + n);

    const double ratio  = fromRate / toRate;                 // passi di ingresso per campione in uscita
    const double cutoff = std::min (1.0, toRate / fromRate) * 0.95;
    const int    W      = (int) std::ceil (16.0 / cutoff);   // semi-larghezza in campioni di ingresso
    const int    R      = 256;                               // risoluzione della tabella

    std::vector<float> table ((size_t) (W * R + 2), 0.0f);
    for (int i = 0; i <= W * R; ++i)
    {
        const double u = (double) i / R;
        const double a = kPi * cutoff * u;
        const double sinc = i == 0 ? 1.0 : std::sin (a) / a;
        const double t = u / W; // 0..1
        const double win = 0.42 + 0.5 * std::cos (kPi * t) + 0.08 * std::cos (2 * kPi * t);
        table[(size_t) i] = (float) (cutoff * sinc * win);
    }

    const size_t outN = (size_t) std::floor ((double) n / ratio);
    std::vector<float> y (outN, 0.0f);
    for (size_t i = 0; i < outN; ++i)
    {
        const double pos = (double) i * ratio;
        const long ip = (long) std::floor (pos);
        const double frac = pos - (double) ip;
        double acc = 0;
        for (int j = -W + 1; j <= W; ++j)
        {
            const long idx = ip + j;
            if (idx < 0 || idx >= (long) n) continue;
            const double d = std::abs ((double) j - frac) * R;
            const int di = (int) d;
            if (di >= W * R) continue;
            const double f = d - di;
            acc += x[idx] * (table[(size_t) di] * (1.0 - f) + table[(size_t) di + 1] * f);
        }
        y[i] = (float) acc;
    }
    return y;
}

// ---------------------------------------------------------------------------------------------
// Mappa delle note: per ogni nota MIDI il picco di magnitudine nei bin entro ±½ semitono.
// Le note sotto kBassSplit usano una FFT lunga il doppio per separare meglio i semitoni gravi.
Spectrogram analyze (const std::vector<float>& x)
{
    Spectrogram s;
    const int n = (int) x.size();
    s.frames = std::max (1, (n + kHop - 1) / kHop);
    s.v.assign ((size_t) s.frames * kNumNotes, 0.0f);

    struct Pass { int size, noteLo, noteHi; };
    const Pass passes[2] = { { kFft, kBassSplit, kMidiHi }, { kBassFft, kMidiLo, kBassSplit - 1 } };

    for (const auto& pass : passes)
    {
        const int N = pass.size;
        FFT fft (N);
        double wsum = 0;
        const auto win = hann (N, wsum);
        const float norm = (float) (2.0 / wsum); // sinusoide di ampiezza A → picco ≈ A

        std::vector<int> lo, hi;
        int maxBin = 0;
        for (int m = pass.noteLo; m <= pass.noteHi; ++m)
        {
            const double f = midiToHz (m);
            int a = (int) std::ceil (f * std::pow (2.0, -1.0 / 24.0) * N / kRate);
            int b = (int) std::floor (f * std::pow (2.0, 1.0 / 24.0) * N / kRate);
            if (a > b) a = b = (int) std::lround (f * N / kRate);
            a = std::max (1, a);
            b = std::min (N / 2 - 1, std::max (a, b));
            lo.push_back (a);
            hi.push_back (b);
            maxBin = std::max (maxBin, b);
        }

        std::vector<float> re ((size_t) N), im ((size_t) N), mag ((size_t) maxBin + 1);
        for (int f = 0; f < s.frames; ++f)
        {
            const long start = (long) f * kHop - N / 2;
            for (int k = 0; k < N; ++k)
            {
                const long idx = start + k;
                re[(size_t) k] = (idx >= 0 && idx < n) ? x[(size_t) idx] * win[(size_t) k] : 0.0f;
                im[(size_t) k] = 0.0f;
            }
            fft.forward (re, im);
            for (int k = 1; k <= maxBin; ++k)
                mag[(size_t) k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) * norm;

            for (int m = pass.noteLo; m <= pass.noteHi; ++m)
            {
                const size_t i = (size_t) (m - pass.noteLo);
                float peak = 0;
                for (int k = lo[i]; k <= hi[i]; ++k) peak = std::max (peak, mag[(size_t) k]);
                s.ref (f, m - kMidiLo) = peak;
            }
        }
    }
    return s;
}

// Riduzione armonici: a ogni nota si toglie una frazione (già pulita) delle note che la
// producono come 2ª, 3ª e 4ª armonica (−12, −19, −24 semitoni).
Spectrogram cleanSpec (const Spectrogram& s)
{
    Spectrogram o = s;
    const float a = 0.5f, b = 0.33f, c = 0.25f;
    for (int f = 0; f < s.frames; ++f)
        for (int n = 12; n < kNumNotes; ++n)
        {
            float v = s.get (f, n) - a * o.get (f, n - 12);
            if (n >= 19) v -= b * o.get (f, n - 19);
            if (n >= 24) v -= c * o.get (f, n - 24);
            o.ref (f, n) = std::max (0.0f, v);
        }
    return o;
}

std::array<float, 12> chromaOf (const Spectrogram& s, int f0, int f1, int midiMax)
{
    std::array<float, 12> c {};
    f0 = std::max (0, f0);
    f1 = std::min (s.frames, f1);
    for (int f = f0; f < f1; ++f)
        for (int n = 0; n < kNumNotes; ++n)
        {
            const int midi = kMidiLo + n;
            if (midi > midiMax) break;
            c[(size_t) pc (midi)] += s.get (f, n);
        }
    return c;
}

// Krumhansl–Schmuckler
Key detectKey (const Spectrogram& s)
{
    static const float major[12] = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    static const float minor[12] = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    Key best;
    const auto c = chromaOf (s, 0, s.frames);
    const float total = std::accumulate (c.begin(), c.end(), 0.0f);
    if (total <= 1e-9f) return best;

    auto corr = [] (const float* a, const float* b)
    {
        double ma = 0, mb = 0;
        for (int i = 0; i < 12; ++i) { ma += a[i]; mb += b[i]; }
        ma /= 12; mb /= 12;
        double num = 0, da = 0, db = 0;
        for (int i = 0; i < 12; ++i)
        {
            num += (a[i] - ma) * (b[i] - mb);
            da  += (a[i] - ma) * (a[i] - ma);
            db  += (b[i] - mb) * (b[i] - mb);
        }
        return (da > 0 && db > 0) ? num / std::sqrt (da * db) : 0.0;
    };

    best.score = -2;
    for (int t = 0; t < 12; ++t)
    {
        float rot[12];
        for (int i = 0; i < 12; ++i) rot[i] = c[(size_t) ((t + i) % 12)];
        for (int mode = 0; mode < 2; ++mode)
        {
            const float r = (float) corr (rot, mode ? minor : major);
            if (r > best.score) { best.score = r; best.tonic = t; best.minor = mode == 1; }
        }
    }
    best.valid = true;
    return best;
}

// Un accordo per battuta: template di triadi maggiori/minori (coseno), più un bonus per la nota
// al basso e uno piccolo per gli accordi diatonici alla tonalità.
std::vector<Chord> detectChords (const Spectrogram& s, double bpm, double offset, double durationSec,
                                 int beatsPerBar, const Key& key)
{
    std::vector<Chord> out;
    if (bpm <= 0 || s.frames == 0 || beatsPerBar <= 0) return out;

    const double barSec = 60.0 / bpm * beatsPerBar, fs = Spectrogram::frameSeconds();
    const int bars = std::max (0, (int) std::ceil ((durationSec - offset) / barSec - 1e-6));
    out.resize ((size_t) bars);

    std::vector<std::array<float, 12>> chroma ((size_t) bars), bass ((size_t) bars);
    std::vector<float> energy ((size_t) bars, 0.0f);
    double meanE = 0;
    int counted = 0;
    for (int b = 0; b < bars; ++b)
    {
        const double t0 = offset + b * barSec, t1 = t0 + barSec;
        const int f0 = (int) std::ceil (t0 / fs), f1 = (int) std::ceil (t1 / fs);
        chroma[(size_t) b] = chromaOf (s, f0, f1);
        bass[(size_t) b]   = chromaOf (s, f0, f1, 52);
        energy[(size_t) b] = std::accumulate (chroma[(size_t) b].begin(), chroma[(size_t) b].end(), 0.0f);
        if (energy[(size_t) b] > 0) { meanE += energy[(size_t) b]; ++counted; }
    }
    if (counted == 0) return out;
    meanE /= counted;

    for (int b = 0; b < bars; ++b)
    {
        if (energy[(size_t) b] < 0.08 * meanE) continue;

        auto c = chroma[(size_t) b];
        double l2 = 0;
        for (float v : c) l2 += (double) v * v;
        l2 = std::sqrt (l2);
        for (float& v : c) v = (float) (v / l2);

        auto bc = bass[(size_t) b];
        const float bsum = std::accumulate (bc.begin(), bc.end(), 0.0f);
        if (bsum > 0) for (float& v : bc) v /= bsum;

        Chord best;
        best.score = -1;
        for (int r = 0; r < 12; ++r)
            for (int minor = 0; minor < 2; ++minor)
            {
                const int third = (r + (minor ? 3 : 4)) % 12, fifth = (r + 7) % 12;
                float score = (c[(size_t) r] + c[(size_t) third] + c[(size_t) fifth]) / std::sqrt (3.0f);
                score += 0.25f * bc[(size_t) r];
                if (key.valid && isDiatonic (r, minor == 1, key)) score += 0.06f;
                if (score > best.score) { best.score = score; best.root = r; best.minor = minor == 1; }
            }
        out[(size_t) b] = best;
    }
    return out;
}

// Tempo: flusso spettrale → autocorrelazione → ricerca fine con un pettine sui battiti.
Tempo detectTempo (const std::vector<float>& x)
{
    Tempo t;
    const int N = 1024, H = 256;
    const int n = (int) x.size();
    const int frames = n / H;
    if (frames < 200) return t; // meno di ~2 s: non affidabile

    FFT fft (N);
    double wsum = 0;
    const auto win = hann (N, wsum);
    const int maxBin = (int) (8000.0 * N / kRate);

    std::vector<float> re ((size_t) N), im ((size_t) N), prev ((size_t) maxBin + 1, 0.0f), env ((size_t) frames, 0.0f);
    for (int i = 0; i < frames; ++i)
    {
        const long start = (long) i * H - N / 2;
        for (int k = 0; k < N; ++k)
        {
            const long idx = start + k;
            re[(size_t) k] = (idx >= 0 && idx < n) ? x[(size_t) idx] * win[(size_t) k] : 0.0f;
            im[(size_t) k] = 0.0f;
        }
        fft.forward (re, im);
        float flux = 0;
        for (int k = 1; k <= maxBin; ++k)
        {
            const float L = std::log1p (1000.0f * std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]) / (float) wsum);
            const float d = L - prev[(size_t) k];
            if (d > 0 && i > 0) flux += d;
            prev[(size_t) k] = L;
        }
        env[(size_t) i] = flux;
    }

    // tolgo la media mobile (±0.4 s) e tengo solo la parte positiva
    const double hopSec = H / kRate;
    const int Wm = (int) (0.4 / hopSec);
    std::vector<double> cum ((size_t) frames + 1, 0.0);
    for (int i = 0; i < frames; ++i) cum[(size_t) i + 1] = cum[(size_t) i] + env[(size_t) i];
    std::vector<float> e ((size_t) frames, 0.0f);
    double meanE = 0;
    for (int i = 0; i < frames; ++i)
    {
        const int a = std::max (0, i - Wm), b = std::min (frames, i + Wm + 1);
        const double avg = (cum[(size_t) b] - cum[(size_t) a]) / (b - a);
        e[(size_t) i] = (float) std::max (0.0, env[(size_t) i] - avg);
        meanE += e[(size_t) i];
    }
    meanE /= frames;
    if (meanE <= 1e-9) return t;

    // autocorrelazione 55–200 BPM, pesata verso ~115 BPM
    const int minLag = (int) std::floor (60.0 / 200.0 / hopSec), maxLag = (int) std::ceil (60.0 / 55.0 / hopSec);
    std::vector<double> acf ((size_t) maxLag + 2, 0.0);
    for (int l = minLag - 1; l <= maxLag + 1; ++l)
    {
        double sum = 0;
        for (int i = 0; i + l < frames; ++i) sum += (double) e[(size_t) i] * e[(size_t) (i + l)];
        acf[(size_t) l] = sum / (frames - l);
    }
    int bestLag = minLag;
    double bestW = -1;
    for (int l = minLag; l <= maxLag; ++l)
    {
        const double bpm = 60.0 / (l * hopSec);
        const double z = std::log2 (bpm / 115.0);
        const double w = acf[(size_t) l] * std::exp (-0.5 * z * z);
        if (w > bestW) { bestW = w; bestLag = l; }
    }
    double lag = bestLag;
    {
        const double a = acf[(size_t) bestLag - 1], b = acf[(size_t) bestLag], c = acf[(size_t) bestLag + 1];
        const double den = a - 2 * b + c;
        if (std::abs (den) > 1e-12) lag += std::clamp (0.5 * (a - c) / den, -0.5, 0.5);
    }
    const double bpm0 = 60.0 / (lag * hopSec);

    auto interp = [&] (double p)
    {
        const int i = (int) p;
        if (i < 0 || i + 1 >= frames) return 0.0f;
        const float fr = (float) (p - i);
        return e[(size_t) i] * (1 - fr) + e[(size_t) i + 1] * fr;
    };
    auto comb = [&] (double bpm, double& phaseOut)
    {
        const double P = 60.0 / bpm / hopSec;
        double best = -1;
        for (double ph = 0; ph < P; ph += 0.5)
        {
            double sum = 0;
            int count = 0;
            for (double p = ph; p < frames - 1; p += P) { sum += interp (p); ++count; }
            const double sc = count ? sum / count : 0;
            if (sc > best) { best = sc; phaseOut = ph; }
        }
        return best;
    };

    double bestBpm = bpm0, bestPhase = 0, bestScore = -1;
    for (double b = bpm0 * 0.96; b <= bpm0 * 1.04; b += 0.02)
    {
        double ph = 0;
        const double sc = comb (b, ph);
        if (sc > bestScore) { bestScore = sc; bestBpm = b; bestPhase = ph; }
    }

    // i loop sono quasi sempre a BPM interi: se ci siamo vicini, preferisco l'intero
    const double rounded = std::round (bestBpm);
    if (std::abs (rounded - bestBpm) < 0.25)
    {
        double ph = 0;
        const double sc = comb (rounded, ph);
        if (sc >= 0.97 * bestScore) { bestBpm = rounded; bestPhase = ph; bestScore = sc; }
    }

    t.bpm = bestBpm;
    t.offset = bestPhase * hopSec;
    // una fase appena sotto un battito intero è in realtà un battito a t ≈ 0
    const double beatSec = 60.0 / bestBpm;
    if (t.offset > beatSec - 0.04) t.offset = std::max (0.0, t.offset - beatSec);
    t.confidence = (float) (bestScore / meanE);
    t.valid = true;
    return t;
}

// ---------------------------------------------------------------------------------------------
bool inScale (int midi, const Key& key) { return !key.valid || scaleDegree (midi, key) >= 0; }

int snapToScale (int midi, const Key& key)
{
    if (!key.valid || inScale (midi, key)) return midi;
    if (inScale (midi + 1, key)) return midi + 1; // a pari distanza si sale
    return midi - 1;
}

std::array<int, 3> diatonicTriad (int rootMidi, const Key& key)
{
    if (!key.valid) return { rootMidi, rootMidi + 4, rootMidi + 7 };
    const int root = snapToScale (rootMidi, key);
    const int deg = scaleDegree (root, key);
    const int* steps = key.minor ? kMinorSteps : kMajorSteps;
    auto up = [&] (int k)
    {
        const int d = deg + k;
        return root + (steps[d % 7] + 12 * (d / 7)) - steps[deg];
    };
    return { root, up (2), up (4) };
}

std::array<int, 3> chordPitchClasses (const Chord& c)
{
    return { c.root, (c.root + (c.minor ? 3 : 4)) % 12, (c.root + 7) % 12 };
}

bool isDiatonic (int root, bool minor, const Key& key)
{
    if (!key.valid) return false;
    Chord c { root, minor, 0 };
    bool all = true;
    for (int p : chordPitchClasses (c)) all = all && inScale (p, key);
    if (all) return true;
    // in minore il V maggiore (armonica) è di casa
    return key.minor && !minor && pc (root - key.tonic) == 7;
}

std::string noteName (int midi)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string (names[pc (midi)]) + std::to_string (midi / 12 - 1);
}

std::string chordName (const Chord& c)
{
    static const char* names[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    if (c.root < 0) return "–";
    return std::string (names[pc (c.root)]) + (c.minor ? "m" : "");
}

std::string keyName (const Key& key)
{
    static const char* names[12] = { "Do", "Do#", "Re", "Mi\xe2\x99\xad", "Mi", "Fa", "Fa#", "Sol", "La\xe2\x99\xad", "La", "Si\xe2\x99\xad", "Si" };
    if (!key.valid) return "–";
    return std::string (names[pc (key.tonic)]) + (key.minor ? " minore" : " maggiore");
}

// ---------------------------------------------------------------------------------------------
// Triadi in posizione stretta attorno al Do4, scegliendo il rivolto più vicino al precedente.
std::vector<NoteEvent> suggestChords (const std::vector<Chord>& chords, int beatsPerBar)
{
    std::vector<NoteEvent> out;
    std::array<int, 3> prev { 57, 60, 64 };
    bool hasPrev = false;

    for (size_t b = 0; b < chords.size(); ++b)
    {
        const auto& ch = chords[b];
        if (ch.root < 0) continue;
        const auto pcs = chordPitchClasses (ch);

        std::array<int, 3> bestV {};
        int bestCost = 1 << 30;
        for (int inv = 0; inv < 3; ++inv)
        {
            std::array<int, 3> v {};
            int low = 52;
            while (pc (low) != pcs[(size_t) inv]) ++low; // nota più grave tra Mi3 e Re#4
            v[0] = low;
            for (int k = 1; k < 3; ++k)
            {
                int m = v[(size_t) k - 1] + 1;
                while (pc (m) != pcs[(size_t) ((inv + k) % 3)]) ++m;
                v[(size_t) k] = m;
            }
            int cost = 0;
            for (int k = 0; k < 3; ++k)
                cost += hasPrev ? std::abs (v[(size_t) k] - prev[(size_t) k]) : std::abs (v[(size_t) k] - 60);
            if (cost < bestCost) { bestCost = cost; bestV = v; }
        }
        for (int m : bestV) out.push_back ({ m, (double) b * beatsPerBar, (double) beatsPerBar });
        prev = bestV;
        hasPrev = true;
    }
    return out;
}

// Una nota per battito presa dal registro grave (pesato verso il basso), con una spinta per
// fondamentale e note dell'accordo della battuta; i battiti uguali consecutivi si uniscono.
std::vector<NoteEvent> suggestBass (const Spectrogram& s, double bpm, double offset,
                                    const std::vector<Chord>& chords, int beatsPerBar)
{
    std::vector<NoteEvent> out;
    if (bpm <= 0 || s.frames == 0) return out;

    const double beatSec = 60.0 / bpm, fs = Spectrogram::frameSeconds();
    const int beats = (int) std::ceil ((s.frames * fs - offset) / beatSec - 1e-6);
    const int hiNote = 55;

    std::vector<int> pick ((size_t) std::max (0, beats), -1);
    std::vector<float> energy ((size_t) std::max (0, beats), 0.0f);
    double meanE = 0;
    for (int k = 0; k < beats; ++k)
    {
        const double t0 = offset + k * beatSec;
        const int f0 = (int) std::ceil (t0 / fs), f1 = (int) std::ceil ((t0 + beatSec) / fs);
        const int bar = k / beatsPerBar;
        const Chord ch = bar < (int) chords.size() ? chords[(size_t) bar] : Chord {};

        float best = 0, tot = 0;
        for (int m = kMidiLo; m <= hiNote; ++m)
        {
            float sum = 0;
            for (int f = std::max (0, f0); f < std::min (s.frames, f1); ++f) sum += s.get (f, m - kMidiLo);
            const float w = m <= 48 ? 1.0f : 1.0f - 0.7f * (float) (m - 48) / (hiNote - 48);
            float v = sum * w;
            tot += v;
            if (ch.root >= 0)
            {
                const auto pcs = chordPitchClasses (ch);
                if (pc (m) == pcs[0]) v *= 1.35f;
                else if (pc (m) == pcs[1] || pc (m) == pcs[2]) v *= 1.1f;
            }
            if (v > best) { best = v; pick[(size_t) k] = m; }
        }
        energy[(size_t) k] = tot;
        meanE += tot;
    }
    if (beats <= 0) return out;
    meanE /= beats;

    for (int k = 0; k < beats; ++k)
    {
        int m = pick[(size_t) k];
        if (m < 0 || energy[(size_t) k] < 0.12 * meanE) continue;
        while (m > 52) m -= 12;
        const bool sameBar = k > 0 && (k / beatsPerBar) == ((k - 1) / beatsPerBar);
        if (sameBar && !out.empty() && out.back().midi == m
            && std::abs (out.back().start + out.back().len - k) < 1e-9)
            out.back().len += 1.0;
        else
            out.push_back ({ m, (double) k, 1.0 });
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
std::vector<float> makeDemo (double sr)
{
    const double bpm = 90.0, beat = 60.0 / bpm, bar = 4 * beat;
    const int bars = 8;
    const size_t n = (size_t) ((bars * bar + 1.0) * sr);
    std::vector<float> y (n, 0.0f);

    struct Bar { int bass; int pad[3]; int mel[4]; };
    const Bar prog[4] = {
        { 45, { 57, 60, 64 }, { 76, 72, 69, 72 } }, // Am
        { 41, { 57, 60, 65 }, { 77, 72, 69, 72 } }, // F
        { 48, { 55, 60, 64 }, { 79, 76, 72, 76 } }, // C
        { 43, { 55, 59, 62 }, { 74, 71, 67, 71 } }, // G
    };

    auto tone = [&] (double t0, double dur, int midi, float amp, int harmonics, double attack, bool pluck)
    {
        const double hz = midiToHz (midi);
        const size_t s0 = (size_t) (t0 * sr), len = (size_t) ((dur + 0.08) * sr);
        for (size_t i = 0; i < len && s0 + i < n; ++i)
        {
            const double t = (double) i / sr;
            double env = std::min (1.0, t / attack);
            if (pluck) env *= std::exp (-t * 2.5 / dur);
            if (t > dur) env *= std::max (0.0, 1.0 - (t - dur) / 0.08);
            double v = 0;
            for (int h = 1; h <= harmonics; ++h) v += std::sin (2 * kPi * hz * h * t) / h;
            y[s0 + i] += (float) (amp * env * v);
        }
    };
    auto kick = [&] (double t0)
    {
        const size_t s0 = (size_t) (t0 * sr), len = (size_t) (0.25 * sr);
        double ph = 0;
        for (size_t i = 0; i < len && s0 + i < n; ++i)
        {
            const double t = (double) i / sr;
            ph += 2 * kPi * (50.0 + 70.0 * std::exp (-t * 30.0)) / sr;
            y[s0 + i] += (float) (0.45 * std::exp (-t * 18.0) * std::sin (ph));
        }
    };

    for (int b = 0; b < bars; ++b)
    {
        const Bar& c = prog[b % 4];
        const double t0 = b * bar;
        for (int m : c.pad) tone (t0, bar - 0.05, m, 0.07f, 3, 0.05, false);
        for (int k = 0; k < 4; ++k)
        {
            tone (t0 + k * beat, beat * 0.9, c.bass, 0.22f, 4, 0.005, true);
            tone (t0 + k * beat, beat * 0.8, c.mel[k], 0.10f, 2, 0.01, true);
            kick (t0 + k * beat);
        }
    }

    float peak = 0;
    for (float v : y) peak = std::max (peak, std::abs (v));
    if (peak > 0) for (float& v : y) v *= 0.8f / peak;
    return y;
}
} // namespace dsp
