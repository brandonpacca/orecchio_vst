// Sintetizzatore polifonico minimale, un timbro per traccia (come voice() nella versione web).
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Model.h"

#include <array>
#include <cmath>

namespace orecchio
{
class TrackSynth
{
public:
    void prepare (double newRate)
    {
        rate = newRate;
        for (auto& v : voices) { v.env.setSampleRate (rate); v.active = false; }
        lpCoef = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 2200.0 / rate));
    }

    void noteOn (int track, int midi, float velocity, int autoOffSamples = 0)
    {
        Voice* target = nullptr;
        for (auto& v : voices)
            if (! v.active) { target = &v; break; }
        if (target == nullptr) // rubo la voce più vecchia
        {
            target = &voices[0];
            for (auto& v : voices)
                if (v.age < target->age) target = &v;
        }

        static const juce::ADSR::Parameters params[kNumTracks] = {
            { 0.004f, 0.30f, 0.65f, 0.08f }, // basso
            { 0.020f, 0.40f, 0.70f, 0.25f }, // accordi
            { 0.005f, 0.20f, 0.55f, 0.15f }, // melodia
        };

        auto& v = *target;
        v.active = true;
        v.releasing = false;
        v.track = juce::jlimit (0, kNumTracks - 1, track);
        v.midi = midi;
        v.vel = velocity;
        v.phase = 0.0;
        v.inc = juce::MidiMessage::getMidiNoteInHertz (midi) / rate;
        v.lp = 0.0f;
        v.autoOff = autoOffSamples;
        v.age = ++counter;
        v.env.reset();
        v.env.setParameters (params[v.track]);
        v.env.noteOn();
    }

    void noteOff (int track, int midi)
    {
        for (auto& v : voices)
            if (v.active && v.track == track && v.midi == midi && ! v.releasing)
            {
                v.env.noteOff();
                v.releasing = true;
                return;
            }
    }

    void allNotesOff()
    {
        for (auto& v : voices)
            if (v.active && ! v.releasing) { v.env.noteOff(); v.releasing = true; }
    }

    // Somma (non sovrascrive) nei due canali.
    void render (float* left, float* right, int numSamples, const float* trackGain)
    {
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const float g = trackGain[v.track] * v.vel;
            for (int i = 0; i < numSamples; ++i)
            {
                if (v.autoOff > 0 && --v.autoOff == 0) { v.env.noteOff(); v.releasing = true; }
                const float e = v.env.getNextSample();
                if (! v.env.isActive()) { v.active = false; v.releasing = false; break; }

                const float s = oscillator (v) * e * g;
                left[i] += s;
                if (right != nullptr) right[i] += s;

                v.phase += v.inc;
                if (v.phase >= 1.0) v.phase -= 1.0;
            }
        }
    }

private:
    struct Voice
    {
        bool active = false, releasing = false;
        int track = 0, midi = 60, autoOff = 0;
        double phase = 0, inc = 0;
        float vel = 1, lp = 0;
        juce::uint32 age = 0;
        juce::ADSR env;
    };

    static float polyBlep (double t, double dt)
    {
        if (t < dt)       { t /= dt;             return (float) (t + t - t * t - 1.0); }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt;  return (float) (t * t + t + t + 1.0); }
        return 0.0f;
    }

    float oscillator (Voice& v)
    {
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        switch (v.track)
        {
            case kBass:
            {
                const double s = std::sin (twoPi * v.phase) + 0.3 * std::sin (2.0 * twoPi * v.phase);
                return (float) std::tanh (1.3 * s) * 0.42f;
            }
            case kChords:
            {
                const float saw = (float) (2.0 * v.phase - 1.0) - polyBlep (v.phase, v.inc);
                v.lp += lpCoef * (saw - v.lp);
                return v.lp * 0.16f;
            }
            default:
            {
                const float tri = 1.0f - 4.0f * (float) std::abs (v.phase - 0.5);
                return tri * 0.30f;
            }
        }
    }

    std::array<Voice, 32> voices;
    double rate = 44100.0;
    float lpCoef = 0.2f;
    juce::uint32 counter = 0;
};
} // namespace orecchio
