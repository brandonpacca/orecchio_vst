#pragma once

#include <cmath>

namespace orecchio
{
enum Track { kBass = 0, kChords = 1, kMelody = 2, kNumTracks = 3 };

// start e len sono in battiti; il battito 0 coincide con il "primo battito" del sample.
struct Note
{
    int track = kMelody;
    int midi = 60;
    double start = 0.0, len = 1.0;

    bool operator== (const Note& o) const
    {
        return track == o.track && midi == o.midi && std::abs (start - o.start) < 1e-9 && std::abs (len - o.len) < 1e-9;
    }
    bool operator!= (const Note& o) const { return ! (*this == o); }
    double end() const { return start + len; }
};

inline const char* trackName (int t)
{
    static const char* names[kNumTracks] = { "Basso", "Accordi", "Melodia" };
    return names[t];
}

inline const char* trackSlug (int t)
{
    static const char* names[kNumTracks] = { "basso", "accordi", "melodia" };
    return names[t];
}
} // namespace orecchio
