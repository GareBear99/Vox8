// bench_ghost.cpp — CPU micro-benchmark for the Spectral Ghost 1024 engine.
//
// Measures mean / median / p95 / worst-case block time at 44.1 / 48 / 96
// kHz, buffer sizes 1 / 16 / 32 / 64 / 128 / 512 / 1024 / 2048, with
// spoken-like input (sine bursts). Reports the block time as a fraction of
// the real-time budget for one instance. This is a VM number, not the 2018
// Intel Mac mini number — the paper's CPU protocol runs the same bench on
// the reference hardware.
//
// Also measures, for real (not computed from the code as read):
//   - the transcendental call rate of analyseBins(): the engine counts
//     every std::cos / std::sin evaluation; calls/sec/channel = counted
//     calls / audio seconds / channels. The paper's ~11M/sec/channel
//     figure is checked against this number.
//   - the cost of the per-band stride fix vs flat stride 4: a standalone
//     timing loop runs the same Goertzel accumulation both ways, so the
//     price of correctness is a measurement, not an assertion. (Flat
//     stride 4 aliases above sr/8 — see Vox8Ghost.h — so it is timed
//     only as a reference, never used.)
//
// Not a pass/fail test: it prints numbers. Run with `make bench`.

#include "Vox8Ghost.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

static double nowMs()
{
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static void benchRate(double sr, int blockSize, const char* label)
{
    vox8::GhostEngine e;
    e.prepare(sr, 2);
    vox8::Parameters p;
    p.mode = vox8::Mode::Vocoder;
    e.setParameters(p);
    e.setMidiNote(69);

    const size_t total = (size_t)(sr * 2.0 / blockSize) * (size_t)blockSize;
    // 2 seconds, rounded down to a whole number of blocks: a final partial
    // block would pollute the minimum (fewer renderFrames in it).
    std::vector<float> in(total), inR(total);
    for (size_t i = 0; i < total; ++i)
    {
        const double t = (double)i / sr;
        // Spoken-like: bursts of a 1 kHz tone, 0.1 s on / 0.1 s off.
        const bool on = std::fmod(t, 0.2) < 0.1;
        in[i] = inR[i] = on ? (float)(0.4 * std::sin(2.0 * 3.14159265358979 *
                                                    1000.0 * (double)i / sr))
                            : 0.0f;
    }

    // Warmup.
    for (size_t off = 0; off < total; off += (size_t)blockSize)
    {
        const int n = (int)std::min<size_t>((size_t)blockSize, total - off);
        float* io[2] = {in.data() + off, inR.data() + off};
        e.process(io, 2, n, nullptr, 0);
    }

    double worst = 0.0;
    double best = 1e9;
    double sum = 0.0;
    int blocks = 0;
    std::vector<double> times;
    for (size_t off = 0; off < total; off += (size_t)blockSize)
    {
        const int n = (int)std::min<size_t>((size_t)blockSize, total - off);
        float* io[2] = {in.data() + off, inR.data() + off};
        const double t0 = nowMs();
        e.process(io, 2, n, nullptr, 0);
        const double dt = nowMs() - t0;
        sum += dt;
        worst = std::max(worst, dt);
        best = std::min(best, dt);
        times.push_back(dt);
        ++blocks;
    }
    std::sort(times.begin(), times.end());
    const double median = times[times.size() / 2];
    const double p95 = times[(size_t)(times.size() * 0.95)];
    const double budgetMs = 1000.0 * (double)blockSize / sr;
    // The VM's scheduling stalls inflate means/medians (a stall can only
    // slow a block, never speed it), so the MINIMUM is the honest
    // stall-free cost on this machine; mean/median are reported alongside.
    std::printf("%-12s sr=%6.0f block=%5d  mean %8.3f us (%5.2f%%)"
                "  median %8.3f us (%5.2f%%)  min %8.3f us (%5.2f%%)"
                "  p95 %8.3f us  worst %8.3f us\n",
                label, sr, blockSize, 1000.0 * sum / blocks,
                100.0 * (sum / blocks) / budgetMs, 1000.0 * median,
                100.0 * median / budgetMs, 1000.0 * best,
                100.0 * best / budgetMs, 1000.0 * p95, 1000.0 * worst);
}

// Measure the counted transcendental rate: run 4 s of audio through the
// engine, read the exact cos/sin count, divide by audio seconds.
static void benchGoertzelRate(double sr)
{
    vox8::GhostEngine e;
    e.prepare(sr, 2);
    vox8::Parameters p;
    p.mode = vox8::Mode::Vocoder;
    e.setParameters(p);
    e.setMidiNote(69);

    const size_t total = (size_t)(sr * 4.0);
    std::vector<float> in(total), inR(total);
    for (size_t i = 0; i < total; ++i)
        in[i] = inR[i] =
            (float)(0.4 * std::sin(2.0 * 3.14159265358979 * 1000.0 *
                                   (double)i / sr));
    const int blockSize = 512;
    for (size_t off = 0; off < total; off += (size_t)blockSize)
    {
        const int n = (int)std::min<size_t>((size_t)blockSize, total - off);
        float* io[2] = {in.data() + off, inR.data() + off};
        e.process(io, 2, n, nullptr, 0);
    }
    const double audioSeconds = (double)total / sr;
    const double callsPerSecPerCh =
        (double)e.getGoertzelCalls() / audioSeconds / 2.0;
    std::printf("Goertzel rate @ %.0f Hz: %.3f M transcendental calls/sec/channel "
                "(%llu calls counted over %.1f s audio, %llu frames)\n",
                sr, callsPerSecPerCh / 1e6,
                (unsigned long long)e.getGoertzelCalls(), audioSeconds,
                (unsigned long long)e.getFramesRendered());
}

// Standalone stride comparison: the same Goertzel accumulation the engine
// runs, at flat stride 4 (as shipped — ALIASES above sr/8) vs the per-band
// stride the engine now uses (4/3/2/1 by band frequency). Times the inner
// loop only, so the ratio is the accumulation cost, not the whole engine.
static void benchStride(double sr)
{
    constexpr int kFft = 1024;
    constexpr int kBands = 128;
    std::vector<float> ring(kFft), window(kFft);
    for (int i = 0; i < kFft; ++i)
    {
        ring[i] = (float)(0.5 * std::sin(2.0 * 3.14159265358979 * 1000.0 *
                                         (double)i / sr));
        const double t = (double)i / (kFft - 1);
        window[i] = (float)(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * t));
    }
    // Min-of-3 trials: the VM's scheduling stalls pollute means (identical
    // work timed 419 ms vs 236 ms across runs), so the minimum is the
    // honest comparison.
    auto timeScheme = [&](bool perBand) {
        const int frames = 200;
        double best = 1e9, sink = 0.0;
        for (int trial = 0; trial < 3; ++trial)
        {
            double trialSink = 0.0;
            const double t0 = nowMs();
            for (int f = 0; f < frames; ++f)
            {
                for (int b = 0; b < kBands; ++b)
                {
                    const float freq =
                        55.0f * std::pow(18500.0f / 55.0f, (float)b / 127.0f);
                    const double w =
                        2.0 * 3.14159265358979 * (double)freq / sr;
                    int stride = 4;
                    if (perBand)
                        while (stride > 1 && sr / (2.0 * stride) <= freq)
                            --stride;
                    double re = 0.0, im = 0.0;
                    for (int i = 0; i < kFft; i += stride)
                    {
                        const double a = w * (double)i;
                        const double s = (double)ring[i] * (double)window[i];
                        re += s * std::cos(a);
                        im -= s * std::sin(a);
                    }
                    trialSink += re + im;
                }
            }
            const double dt = nowMs() - t0;
            if (dt < best)
            {
                best = dt;
                sink = trialSink;
            }
        }
        return std::pair<double, double>(best, sink);
    };
    const auto flat4 = timeScheme(false);
    const auto perBand = timeScheme(true);
    std::printf("Goertzel accumulation @ %.0f Hz (200 frames x 128 bands): "
                "flat stride 4 %.2f ms, per-band stride %.2f ms, ratio %.2fx "
                "(sink %.3f / %.3f)\n",
                sr, flat4.first, perBand.first,
                perBand.first / flat4.first, flat4.second, perBand.second);
}

int main()
{
    std::printf("Vox8 Spectral Ghost 1024 — CPU bench (this VM, "
                "1 instance, stereo)\n");
    const double rates[] = {44100.0, 48000.0, 96000.0};
    const int blocks[] = {1, 16, 32, 64, 128, 512, 1024, 2048};
    for (double sr : rates)
        for (int bs : blocks)
            benchRate(sr, bs, "vocoder");
    std::printf("\n");
    for (double sr : rates)
        benchGoertzelRate(sr);
    std::printf("\n");
    for (double sr : rates)
        benchStride(sr);
    return 0;
}
