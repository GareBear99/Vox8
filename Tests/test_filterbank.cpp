// Vox8 filterbank engine — standalone tests.
//
// No DAW, no host, no framework: this file compiles only the DSP headers
// with no framework on the include path. Every check counts; the run fails
// if any check fails. Run with `make test` (see Makefile).
//
// Test signals are synthetic (sines, bursts, noise) — no corpus is invented
// here. The STOI corpus and Track A's test signals arrive separately and
// will be run against this engine when they land; until then these tests
// prove the engine does what the paper says it does.

#include "Vox8Filterbank.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

#define CHECK_NEAR(a, b, tol) do { \
    ++g_checks; \
    const double _a = (a), _b = (b); \
    if (std::abs(_a - _b) > (tol)) { \
        ++g_failures; \
        std::printf("FAIL %s:%d: %s (%.6g) != %s (%.6g) tol %.6g\n", \
            __FILE__, __LINE__, #a, _a, #b, _b, (double)(tol)); \
    } \
} while (0)

static constexpr double kSr = 48000.0;

static void fillSine(std::vector<float>& buf, double freq, double amp = 0.5,
                     double phase0 = 0.0)
{
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = static_cast<float>(amp * std::sin(2.0 * 3.14159265358979 *
                                                   freq * (double)i / kSr +
                                                   phase0));
}

static double rms(const float* buf, size_t n)
{
    double s = 0.0;
    for (size_t i = 0; i < n; ++i)
        s += (double)buf[i] * (double)buf[i];
    return std::sqrt(s / (double)n);
}

static double peakAbs(const float* buf, size_t n)
{
    double p = 0.0;
    for (size_t i = 0; i < n; ++i)
        p = std::max(p, std::abs((double)buf[i]));
    return p;
}

// Goertzel magnitude at one frequency, Hann-windowed. Used to measure which
// band the vocoded output energy sits in.
static double goertzel(const float* buf, size_t n, double freq)
{
    const double w = 2.0 * 3.14159265358979 * freq / kSr;
    const double cw = std::cos(w);
    const double coeff = 2.0 * cw;
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double hann = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 *
                                                (double)i / (double)(n - 1));
        const double x = (double)buf[i] * hann;
        const double s = x + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double re = s1 - s2 * cw;
    const double im = s2 * std::sin(w);
    return std::sqrt(re * re + im * im) / (double)n;
}

// The engine holds std::atomic display taps, so it is neither copyable nor
// movable: initialise it in place, never return it by value.
static void initEngine(vox8::FilterbankEngine& e, vox8::Parameters p)
{
    e.prepare(kSr, 2);
    e.setParameters(p);
    e.setMidiNote(69); // A4 = 440 Hz carrier
}

// Process `in` through the engine in blocks of `blockSize`. `sc` is the
// optional sidechain (nullptr = none). Returns the processed buffer.
static std::vector<float> run(vox8::FilterbankEngine& e,
                              const std::vector<float>& in,
                              const std::vector<float>* sc = nullptr,
                              int blockSize = 512)
{
    std::vector<float> out = in;
    std::vector<float> scBuf;
    if (sc)
        scBuf = *sc;
    float* io[2] = {out.data(), out.data()};
    const float* sci[2] = {nullptr, nullptr};
    if (sc)
        sci[0] = sci[1] = scBuf.data();
    // Stereo: duplicate mono into both channels via the same pointer is not
    // valid (the engine writes per channel), so run true stereo with two
    // identical buffers.
    std::vector<float> outR = in;
    std::vector<float> scR;
    if (sc)
        scR = *sc;
    io[1] = outR.data();
    if (sc)
        sci[1] = scR.data();

    for (size_t off = 0; off < out.size(); off += (size_t)blockSize)
    {
        const int n = (int)std::min<size_t>((size_t)blockSize,
                                            out.size() - off);
        float* blk[2] = {io[0] + off, io[1] + off};
        const float* sblk[2] = {nullptr, nullptr};
        if (sc)
        {
            sblk[0] = sci[0] + off;
            sblk[1] = sci[1] + off;
        }
        e.process(blk, 2, n, sc ? sblk : nullptr, sc ? 2 : 0);
    }
    return out; // left channel
}

// --- 1. Band layout ---------------------------------------------------------

static void testBandLayout()
{
    CHECK_NEAR(vox8::bandEdgeLo(0, kSr), 55.0, 1e-3);
    CHECK_NEAR(vox8::bandEdgeLo(vox8::kNumBands, kSr), 18500.0, 1.0);
    for (int i = 0; i < vox8::kNumBands; ++i)
    {
        const float lo = vox8::bandEdgeLo(i, kSr);
        const float hi = vox8::bandEdgeHi(i, kSr);
        CHECK(hi > lo);
        CHECK(lo > 0.0f);
        if (i + 1 < vox8::kNumBands)
            CHECK(vox8::bandEdgeLo(i + 1, kSr) > lo);
    }
    // Geometric spacing: ratio between adjacent edges is constant.
    const float r0 = vox8::bandEdgeLo(1, kSr) / vox8::bandEdgeLo(0, kSr);
    const float r1 = vox8::bandEdgeLo(64, kSr) / vox8::bandEdgeLo(63, kSr);
    CHECK_NEAR(r0, r1, 1e-4);
    // Sample-rate cap: at 32 kHz the top edge follows 0.47 * sr.
    CHECK_NEAR(vox8::bandEdgeLo(vox8::kNumBands, 32000.0), 0.47 * 32000.0,
               1.0);
}

// --- 2. Biquads -------------------------------------------------------------

static void testBiquads()
{
    vox8::Biquad lp, hp;
    lp.setLowpass(1000.0f, (float)kSr);
    hp.setHighpass(1000.0f, (float)kSr);
    // DC through the lowpass settles at ~1; through the highpass at ~0.
    float yLp = 0.0f, yHp = 0.0f;
    for (int i = 0; i < 20000; ++i)
    {
        yLp = lp.process(1.0f);
        yHp = hp.process(1.0f);
    }
    CHECK_NEAR(yLp, 1.0, 0.01);
    CHECK_NEAR(yHp, 0.0, 0.01);
    // No NaN after an impulse.
    vox8::Biquad lp2;
    lp2.setLowpass(100.0f, (float)kSr);
    float y = lp2.process(1.0f);
    CHECK(std::isfinite(y));
    for (int i = 0; i < 1000; ++i)
        y = lp2.process(0.0f);
    CHECK(std::isfinite(y));
    CHECK(std::abs(y) < 1.0f);
}

// --- 3. Silence / finiteness -------------------------------------------------

static void testSilenceAndFiniteness()
{
    const std::vector<float> silence((size_t)(kSr * 0.5), 0.0f);
    for (int mode = 0; mode < 4; ++mode)
    {
        vox8::Parameters p;
        p.mode = (vox8::Mode)mode;
        p.air = 0.0f;
        vox8::FilterbankEngine e; initEngine(e, p);
        const std::vector<float> out = run(e, silence);
        // Mask mode passes the carrier when the modulator is silent — that
        // is correct Mask behaviour, not silence. The other modes must be
        // silent (carrier x zero envelope).
        if (mode == 1)
        {
            CHECK(peakAbs(out.data(), out.size()) > 0.0);
        }
        else
        {
            // Silence in gives silence out to the 1e-12 denormal-guard
            // floor: the envelope magnitude floor is sqrt(1e-12) = 1e-6,
            // which leaks ~1e-6 of carrier into the output. Inaudible
            // (-120 dBFS); the bound below is the honest one.
            CHECK(peakAbs(out.data(), out.size()) < 1e-5);
        }
    }

    // Extreme parameters + loud input + DC: no NaN/Inf anywhere.
    vox8::Parameters p;
    p.mode = vox8::Mode::Morph;
    p.clarity = 0.0f;
    p.formantShift = 24.0f;
    p.ghost = 1.0f;
    p.air = 1.0f;
    p.quality = 1.0f;
    p.outputGainDb = 24.0f;
    p.morph = 1.0f;
    p.stereoWidth = 2.0f;
    p.transientProtect = 1.0f;
    p.maskDepth = 1.0f;
    std::vector<float> loud((size_t)(kSr * 0.5), 0.95f);
    vox8::FilterbankEngine e; initEngine(e, p);
    const std::vector<float> out = run(e, loud);
    for (float v : out)
        CHECK(std::isfinite(v));
    CHECK(peakAbs(out.data(), out.size()) <= 1.01);
}

// --- 4. Envelope transfer: output follows the modulator ----------------------

static void testEnvelopeTransfer()
{
    // Modulator: 1 kHz sine bursts, 0.1 s on / 0.1 s off, twice.
    std::vector<float> mod((size_t)(kSr * 0.4), 0.0f);
    for (size_t i = 0; i < mod.size(); ++i)
    {
        const double t = (double)i / kSr;
        const bool on = (t < 0.1) || (t >= 0.2 && t < 0.3);
        mod[i] = on ? (float)(0.5 * std::sin(2.0 * 3.14159265358979 *
                                             1000.0 * (double)i / kSr))
                    : 0.0f;
    }
    vox8::Parameters p;
    p.mode = vox8::Mode::Vocoder;
    p.clarity = 1.0f; // fastest attack/release
    p.air = 0.0f;
    vox8::FilterbankEngine e; initEngine(e, p);
    const std::vector<float> out = run(e, mod);
    const double onRms = rms(out.data() + (size_t)(kSr * 0.03),
                             (size_t)(kSr * 0.05));
    const double offRms = rms(out.data() + (size_t)(kSr * 0.13),
                              (size_t)(kSr * 0.05));
    CHECK(onRms > 0.001);
    CHECK(onRms > 5.0 * offRms);
}

// --- 5. Freeze holds ----------------------------------------------------------

static void testFreeze()
{
    std::vector<float> mod((size_t)(kSr * 0.6), 0.0f);
    fillSine(mod, 1000.0, 0.5);
    // First 0.2 s: modulator on. Then silence. Freeze engages at 0.2 s.
    for (size_t i = (size_t)(kSr * 0.2); i < mod.size(); ++i)
        mod[i] = 0.0f;

    vox8::Parameters p;
    p.mode = vox8::Mode::Vocoder;
    p.air = 0.0f;
    vox8::FilterbankEngine e; initEngine(e, p);
    // Run the first 0.2 s unfrozen, then flip freeze on.
    std::vector<float> head(mod.begin(), mod.begin() + (size_t)(kSr * 0.2));
    run(e, head);
    p.freeze = 1.0f;
    e.setParameters(p);
    std::vector<float> tail(mod.begin() + (size_t)(kSr * 0.2), mod.end());
    const std::vector<float> tailOut = run(e, tail);

    const double liveRms = rms(tailOut.data(), (size_t)(kSr * 0.05));
    const double heldRms = rms(tailOut.data() + (size_t)(kSr * 0.3),
                               (size_t)(kSr * 0.05));
    CHECK(liveRms > 0.001);
    // Frozen output 0.3 s after the modulator stopped is still most of the
    // live level: the frozen envelope holds indefinitely.
    CHECK(heldRms > 0.5 * liveRms);

    // Without freeze, the same tail decays to near silence.
    vox8::FilterbankEngine e2; initEngine(e2, p);
    p.freeze = 0.0f;
    e2.setParameters(p);
    run(e2, head);
    const std::vector<float> tailOut2 = run(e2, tail);
    const double decayedRms = rms(tailOut2.data() + (size_t)(kSr * 0.3),
                                  (size_t)(kSr * 0.05));
    CHECK(decayedRms < 0.2 * liveRms);
}

// --- 6. Formant shift moves the envelope --------------------------------------
// Formant shift remaps the envelope READ position, not the stored envelope,
// so it is measured on the output, not on the display bins: the spectral
// centroid of the vocoded output (energy-weighted over the carrier
// harmonics) must move up with a positive shift and down with a negative
// one. A spectral shift would move the pitch too; envelope remapping moves
// only the resonances, which is what the centroid measures here.

static double outputCentroid(vox8::FilterbankEngine& e,
                             const std::vector<float>& mod)
{
    const std::vector<float> out = run(e, mod);
    const size_t start = (size_t)(kSr * 0.1);
    const size_t n = (size_t)(kSr * 0.25);
    double num = 0.0, den = 0.0;
    for (int k = 1; k <= 12; ++k)
    {
        const double f = 440.0 * k;
        const double m = goertzel(out.data() + start, n, f);
        num += f * m * m;
        den += m * m;
    }
    return num / den;
}

static void testFormantShift()
{
    std::vector<float> mod((size_t)(kSr * 0.4), 0.0f);
    fillSine(mod, 1000.0, 0.5);

    auto centroidForShift = [&](float shift) {
        vox8::Parameters p;
        p.mode = vox8::Mode::Vocoder;
        p.air = 0.0f;
        p.carrierBlend = 1.0f; // no dry in the carrier: clean measurement
        p.transientProtect = 0.0f; // no dry transient bleed in the output
        p.formantShift = shift;
        vox8::FilterbankEngine e;
        initEngine(e, p);
        return outputCentroid(e, mod);
    };

    const double c0 = centroidForShift(0.0f);
    const double cUp = centroidForShift(12.0f);
    const double cDown = centroidForShift(-12.0f);

    // 1 kHz modulator, 440 Hz carrier: unshifted centroid sits near 1 kHz.
    CHECK(c0 > 800.0 && c0 < 1200.0);
    CHECK(cUp > c0 + 100.0);
    CHECK(cDown < c0 - 100.0);
}

// --- 7. Real Morph: two envelopes interpolate ---------------------------------

static void testMorph()
{
    // Main input (dry) = 3 kHz sine -> modulator 2.
    // Sidechain = 500 Hz sine -> modulator 1.
    // (Sidechain present => it IS modulator 1, per the reference.)
    std::vector<float> dry((size_t)(kSr * 0.4), 0.0f);
    std::vector<float> sc((size_t)(kSr * 0.4), 0.0f);
    fillSine(dry, 3000.0, 0.5);
    fillSine(sc, 500.0, 0.5);

    auto runMorph = [&](float morphAmt, double& near500, double& near3000,
                        double& outRms) {
        vox8::Parameters p;
        p.mode = vox8::Mode::Morph;
        p.morph = morphAmt;
        p.air = 0.0f;
        p.transientProtect = 0.0f; // no dry transient bleed in the output
        p.carrierBlend = 1.0f; // no dry in the carrier: clean measurement
        vox8::FilterbankEngine e; initEngine(e, p);
        const std::vector<float> out = run(e, dry, &sc);
        const size_t start = (size_t)(kSr * 0.1);
        const size_t n = (size_t)(kSr * 0.25);
        // The vocoded output's energy sits at CARRIER harmonics, not at the
        // modulator frequencies: measure the harmonic nearest each modulator
        // (440 Hz for the 500 Hz modulator, 2640 + 3080 Hz for 3 kHz).
        near500 = goertzel(out.data() + start, n, 440.0);
        near3000 = goertzel(out.data() + start, n, 2640.0) +
                   goertzel(out.data() + start, n, 3080.0);
        outRms = rms(out.data() + start, n);
    };

    double a500, a3000, r0, b500, b3000, r1, m500, m3000, rm;
    runMorph(0.0f, a500, a3000, r0);
    runMorph(1.0f, b500, b3000, r1);
    runMorph(0.5f, m500, m3000, rm);

    // morph=0: output sits at modulator 1 (500 Hz), not modulator 2.
    CHECK(a500 > 2.0 * a3000);
    // morph=1: output sits at modulator 2 (3 kHz), not modulator 1.
    CHECK(b3000 > 2.0 * b500);
    // The morph moves energy between the sources: each source's energy is
    // several times stronger when it is selected than when it is not.
    CHECK(a500 > 3.0 * b500);
    CHECK(b3000 > 3.0 * a3000);
    // Midpoint: a hybrid — both present at comparable level, and the total
    // is NOT the sum of the two endpoints (that would be a crossfade).
    CHECK(m500 > 0.25 * m3000 && m500 < 4.0 * m3000);
    CHECK(rm < r0 + r1);
    CHECK(rm > 0.3 * std::max(r0, r1));
}

// --- 8. Mask: per-band pocket, no broadband pumping ----------------------------

static void testMask()
{
    // Modulator: continuous 1 kHz sine. Carrier: internal MIDI 440 Hz saw.
    std::vector<float> mod((size_t)(kSr * 0.4), 0.0f);
    fillSine(mod, 1000.0, 0.5);

    auto runMask = [&](float depth, double& near1000, double& far5000) {
        vox8::Parameters p;
        p.mode = vox8::Mode::Mask;
        p.maskDepth = depth;
        p.air = 0.0f;
        p.transientProtect = 0.0f; // no dry transient bleed in the output
        p.carrierBlend = 1.0f;
        vox8::FilterbankEngine e; initEngine(e, p);
        const std::vector<float> out = run(e, mod);
        const size_t start = (size_t)(kSr * 0.1);
        const size_t n = (size_t)(kSr * 0.25);
        // As in the morph test, the output's energy sits at carrier
        // harmonics: near = 880 + 1320 Hz (around the 1 kHz modulator),
        // far = 4400 + 4840 + 5280 Hz.
        near1000 = goertzel(out.data() + start, n, 880.0) +
                   goertzel(out.data() + start, n, 1320.0);
        far5000 = goertzel(out.data() + start, n, 4400.0) +
                  goertzel(out.data() + start, n, 4840.0) +
                  goertzel(out.data() + start, n, 5280.0);
    };

    double off1000, off5000, on1000, on5000;
    runMask(0.0f, off1000, off5000);
    runMask(0.8f, on1000, on5000);

    // The band the modulator occupies is ducked hard...
    CHECK(on1000 < 0.5 * off1000);
    // ...while a far band is barely touched: no broadband pumping.
    CHECK(on5000 > 0.7 * off5000);
}

// --- 9. Determinism ------------------------------------------------------------

static void testDeterminism()
{
    std::vector<float> in((size_t)(kSr * 0.2), 0.0f);
    fillSine(in, 440.0, 0.4);
    vox8::Parameters p;
    p.mode = vox8::Mode::Vocoder;
    vox8::FilterbankEngine e1; initEngine(e1, p);
    vox8::FilterbankEngine e2; initEngine(e2, p);
    const std::vector<float> o1 = run(e1, in);
    const std::vector<float> o2 = run(e2, in);
    CHECK(o1.size() == o2.size());
    for (size_t i = 0; i < o1.size(); ++i)
        CHECK(o1[i] == o2[i]);
}

// --- 10. Block-size and sample-rate smoke ---------------------------------------

static void testBlockSizesAndRates()
{
    const int blocks[] = {1, 16, 32, 64, 128, 512, 1024, 2048};
    for (int bs : blocks)
    {
        vox8::Parameters p;
        vox8::FilterbankEngine e; initEngine(e, p);
        std::vector<float> in(4096, 0.0f);
        fillSine(in, 880.0, 0.3);
        const std::vector<float> out = run(e, in, nullptr, bs);
        for (float v : out)
            CHECK(std::isfinite(v));
    }
    const double rates[] = {44100.0, 48000.0, 88200.0, 96000.0, 192000.0};
    for (double rate : rates)
    {
        vox8::FilterbankEngine e;
        e.prepare(rate, 2);
        vox8::Parameters p;
        e.setParameters(p);
        e.setMidiNote(69);
        std::vector<float> in(4096, 0.0f);
        std::vector<float> inR(4096, 0.0f);
        fillSine(in, 880.0, 0.3);
        fillSine(inR, 880.0, 0.3);
        float* io[2] = {in.data(), inR.data()};
        // Note: fillSine uses kSr for its phase; at other rates this is
        // just an arbitrary test signal, which is all the smoke test needs.
        e.process(io, 2, 4096, nullptr, 0);
        for (float v : in)
            CHECK(std::isfinite(v));
        for (float v : inR)
            CHECK(std::isfinite(v));
    }
}

// --- 11. Stereo channel independence ---------------------------------------
// Regression: each band's filters must hold per-channel state. Sharing one
// Biquad across channels interleaves both channels' samples through one
// state and corrupts the response (found by the STOI cross-check against
// the reference, 2026-11-05: stereo envelopes ran 2x-27x hot
// off-frequency while mono matched the reference exactly).
static void testStereoChannelIndependence()
{
    const size_t n = (size_t)(kSr * 0.5);
    std::vector<float> left(n, 0.0f), right(n, 0.0f);
    fillSine(left, 1000.0, 0.5);
    fillSine(right, 200.0, 0.5);
    vox8::Parameters p;
    p.mode = vox8::Mode::Vocoder;
    p.air = 0.0f;
    p.transientProtect = 0.0f;

    // Stereo run with different content per channel.
    vox8::FilterbankEngine eSt; initEngine(eSt, p);
    std::vector<float> stL = left, stR = right;
    for (size_t off = 0; off < n; off += 512)
    {
        const int m = (int)std::min<size_t>(512, n - off);
        float* blk[2] = {stL.data() + off, stR.data() + off};
        eSt.process(blk, 2, m, nullptr, 0);
    }

    // Mono run of the same left content: the stereo left output must
    // match it sample for sample (channel 0's carrier has no offset).
    vox8::FilterbankEngine eMo;
    eMo.prepare(kSr, 1);
    eMo.setParameters(p);
    eMo.setMidiNote(69);
    std::vector<float> mo = left;
    for (size_t off = 0; off < n; off += 512)
    {
        const int m = (int)std::min<size_t>(512, n - off);
        float* blk[1] = {mo.data() + off};
        eMo.process(blk, 1, m, nullptr, 0);
    }
    CHECK(mo.size() == stL.size());
    double maxDiff = 0.0;
    for (size_t i = 0; i < n; ++i)
        maxDiff = std::max(maxDiff, std::abs((double)mo[i] - (double)stL[i]));
    CHECK(maxDiff < 1e-6);

    // Same left content, silent right: the left output must be unchanged.
    vox8::FilterbankEngine eSt2; initEngine(eSt2, p);
    std::vector<float> st2L = left, st2R(n, 0.0f);
    for (size_t off = 0; off < n; off += 512)
    {
        const int m = (int)std::min<size_t>(512, n - off);
        float* blk[2] = {st2L.data() + off, st2R.data() + off};
        eSt2.process(blk, 2, m, nullptr, 0);
    }
    maxDiff = 0.0;
    for (size_t i = 0; i < n; ++i)
        maxDiff = std::max(maxDiff, std::abs((double)stL[i] - (double)st2L[i]));
    CHECK(maxDiff < 1e-6);
}

int main()
{
    testBandLayout();
    testBiquads();
    testSilenceAndFiniteness();
    testEnvelopeTransfer();
    testFreeze();
    testFormantShift();
    testMorph();
    testMask();
    testDeterminism();
    testBlockSizesAndRates();
    testStereoChannelIndependence();

    std::printf("%d checks, %d failures: %s\n", g_checks, g_failures,
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return g_failures == 0 ? 0 : 1;
}
