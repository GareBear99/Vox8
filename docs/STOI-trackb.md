# Vox8 Track B — STOI on the fixed corpus (filterbank engine, 128 bands)

Measured 2026-11-05, corrected the same day after the measurement found
and fixed a stereo bug in the engine (see "The stereo bug" below).
Renderer: `Tests/RenderCorpus.cpp` (headless, framework-free, compiles
against `Source/DSP` only). Scorer: `Tests/score_stoi.py`
(envelope-lag alignment + ARC-AudioBench STOI). Renders (WAVs +
per-file JSON) live outside the repo at
`~/workspace/vox8-bench/corpus/renders-trackb/` — they are not committed.

## Result (corrected)

| | Track B (iPlug2) | Track A (JUCE) | diff |
|---|---|---|---|
| Overall (n=20) | **0.5818** | 0.5817 | +0.0001 |
| bdl, male (n=10) | **0.6136** (0.574–0.664) | 0.6136 (0.574–0.664) | 0.0000 |
| slt, female (n=10) | **0.5499** (0.484–0.597) | 0.5499 (0.507–0.597) | 0.0000 |

The two tracks agree to the fourth decimal: 17 of 20 files score
identically, and the other three differ by 0.0001 (16-bit render
quantisation). The paper's number for a 128-band filterbank vocoder
at a fixed C4 carrier on this corpus is STOI **0.58**, measured
independently by two implementations.

Per-file STOI (Track B; Track A identical except where noted):

| Sentence | bdl | slt |
|---|---|---|
| arctic_a0104 | 0.6222 | 0.5511 |
| arctic_a0106 | 0.5739 | 0.5597 |
| arctic_a0108 | 0.6074 | 0.5465 |
| arctic_a0111 | 0.6585 | 0.5065 |
| arctic_a0115 | 0.5868 | 0.5625 |
| arctic_a0125 | 0.5753 | 0.5240 |
| arctic_a0126 | 0.6471 | 0.5973 |
| arctic_b0011 | 0.5973 (A: 0.5972) | 0.5903 (A: 0.5902) |
| arctic_b0016 | 0.6039 | 0.5472 (A: 0.5471) |
| arctic_b0019 | 0.6637 | 0.5140 |

Mean per-band scores, overall (Track B; Track A identical to 4
decimals on every band except band 2, where Track B reads 0.5932
against Track A's 0.5933):

| Band | Centre | Track B | Track A |
|---|---|---|---|
| 1 | 150 Hz | 0.3384 | 0.3384 |
| 2 | 189 Hz | 0.5932 | 0.5933 |
| 3 | 238 Hz | 0.6605 | 0.6605 |
| 4 | 300 Hz | 0.5943 | 0.5943 |
| 5 | 378 Hz | 0.4622 | 0.4622 |
| 6 | 476 Hz | 0.6921 | 0.6921 |
| 7 | 600 Hz | 0.7076 | 0.7076 |
| 8 | 756 Hz | 0.6987 | 0.6987 |
| 9 | 952 Hz | 0.7209 | 0.7209 |
| 10 | 1200 Hz | 0.6840 | 0.6840 |
| 11 | 1512 Hz | 0.5788 | 0.5788 |
| 12 | 1905 Hz | 0.5071 | 0.5071 |
| 13 | 2400 Hz | 0.4298 | 0.4298 |
| 14 | 3024 Hz | 0.4949 | 0.4949 |
| 15 | 3810 Hz | 0.5637 | 0.5637 |

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
  no warnings).
- All 20 files rendered (peaks 0.096–0.174, no clipping, no silence).
- Bench: ARC-AudioBench HEAD `a4fdd5a` (includes the STOI commit
  `95fa999`), pulled fresh before the run. All 20 files scored,
  frames kept = frames total on every file (no frames dropped).
- Track A's renders were not touched: `renders/stoi_summary.json`
  still reads 0.5817, and Track B's renders + summary live in the
  separate `renders-trackb/` directory.
- Filterbank suite after the fix: 107,746 checks, 0 failures
  (includes the new stereo-independence regression test).
- Cross-check that closes the loop: the fixed Track B renders match
  Track A's renders to 1 LSB of 16-bit audio (RMS diff
  0.000012–0.000018, max diff 0.000061, correlation 1.000000 on
  all four files spot-checked).

## The stereo bug (found by this measurement, fixed the same day)

The first Track B measurement scored **0.5678** overall
(bdl 0.6075, slt 0.5281) — close to Track A, but the female speaker
was systematically 0.017–0.030 lower on all 10 files, concentrated
in STOI bands 2 (189 Hz, −0.088) and 5 (378 Hz, −0.068). That
pattern was investigated instead of reported as a final number:

- The clean references are BIT-IDENTICAL between tracks (same
  linear resampler), so the inputs match exactly.
- The pre-fix vocoded outputs correlated 0.998+, but the difference
  was concentrated below 250 Hz (difference energy 3x the signal
  there, 3–6% above 250 Hz).
- Ruled out: biquad coefficient precision (double-precision
  coefficients changed the output not at all) and the filter design
  (JUCE's tan-based `IIR::Coefficients` are algebraically identical
  to the RBJ cookbook used here — verified term by term against
  JUCE 8.0.5 source — and a Python model of both bandpass pairs
  agrees to 0.1% at every probe frequency).
- Localised with synthetic probes (200 Hz sine, 1000 Hz sine, white
  noise) rendered through Track A's renderer (built from read-only
  copies in workspace scratch, JUCE 8.0.5) and this engine: the
  difference reproduced on every probe, including a 1000 Hz sine
  with no low-frequency content — so it was not speech-specific
  and not in the modulator path alone.
- Decisive test: per-band display bins (envelope x10) after a
  steady 1000 Hz sine. Track B stereo bins ran 2x–27x hot
  off-frequency; Track B MONO bins matched Track A to 0.05% on
  all 128 bands. Root cause: each `Band` held ONE `Biquad` per
  filter role, shared across channels, while the reference
  (JUCE `ProcessorDuplicator`) holds per-channel filter state.
  The engine processes channel 0's block then channel 1's
  identical block through the same filter state, so every filter
  saw each block's samples twice and the response corrupted —
  worst in the low bands, where the female F0 sits.
- Fix (`Source/DSP/Vox8Filterbank.h`): every band filter is now a
  per-channel array (`std::array<Biquad, kMaxChannels>`), indexed
  by channel in `buildBandFilters`, `reset`, `analyseEnvelope`
  and the carrier path. Still fixed-size member storage, still
  allocation-free in `process()`, same per-sample work count
  (CPU unchanged). No parameter, API or shell change — the shell
  only holds a `FilterbankEngine` and calls its public API.
- Regression test (`Tests/test_filterbank.cpp`,
  `testStereoChannelIndependence`): stereo left output must equal
  the mono run of the same content (< 1e-6), and silencing the
  right channel must not change the left output. Verified both
  ways: it FAILS exactly those 2 checks on the pre-fix engine
  and passes on the fixed engine (suite 107,746 checks, 0
  failures).
- Note for the record: display bins read at end of file are
  inflated in BOTH engines by low-band filter ring-down during
  the renderer's zero-padded tail (band-0 envelope 0.0056 vs
  0.0027 steady-state). That is common-mode — it does not affect
  the A/B comparison — and the padded tail is ~8 ms of a 3 s
  file, so it does not affect STOI either.

The pre-fix 0.5678 number is superseded and must not be cited; the
corrected 0.5818 above is the Track B result. The earlier commit
(d48d391) carried the pre-fix number and a wrong hypothesis (float
evaluation order); this correction replaces both.

## Reduced band counts (32/16 — the saturation question)

Not run yet on Track B, and not a blocker. The engine does not
support it straightforwardly: `kNumBands` is a compile-time
constant baked into the band layout, gains and filter arrays, so a
32/16-band run needs a rebuild with a different constant (a scratch
experiment, not a shipped change). Track A is running its own
32-band variant concurrently. Follow-up: build the renderer with
`kNumBands` set to 32 and 16 in a scratch copy, render the same
corpus, score the same way, and compare against 0.5818 @ 128.

## What is next

- Reduced-band runs (32/16) as a scratch experiment, same corpus
  and scoring.
- Shell re-verification after the engine fix (pluginval / VST3
  validator): not re-run on the VM yet. The change is private to
  the engine (no API, parameter or shell change) and both DSP-only
  consumers rebuild clean, but the Phase 2 shell verdict should
  be re-confirmed before the next release cut.
- Ghost STOI stays out until the Ghost resynthesis redesign lands
  (owed decision; the broken resynthesis would make the number
  meaningless). Note: the Ghost engine holds no biquads, so the
  stereo filter-state bug above does not apply to it.
