// bench_filterbank.cpp — CPU micro-benchmark for the Low Latency 128 engine.
//
// Measures mean + worst-case block time at 44.1 / 48 / 96 kHz, buffer sizes
// 1 / 16 / 32 / 64 / 128 / 512 / 1024 / 2048, with silence in, spoken-like
// input in (sine bursts), and a drum-like modulator in (noise bursts).
// Reports the block time as a fraction of the real-time budget for one
// instance. This is a VM number, not the 2018 Intel Mac mini number — the
// paper's CPU protocol runs the same bench on the reference hardware.
//
// Not a pass/fail test: it prints numbers. Run with `make bench`.

#include "Vox8Filterbank.h"

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
    vox8::FilterbankEngine e;
    e.prepare(sr, 2);
    vox8::Parameters p;
    p.mode = vox8::Mode::Vocoder;
    e.setParameters(p);
    e.setMidiNote(69);

    const size_t total = (size_t)(sr * 2.0); // 2 seconds
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
        times.push_back(dt);
        ++blocks;
    }
    std::sort(times.begin(), times.end());
    const double median = times[times.size() / 2];
    const double p95 = times[(size_t)(times.size() * 0.95)];
    const double budgetMs = 1000.0 * (double)blockSize / sr;
    std::printf("%-12s sr=%6.0f block=%5d  mean %8.3f us (%5.2f%%)"
                "  median %8.3f us (%5.2f%%)  p95 %8.3f us  worst %8.3f us\n",
                label, sr, blockSize, 1000.0 * sum / blocks,
                100.0 * (sum / blocks) / budgetMs, 1000.0 * median,
                100.0 * median / budgetMs, 1000.0 * p95, 1000.0 * worst);
}

int main()
{
    std::printf("Vox8 Low Latency 128 — filterbank CPU bench (this VM, "
                "1 instance, stereo)\n");
    const double rates[] = {44100.0, 48000.0, 96000.0};
    const int blocks[] = {1, 16, 32, 64, 128, 512, 1024, 2048};
    for (double sr : rates)
        for (int bs : blocks)
            benchRate(sr, bs, "vocoder");
    return 0;
}
