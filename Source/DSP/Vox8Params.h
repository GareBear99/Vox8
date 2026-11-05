// Vox8Params.h — the shared parameter contract for Vox8.
//
// One parameter struct for both engines (filterbank and Ghost). The iPlug2
// shell maps its IParams onto this struct; the DSP never sees the framework.
// Parameter indices in the shell are frozen before the first release; this
// struct is the stable contract underneath them.

#pragma once

namespace vox8 {

enum class Mode : int
{
    Vocoder = 0,
    Mask = 1,
    Ghost = 2,
    Morph = 3
};

struct Parameters
{
    Mode mode = Mode::Vocoder;
    float mix = 1.0f;             // wet/dry mix, 0..1
    float carrierBlend = 0.65f;   // internal carrier vs dry input, 0..1
    float maskDepth = 0.5f;       // Mask mode depth, 0..1
    float clarity = 0.55f;        // envelope speed: attack 0.75-9.75 ms,
                                  // release 22-242 ms
    float formantShift = 0.0f;    // semitones; envelope remap, NOT a
                                  // spectral shift (that would be the
                                  // chipmunk bug)
    float ghost = 0.0f;           // Ghost mode intensifier, 0..1
    float air = 0.15f;            // noise excitation for fricatives, 0..1
    float freeze = 0.0f;          // >0.5 holds the frozen envelope
    float transientProtect = 0.35f; // manual macro: dry transient bleed,
                                  // 0..1. NOT a detector (no Roebel-style
                                  // detection exists yet); label it honestly.
    float stereoWidth = 1.0f;     // 0..2, 1 = untouched
    float quality = 0.75f;        // drive / macro, 0..1
    float outputGainDb = 0.0f;    // dB
    float morph = 0.5f;           // Morph amount, 0..1: 0 = modulator 1,
                                  // 1 = modulator 2, 0.5 = hybrid.
                                  // (The reference had no morph parameter;
                                  // its mode 3 was a blend. See
                                  // docs/ARCHITECTURE.md.)
    int engineMode = 0;           // 0 = Low Latency 128 filterbank,
                                  // 1 = Spectral Ghost 1024
};

} // namespace vox8
