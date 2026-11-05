# Vox8 Track B — STOI on the fixed corpus (filterbank engine, 128 bands)

Measured 2026-11-05. Renderer: `Tests/RenderCorpus.cpp` (headless,
framework-free, compiles against `Source/DSP` only). Scorer:
`Tests/score_stoi.py` (envelope-lag alignment + ARC-AudioBench STOI).
Renders (WAVs + per-file JSON) live outside the repo at
`~/workspace/vox8-bench/corpus/renders-trackb/` — they are not committed.

## Result

| | Track B (iPlug2) | Track A (JUCE) | diff |
|---|---|---|---|
| Overall (n=20) | **0.5678** | 0.5817 | −0.0139 |
| bdl, male (n=10) | **0.6075** (0.568–0.656) | 0.6136 (0.574–0.664) | −0.0061 |
| slt, female (n=10) | **0.5281** (0.484–0.577) | 0.5499 (0.507–0.597) | −0.0218 |

The two tracks agree closely: the male speaker is within 0.006 and
every male file is within 0.014. The female speaker is systematically
lower by 0.017–0.030 on all 10 files — a real, small divergence, not
noise (see "Why Track B is lower on slt" below). The paper's
conclusion is the same on either number: a 128-band filterbank
vocoder at a fixed C4 carrier scores STOI ~0.57–0.58 on this corpus.

Per-file STOI (Track B):

| Sentence | bdl | slt |
|---|---|---|
| arctic_a0104 | 0.6082 | 0.5317 |
| arctic_a0106 | 0.5727 | 0.5420 |
| arctic_a0108 | 0.6028 | 0.5165 |
| arctic_a0111 | 0.6557 | 0.4840 |
| arctic_a0115 | 0.5847 | 0.5455 |
| arctic_a0125 | 0.5682 | 0.5001 |
| arctic_a0126 | 0.6395 | 0.5770 |
| arctic_b0011 | 0.5857 | 0.5638 |
| arctic_b0016 | 0.6069 | 0.5229 |
| arctic_b0019 | 0.6504 | 0.4970 |

Mean per-band scores, overall (Track B vs Track A):

| Band | Centre | Track B | Track A | diff |
|---|---|---|---|---|
| 1 | 150 Hz | 0.3371 | 0.3384 | −0.0013 |
| 2 | 189 Hz | 0.5053 | 0.5933 | −0.0880 |
| 3 | 238 Hz | 0.6620 | 0.6605 | +0.0016 |
| 4 | 300 Hz | 0.5962 | 0.5943 | +0.0019 |
| 5 | 378 Hz | 0.3948 | 0.4622 | −0.0675 |
| 6 | 476 Hz | 0.6901 | 0.6921 | −0.0020 |
| 7 | 600 Hz | 0.7048 | 0.7076 | −0.0028 |
| 8 | 756 Hz | 0.6961 | 0.6987 | −0.0026 |
| 9 | 952 Hz | 0.7162 | 0.7209 | −0.0048 |
| 10 | 1200 Hz | 0.6794 | 0.6840 | −0.0047 |
| 11 | 1512 Hz | 0.5744 | 0.5788 | −0.0044 |
| 12 | 1905 Hz | 0.5037 | 0.5071 | −0.0034 |
| 13 | 2400 Hz | 0.4252 | 0.4298 | −0.0045 |
| 14 | 3024 Hz | 0.4824 | 0.4949 | −0.0125 |
| 15 | 3810 Hz | 0.5487 | 0.5637 | −0.0150 |

13 of 15 bands agree within 0.015. The whole gap lives in bands 2
(189 Hz, −0.088) and 5 (378 Hz, −0.068) — the bands where a female
voice's fundamental (~190–220 Hz) and its neighbourhood sit.

## Render settings (identical to Track A, for comparability)

Vocoder mode, 128 bands, fixed carrier MIDI 60 (C4, 261.63 Hz),
carrierBlend 1.0 (pure internal carrier), mix 1.0, clarity 0.55,
quality 0.75, air 0, transientProtect 0, formantShift 0, output 0 dB,
44.1 kHz, 512-sample blocks, mono input duplicated to both channels,
left channel taken as output. No setting differs from Track A's
render protocol. The clean reference is the resampled 44.1 kHz input
(exactly what the DSP received). The iPlug2 shell output is
bit-identical to this standalone engine (Phase 2 shell test, max diff
0.0), so rendering through the core here is rendering through the
shell's DSP.

Corpus: the fixed corpus (`~/workspace/vox8-bench/corpus/CORPUS.md`) —
10 CMU ARCTIC sentences, speakers bdl + slt, same prompts both
speakers. (Documented deviation: ARCTIC contains no Harvard
sentences; the corpus is 10 ARCTIC sentences instead.)

## Alignment

Waveform cross-correlation is meaningless for a vocoder (Track B:
corr 0.010–0.104, same as Track A's 0.01–0.10 — the output is a
carrier modulated by the input envelope, not a shifted copy of the
input). Alignment used the 10 ms RMS-envelope lag, per file, before
scoring: Track B lags +20 to +30 ms on all 20 files (median +20 ms),
corr 0.88–0.96 — the same range Track A found. Each degraded file
was shifted by its measured envelope lag
(`*_vocoded_aligned.wav` in the renders dir).

## Verification

- Identity sanity: clean vs clean scores STOI 1.0, all 15 bands 1.0
  (Bench `stoi`, same files the renders use).
- Renderer builds clean (`g++ -std=c++17 -O2 -Wall -Wextra`,
  no warnings after the build-command comment fix).
- All 20 files rendered (peaks 0.096–0.174, no clipping, no silence).
- Bench: ARC-AudioBench HEAD `a4fdd5a` (includes the STOI commit
  `95fa999`), pulled fresh before the run. All 20 files scored,
  frames kept = frames total on every file (no frames dropped).
- Track A's renders were not touched: `renders/stoi_summary.json`
  still reads 0.5817, and Track B's renders + summary live in the
  separate `renders-trackb/` directory.

## Why Track B is lower on slt (investigation so far)

The DSP formulas are the same on both tracks — same band layout
(55 Hz–18.5 kHz log, 128 bands), same RBJ transfer functions
(Q=0.707), same envelope followers (attack 4.8 ms / release 121 ms
at clarity 0.55), same envGain/drive, same carrier blend
(0.44 saw / 0.26 sine / 0.18 pulse + sub). What the measurements
show:

- The clean references are BIT-IDENTICAL between tracks
  (same linear resampler), so the inputs match exactly.
- The vocoded outputs correlate 0.998+ (RMS diff ~0.0015–0.0019
  against RMS ~0.027–0.034, best sample lag 0). The engines are
  close, not identical.
- The output difference is concentrated below 250 Hz: in the
  50–150 Hz band the difference energy is ~3x the signal energy
  there, and 150–250 Hz ~0.9–1.1x — while above 250 Hz the
  difference is only 3–6% of the signal per band. (The sub-250 Hz
  signal energy itself is tiny, ~0.02% of the total, so this is a
  small absolute difference that lands exactly on STOI bands 2
  and 5 for the female speaker, whose F0 sits there.)
- Ruled out: biquad coefficient precision. Rebuilding the
  renderer with double-precision coefficient computation changed
  the output not at all (RMS diff vs Track A identical to 6
  decimals).
- Ruled out as a design difference: JUCE's
  `IIR::Coefficients::makeLowPass/makeHighPass` (tan-based
  bilinear design) is algebraically identical to the RBJ cookbook
  used here — verified term by term against JUCE 8.0.5 source —
  and JUCE's `IIR::Filter` is the same Direct Form II transposed
  structure as `Biquad::process`. The remaining difference is
  therefore float evaluation order / state rounding somewhere in
  the low-band filters, not a different transfer function.
- Not yet localised to the modulator path vs the carrier path:
  a probe build of Track A's renderer from read-only copies
  (JUCE 8.0.5, built outside both repos) is in progress to A/B
  the engines on synthetic probes (200 Hz sine, white noise) and
  pin the divergence down. This doc will be updated when it
  lands. No engine change follows from this STOI gap alone: the
  gap is 0.014 overall, the formulas agree, and chasing it by
  retuning the shipped engine would be fitting to Track A's
  float rounding rather than to the paper.

## Reduced band counts (32/16 — the saturation question)

Not run yet, and not a blocker. The engine does not support it
straightforwardly: `kNumBands` is a compile-time constant baked
into the band layout, gains and filter arrays, so a 32/16-band run
needs a rebuild with a different constant (a scratch experiment,
not a shipped change). Follow-up: build the renderer with
`kNumBands` set to 32 and 16 in a scratch copy, render the same
corpus, score the same way, and compare against 0.5678 @ 128.

## What is next

- Localise the slt low-band divergence with the JUCE A/B probe
  (in progress); update this doc with the finding.
- Reduced-band runs (32/16) as a scratch experiment, same corpus
  and scoring.
- Ghost STOI stays out until the Ghost resynthesis redesign lands
  (owed decision; the broken resynthesis would make the number
  meaningless).
