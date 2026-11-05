// Vox8Core.h — shared framework-free DSP utilities for Vox8.
//
// Vox8 is a spectral vocoder (it processes audio; it does not synthesise
// singing). This file, and every file under Source/DSP/, is plain C++ with
// zero framework includes: no JUCE, no iPlug2, no host headers. The DSP core
// survives any future framework move, including a move to a language of our
// own. The iPlug2 shell is a thin wrapper around it, not the other way round.
//
// Design law (from the FreeEQ8 real-time safety doctrine, carried over):
// - no heap allocation inside process()
// - no coefficient rebuilds inside process()
// - no locks, file I/O, background threads, or UI ownership in DSP
// - atomic display taps only (memory_order_relaxed) for the UI
//
// Reference design: FreeVox8 (GareBear99/FreeVox8, HEAD 02eedfe). This core
// is a rebuild in plain C++, not a copy of the JUCE code: the filterbank uses
// RBJ cookbook biquads (the same transfer function as the reference's
// Butterworth HP+LP pairs at Q=0.707) instead of JUCE filter objects, and
// the mode formulas are stated per engine in docs/ARCHITECTURE.md.

#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace vox8 {

// ---------------------------------------------------------------------------
// Band layout: 128 log-spaced bands, 55 Hz - 18.5 kHz.
// f_lo(i) = 55 * (18500/55)^(i/128); f_hi(i) = max(f_lo(i) + 18, f_lo(i+1)).
// Log spacing matches the ear; linear spacing would waste bands on the top
// octave. Intelligibility saturates at ~20-30 bands; 128 is a Ghost/Morph
// resolution choice, not an intelligibility choice. Never claim otherwise.
// ---------------------------------------------------------------------------

inline constexpr int kNumBands = 128;
inline constexpr int kMaxChannels = 2;
inline constexpr float kBandMinHz = 55.0f;
inline constexpr float kBandMaxHz = 18500.0f;

inline float bandMaxHz(double sampleRate) noexcept
{
    // Cap the top edge at 0.47 * sr so no band sits on Nyquist.
    const float nyquistSafe = static_cast<float>(sampleRate * 0.47);
    return std::min(kBandMaxHz, std::max(4000.0f, nyquistSafe));
}

inline float bandEdgeLo(int index, double sampleRate) noexcept
{
    const float maxHz = bandMaxHz(sampleRate);
    const float t = static_cast<float>(index) / static_cast<float>(kNumBands);
    return kBandMinHz * std::pow(maxHz / kBandMinHz, t);
}

inline float bandEdgeHi(int index, double sampleRate) noexcept
{
    const float lo = bandEdgeLo(index, sampleRate);
    return std::max(lo + 18.0f, bandEdgeLo(index + 1, sampleRate));
}

// Per-band gain: slight high-frequency emphasis counteracting the natural
// energy rolloff of speech formants.
inline float bandGain(int band) noexcept
{
    const float n = static_cast<float>(band) / static_cast<float>(kNumBands - 1);
    return 0.038f + 0.040f * n;
}

// Quadratic high-band weight: consonant lift for sibilance and breath.
inline float highBandWeight(int band) noexcept
{
    const float n = static_cast<float>(band) / static_cast<float>(kNumBands - 1);
    return n * n;
}

// ---------------------------------------------------------------------------
// RBJ cookbook biquad (Audio EQ Cookbook). Q = 0.7071 gives the Butterworth
// response the reference uses. Direct Form II transposed, float state.
// ---------------------------------------------------------------------------

struct Biquad
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void setLowpass(float fc, float sr, float q = 0.70710678f) noexcept
    {
        const float w0 = 2.0f * 3.14159265358979f * fc / static_cast<float>(sr);
        const float alpha = std::sin(w0) / (2.0f * q);
        const float cosw = std::cos(w0);
        const float a0 = 1.0f + alpha;
        b0 = ((1.0f - cosw) * 0.5f) / a0;
        b1 = (1.0f - cosw) / a0;
        b2 = ((1.0f - cosw) * 0.5f) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setHighpass(float fc, float sr, float q = 0.70710678f) noexcept
    {
        const float w0 = 2.0f * 3.14159265358979f * fc / static_cast<float>(sr);
        const float alpha = std::sin(w0) / (2.0f * q);
        const float cosw = std::cos(w0);
        const float a0 = 1.0f + alpha;
        b0 = ((1.0f + cosw) * 0.5f) / a0;
        b1 = -(1.0f + cosw) / a0;
        b2 = ((1.0f + cosw) * 0.5f) / a0;
        a1 = (-2.0f * cosw) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    float process(float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }
};

// ---------------------------------------------------------------------------
// Small shared helpers.
// ---------------------------------------------------------------------------

inline float clampf(float v, float lo, float hi) noexcept
{
    return std::min(hi, std::max(lo, v));
}

inline float midiNoteToHz(int note) noexcept
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
}

inline float decibelsToGain(float db) noexcept
{
    return std::pow(10.0f, db / 20.0f);
}

inline float fastSaturate(float x) noexcept { return std::tanh(x); }

// White noise, LCG. Deterministic per seed; no rand(), no state outside the
// engine that owns it.
struct NoiseSource
{
    uint32_t state = 0x12345678u;

    float next() noexcept
    {
        state = 1664525u * state + 1013904223u;
        return (static_cast<float>((state >> 9) & 0x7FFFFF) / 4194304.0f) - 1.0f;
    }
};

// Envelope-follower coefficient from a time constant in milliseconds:
// coeff = exp(-1 / (ms * 0.001 * sr)). Attack 0.75-9.75 ms, release 22-242 ms,
// both derived from the clarity parameter (see Vox8Filterbank).
inline float coeffFromMs(float ms, double sampleRate) noexcept
{
    const float samples = std::max(1.0f, ms * 0.001f * static_cast<float>(sampleRate));
    return std::exp(-1.0f / samples);
}

} // namespace vox8
