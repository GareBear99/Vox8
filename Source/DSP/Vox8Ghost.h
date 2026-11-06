// Vox8Ghost.h — Spectral Ghost 1024 STFT/OLA backend.
//
// Framework-free rebuild of the reference's Spectral Ghost 1024 engine
// (FreeVox8, FreeVox8SpectralEngine.h). Plain C++: no JUCE, no iPlug2. The
// shell owns MIDI parsing and host buffers; this engine takes plain float
// buffers and a MIDI note number.
//
// Design, per the Vox8 paper (vox8-paper.md section 4.3):
// - 1024-sample Hann window, hop 256 (75% overlap), ring buffers
//   (inputRing = carrier, modRing = modulator, olaRing = output).
//   writeIndex advances sample-by-sample; hopCountdown triggers
//   renderFrame() every 256 samples.
// - Goertzel-style analyseBins(): fixed-frequency DFT accumulation at
//   w = 2*pi*bandFreq/sr, with a PER-BAND stride (4 below sr/8, 3 below
//   sr/6, 2 below sr/4, 1 above; magnitudes normalised by stride/4).
//   A flat stride 4 ALIASES every band above sr/8 into mirrors of
//   low-frequency content (Track A, 2026-11-05) — do not go back to it.
//   Then an envelope follower (fast attack 0.24/0.76, slow release
//   0.92/0.08). The magnitudes are the modulator envelope; phase is
//   discarded — the carrier ring supplies the phase at resynthesis.
// - RESYNTHESIS — FFT cross-synthesis (redesign, 2026-11-06, Decision 5
//   / paper §6 rank 9). renderFrame() takes the windowed carrier frame,
//   FFTs it, multiplies each bin's magnitude by the modulator envelope
//   at that bin's frequency (log-interpolated from the 128 log bands,
//   formant-shifted, morph-interpolated), keeps the carrier's phase,
//   IFFTs, windows and overlap-adds (same 0.18 OLA scale as the
//   reference). This REPLACES the reference's per-sample envelope sweep
//   (e = readEnv(t*127 - shift)), which shaped each output sample by a
//   single band and averaged the spectral character away over the
//   overlapping frames — measured 2026-11-05: output on/off ratio 1.32
//   on bursts the envelope array tracks at 41.5x. The sweep is gone;
//   the 9 failing checks it caused are the redesign's acceptance tests.
// - NO 0.72 CARRIER FLOOR (redesign, Decision 2). The reference's
//   Vocoder formula passed carrier at 0.72 gain with a zero envelope
//   (silence in -> 0.083 out, -21.6 dBFS — Track A). The floor is
//   removed: the per-bin gain is e*(2.8 + clarity*4) with no base term,
//   so silence in -> silence out (the golden test, and the redesign's
//   acceptance criterion). Mask mode is the exception: it DUCKS the
//   carrier where the modulator is strong, so a silent modulator leaves
//   the carrier passing unducked — correct Mask behaviour, not a leak.
// - frozen[] leaky-updates (0.998/0.002) while freeze is off and holds
//   while freeze is on; renderFrame() reads the frozen snapshot when
//   freeze is active.
// - Formant shift is envelope remapping (10 bins per octave in this
//   engine — the Ghost scale; the filterbank uses 8. Track A is
//   reconciling the number; this engine keeps the reference Ghost value
//   until the reconciliation lands).
// - Carrier: internal MIDI carrier, Ghost blend 0.46 saw / 0.32 sine /
//   0.18 pulse, x0.72, + 28% dry (fixed — the Ghost engine has no
//   carrierBlend parameter in the reference; the filterbank's
//   carrierBlend is not read here. See docs/ARCHITECTURE.md).
// - Air: white noise weighted toward the top of the frame (t^2 * 0.018),
//   added to the IFFT'd frame before windowing (Mask mode carries no
//   Air term, as in the reference).
// - Transient Protect: the reference Ghost engine accepted the parameter
//   but never read it (a no-op as shipped). The Step 1 fix wires the
//   same first-difference gate as the filterbank: transientReturn =
//   dry * transientProtect * transient * 0.12, mixed into the wet
//   signal. It is a manual macro, not a detector; do not cite Roebel
//   2003 for it.
//
// Differences from the reference, deliberate and recorded in
// docs/ARCHITECTURE.md:
// - Morph (mode 3) is a REAL morph here: two modulator envelopes (main
//   input + sidechain) interpolated per band, e = (1-a)*e1 + a*e2,
//   imposed on one carrier. The reference's mode 3 was a blend
//   (sat(y*0.72 + car*(0.55 + ghost))). The second envelope is only
//   analysed when mode == Morph, so normal modes pay no extra CPU.
// - Mask (mode 1) is per-bin ducking: y = car*(1 - min(0.96,
//   e*maskDepth*16.0)). The multiplier is 16.0, recalibrated in the
//   redesign (the reference's 3.2 was never validated against a working
//   resynthesis and gives only a ~14% duck for a 0.5 sine at depth 0.8
//   — not an audible pocket; see renderFrame).
// - Resynthesis is FFT cross-synthesis, not the reference's envelope
//   sweep (see above); the 0.72 carrier floor is removed (see above).
//
// Real-time safety: all storage is fixed-size member arrays (including
// the FFT scratch buffers and the precomputed bit-reversal and
// bin->band tables). process() allocates nothing, locks nothing,
// touches no files, spawns no threads. Display taps are
// std::atomic<float>, memory_order_relaxed. The Goertzel accumulators
// run in double (as in the reference); the transcendental cost is
// measured in Tests/bench_ghost.cpp, not claimed. The FFT is a fixed
// 1024-point radix-2 transform with precomputed tables — deterministic,
// no allocation, ~20k butterfly ops per frame per channel.

#pragma once

#include "Vox8Core.h"
#include "Vox8Params.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace vox8 {

class GhostEngine
{
public:
    static constexpr int kFftSize = 1024;
    static constexpr int kHopSize = 256;
    static constexpr int kAnalysisStride = 4; // MAXIMUM analysis stride
                                              // (low bands); higher bands
                                              // step finer — see
                                              // analyseBins(). Never a
                                              // flat stride 4.
    static constexpr int kFrameDelaySamples = kFftSize; // 1024 samples
                                                        // (~23 ms @ 44.1k)

    // Ghost band frequencies: 55 Hz - 18.5 kHz, log-spaced over 127 steps
    // (band 127 is exactly 18.5 kHz). NOTE: the filterbank's bandEdgeLo
    // divides by 128; the Ghost reference divides by 127. Kept as shipped
    // per engine; Track A is reconciling the layout.
    static float bandFreq(int band) noexcept
    {
        const float t = static_cast<float>(std::min(kNumBands - 1,
                                                    std::max(0, band))) /
                        static_cast<float>(kNumBands - 1);
        return kBandMinHz * std::pow(kBandMaxHz / kBandMinHz, t);
    }

    void prepare(double sampleRate, int numChannels) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        channels = std::min(kMaxChannels, std::max(1, numChannels));
        for (int i = 0; i < kFftSize; ++i)
        {
            const double t = static_cast<double>(i) /
                             static_cast<double>(kFftSize - 1);
            window[static_cast<size_t>(i)] =
                static_cast<float>(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * t));
        }
        // Bit-reversal table for the 1024-point FFT (10 bits).
        for (int i = 0; i < kFftSize; ++i)
        {
            int r = 0;
            for (int b = 0; b < 10; ++b)
                if (i & (1 << b))
                    r |= 1 << (9 - b);
            bitRev[static_cast<size_t>(i)] = r;
        }
        // Per-FFT-bin band position: bin k sits at f = k*sr/1024; the
        // Ghost band layout is log-spaced (55 Hz - 18.5 kHz over 127
        // steps), so the band position is 127*log(f/55)/log(18500/55),
        // clamped to [0, 127]. Precomputed per prepare() (sr is fixed
        // after prepare); the formant shift is applied at read time.
        const double logRange = std::log(18500.0 / 55.0);
        for (int k = 0; k <= kFftSize / 2; ++k)
        {
            const double f = static_cast<double>(k) * sr /
                             static_cast<double>(kFftSize);
            float pos = 0.0f;
            if (f > 55.0)
                pos = static_cast<float>(127.0 * std::log(f / 55.0) / logRange);
            binBandPos[static_cast<size_t>(k)] = clampf(pos, 0.0f, 127.0f);
        }
        reset();
    }

    void reset() noexcept
    {
        for (auto& a : inputRing)
            a.fill(0.0f);
        for (auto& a : modRing)
            a.fill(0.0f);
        for (auto& a : modRingB)
            a.fill(0.0f);
        for (auto& a : olaRing)
            a.fill(0.0f);
        for (auto& a : env)
            a.fill(0.0f);
        for (auto& a : frozen)
            a.fill(0.0f);
        for (auto& a : envB)
            a.fill(0.0f);
        for (auto& a : frozenB)
            a.fill(0.0f);
        for (auto& v : displayBins)
            v.store(0.0f, std::memory_order_relaxed);
        phase.fill(0.0);
        previousInput.fill(0.0f);
        writeIndex = 0;
        hopCountdown = kHopSize;
        currentMidiNote = 48;
        noise = NoiseSource{};
        framesRendered = 0;
        goertzelCalls = 0;
    }

    void setParameters(const Parameters& p) noexcept { params = p; }
    void setMidiNote(int note) noexcept
    {
        currentMidiNote = std::min(127, std::max(0, note));
    }
    int getMidiNote() const noexcept { return currentMidiNote; }

    // In-place processing. Same routing contract as FilterbankEngine:
    //   io: [channel][sample] dry input (also modulator 1 when no
    //       sidechain is present).
    //   sidechain: [channel][sample] second modulator source, or nullptr.
    //       When present it is modulator 1 (the reference behaviour);
    //       the main input becomes modulator 2 for Morph. When absent,
    //       modulator 1 = dry input and Morph falls back to envelope 1.
    //   numSamples: any block size 1..2048 and beyond.
    void process(float* const* io, int numChannels, int numSamples,
                 const float* const* sidechain, int sidechainChannels) noexcept
    {
        const int numCh = std::min(channels, numChannels);
        if (numSamples <= 0 || numCh <= 0 || io == nullptr)
            return;

        const float wetMix = clampf(params.mix, 0.0f, 1.0f);
        const float dryMix = 1.0f - wetMix;
        const float outGain = decibelsToGain(params.outputGainDb);
        const float transientProtect = clampf(params.transientProtect, 0.0f, 1.0f);
        const float stereoWidth = clampf(params.stereoWidth, 0.0f, 2.0f);
        const bool morphMode = params.mode == Mode::Morph;
        const bool hasSidechain = sidechain != nullptr && sidechainChannels > 0;

        for (int n = 0; n < numSamples; ++n)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                float* x = io[ch];
                const float dry = x[n];
                const float* sc = hasSidechain
                    ? sidechain[std::min(ch, sidechainChannels - 1)]
                    : nullptr;
                const float modulatorA = sc ? sc[n] : dry;
                const float modulatorB = dry; // second morph source

                inputRing[static_cast<size_t>(ch)]
                         [static_cast<size_t>(writeIndex)] =
                    makeCarrier(dry, ch);
                modRing[static_cast<size_t>(ch)]
                       [static_cast<size_t>(writeIndex)] = modulatorA;
                modRingB[static_cast<size_t>(ch)]
                        [static_cast<size_t>(writeIndex)] = modulatorB;

                // Transient Protect: manual macro, dry transient bleed.
                // Same first-difference gate as the filterbank (the Step 1
                // fix — the reference Ghost engine never read this
                // parameter). No detector exists; do not describe it as one.
                const float transient = clampf(
                    std::abs(dry - previousInput[static_cast<size_t>(ch)]) * 12.0f,
                    0.0f, 1.0f);
                previousInput[static_cast<size_t>(ch)] = dry;
                const float transientReturn =
                    dry * transientProtect * transient * 0.12f;

                float wet = olaRing[static_cast<size_t>(ch)]
                                   [static_cast<size_t>(writeIndex)];
                olaRing[static_cast<size_t>(ch)]
                       [static_cast<size_t>(writeIndex)] = 0.0f;
                x[n] = fastSaturate((dry * dryMix +
                                     (wet + transientReturn) * wetMix) *
                                    outGain);
            }

            writeIndex = (writeIndex + 1) & (kFftSize - 1);
            if (--hopCountdown <= 0)
            {
                hopCountdown = kHopSize;
                renderFrame(numCh, morphMode, hasSidechain);
            }
        }

        applyStereoWidth(io, numSamples, numCh, stereoWidth);
    }

    float getDisplayBin(int index) const noexcept
    {
        index = std::min(kNumBands - 1, std::max(0, index));
        return displayBins[static_cast<size_t>(index)].load(
            std::memory_order_relaxed);
    }

    int getDisplayBinCount() const noexcept { return kNumBands; }

    // Test/bench access: the live envelope of band `index`, channel `ch`
    // (frozen value when freeze is on). Not a hot-path function.
    float getEnvelopeForTest(int index, int ch, bool second) const noexcept
    {
        index = std::min(kNumBands - 1, std::max(0, index));
        ch = std::min(kMaxChannels - 1, std::max(0, ch));
        const bool freezeOn = params.freeze > 0.5f;
        if (second)
            return freezeOn ? frozenB[static_cast<size_t>(ch)]
                                   [static_cast<size_t>(index)]
                            : envB[static_cast<size_t>(ch)]
                                  [static_cast<size_t>(index)];
        return freezeOn ? frozen[static_cast<size_t>(ch)]
                               [static_cast<size_t>(index)]
                        : env[static_cast<size_t>(ch)]
                              [static_cast<size_t>(index)];
    }

    // Test access: the frozen snapshot directly, regardless of the freeze
    // parameter (the freeze-blur pilot reads this). Not a hot-path function.
    float getFrozenForTest(int index, int ch, bool second) const noexcept
    {
        index = std::min(kNumBands - 1, std::max(0, index));
        ch = std::min(kMaxChannels - 1, std::max(0, ch));
        if (second)
            return frozenB[static_cast<size_t>(ch)]
                          [static_cast<size_t>(index)];
        return frozen[static_cast<size_t>(ch)][static_cast<size_t>(index)];
    }

    // Test access: the envelope value renderFrame() would read at a given
    // band position — i.e. the formant-shifted, morph-interpolated read.
    // This is how Morph and formant shift are verified at the envelope
    // level (where they live), independent of the resynthesis path.
    // Not a hot-path function.
    float getMorphedEnvelopeForTest(float bandPosition, int ch) const noexcept
    {
        ch = std::min(kMaxChannels - 1, std::max(0, ch));
        const bool freezeOn = params.freeze > 0.5f;
        const float shift = params.formantShift / 12.0f * 10.0f;
        const float pos = bandPosition - shift;
        float e = readEnv(env[static_cast<size_t>(ch)],
                          frozen[static_cast<size_t>(ch)], pos, freezeOn);
        if (params.mode == Mode::Morph)
        {
            const float eB = readEnv(envB[static_cast<size_t>(ch)],
                                     frozenB[static_cast<size_t>(ch)], pos,
                                     freezeOn);
            const float morphAmt = clampf(params.morph, 0.0f, 1.0f);
            e = (1.0f - morphAmt) * e + morphAmt * eB;
        }
        return e;
    }

    // Measurement counters (bench + tests). goertzelCalls counts every
    // std::cos / std::sin evaluation inside analyseBins — the ~11M/sec
    // figure from the paper, measured for real in Tests/bench_ghost.cpp.
    uint64_t getFramesRendered() const noexcept { return framesRendered; }
    uint64_t getGoertzelCalls() const noexcept { return goertzelCalls; }

private:
    using Frame = std::array<float, kFftSize>;
    using Spec = std::array<float, kNumBands>;

    std::array<Frame, kMaxChannels> inputRing{};
    std::array<Frame, kMaxChannels> modRing{};
    std::array<Frame, kMaxChannels> modRingB{}; // second morph source
    std::array<Frame, kMaxChannels> olaRing{};
    std::array<Spec, kMaxChannels> env{};
    std::array<Spec, kMaxChannels> frozen{};
    std::array<Spec, kMaxChannels> envB{}; // second morph envelope
    std::array<Spec, kMaxChannels> frozenB{};
    std::array<float, kFftSize> window{};
    std::array<float, kFftSize> fftRe{}; // FFT scratch (per frame, per ch)
    std::array<float, kFftSize> fftIm{};
    std::array<int, kFftSize> bitRev{};  // FFT bit-reversal table (prepare)
    std::array<float, kFftSize / 2 + 1> binBandPos{}; // FFT bin -> log-band
                                                      // position (prepare)
    std::array<std::atomic<float>, kNumBands> displayBins{};
    std::array<double, kMaxChannels> phase{0.0, 0.0};
    std::array<float, kMaxChannels> previousInput{0.0f, 0.0f};
    Parameters params;
    double sr = 44100.0;
    int channels = kMaxChannels;
    int currentMidiNote = 48;
    int writeIndex = 0;
    int hopCountdown = kHopSize;
    NoiseSource noise;
    uint64_t framesRendered = 0;
    uint64_t goertzelCalls = 0;

    // FFT cross-synthesis (redesign, 2026-11-06, Decision 5 / paper §6
    // rank 9). Replaces the reference's per-sample envelope sweep, which
    // shaped each output sample by a single band and averaged the
    // spectral character away (measured: output on/off 1.32 on bursts
    // the envelope array tracks at 41.5x). The carrier frame is FFT'd,
    // each bin's magnitude is multiplied by the modulator envelope at
    // that bin's frequency (log-interpolated from the 128 log bands,
    // formant-shifted, morph-interpolated), the carrier's phase is
    // kept, and the IFFT'd frame is windowed and overlap-added (same
    // 0.18 OLA scale as the reference). The 0.72 carrier floor is GONE
    // (Decision 2): the per-bin gain is e*(2.8 + clarity*4) with no base
    // term, so silence in -> silence out. Mask mode is the exception
    // (it ducks the carrier, so a silent modulator leaves the carrier
    // passing unducked — correct Mask behaviour, not a leak).
    void renderFrame(int numCh, bool morphMode, bool hasSidechain) noexcept
    {
        ++framesRendered;
        const bool freezeOn = params.freeze > 0.5f;
        const float clarity = clampf(params.clarity, 0.0f, 1.0f);
        const float ghost = clampf(params.ghost, 0.0f, 1.0f);
        const float air = clampf(params.air, 0.0f, 1.0f);
        const float maskDepth = clampf(params.maskDepth, 0.0f, 1.0f);
        const float morphAmt = clampf(params.morph, 0.0f, 1.0f);
        // Ghost formant scale: 10 bins per octave. NOTE: the filterbank
        // uses 8 bins per octave in the reference — the two engines
        // disagree, and Track A is reconciling the number. This engine
        // keeps the reference Ghost value until the reconciliation lands.
        const float shift = params.formantShift / 12.0f * 10.0f;
        // Vocoder per-bin gain: the reference's envelope drive, now
        // applied per FFT bin instead of per sample. NO 0.72 floor.
        const float vocGain = 2.8f + clarity * 4.0f;
        const float ghostMul = 1.0f + ghost * 2.4f;
        const bool isMask = params.mode == Mode::Mask;

        for (int ch = 0; ch < numCh; ++ch)
        {
            analyseBins(ch, freezeOn, /*second=*/false);
            if (morphMode && hasSidechain)
                analyseBins(ch, freezeOn, /*second=*/true);

            // 1. Windowed carrier frame -> FFT. The carrier's phase is
            // kept; the envelope is imposed on magnitudes only.
            for (int i = 0; i < kFftSize; ++i)
            {
                const int idx = (writeIndex + i) & (kFftSize - 1);
                fftRe[static_cast<size_t>(i)] =
                    inputRing[static_cast<size_t>(ch)]
                             [static_cast<size_t>(idx)] *
                    window[static_cast<size_t>(i)];
                fftIm[static_cast<size_t>(i)] = 0.0f;
            }
            fft1024(fftRe.data(), fftIm.data(), /*inverse=*/false);

            // 2. Per-bin cross-synthesis gain from the envelope. The
            // gain is real and even, so the IFFT stays real.
            for (int k = 0; k <= kFftSize / 2; ++k)
            {
                const float e = readEnvelopeAtFreq(
                    ch, k, freezeOn, morphMode, hasSidechain, morphAmt,
                    shift);
                float g = 0.0f;
                switch (params.mode)
                {
                    case Mode::Vocoder:
                        g = e * vocGain;
                        break;
                    case Mode::Mask:
                        // Per-bin ducking, up to 96% suppression. The
                        // multiplier is 16.0, NOT the reference's 3.2:
                        // the reference value was never validated
                        // against a working resynthesis (the sweep
                        // smeared the duck away), and 3.2 gives only a
                        // ~14% duck for a 0.5-amplitude sine at depth
                        // 0.8 — barely audible, not a pocket. 16.0 makes
                        // that signal duck ~69% at its peak band (an
                        // audible pocket), and a full-scale modulator at
                        // depth 1.0 still clamps at 0.96. Redesign
                        // calibration, 2026-11-06 — see Tests.
                        g = 1.0f - std::min(0.96f, e * maskDepth * 16.0f);
                        break;
                    case Mode::Ghost:
                        // Ghost as a mode is an intensifier on the
                        // vocoded signal; the freeze itself is the
                        // frozen[] array, which any mode can read.
                        g = e * vocGain * ghostMul;
                        break;
                    case Mode::Morph:
                        // Real morph: the envelope interpolation above
                        // is the morph. g is the vocoded signal from
                        // the morphed envelope — not the reference's
                        // blend (sat(y*0.72 + car*(0.55 + ghost))).
                        g = e * vocGain;
                        break;
                }
                fftRe[static_cast<size_t>(k)] *= g;
                fftIm[static_cast<size_t>(k)] *= g;
                if (k > 0 && k < kFftSize / 2)
                {
                    fftRe[static_cast<size_t>(kFftSize - k)] *= g;
                    fftIm[static_cast<size_t>(kFftSize - k)] *= g;
                }
            }

            // 3. IFFT -> window -> OLA (same 0.18 scale as the
            // reference). Air is added to the IFFT'd frame before
            // windowing (Mask mode carries no Air term, as shipped).
            fft1024(fftRe.data(), fftIm.data(), /*inverse=*/true);
            for (int i = 0; i < kFftSize; ++i)
            {
                const int idx = (writeIndex + i) & (kFftSize - 1);
                const float t = static_cast<float>(i) /
                                static_cast<float>(kFftSize - 1);
                float y = fftRe[static_cast<size_t>(i)];
                if (!isMask)
                    y += air * t * t * noise.next() * 0.018f;
                olaRing[static_cast<size_t>(ch)][static_cast<size_t>(idx)] +=
                    y * window[static_cast<size_t>(i)] * 0.18f;
            }
        }
    }

    // Iterative radix-2 FFT, 1024 points, in place. Forward: X[k] =
    // sum x[n] exp(-2*pi*i*k*n/N). Inverse: same with +angle, scaled by
    // 1/N. Uses the precomputed bit-reversal table (bitRev).
    // Deterministic, allocation-free, ~10k butterfly ops per call.
    void fft1024(float* re, float* im, bool inverse) noexcept
    {
        for (int i = 0; i < kFftSize; ++i)
        {
            const int j = bitRev[static_cast<size_t>(i)];
            if (j > i)
            {
                std::swap(re[i], re[j]);
                std::swap(im[i], im[j]);
            }
        }
        for (int len = 2; len <= kFftSize; len <<= 1)
        {
            const double ang = (inverse ? 2.0 : -2.0) *
                               3.14159265358979 / static_cast<double>(len);
            const double wr = std::cos(ang);
            const double wi = std::sin(ang);
            const int half = len >> 1;
            for (int i = 0; i < kFftSize; i += len)
            {
                double cwr = 1.0, cwi = 0.0;
                for (int j = 0; j < half; ++j)
                {
                    const int u = i + j;
                    const int v = i + j + half;
                    const double vr = static_cast<double>(re[v]) * cwr -
                                      static_cast<double>(im[v]) * cwi;
                    const double vi = static_cast<double>(re[v]) * cwi +
                                      static_cast<double>(im[v]) * cwr;
                    const double ur = static_cast<double>(re[u]);
                    const double ui = static_cast<double>(im[u]);
                    re[v] = static_cast<float>(ur - vr);
                    im[v] = static_cast<float>(ui - vi);
                    re[u] = static_cast<float>(ur + vr);
                    im[u] = static_cast<float>(ui + vi);
                    const double nwr = cwr * wr - cwi * wi;
                    cwi = cwr * wi + cwi * wr;
                    cwr = nwr;
                }
            }
        }
        if (inverse)
        {
            const float s = 1.0f / static_cast<float>(kFftSize);
            for (int i = 0; i < kFftSize; ++i)
            {
                re[i] *= s;
                im[i] *= s;
            }
        }
    }

    // The envelope value renderFrame() reads at a given FFT bin — i.e.
    // the formant-shifted, morph-interpolated read at the bin's
    // frequency. The bin->band position is precomputed in prepare();
    // the formant shift is applied here, at read time.
    float readEnvelopeAtFreq(int ch, int bin, bool freezeOn, bool morphMode,
                             bool hasSidechain, float morphAmt,
                             float shift) const noexcept
    {
        const float pos = binBandPos[static_cast<size_t>(bin)] - shift;
        float e = readEnv(env[static_cast<size_t>(ch)],
                          frozen[static_cast<size_t>(ch)], pos, freezeOn);
        if (morphMode && hasSidechain)
        {
            const float eB = readEnv(envB[static_cast<size_t>(ch)],
                                     frozenB[static_cast<size_t>(ch)], pos,
                                     freezeOn);
            e = (1.0f - morphAmt) * e + morphAmt * eB;
        }
        return e;
    }

    void analyseBins(int ch, bool freezeOn, bool second) noexcept
    {
        // Per-band stride (Track A finding, 2026-11-05 — do NOT go back to
        // a flat stride 4): the Goertzel sum at stride s is alias-free
        // only below sr/(2*s). Stride 4 is safe only below sr/8 (~5512 Hz
        // at 44.1 kHz); above that, a flat stride-4 accumulation mirrors
        // low-frequency content into the high bands (a pure 676 Hz sine
        // lit band 117 at 11,701 Hz STRONGER than its true band 55).
        // Stride drops 4 -> 3 -> 2 -> 1 as the band frequency rises, and
        // the magnitude is normalised by stride/4 (a stride-s sum has 4/s
        // times as many terms as the stride-4 sum the 0.0018 scale
        // assumes). At 44.1 kHz the split is 101 / 6 / 9 / 12 bands, and
        // the cost rises ~11.3M -> ~15.4M transcendental calls/sec/channel
        // — the price of correctness, measured in Tests/bench_ghost.cpp.
        const auto& ring = second ? modRingB[static_cast<size_t>(ch)]
                                  : modRing[static_cast<size_t>(ch)];
        auto& envArr = second ? envB[static_cast<size_t>(ch)]
                              : env[static_cast<size_t>(ch)];
        auto& frozenArr = second ? frozenB[static_cast<size_t>(ch)]
                                 : frozen[static_cast<size_t>(ch)];

        for (int b = 0; b < kNumBands; ++b)
        {
            const float freq = bandFreq(b);
            const double w = 2.0 * 3.14159265358979 *
                             static_cast<double>(freq) / sr;
            int stride = kAnalysisStride;
            while (stride > 1 && sr / (2.0 * stride) <= freq)
                --stride;
            // Exact transcendental count for this band: one cos + one sin
            // per accumulation step.
            goertzelCalls += static_cast<uint64_t>(
                2 * ((kFftSize + stride - 1) / stride));
            double re = 0.0, im = 0.0;
            for (int i = 0; i < kFftSize; i += stride)
            {
                const int idx = (writeIndex + i) & (kFftSize - 1);
                const double a = w * static_cast<double>(i);
                const double s =
                    static_cast<double>(ring[static_cast<size_t>(idx)]) *
                    static_cast<double>(window[static_cast<size_t>(i)]);
                re += s * std::cos(a);
                im -= s * std::sin(a);
            }
            const float mag =
                static_cast<float>(std::sqrt(re * re + im * im)) * 0.0018f *
                static_cast<float>(stride) * 0.25f;
            float& e = envArr[static_cast<size_t>(b)];
            e = mag > e ? 0.24f * e + 0.76f * mag
                        : 0.92f * e + 0.08f * mag;

            if (!freezeOn)
                frozenArr[static_cast<size_t>(b)] =
                    0.998f * frozenArr[static_cast<size_t>(b)] + 0.002f * e;

            if (!second)
            {
                const float visible = freezeOn
                    ? frozenArr[static_cast<size_t>(b)]
                    : e;
                const float previous =
                    displayBins[static_cast<size_t>(b)].load(
                        std::memory_order_relaxed);
                displayBins[static_cast<size_t>(b)].store(
                    previous * 0.88f +
                        clampf(visible * 8.0f, 0.0f, 1.0f) * 0.12f,
                    std::memory_order_relaxed);
            }
        }
    }

    static float readEnv(const Spec& e, const Spec& f, float pos,
                         bool freezeOn) noexcept
    {
        const Spec& s = freezeOn ? f : e;
        const float p = clampf(pos, 0.0f, static_cast<float>(kNumBands - 1));
        const int lo = static_cast<int>(std::floor(p));
        const int hi = std::min(kNumBands - 1, lo + 1);
        const float t = p - static_cast<float>(lo);
        return s[static_cast<size_t>(lo)] +
               (s[static_cast<size_t>(hi)] - s[static_cast<size_t>(lo)]) * t;
    }

    // Internal MIDI carrier. Ghost blend, as shipped in the reference's
    // Spectral Ghost 1024 engine: 0.46 saw / 0.32 sine / 0.18 pulse,
    // x0.72, + 28% dry. NOTE: this is FIXED — the reference Ghost engine
    // has no carrierBlend parameter (that belongs to the filterbank
    // carrier). Track A is reconciling the two blends; this engine keeps
    // the reference Ghost blend until the reconciliation lands.
    float makeCarrier(float dry, int channel) noexcept
    {
        const float hz = midiNoteToHz(currentMidiNote);
        auto& ph = phase[static_cast<size_t>(
            std::min(kMaxChannels - 1, std::max(0, channel)))];
        ph += 2.0 * 3.14159265358979 * hz / sr;
        if (ph > 2.0 * 3.14159265358979)
            ph -= 2.0 * 3.14159265358979;

        const float sine =
            static_cast<float>(std::sin(ph + 0.13 * channel));
        const float saw = static_cast<float>(ph / 3.14159265358979 - 1.0);
        const float pulse = sine >= 0.0f ? 0.52f : -0.52f;
        return (0.46f * saw + 0.32f * sine + 0.18f * pulse) * 0.72f +
               dry * 0.28f;
    }

    static void applyStereoWidth(float* const* io, int numSamples, int numCh,
                                 float width) noexcept
    {
        if (numCh < 2 || std::abs(width - 1.0f) < 0.001f)
            return;
        float* l = io[0];
        float* r = io[1];
        for (int i = 0; i < numSamples; ++i)
        {
            const float mid = 0.5f * (l[i] + r[i]);
            const float side = 0.5f * (l[i] - r[i]) * width;
            l[i] = mid + side;
            r[i] = mid - side;
        }
    }
};

} // namespace vox8
