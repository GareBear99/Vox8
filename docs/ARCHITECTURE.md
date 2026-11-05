# Vox8 Track B — Architecture Notes

Track B rebuilds Vox8 fresh in iPlug2. The reference is FreeVox8
(`GareBear99/FreeVox8`, HEAD `02eedfe`, 4 commits, GPL-3.0), read from the
read-only clone at `~/workspace/FreeVox8-ref/`. The specification is the
Vox8 paper (`~/workspace/vox8-paper.md`, Draft v0.1) plus the three study
reports (`~/workspace/vox8-*-research.md`). The iPlug2 approach follows
`~/workspace/therum-iplug2-plan.md` (framework-free DSP core first, prove
it in iPlug2, parameters + state, UI last).

## What Vox8 is

A spectral vocoder, NOT a singing synth. It processes audio; it does not
synthesise singing. Modulator (voice/synth/drum) + carrier (synth/MIDI/
sidechain) → modulator's spectral envelope imposed on the carrier.

## Step 1 (DONE 2026-11-05): framework-free DSP core, filterbank engine

`Source/DSP/Vox8Core.h` (band layout, RBJ biquads, helpers),
`Vox8Params.h` (shared parameter contract), `Vox8Filterbank.h` (Low
Latency 128 engine). Tests: `Tests/test_filterbank.cpp`, 107,743 checks,
0 failures; `make check` clean (0 framework includes, 0 heap allocation
in the core headers). The core is plain C++17, `float` sample type (kept
from the reference; iPlug2 defaults to `double` — the shell will decide
whether to widen at the boundary, and the decision will be recorded here
when it is made).

### Rebuild choices (not copies)

- **Filters:** RBJ cookbook biquads (HP + LP per band per path, Q=0.707)
  instead of JUCE `ProcessorDuplicator<IIR::Filter>`. Same transfer
  function as the reference's Butterworth pairs. 512 filter instances per
  channel, built once in `prepare()`, never rebuilt in `process()`.
- **Envelope followers:** asymmetric IIR, `mag = sqrt(band² + 1e-12)`,
  attack 0.75–9.75 ms / release 22–242 ms from clarity. Same as reference.
- **Freeze:** leaky `frozenEnv` (0.9992/0.0008), read when freeze > 0.5.
  Same as reference. Freeze is not subject to the intraframe smear
  (envelope followers track continuously; there is no frame assumption).
- **Carrier:** filterbank blend 0.44 saw / 0.26 sine / 0.18 pulse + sub
  (0.22 sine at half phase), blended with dry via `carrierBlend`. See
  "pending reconciliation" below.
- **Transient Protect — TRACED (2026-11-05, read from `FreeVox8DSP.h`):**
  it is a manual macro, not a detector. `transient = clamp(|dry − prev| ×
  12, 0, 1)`; `transientReturn = dry × transientProtect × transient ×
  0.12` added to the vocoded signal. No Roebel-style detection exists.
  Labelled honestly in code and parameters; do not cite Roebel 2003
  for it. (This answers the paper's §8.6 open item for this engine.)
- **Air:** white noise (LCG), `breath = air × noise × (0.010 + envSum ×
  0.085)`. White, as in the reference; colour is a listening decision.

### Deliberate deviations from the reference

1. **Morph (mode 3) is a REAL morph here.** The reference's mode 3 is a
   blend (`0.58 × vocoded + 0.42 × tanh(dry + vocoded × …)`). This engine
   runs two modulator envelopes (main input + sidechain) and interpolates
   per band, `e = (1−α)·e₁ + α·e₂`, imposed on one carrier; `α` is the
   new `morph` parameter (default 0.5). Verified by test: morph=0 puts
   the output at modulator 1, morph=1 at modulator 2, midpoint is a
   hybrid (both present, total below the sum of the endpoints — not a
   crossfade). The second envelope is only analysed when mode == Morph,
   so normal modes pay no extra CPU. When no sidechain is present, Morph
   falls back to envelope 1 (documented in the header).
2. **Mask (mode 1) is per-band ducking here.** The reference's filterbank
   Mask was a broadband reduction driven by the summed envelope
   (`reduction = envSum × maskDepth × (2.6 + quality)` applied to dry) —
   that ducks the whole carrier, which is the pumping failure the golden
   tests forbid. This engine uses the Ghost engine's formula per band:
   `y = car × (1 − min(0.96, e × maskDepth × 3.2))`, summed with band
   gains. Verified by test: the band the modulator occupies is ducked to
   28% while a far band is untouched (102%).
3. **Sidechain routing, stated explicitly:** sidechain present ⇒ it IS
   modulator 1 (reference behaviour); the main input becomes modulator 2
   for Morph. Sidechain absent ⇒ modulator 1 = dry input.

### Measured this step (all on this VM, 48 kHz unless stated)

- Silence in → silence out to the 1e-12 denormal-guard floor: peak
  1.09e-06 (modes 0/2/3). Mask mode passes the carrier when the modulator
  is silent — correct Mask behaviour, not a failure.
- No NaN/Inf at parameter extremes (clarity 0, formant ±24 st, ghost 1,
  air 1, quality 1, +24 dB out, morph 1, width 2, transient 1, mask 1)
  with loud DC input; output stays ≤ 1.01 (tanh saturation).
- Envelope transfer: burst RMS on/off ratio > 5× (clarity 1).
- Freeze: output 0.3 s after the modulator stops is still > 50% of live
  level with freeze on; < 20% with freeze off.
- Formant shift: output spectral centroid (440 Hz harmonics) −12 st →
  821 Hz, 0 → 998 Hz, +12 st → 1247 Hz. Envelope remapping moves the
  resonances without moving the pitch.
- Determinism: two runs bit-identical.
- Block sizes 1–2048 and sample rates 44.1–192 kHz: finite throughout.
- CPU (bench, 1 stereo instance, spoken-like input, median % of budget,
  stable across two runs): 44.1 kHz ≈ 34%, 48 kHz ≈ 37%, 96 kHz ≈ 74%
  — flat per sample (~7.6 µs per stereo sample on this VM), as expected
  for 512 biquads/channel/sample. Heavy but within budget on medians at
  all rates here. Means and worst-case are polluted by VM scheduling
  stalls (worst blocks 24–101 ms) and are not quoted as engine numbers.
  The 2018 Intel Mac mini numbers are still to be measured, and the p95
  on a quiet machine is what decides dropout-free performance. Optimisation
  (SIMD, cheaper filters, band reduction) stays on the list.

## Pending reconciliation (from Track A — do not guess)

1. **Carrier blend.** The reference's two engines disagree: filterbank
   `makeCarrier` is 0.44 saw / 0.26 sine / 0.18 pulse + sub (this is what
   the repo paper §3.2 states); Ghost `makeCarrier` is 0.46 saw / 0.32
   sine / 0.18 pulse, ×0.72, + 28% dry. The paper's §4.6 "discrepancy"
   quotes the Ghost numbers as if they were the filterbank's — they are
   the Ghost engine's. This track keeps each engine's own blend until
   Track A reconciles to one canonical set.
2. **Formant-shift bin scale.** Filterbank: 8 bins/octave. Ghost: 10
   bins/octave. This engine keeps 8 until Track A reconciles.
3. **STOI corpus + test signals.** Track A is establishing the fixed
   corpus (10 Harvard/IEEE sentences, male + female) and test signals.
   This track runs the SAME tests when they land; no corpus is invented
   here and no STOI number is claimed yet.

## iPlug2 pin

Recorded, not yet verified by a build: iPlug2 `master` at commit
`d82b2e99` (the commit current when the iPlug2 research was written,
2026-11-05; 13,444 commits, 2 releases / 7 tags — pin a commit, per the
Therum plan). The pin is verified at Phase 2, when the shell is built:
VST3 + AU + Standalone on macOS (Intel, Gary's 2018 Mac mini — minimum
OS to be checked against the pinned commit, not assumed), pluginval 10.
Graphics backend: NanoVG/Metal (default, light) unless Phase 2 shows
otherwise; Skia only if shader-driven UI is wanted. Sample type at the
shell boundary: undecided (core is `float`).

## NOT being built in this track

- 256-band tier or intraframe (WASPAA 2001 β/λ) correction — ProVox8 or
  free is undecided by Gary; the freeze pilot is Track A's step 2 and
  decides whether the correction has audible value. Do not implement it
  here unless the pilot says so.
- Singing synth — Vox8 processes audio; a singing synth would be a
  separate product, undecided.
- Full phase-vocoder backend (paper §6 rank 9) — after the freeze pilot.
- Three-way Moliner morph (sines/transients/noise) — ProVox8-scale.

## Next steps

1. Step 2: Spectral Ghost 1024 engine (`Vox8Ghost.h`) — Goertzel-style
   `analyseBins()` (with the ~11M transcendental-calls/sec/channel cost
   noted, not hidden), frozen envelope array, OLA resynthesis from the
   carrier ring, Ghost/Mask/Morph mode formulas per the reference, Air
   `t²·0.018` weighting. Same test discipline + freeze-blur test
   (sustained vibrato vowel, peak-bin vs adjacent-bin energy).
2. Phase 2: iPlug2 shell — prove the core in the `IPlugInstrument`
   pattern, parameters + state (frozen indices), minimal UI, VST3 + AU +
   Standalone, pluginval 10.
3. Measurements when Track A's corpus lands: STOI harness, latency/CPU
   protocol per engine, competitor A/B.
