# Vox8 Track B — Architecture Notes

Track B rebuilds Vox8 fresh in iPlug2. The reference is FreeVox8
(`GareBear99/FreeVox8`, HEAD `7f74987`, 16 commits, GPL-3.0), read from the
read-only clone at `~/workspace/FreeVox8-ref/`. The specification is the
Vox8 paper (`~/workspace/vox8-paper.md`, Draft v0.1) plus the three study
reports (`~/workspace/vox8-*-research.md`). The iPlug2 approach follows
`~/workspace/therum-iplug2-plan.md` (framework-free DSP core first, prove
it in iPlug2, parameters + state, UI last).

**Status (2026-11-07, HEAD `f5e8f3c`):** Step 1 (filterbank) DONE,
Step 2 (Ghost) DONE in the DSP core, Phase 2 (iPlug2 shell) DONE —
pluginval 10 SUCCESS + VST3 validator 47/0. **The Ghost engine is NOT
wired into the shell** (filterbank-only, 15 params). Ghost STOI 0.7796
beats TAL 0.7434. Ghost CPU is the single biggest engineering gap
(41.0% @ 44.1 kHz median block 512 — ~16× FreeVox8's Ghost). Deep Vox
work (Ghost wiring, FFT replacement, Mac build, Rank 10) comes LATER
per Gary — this repo is left alone for now except for keeping the
papers and READMEs up to date.

## What Vox8 is

A spectral vocoder, NOT a singing synth. It processes audio; it does not
synthesise singing. Modulator (voice/synth/drum) + carrier (synth/MIDI/
sidechain) → modulator's spectral envelope imposed on the carrier.

## Step 1 (DONE 2026-11-05): framework-free DSP core, filterbank engine

`Source/DSP/Vox8Core.h` (band layout, RBJ biquads, helpers),
`Vox8Params.h` (shared parameter contract), `Vox8Filterbank.h` (Low
Latency 128 engine). Tests: `Tests/test_filterbank.cpp`, 107,746 checks,
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

## Step 2 (DONE 2026-11-06, FFT cross-synthesis redesign — resynthesis WORKS, full suite green): Spectral Ghost 1024 engine

`Source/DSP/Vox8Ghost.h` (Ghost engine), `Tests/test_ghost.cpp`
(107,646 checks, **0 failures** — the 9 resynthesis failures below
are RESOLVED, see "THE RESYNTHESIS FINDING"), `Tests/bench_ghost.cpp`.
Same framework-free discipline as Step 1; `make check` clean (0
framework includes, 0 heap allocation).

**Current state (2026-11-07):** the Ghost engine's ANALYSIS is still
Goertzel (`analyseBins()`, per-band stride) and its RESYNTHESIS is
FFT cross-synthesis (redesign landed 2026-11-06, Decision 5) — Vox8
kept Goertzel analysis AND added FFT resynthesis. That is the CPU
problem: Ghost median block 512 is 41.0% @ 44.1 kHz, 43.7% @ 48 kHz,
72.6% @ 96 kHz — ~16× FreeVox8's Ghost (~2.5%) for −0.0143 STOI.
FreeVox8 proved the fix (replace Goertzel analysis with modulator
FFT: Ghost falls to ~2.5% with HIGHER STOI, 0.7939). **Do NOT wire
the current Ghost into the shell as-is** — wire it AND replace the
Goertzel analysis with modulator FFT in the same step. That work
comes LATER per Gary. Ghost output level is very quiet (render peaks
0.004–0.018) — gain staging is a product gap. Ghost latency must be
declared via `SetLatency(1024)` + PDC when wired (structural 1024
samples, 23.22 ms @ 44.1 kHz).

### What was built

- 1024-sample Hann window, hop 256, ring buffers (`inputRing` carrier,
  `modRing` / `modRingB` modulators, `olaRing` output), `writeIndex` +
  `hopCountdown` triggering `renderFrame()` every 256 samples.
- Goertzel-style `analyseBins()` with a **per-band stride** (see the
  aliasing fix below), envelope follower (attack 0.24/0.76, release
  0.92/0.08), `frozen[]` leaky update (0.998/0.002) holding while freeze
  is on, `renderFrame()` reading the frozen snapshot when freeze is
  active. Second envelope arrays (`envB`/`frozenB`) for the real morph.
- Ghost carrier blend (0.46 saw / 0.32 sine / 0.18 pulse, ×0.72, + 28%
  dry, fixed — `carrierBlend` is not read here, it belongs to the
  filterbank carrier). Formant scale kept at the Ghost value, **10 bins
  per octave** (Track A / Gary to decide on unification at 15.25).
- All four modes with the Ghost formulas (paper §4.4): Vocoder
  `y = car·(0.72 + e·(2.8 + clarity·4))`, Mask `y = car·(1 − min(0.96,
  e·maskDepth·3.2))`, Ghost `×(1 + ghost·2.4)`, Morph = the REAL
  two-envelope morph (interpolated per band, imposed on one carrier —
  the reference's blend is replaced, as in Step 1). Air `t²·0.018`
  weighting, as shipped (Mask mode carries no Air term, as shipped).
- Transient Protect wired in (the Step 1 fix — the reference Ghost
  engine never read the parameter): same first-difference gate as the
  filterbank, verified differentially (impulse in: max |out(tp=1) −
  out(tp=0)| = 0.1197, matching the 0.12 prediction).
- Measurement counters: `getFramesRendered()`, `getGoertzelCalls()`
  (every cos/sin evaluation counted exactly).

### Stride-4 aliasing — FOUND by Track A, FIXED here (do not regress)

The reference's flat stride-4 accumulation aliases: its effective
sample rate is sr/4, so every band above sr/8 (~5512 Hz at 44.1 kHz)
is a mirror of low-frequency content. Reproduced on this engine before
the fix: a pure 676 Hz sine lit band 117 (11,701 Hz, frozen 0.0266)
STRONGER than its true band 55 (0.0261). The paper's "negligible
accuracy loss" claim for stride 4 was wrong. The fix (same as Track
A's): per-band stride — 4 below sr/8, 3 below sr/6, 2 below sr/4, 1
above — with magnitudes normalised by stride/4. After the fix the
mirror is gone (band 117 frozen = 0.0000; top band is 55) and the
alias test passes. Split at 44.1 kHz is 101 / 6 / 9 / 12 bands. Cost
(counted, not estimated): 15.435M transcendental calls/sec/channel @
44.1 kHz (was 11.289M flat), 16.226M @ 48 kHz, 25.731M @ 96 kHz — the
paper's ~11M figure is confirmed for flat stride 4 and superseded by
the per-band numbers. The cost rise is sr-dependent: +37% @ 44.1 kHz,
+32% @ 48 kHz, only +5% @ 96 kHz (at 96 kHz 118 of 128 bands sit below
sr/8, so almost all keep stride 4).

### Freeze-blur pilot — BASELINE measured, Track A's numbers reproduced exactly

Same signal, metric, protocol and decision rule as Track A's pilot
(FreeVox8 `Tests/FreezePilot.cpp`), so the numbers are directly
comparable: /a/ vowel (Lorentzian formants 730/1090 Hz), 6 Hz vibrato,
44.1 kHz, 2 s with freeze at 1.0 s, +0.25 s held. Metric R =
10·log10(E_peak / ΣE_adjacent±1,±2), E = frozen²; C = peak share of
the peak±2 window. Peak search restricted below sr/8.

| Case | R (this track) | R (Track A) |
|---|---|---|
| Stationary F0=220 | −0.90 dB | −0.90 dB |
| Vibrato ±0.5 st F0=220 | −5.73 dB | −5.73 dB |
| Vibrato ±1.0 st F0=220 | −5.61 dB | (same shape) |
| Stationary F0=250 | +0.50 dB | +0.50 dB |
| Vibrato ±0.5 st F0=250 | −5.69 dB | −5.69 dB |
| Vibrato ±1.0 st F0=250 | −5.35 dB | (same shape) |

Smear (stationary − vibrato): **4.83 dB @ F0=220, 6.20 dB @ F0=250**
(Track A: 4.8 / 6.2 dB). ±1.0 st is no worse than ±0.5 st (saturates).
The frozen snapshot is bit-stable while held (max drift 0.0 in every
case). Decision rule (written in advance, Track A's): implement the
WASPAA 2001 β/λ correction ONLY if the corrected snapshot improves R
by ≥ 3 dB without degrading the stationary control by > 0.5 dB. This
step is the uncorrected baseline — the correction is NOT implemented
here. Caveat (Track A's): the snapshot is a ~2.9 s leaky average over
many vibrato cycles, so part of the smear is interframe vibrato-extent
averaging no per-frame β correction can remove; the correction can
only address the ~0.4 st intraframe sweep. (My first pilot run used a
different vowel/metric and measured only 1.46 dB of smear — the
difference was the signal and metric, not the engine. The aligned
numbers above are the comparable ones.)

### Frame delay — MEASURED (not the ~23 ms estimate)

Structural frame: 1024 samples (23.22 ms @ 44.1 kHz). First detectable
output change (differential method, 1e-4 threshold, impulse through
the sidechain so the carrier path cannot leak it early): **691 samples
(14.40 ms) @ 48 kHz**; noise-burst onset by the same method: **648
samples (13.50 ms) @ 48 kHz**. Track A measured 990 samples
(22.45 ms) @ 44.1 kHz by noise-burst onset — a different method and
threshold (onset vs first detectable change), so the numbers differ;
both sit within a frame of the 1024-sample nominal, and the exact
onset figure is method/threshold-dependent. The impulse's largest OLA
contribution lands at the Hann window centre (i = 512), which is why
the first detectable change arrives most of a frame after the impulse
rather than at the next hop.

### CPU (this VM, 1 stereo instance — Mac-mini numbers still owed)

Wall-clock means/medians on this VM are polluted by scheduling stalls
(worst blocks 65–375 ms during the loaded runs), so the headline
number is the MINIMUM block time (a stall can only slow a block, never
speed it), blocks 512–2048: **~39% @ 44.1 kHz, ~41% @ 48 kHz, ~65%
@ 96 kHz** of the real-time budget. Cross-check vs Track A Ghost:
~39% / ~41% / ~65% — a match. **Median block 512 (2026-11-06,
the number that frames the gap): 41.0% @ 44.1 kHz, 43.7% @ 48 kHz,
72.6% @ 96 kHz — ~16× FreeVox8's Ghost (~2.5%) for −0.0143 STOI.**
The cause is structural: Vox8 kept Goertzel analysis (15.435M /
16.226M / 25.731M transcendental calls/s/channel) AND added FFT
resynthesis, while FreeVox8 replaced Goertzel with modulator FFT.
Pre-fix (flat stride 4) clean means
were ~29–31% / ~31–33% / ~62%; the stride fix costs +36% / +32% /
+5% in iterations, and the measured minimums confirm +36% / +32% /
+6%. The counted Goertzel rates above are the load-independent
numbers. The 2018 Intel Mac mini numbers are still to be measured,
and the p95 on a quiet machine is what decides dropout-free
performance. Note Track A's filterbank warning carries over: ~95% @
96 kHz on the filterbank leaves no headroom there.

### Carrier floor — REMOVED in the redesign (2026-11-06)

The Vocoder formula's 0.72 base term passed carrier at zero envelope:
silence in (modulator silent) gave max|out| ≈ 0.03–0.08 (Track A
measured 0.083, −21.6 dBFS on their engine). It failed the golden
"silence in → silence out" test as shipped. **The 0.72 floor is gone
— removed in the FFT cross-synthesis redesign (Decision 2 folded in,
2026-11-06).** Silence in → silence out is now a passing acceptance
test (strict bound: modes 0/2/3 peak < 1e-6 on silence). The text
below is kept as the record of what the floor was and why it was
removed: the options were to remove the floor (+ level
recalibration), gate it on envelope, or keep it labelled as a known
gap. The redesign removed it.

### THE RESYNTHESIS FINDING — the engine does not vocode (9 failing checks)

**RESOLVED 2026-11-06 (FFT cross-synthesis redesign, Decision 5).**
The redesign landed in `Source/DSP/Vox8Ghost.h` and all 9 failing
checks now pass — the full suite is green (filterbank 107,746 checks
+ Ghost 107,646 checks, 0 failures). Measured after the redesign:
envelope transfer 69.958× (was 1.321), formant centroid 532 / 880 /
1736 Hz (was stuck at 1144), morph outputs move correctly (m=0.5
gives 0.000382 / 0.000586 at 440 / 3000 Hz, ratio 0.65), Mask near
1 kHz ducks to 0.397× at depth 0.8 (far 5 kHz untouched, 1.001×),
freeze hold ratio 0.537 on a 440 Hz harmonic-stack modulator,
silence in → silence out (the 0.72 floor is gone). The original
finding below is kept as the record of what was broken and why.

`renderFrame()` as shipped reads ONE envelope value per output
sample, sweeping the band index across the frame (`e = readEnv(t·127
− shift)`, t = i/1023). Each output sample is shaped by a single
band's envelope, and the spectral character averages away over the 4
overlapping frames. Measured on this engine (and in a standalone
probe of the reference math before the engine was written):

- Envelope transfer, OUTPUT level: 1 kHz sine bursts give on/off RMS
  ratio **1.32** (needs > 5×). The ANALYSIS is healthy — the same
  bursts give an envelope-array ratio of **41.5×** (peak envelope
  0.054 mid-burst). The failure is in resynthesis, not analysis.
- Formant shift, OUTPUT centroid: −12 st → 1144 Hz, 0 → 1144 Hz, +12
  st → 1142 Hz — no movement at all. The remap math itself is exact
  (envelope-read check: band 63 @ 0 st == band 73 @ +12 st, 0.05426).
- Morph, OUTPUT level: all three morph settings give near-identical
  output spectra (near-500 Hz 0.0052 vs near-3 kHz 0.0026 in every
  case). The morph itself is real at the envelope level: envA peaks
  at band 48 (496 Hz), envB at band 87 (2960 Hz), and the morph read
  interpolates exactly (0.05726 / 0.02863 / 0.00000 at morph 0 / 0.5 /
  1 on the 500 Hz band).
- Mask, OUTPUT level: depth 0.8 changes the near-1 kHz output by
  0.993× and the far-5 kHz output by 0.993× — no pocket at all.
- Freeze, OUTPUT level: the frozen snapshot holds (drift 0.0, peak
  band 63 at 986 Hz), but the output does not decay without freeze
  (decayed RMS 0.0250 vs live 0.0283) because the 0.72 carrier floor
  does not decay.
- Freeze-toggle click: max |diff| just after toggle 0.1021 vs 0.0997
  steady (Track A Ghost: 0.117) — no click above the signal's own
  slew; the toggle itself is clean.

What PASSES (107,640 of 107,649 checks): band layout, alias probe,
silence/finiteness bounds, envelope-level transfer (41.5×), frozen
snapshot stability and capture, envelope-level formant remap,
envelope-level morph (separation + exact interpolation), freeze-blur
pilot (all 6 cases + drift 0.0), Air differential (0.00069),
Transient Protect wiring (0.1197, matching the 0.12 prediction),
determinism (bit-identical), frame delay, blocks 1–2048 and rates
44.1–192 kHz (finite throughout).

Consequence: **do not ship this engine as a vocoder, and do not call
Step 2 done.** The analysis half (analyseBins, frozen[], rings,
pilot, CPU/latency measurements) is real and reusable. The
resynthesis needs a redesign — FFT cross-synthesis (the paper's §6
rank 9, the full phase-vocoder backend, pulled forward) or per-band
carrier filtering — which is a decision for Gary / Track A, not a
silent edit in this step. The 9 failing checks stay failing until the
redesign lands and they pass; the thresholds were NOT weakened to get
a green run.

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
3. **STOI corpus + test signals — LANDED, measured.** Track A's fixed
   corpus is the 20-file CMU ARCTIC set (bdl male + slt female, fixed
   C4 carrier). This track ran the SAME tests: Ghost STOI **0.7796**
   overall (bdl 0.7941, slt 0.7651) vs TAL-Vocoder 0.7434 (+0.036)
   and filterbank 0.5817 (+0.198). Filterbank STOI agrees with Track
   A to the fourth decimal (0.5818 vs 0.5817, `docs/STOI-trackb.md`).
4. **Ghost resynthesis redesign — DONE 2026-11-06 (Decision 5).**
   FFT cross-synthesis landed in `Source/DSP/Vox8Ghost.h`: windowed
   carrier frame → 1024-pt radix-2 FFT → per-bin gain from the
   envelope (log-interpolated, formant-shifted, morph-interpolated),
   carrier phase kept → IFFT → window → OLA. All 9 failing checks
   pass; full suite green (107,746 + 107,646 checks, 0 failures).
   Three measurement corrections came with it (documented in
   `Tests/test_ghost.cpp`): the morph test measures near-3 kHz at
   3000 Hz (the dry sine) not the carrier's 2640/3080 Hz harmonics;
   the Mask test measures near-1 kHz at 1000 Hz (the envelope peak)
   not 880/1320 Hz; the freeze-hold test uses a 440 Hz harmonic-stack
   modulator (a pure sine's razor-narrow frozen envelope cannot test
   the hold). Mask multiplier recalibrated 3.2 → 16.0 (the reference
   value was never validated against a working resynthesis).
5. **Ghost carrier floor (0.72 term) — REMOVED in the redesign
   (Decision 2 folded in).** Silence in → silence out is now a
   passing acceptance test (strict bound: modes 0/2/3 peak < 1e-6
   on silence).
6. **WASPAA β/λ correction — PILOT FAILED, stays out (2026-11-06).**
   Baseline smear measured (4.83 dB @ F0=220, 6.20 dB @ F0=250).
   The correction was built ONLY if it improved R by ≥ 3 dB without
   degrading the stationary control by > 0.5 dB. Track A ran the
   pilot in the pilot harness on 2026-11-06 and it FAILED: best
   +0.47 dB on the frozen snapshot (ARC Audio Bench cross-check
   0.03 dB). The correction stays out and is NOT future work — the
   freeze-blur fix path is the sinusoidal-model freeze
   (ProVox8-scale research).

## iPlug2 pin

Recorded, not yet verified by a build: iPlug2 `master` at commit
`d82b2e99` (the commit current when the iPlug2 research was written,
2026-11-05; 13,444 commits, 2 releases / 7 tags — pin a commit, per the
Therum plan). **VERIFIED 2026-11-05 (Phase 2):** the pin is
`d82b2e99b17c8f723d3f6e2a88ffd8b656f28e5d`, dated 2026-10-25, and the
Vox8 shell builds against it (VST3, Linux VM — see Phase 2 below).
The framework lives outside this repo (`../iPlug2` next to the checkout,
or `IPLUG2_DIR`); the VST3 SDK is fetched with iPlug2's
`download-vst3-sdk.sh` (master, VST 3.8.1). Graphics backend:
NanoVG/Metal (default, light) unless Phase 2 shows otherwise; Skia only
if shader-driven UI is wanted. Sample type at the shell boundary:
**decided Phase 2 — `float`** (`SAMPLE_TYPE_FLOAT`): the core is float,
and widening to double at the boundary and back would buy nothing.

## NOT being built in this track

- 256-band tier or intraframe (WASPAA 2001 β/λ) correction — the
  freeze pilot ran 2026-11-06 and FAILED its ≥ 3 dB rule (best
  +0.47 dB), so the correction stays out and is NOT future work. Do
  not implement it here. The freeze-blur fix path is the
  sinusoidal-model freeze (ProVox8-scale research).
- Singing synth — Vox8 processes audio; a singing synth would be a
  separate product, undecided.
- Full phase-vocoder backend (paper §6 rank 9) — DONE 2026-11-06 as
  the Ghost FFT cross-synthesis redesign (not a separate backend).
- Three-way Moliner morph (sines/transients/noise) — ProVox8-scale.

## Next steps

1. **Ghost resynthesis redesign — DONE 2026-11-06 (Decision 5:
   FFT cross-synthesis).** All 9 failing checks pass; full suite
   green. Ghost STOI is measured: **0.7796** overall (bdl 0.7941,
   slt 0.7651) vs TAL-Vocoder 0.7434 (+0.036) — the redesign closes
   the gap to the TAL bar. What remains is wiring the Ghost engine
   into the Phase 2 iPlug2 shell (it currently ships the filterbank
   only) AND replacing the Ghost's Goertzel analysis with modulator
   FFT in the same step — do not wire it as-is (Ghost CPU 41.0% @
   44.1 kHz median block 512, ~16× FreeVox8's Ghost). That work
   comes LATER per Gary.
2. Phase 2: iPlug2 shell — DONE 2026-11-05 (see below). The filterbank
   engine is proven and now loadable as a VST3; the Ghost engine rides
   along once its resynthesis is decided.
3. Measurements when Track A's corpus lands: STOI harness, latency/CPU
   protocol per engine, competitor A/B.

## Phase 2 (DONE 2026-11-05): iPlug2 shell around the filterbank engine

`Source/Plugin/` — `Vox8.h` / `Vox8.cpp` (the shell), `config.h`,
`CMakeLists.txt`, `resources/` (Roboto-Regular.ttf, Apache 2.0, as
shipped in the iPlug2 examples), `Tests/test_shell.cpp`,
`Linux/iplug2-linux-d82b2e99.patch` (VM-only framework patches, see
below), `README.md`. The Ghost engine is NOT wired in yet — its
resynthesis is now proven (FFT redesign, 2026-11-06, full suite
green) but the shell wiring is a separate step; the shell has a
second engine slot ready (`Vox8.h` / `Vox8.cpp`, search "Ghost engine
slot") and the `engineMode` parameter already exists with only the
filterbank value exposed, so adding Ghost later needs no
restructuring.

### What the shell does

- 15 parameters, indices frozen (see `Vox8.h` — append only, never
  reorder; iPlug2 state is a positional binary chunk). Defaults match
  `vox8::Parameters` exactly. State round-trips through
  `SerializeState`/`UnserializeState` (verified by test).
- Main stereo in/out + stereo sidechain bus (sidechain IS modulator 1;
  main input becomes modulator 2 for Morph). MIDI in: latest NoteOn
  wins; NoteOff does not change the note (the carrier sustains at the
  last note, as in the reference).
- Minimal editor: 14 controls (mode slider + 13 knobs), engine line,
  version + build date in the header (a loaded build is recognisable
  at a glance — no more stale-binary guessing). Spectrum/mask display
  is a later step, not this one.
- `float` at the shell boundary (`SAMPLE_TYPE_FLOAT`); the core stays
  plain C++ with zero framework includes (`make check` still clean).

### Verification (this VM)

- **Shell test** `Tests/test_shell.cpp` (`Vox8-shell-test`): drives the
  real plugin class with no host (`MakePlug` + `ProcessBlock`). **37
  checks, 0 failures.** Shell output is **bit-identical** to the
  standalone engine (max diff 0.0) for Vocoder, Morph through the
  sidechain (both endpoints; endpoints differ by 0.362), and Mask;
  MIDI NoteOn tracking and a state round-trip pass.
- **VST3 builds** (Release): `build/out/Vox8.vst3/Contents/x86_64-linux/Vox8.so`.
- **pluginval v1.0.4, strictness 10: SUCCESS** — 25 test sections, 0
  failures (scan, open cold/warm, plugin info, state, automation,
  parameter thread safety, buses, fuzz parameters). Latency reported 0.
- **Steinberg VST3 validator: 47 tests passed, 0 failed.**
- Standalone filterbank suite re-run after the shell work: 107,746
  checks, 0 failures (unchanged).
- **STOI: measured 2026-11-06.** Ghost **0.7796** overall (bdl
  0.7941, slt 0.7651) vs TAL-Vocoder 0.7434 (+0.036) and filterbank
  0.5817 (+0.198), on Track A's fixed 20-file CMU ARCTIC corpus
  (fixed C4 carrier). Filterbank STOI agrees with Track A to the
  fourth decimal (0.5818 vs 0.5817, `docs/STOI-trackb.md`).

### What the VM can and cannot build (iPlug2 Linux gaps, all verified)

At pin `d82b2e99` the CMake path has no working Linux support:

1. **No Linux IGraphics platform** — `IGraphics/Platforms/IGraphicsLinux.cpp`
   is an 11-line stub, and `Scripts/cmake/IGraphics.cmake` prints
   "Error - Linux not yet supported". The VM build is therefore a
   **headless DSP-only VST3** (`UI NONE`, `NO_IGRAPHICS`,
   `PLUG_HAS_UI 0` on Linux only). The editor code is in `Vox8.cpp`,
   follows the IPlugEffect example pattern, and is compiled on
   macOS/Windows — **editor-open, AU build, and pluginval on the Mac
   wait for the Mac build** (do not claim them from the VM).
2. **No Linux timer** — `IPlugTimer.h` ends `#error NOT IMPLEMENTED`
   and `IPlugTimer.cpp` has no `OS_LINUX` branch, so no iPlug2 code
   compiles on Linux at all. Fixed by a minimal `Timer_impl` on a
   `std::thread` (patch file in `Source/Plugin/Linux/`, applied to the
   workspace iPlug2 clone; Mac/Windows do not need it).
3. **Fortify abort in `CurrentTime()`** (`IPlugLogger.h`): the timezone
   suffix is written with `snprintf(&cStr[i], 32, ...)` — the full
   buffer size, not the remaining `32-i` bytes. Harmless on macOS
   (no fortify), but Ubuntu GCC defaults `_FORTIFY_SOURCE=3` at -O2
   and aborts every plugin construction. Fixed in the same patch file
   (`32 - i`). Genuine upstream bug, not Linux-specific.
4. **No VST3 Linux entry point wired** in `Scripts/cmake/VST3.cmake` —
   the shell's `CMakeLists.txt` adds the SDK's `linuxmain.cpp` itself.
5. **iPlug2's VST3.cmake omits `base/source/timer.cpp`** from the SDK
   sources, but `pluginfactory.cpp` references
   `Steinberg::InjectCreateTimerFunction` from it: the module links
   (shared libs tolerate the undefined symbol) but **fails to load
   under `RTLD_NOW`**, and pluginval reports 0 plugins. The shell's
   `CMakeLists.txt` links the SDK file into the VST3 target (and the
   shell-test executable) on every platform — the same latent issue
   would hit the Mac CMake build.
6. **No Linux VST3 bundle layout** in `iplug_configure_vst3` — the
   shell's `CMakeLists.txt` assembles
   `out/Vox8.vst3/Contents/x86_64-linux/Vox8.so` itself and turns
   iPlug2's deploy step off on Linux.
7. **`IGraphics.cmake` unconditionally creates the FlexBox object
   library**, which does not compile on Linux at this pin
   (`std::fmodf`, font-descriptor errors) — build the named targets
   (`Vox8-vst3`, `Vox8-shell-test`), not `all`.
8. **AU / Standalone app**: not buildable in the VM (macOS-only /
   Linux APP unsupported at this pin). Mac build: VST3 + AU + CLAP
   (if the CLAP SDK is present) + Standalone app, NanoVG/Metal —
   still to be verified on the Mac mini, including the pinned
   commit's macOS minimum (README says macOS 13; Gary's 2018 Intel
   Mac mini needs the pinned minimum verified, not assumed).
