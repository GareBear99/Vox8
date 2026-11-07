# Vox8

A spectral vocoder, rebuilt in iPlug2. Vox8 processes audio; it does not
synthesise singing. It takes a modulator (a voice, a synth, a drum) and a
carrier (a synth, a MIDI note, a sidechain) and transfers the modulator's
spectral envelope onto the carrier.

This is Track B of the Vox8 build (Gary, 2026-11-05: "both for vox 8"):
Track A finishes the FreeVox8 JUCE repo to a certified v1.0; this track
rebuilds the same product fresh in iPlug2. The FreeVox8 repo
(`GareBear99/FreeVox8`, HEAD `7f74987`) is the reference for the DSP design
— read it, rebuild it in iPlug2, do not copy the JUCE code. A read-only
reference clone lives at `~/workspace/FreeVox8-ref/`; Track A's working
clone is `~/workspace/FreeVox8/` — do not touch it.

**Status (2026-11-07, HEAD `f5e8f3c`):** Step 1 (filterbank engine)
DONE, Step 2 (Ghost engine) DONE in the DSP core, Phase 2 (iPlug2
shell) DONE — pluginval strictness 10 SUCCESS + Steinberg VST3
validator 47/0 (2026-11-05, Linux headless VST3 only). **The Ghost
engine is NOT wired into the shell** — the shell is filterbank-only
(15 params, `engineMode` exposes only "Low Latency 128"). Ghost STOI
0.7796 beats TAL-Vocoder 0.7434. Ghost CPU is the single biggest
engineering gap (median block 512: 41.0% @ 44.1 kHz — ~16× FreeVox8's
Ghost, because Vox8 kept Goertzel analysis AND added FFT
resynthesis); the fix is to wire Ghost into the shell AND replace its
Goertzel analysis with modulator FFT in the same step — do not wire
it as-is. Deep Vox work (Ghost wiring, FFT replacement, Mac build,
Rank 10) comes LATER per Gary — this repo is left alone for now
except for keeping the papers and READMEs up to date.

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
- `Tests/test_filterbank.cpp` — **107,746 checks, 0 failures.**
  Band layout, biquads, silence/finiteness at extremes, envelope transfer,
  freeze hold, formant shift (spectral centroid moves ±12 semitones:
  821 / 998 / 1247 Hz), real two-envelope morph (midpoint is a hybrid,
  not a crossfade), per-band mask (pocket without broadband pumping),
  determinism, block sizes 1–2048, sample rates 44.1–192 kHz.
- `make check` — 0 framework includes, 0 heap allocation in the core.
- `Tests/bench_filterbank.cpp` — CPU micro-benchmark (see below).

**Step 2 — DONE (2026-11-06): Spectral Ghost 1024 engine, FFT
cross-synthesis redesign (Decision 5).** All 9 resynthesis failures
fixed — the full suite is green (filterbank 107,746 checks + Ghost
107,646 checks = 215,392 checks, 0 failures). Ghost STOI 0.7796 beats
TAL-Vocoder 0.7434 (see Measurements below). The Ghost engine lives in
the DSP core (`Source/DSP/Vox8Ghost.h`) but is **NOT wired into the
iPlug2 shell** — the shell ships the filterbank only; wiring is the
pending step, and it comes LATER per Gary.

**Phase 2 — DONE (2026-11-05): iPlug2 shell around the filterbank
engine.** VST3 (Linux headless, `NO_IGRAPHICS`), pluginval v1.0.4
strictness 10 **SUCCESS** (25 sections, 0 failures) + Steinberg VST3
validator **47 passed, 0 failed**. Editor-open, AU build, and pluginval
on the Mac wait for the Mac build (do not claim them from the VM).

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

## Measurements (measured, 2026-11-05/06 — not targets)

- **STOI (fixed 20-file CMU ARCTIC corpus, fixed C4 carrier):**
  Ghost **0.7796** overall (bdl 0.7941, slt 0.7651) vs TAL-Vocoder
  0.7434 (+0.036) and filterbank 0.5817 (+0.198). The Ghost beats TAL;
  the filterbank loses to TAL by 0.16 even tuned.
- **Ghost latency:** first change 690 samples (14.38 ms @ 48 kHz),
  structural 1024 samples (23.22 ms @ 44.1 kHz). Must be declared via
  `SetLatency(1024)` + PDC when Ghost is wired into the shell.
- **Ghost CPU — the single biggest engineering gap (median block 512,
  this VM): 41.0% @ 44.1 kHz, 43.7% @ 48 kHz, 72.6% @ 96 kHz** —
  ~16× FreeVox8's Ghost (~2.5%) for −0.0143 STOI, because Vox8 kept
  Goertzel analysis AND added FFT resynthesis. FreeVox8 proved the
  fix: replace Goertzel analysis with modulator FFT (Ghost falls to
  ~2.5% with HIGHER STOI, 0.7939). Do NOT wire the current Ghost
  into the shell as-is — wire it AND replace the Goertzel analysis
  with modulator FFT in the same step.
- **Ghost output level is very quiet** (render peaks 0.004–0.018 vs
  TAL 0.66–1.04) — gain staging is a product gap.
- **Mask usable range is EMPTY** at the 1 dB pumping bar, in both
  engines (see the paper §7 / FreeVox8 PAPER.md §3.5, §4.4).
- **Freeze blur:** ~5 dB peak-concentration loss on frozen vibrato
  (4.83 dB @ F0=220, 6.20 dB @ F0=250). The WASPAA β/λ correction was
  piloted by Track A and FAILED its ≥ 3 dB rule (best +0.47 dB) — it
  stays out; the fix path is the sinusoidal-model freeze
  (ProVox8-scale research).

Track A (FreeVox8) is the reference for the corpus and test signals;
this track runs the SAME tests so the two tracks' results are directly
comparable. The filterbank STOI agrees to the fourth decimal between
the two tracks (0.5818 vs 0.5817, see `docs/STOI-trackb.md`).

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
