#!/usr/bin/env python3
"""Score the Vox8 STOI corpus renders with the ARC-AudioBench STOI tool.

For each (clean44, vocoded) pair in renders-trackb/:
  1. Measure the ENVELOPE lag (10 ms RMS envelopes, cross-correlated
     over +/-100 ms). The waveform cross-correlation the renderer prints
     is meaningless for a vocoder (the output is a carrier modulated by
     the input envelope, not a shifted copy of the input waveform --
     its correlations were 0.01-0.10, i.e. noise). If the median envelope
     lag is <= 15 ms, score with no shift (the filterbank has no frame
     delay; its ~5 ms envelope-attack lag is part of the vocoder, and
     STOI's 384 ms segments tolerate it). Otherwise shift the degraded
     file by the per-file envelope lag before scoring.
  2. Run `arc_audiobench.py stoi clean degraded --out` per file.
  3. Report per-file scalar + band means, then the mean across the 10
     sentences per speaker and overall.
"""
import argparse
import json
import math
import os
import subprocess
import sys
import wave

BENCH = os.path.expanduser("~/workspace/ARC-AudioBench/scripts/arc_audiobench.py")
IDS = ["arctic_a0104", "arctic_a0106", "arctic_a0108", "arctic_a0111",
       "arctic_a0115", "arctic_a0125", "arctic_a0126", "arctic_b0011",
       "arctic_b0016", "arctic_b0019"]


def read_wav_mono(path):
    with wave.open(path, "rb") as wf:
        nch, sw, sr, n = wf.getnchannels(), wf.getsampwidth(), wf.getframerate(), wf.getnframes()
        raw = wf.readframes(n)
    assert sw == 2, "need 16-bit"
    import struct
    ints = struct.unpack("<%dh" % (len(raw) // 2), raw)
    if nch == 1:
        return [v / 32768.0 for v in ints], sr
    return [ints[i] / 32768.0 for i in range(0, len(ints), nch)], sr


def write_wav_mono(path, x, sr):
    import struct
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        wf.writeframes(struct.pack("<%dh" % len(x),
                                   *[max(-32767, min(32767, int(round(v * 32767)))) for v in x]))


def rms_envelope(x, win):
    env = []
    for i in range(0, len(x) - win + 1, win):
        s = 0.0
        for v in x[i:i + win]:
            s += v * v
        env.append(math.sqrt(s / win))
    return env


def envelope_lag(clean, deg, sr, win_ms=10, max_ms=100):
    win = int(sr * win_ms / 1000)
    ec = rms_envelope(clean, win)
    ed = rms_envelope(deg, win)
    max_lag = int(max_ms / win_ms)
    n = min(len(ec), len(ed))
    best, best_score = 0, -1e30
    for lag in range(-max_lag, max_lag + 1):
        num = den_c = den_d = 0.0
        for i in range(max(0, -lag), min(n, n - lag)):
            c, d = ec[i], ed[i + lag]
            num += c * d
            den_c += c * c
            den_d += d * d
        score = num / math.sqrt(den_c * den_d + 1e-18)
        if score > best_score:
            best, best_score = lag, score
    return best * win_ms, best_score  # lag in ms, correlation


def main():
    ap = argparse.ArgumentParser(description="Score Vox8 STOI corpus renders-trackb")
    ap.add_argument("--renders", default=os.path.expanduser(
        "~/workspace/vox8-bench/corpus/renders-trackb"),
        help="dir with <sp>_<id>_clean44.wav + <sp>_<id>_vocoded.wav pairs")
    ap.add_argument("--summary", default="stoi_summary.json",
        help="summary filename written inside --renders (default stoi_summary.json)")
    args = ap.parse_args()
    RENDERS = args.renders
    lags = []
    files = []
    for sp in ("bdl", "slt"):
        for pid in IDS:
            clean_p = os.path.join(RENDERS, f"{sp}_{pid}_clean44.wav")
            deg_p = os.path.join(RENDERS, f"{sp}_{pid}_vocoded.wav")
            clean, sr = read_wav_mono(clean_p)
            deg, sr2 = read_wav_mono(deg_p)
            assert sr == sr2
            lag_ms, corr = envelope_lag(clean, deg, sr)
            lags.append(lag_ms)
            files.append((sp, pid, clean_p, deg_p, lag_ms, corr))
            print(f"{sp} {pid}: envelope lag {lag_ms:+d} ms (corr {corr:.3f})")
    lags_sorted = sorted(lags)
    median_lag = lags_sorted[len(lags) // 2]
    print(f"median envelope lag: {median_lag:+d} ms (range {min(lags):+d}..{max(lags):+d})")
    shift = abs(median_lag) > 15
    print("scoring with per-file envelope shift" if shift else
          "scoring with NO shift (median envelope lag <= 15 ms)")

    results = []
    for sp, pid, clean_p, deg_p, lag_ms, corr in files:
        deg_use = deg_p
        if shift:
            deg, sr = read_wav_mono(deg_p)
            shift_samp = int(round(lag_ms * sr / 1000))
            if shift_samp > 0:
                deg = deg[shift_samp:]
            elif shift_samp < 0:
                deg = [0.0] * (-shift_samp) + deg
            deg_use = os.path.join(RENDERS, f"{sp}_{pid}_vocoded_aligned.wav")
            write_wav_mono(deg_use, deg, sr)
        out_p = os.path.join(RENDERS, f"{sp}_{pid}.stoi.json")
        r = subprocess.run([sys.executable, BENCH, "stoi", clean_p, deg_use,
                            "--out", out_p], capture_output=True, text=True)
        if r.returncode != 0:
            print(f"STOI FAILED {sp} {pid}: {r.stderr.strip()[-300:]}")
            continue
        d = json.load(open(out_p))
        results.append((sp, pid, d["stoi"], d["band_means"], d["frames_kept"], d["segments"], d["band_centres_hz"]))
        print(f"{sp} {pid}: STOI {d['stoi']:.4f} (frames kept {d['frames_kept']}/{d['frames_total']}, segments {d['segments']})")

    print("\nper-file STOI:")
    for sp, pid, s, _, _, _, _ in results:
        print(f"  {sp} {pid}: {s:.4f}")
    for sp in ("bdl", "slt"):
        ss = [s for s2, _, s, _, _, _, _ in results if s2 == sp]
        print(f"mean {sp} (male)" if sp == "bdl" else f"mean {sp} (female)",
              f": {sum(ss)/len(ss):.4f} (n={len(ss)})")
    all_s = [s for _, _, s, _, _, _, _ in results]
    print(f"mean overall: {sum(all_s)/len(all_s):.4f} (n={len(all_s)})")
    nb = len(results[0][3])
    centres = results[0][6]
    print("mean per-band scores (overall):")
    for b in range(nb):
        vals = [bm[b] for _, _, _, bm, _, _, _ in results]
        print(f"  band {b+1:2d} ({centres[b]:.0f} Hz): {sum(vals)/len(vals):.4f}")
    json.dump([{"speaker": sp, "id": pid, "stoi": s, "band_means": bm,
                "frames_kept": fk, "segments": sg}
               for sp, pid, s, bm, fk, sg, _ in results],
              open(os.path.join(RENDERS, args.summary), "w"), indent=1)
    print(f"wrote renders/{args.summary}")


if __name__ == "__main__":
    main()
