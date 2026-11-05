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

Measured 2026-11-05, same corpus, same settings, same alignment and
scorer as the 128-band run. Method: scratch copies of `Source/DSP`
in `/tmp/vox8-nbands/32` and `/tmp/vox8-nbands/16` with only
`kNumBands` changed in `Vox8Core.h` (128 → 32 / 16) — the band edges,
gains and filter arrays all derive from that one constant, so the
scratch engines are the same code at a different band count. The
committed 128-band engine was not touched. Renders at
`~/workspace/vox8-bench/corpus/renders-trackb-32/` and
`renders-trackb-16/` (not committed). Envelope lags +20 to +30 ms,
median +20 ms, same as 128 bands; frames kept = frames total on all
40 files.

| Bands | Track B | Track A | diff | bdl (B / A) | slt (B / A) |
|---|---|---|---|---|---|
| 128 | **0.5818** | 0.5817 | +0.0001 | 0.6136 / 0.6136 | 0.5499 / 0.5499 |
| 32 | **0.5817** | 0.5816 | +0.0001 | 0.6133 / 0.6132 | 0.5501 / 0.5499 |
| 16 | **0.5809** | 0.5807 | +0.0002 | 0.6123 / 0.6122 | 0.5494 / 0.5492 |

The two tracks agree to 0.0002 at every band count, per file as well
as overall (largest per-file gap: 0.0002, e.g. 32-band slt a0106
0.5582 vs 0.5580). Intelligibility saturates by ~16 bands on both
tracks: Track B spread 128→16 is 0.0009, Track A's is 0.0010. The
paper's "saturates at ~20–30 bands" holds; 128 bands is a
Ghost/Morph resolution choice, not an intelligibility choice.

Per-file STOI, 32 bands (Track B; Track A in brackets where it differs):

| Sentence | bdl | slt |
|---|---|---|
| arctic_a0104 | 0.6228 (A: 0.6227) | 0.5507 (A: 0.5506) |
| arctic_a0106 | 0.5701 (A: 0.5700) | 0.5582 (A: 0.5580) |
| arctic_a0108 | 0.6069 (A: 0.6068) | 0.5449 (A: 0.5447) |
| arctic_a0111 | 0.6580 (A: 0.6579) | 0.5080 (A: 0.5079) |
| arctic_a0115 | 0.5870 (A: 0.5869) | 0.5604 (A: 0.5600) |
| arctic_a0125 | 0.5773 (A: 0.5772) | 0.5252 |
| arctic_a0126 | 0.6453 (A: 0.6452) | 0.5924 (A: 0.5923) |
| arctic_b0011 | 0.5971 | 0.5947 (A: 0.5946) |
| arctic_b0016 | 0.6037 (A: 0.6035) | 0.5482 |
| arctic_b0019 | 0.6651 (A: 0.6649) | 0.5180 (A: 0.5178) |

Per-file STOI, 16 bands (Track B; Track A in brackets where it differs):

| Sentence | bdl | slt |
|---|---|---|
| arctic_a0104 | 0.6214 (A: 0.6213) | 0.5500 (A: 0.5499) |
| arctic_a0106 | 0.5682 (A: 0.5680) | 0.5576 (A: 0.5572) |
| arctic_a0108 | 0.6061 | 0.5442 (A: 0.5439) |
| arctic_a0111 | 0.6573 (A: 0.6572) | 0.5065 (A: 0.5064) |
| arctic_a0115 | 0.5860 (A: 0.5859) | 0.5596 (A: 0.5591) |
| arctic_a0125 | 0.5757 (A: 0.5755) | 0.5244 (A: 0.5245) |
| arctic_a0126 | 0.6444 | 0.5916 |
| arctic_b0011 | 0.5965 (A: 0.5966) | 0.5952 (A: 0.5949) |
| arctic_b0016 | 0.6033 (A: 0.6031) | 0.5484 (A: 0.5481) |
| arctic_b0019 | 0.6645 (A: 0.6644) | 0.5169 (A: 0.5165) |

Mean per-band scores, overall (Track B 32 / Track A 32 / Track B 16 /
Track A 16):

| Band | Centre | B32 | A32 | B16 | A16 |
|---|---|---|---|---|---|
| 1 | 150 Hz | 0.3434 | 0.3431 | 0.3434 | 0.3433 |
| 2 | 189 Hz | 0.5843 | 0.5843 | 0.5847 | 0.5846 |
| 3 | 238 Hz | 0.6484 | 0.6484 | 0.6458 | 0.6459 |
| 4 | 300 Hz | 0.5844 | 0.5845 | 0.5825 | 0.5827 |
| 5 | 378 Hz | 0.4666 | 0.4662 | 0.4683 | 0.4678 |
| 6 | 476 Hz | 0.6924 | 0.6924 | 0.6920 | 0.6920 |
| 7 | 600 Hz | 0.7176 | 0.7176 | 0.7177 | 0.7176 |
| 8 | 756 Hz | 0.7037 | 0.7034 | 0.7029 | 0.7024 |
| 9 | 952 Hz | 0.7229 | 0.7227 | 0.7220 | 0.7215 |
| 10 | 1200 Hz | 0.6848 | 0.6846 | 0.6836 | 0.6832 |
| 11 | 1512 Hz | 0.5790 | 0.5788 | 0.5792 | 0.5789 |
| 12 | 1905 Hz | 0.5078 | 0.5077 | 0.5080 | 0.5079 |
| 13 | 2400 Hz | 0.4306 | 0.4306 | 0.4296 | 0.4297 |
| 14 | 3024 Hz | 0.4950 | 0.4950 | 0.4920 | 0.4919 |
| 15 | 3810 Hz | 0.5645 | 0.5644 | 0.5617 | 0.5614 |

One real behavioural difference, not a scoring difference: output
level scales with band count in this engine, and in the opposite
direction from Track A. `bandGain` is ~0.038–0.078 per band with no
per-count normalisation, so the vocoded sum grows with the number of
bands: mean render peak 0.126 at 128 bands, 0.040 at 32, 0.028 at 16.
Track A's renders go the other way (mean peak 0.160 at 32, 0.218 at
16 — wider bands pass more energy per band). STOI is level-invariant
enough that the scores still agree to 0.0002, and the 16-band peaks
(0.022–0.039, about −33 to −28 dBFS) sit far above 16-bit quantisation
noise, so the numbers stand. But a shipped 16/32-band tier on this
engine would need gain compensation (or a `1/kNumBands`-style
normalisation) — the level difference is a calibration note, not a
STOI caveat.

## What is next

- Reduced-band runs (32/16): DONE 2026-11-05 — see section above.
  0.5817 @ 32, 0.5809 @ 16; saturation confirmed on both tracks.
- Shell re-verification after the engine fix (pluginval / VST3
  validator): in progress 2026-11-05 (shell rebuild + re-run on the VM).
- Ghost STOI stays out until the Ghost resynthesis redesign lands
  (owed decision; the broken resynthesis would make the number
  meaningless). Note: the Ghost engine holds no biquads, so the
  stereo filter-state bug above does not apply to it.
