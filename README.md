# Vox8

A spectral vocoder, rebuilt in iPlug2. Vox8 processes audio; it does not
synthesise singing. It takes a modulator (a voice, a synth, a drum) and a
carrier (a synth, a MIDI note, a sidechain) and transfers the modulator's
spectral envelope onto the carrier.

This is Track B of the Vox8 build (Gary, 2026-11-05: "both for vox 8"):
Track A finishes the FreeVox8 JUCE repo to a certified v1.0; this track
rebuilds the same product fresh in iPlug2. The FreeVox8 repo
(`GareBear99/FreeVox8`, HEAD `02eedfe`) is the reference for the DSP design
— read it, rebuild it in iPlug2, do not copy the JUCE code. A read-only
reference clone lives at `~/workspace/FreeVox8-ref/`; Track A's working
clone is `~/workspace/FreeVox8/` — do not touch it.

## Design

Dual-engine, as specified in `~/workspace/vox8-paper.md`:

- **Low Latency 128 filterbank engine** (this step): 128 log-spaced bands,
  55 Hz–18.5 kHz; per-band envelope followers with clarity-dependent
  attack/release; freeze via a leaky frozen-envelope array.
- **Spectral Ghost 1024 STFT/OLA backend** (next step): 1024-sample Hann
  window, 256 hop, 128 log-spaced analysis bands, frozen envelope array,
  OLA resynthesis from the carrier ring (the carrier supplies the phase).
- **Ghost / Morph / Mask bundle:** Vocoder mode (carrier × envelope), Mask
  mode (modulator-driven per-band ducking of the carrier, up to 96%),
  Ghost/freeze (frozen envelope held indefinitely), and a REAL Morph —
  interpolate two modulator envelopes per band
  (`e = (1−α)·e₁ + α·e₂`), imposed on one carrier. The reference's mode 3
  was a blend, not a morph; it is not shipped here.
- **Formant shift** as envelope remapping (moves the vocal-tract resonances
  without moving the pitch; never a spectral shift).
- **Internal MIDI carrier** (saw/sine/pulse/sub blend), **Air / noise
  excitation** for fricatives.
- **Real-time safety** per the FreeEQ8 doctrine: allocation-free hot path,
  no locks, no file I/O, no background threads in the DSP; atomic display
  taps only.
- **Framework-free DSP core:** the DSP in `Source/DSP/` is plain C++ with
  zero framework includes, proven by standalone tests that compile only
  DSP files with no framework on the include path. The iPlug2 shell is the
  thin wrapper around it. The core survives any future framework move,
  including a move to a language of our own.

## Status

**Step 1 — DONE (2026-11-05): framework-free DSP core, filterbank engine.**

- `Source/DSP/Vox8Core.h` — band layout, RBJ biquads, shared helpers.
- `Source/DSP/Vox8Params.h` — the shared parameter contract.
- `Source/DSP/Vox8Filterbank.h` — Low Latency 128 engine, all four modes.
- `Tests/test_filterbank.cpp` — **107,743 checks, 0 failures.**
  Band layout, biquads, silence/finiteness at extremes, envelope transfer,
  freeze hold, formant shift (spectral centroid moves ±12 semitones:
  821 / 998 / 1247 Hz), real two-envelope morph (midpoint is a hybrid,
  not a crossfade), per-band mask (pocket without broadband pumping),
  determinism, block sizes 1–2048, sample rates 44.1–192 kHz.
- `make check` — 0 framework includes, 0 heap allocation in the core.
- `Tests/bench_filterbank.cpp` — CPU micro-benchmark (see below).

**Next:** Step 2 — Spectral Ghost 1024 engine (Goertzel analysis,
frozen envelope array, OLA resynthesis, ~23 ms frame delay), same test
discipline. Then the iPlug2 shell (Phase 2): prove the core in iPlug2's
`IPlugInstrument` pattern, VST3 + AU + Standalone, pluginval 10.

## CPU (this VM, not the reference hardware)

`Tests/bench_filterbank.cpp`, 1 stereo instance, spoken-like input
(1 kHz bursts). Median block time as % of the real-time budget, stable
across two runs (means and worst-case are polluted by this VM's scheduling
stalls — worst blocks of 24–101 ms — and are not quoted as engine numbers):

| sr | block 1 | block 64 | block 512 | block 2048 |
|----|---------|----------|-----------|------------|
| 44.1 kHz | ~34% | ~34% | ~34% | ~34% |
| 48 kHz | ~37% | ~37% | ~38% | ~37% |
| 96 kHz | ~73% | ~74% | ~74% | ~74% |

The cost is flat per sample (~7.6 µs per stereo sample on this VM), as
expected for 512 biquads/channel/sample. The finding: the filterbank engine
as rebuilt is heavy — about a third of a core at 44.1/48 kHz, three
quarters at 96 kHz on this VM — but within budget on medians at all rates
here. The 2018 Intel Mac mini numbers are still to be measured, and the
p95 on a quiet machine is the number that decides dropout-free
performance; this VM cannot produce a clean p95. No CPU claim is made
beyond these measured numbers.

## Measurements (targets, from the paper)

STOI on the fixed corpus at least equal to TAL-Vocoder (free) and Ableton
Vocoder (stock), with no worse consonant-band loss; Low Latency 128 engine
latency within a small multiple of Morphoder's 639 samples; Ghost engine
~23 ms frame delay stated, not hidden; frozen vibrato vowel holds without
audible wobble; morph midpoint sounds like a hybrid, not a crossfade; Mask
pocket without broadband pumping; pluginval strictness 10. Track A is
establishing the corpus and test signals; this track runs the SAME tests
so the two tracks' results are directly comparable. Until Track A's corpus
lands, no STOI number is claimed here.

## Decisions and open questions

See `docs/ARCHITECTURE.md` — the reconciled numbers pending from Track A
(carrier blend, formant bin scale), the deliberate deviations from the
reference (real Morph, per-band Mask), the iPlug2 pin, and what is NOT
being built (no 256-band tier, no intraframe correction, no singing synth).

## Build the tests

```
cd Tests && make test    # build + run the suite
cd Tests && make check   # framework-freedom + no-allocation greps
cd Tests && make bench   # CPU micro-benchmark
```

## Licence

GPL-3.0, same as the FreeVox8 reference.
