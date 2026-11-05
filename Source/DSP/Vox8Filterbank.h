// Vox8Filterbank.h — Low Latency 128 filterbank vocoder engine.
//
// Framework-free rebuild of the reference's Low Latency 128 engine
// (FreeVox8, FreeVox8DSP.h). Plain C++: no JUCE, no iPlug2. The shell owns
// MIDI parsing and host buffers; this engine takes plain float buffers and
// a MIDI note number.
//
// Design, per the Vox8 paper (vox8-paper.md section 4.2):
// - 128 log-spaced bands, 55 Hz-18.5 kHz, geometric edges.
// - Per band, per channel: modulator HP+LP and carrier HP+LP (RBJ biquads,
//   Q=0.707), built once in prepare(), never rebuilt in process(). 768
//   filter instances per channel (6 per band), evaluated every sample —
//   that is the CPU story of this engine; measure it
//   (Tests/bench_filterbank.cpp) rather than claiming anything about it.
//   The filters are per channel, not shared: sharing one filter instance
//   across channels interleaves the two channels' samples through one
//   state, which corrupts the response (found by the STOI cross-check
//   against the reference, 2026-11-05: stereo envelopes ran 2x-27x hot
//   off-frequency, mono matched the reference exactly).
// - Per-band envelope followers, asymmetric attack/release from clarity:
//   attack 0.75-9.75 ms, release 22-242 ms.
// - Freeze: when freeze > 0.5 the engine stops writing frozenEnv and reads
//   the frozen snapshot indefinitely. The 0.0008 leak makes the snapshot a
//   slowly-converging running average, not a single-frame capture.
// - Formant shift is envelope remapping (8 bins per octave in this engine),
//   never a spectral shift.
// - Carrier: internal MIDI carrier, saw/sine/pulse/sub blend.
// - Air: white noise excitation for fricatives.
// - Transient Protect is a manual macro (dry transient bleed), NOT a
//   detector. Do not cite Roebel 2003 for it.
//
// Differences from the reference, deliberate and recorded in
// docs/ARCHITECTURE.md:
// - Morph (mode 3) is a REAL morph here: two modulator envelopes (main
//   input + sidechain) interpolated per band, e = (1-a)*e1 + a*e2, imposed
//   on one carrier. The reference's mode 3 was a blend, not a morph.
// - Mask (mode 1) is per-band carrier ducking here, up to 96%, the same
//   formula the Ghost engine uses. The reference's filterbank Mask was a
//   broadband reduction driven by the summed envelope — that pumps, which
//   is the failure the golden tests forbid.
// - The second envelope is only analysed when mode == Morph, so the normal
//   modes pay no extra CPU for it.
//
// Real-time safety: all storage is fixed-size member arrays. process()
// allocates nothing, locks nothing, touches no files, spawns no threads.
// Display taps are std::atomic<float>, memory_order_relaxed.

#pragma once

#include "Vox8Core.h"
#include "Vox8Params.h"

#include <array>
#include <atomic>
#include <cmath>

namespace vox8 {

class FilterbankEngine
{
public:
    void prepare(double sampleRate, int numChannels) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        channels = std::min(kMaxChannels, std::max(1, numChannels));

        for (auto& b : bands)
        {
            b.envA.fill(0.0f);
            b.envB.fill(0.0f);
            b.frozenA.fill(0.0f);
            b.frozenB.fill(0.0f);
        }

        buildBandFilters();
        reset();
    }

    void reset() noexcept
    {
        for (auto& b : bands)
        {
            for (int ch = 0; ch < kMaxChannels; ++ch)
            {
                b.modLowpass[static_cast<size_t>(ch)].reset();
                b.modHighpass[static_cast<size_t>(ch)].reset();
                b.modLowpassB[static_cast<size_t>(ch)].reset();
                b.modHighpassB[static_cast<size_t>(ch)].reset();
                b.carLowpass[static_cast<size_t>(ch)].reset();
                b.carHighpass[static_cast<size_t>(ch)].reset();
            }
            b.envA.fill(0.0f);
            b.envB.fill(0.0f);
            b.frozenA.fill(0.0f);
            b.frozenB.fill(0.0f);
        }
        for (auto& v : displayBins)
            v.store(0.0f, std::memory_order_relaxed);
        phase.fill(0.0);
        previousInput.fill(0.0f);
        currentMidiNote = 48;
        noise = NoiseSource{};
    }

    void setParameters(const Parameters& p) noexcept { params = p; }
    void setMidiNote(int note) noexcept
    {
        currentMidiNote = std::min(127, std::max(0, note));
    }
    int getMidiNote() const noexcept { return currentMidiNote; }

    // In-place processing.
    //   io: [channel][sample] audio buffers (the dry input; also modulator 1
    //       when no sidechain is present). The internal MIDI carrier is the
    //       carrier source; the dry input is blended into it via carrierBlend.
    //   sidechain: [channel][sample] second modulator source, or nullptr.
    //       When present it is modulator 1 (the reference behaviour: the
    //       sidechain IS the modulator); the main input becomes modulator 2
    //       for Morph. When absent, modulator 1 = dry input and Morph has no
    //       second source (morph falls back to envelope 1, documented).
    //   numSamples: block size; any value 1..2048 and beyond is supported.
    void process(float* const* io, int numChannels, int numSamples,
                 const float* const* sidechain, int sidechainChannels) noexcept
    {
        const int numCh = std::min(channels, numChannels);
        if (numSamples <= 0 || numCh <= 0 || io == nullptr)
            return;

        const float midiHz = midiNoteToHz(currentMidiNote);
        const float wetMix = clampf(params.mix, 0.0f, 1.0f);
        const float dryMix = 1.0f - wetMix;
        const float outGain = decibelsToGain(params.outputGainDb);
        const float clarity = clampf(params.clarity, 0.0f, 1.0f);
        const float ghost = clampf(params.ghost, 0.0f, 1.0f);
        const float maskDepth = clampf(params.maskDepth, 0.0f, 1.0f);
        const float air = clampf(params.air, 0.0f, 1.0f);
        const float transientProtect = clampf(params.transientProtect, 0.0f, 1.0f);
        const float stereoWidth = clampf(params.stereoWidth, 0.0f, 2.0f);
        const float quality = clampf(params.quality, 0.0f, 1.0f);
        const float morphAmt = clampf(params.morph, 0.0f, 1.0f);
        const bool freezeOn = params.freeze > 0.5f;
        const bool morphMode = params.mode == Mode::Morph;
        const bool hasSidechain = sidechain != nullptr && sidechainChannels > 0;

        const float attackMs = 0.75f + (1.0f - clarity) * 9.0f;
        const float releaseMs = 22.0f + (1.0f - clarity) * 220.0f;
        const float attackCoeff = coeffFromMs(attackMs, sr);
        const float releaseCoeff = coeffFromMs(releaseMs, sr);
        // Filterbank formant scale: 8 bins per octave. NOTE: the Ghost
        // engine uses 10 bins per octave in the reference — the two engines
        // disagree, and Track A is reconciling the number. This engine keeps
        // the reference filterbank value until the reconciliation lands.
        const float formantBins = params.formantShift / 12.0f * 8.0f;
        const float drive = 1.10f + quality * 0.90f;
        const float envGain = 2.15f + clarity * 4.25f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* x = io[ch];
            const float* sc = hasSidechain
                ? sidechain[std::min(ch, sidechainChannels - 1)]
                : nullptr;

            for (int n = 0; n < numSamples; ++n)
            {
                const float dry = x[n];
                const float modulatorA = sc ? sc[n] : dry;
                const float modulatorB = dry; // second morph source
                const float carrier = makeCarrierSample(midiHz, dry, ch);
                const float transient = clampf(
                    std::abs(dry - previousInput[static_cast<size_t>(ch)]) * 12.0f,
                    0.0f, 1.0f);
                previousInput[static_cast<size_t>(ch)] = dry;

                analyseEnvelope(ch, modulatorA, attackCoeff, releaseCoeff,
                                freezeOn, /*second=*/false);
                if (morphMode && hasSidechain)
                    analyseEnvelope(ch, modulatorB, attackCoeff, releaseCoeff,
                                    freezeOn, /*second=*/true);

                float vocoded = 0.0f;
                float masked = 0.0f;
                float envelopeSum = 0.0f;

                for (int bi = 0; bi < kNumBands; ++bi)
                {
                    const float eA = readShiftedEnvelope(
                        ch, static_cast<float>(bi) - formantBins, freezeOn,
                        /*second=*/false);
                    float e = eA;
                    if (morphMode && hasSidechain)
                    {
                        const float eB = readShiftedEnvelope(
                            ch, static_cast<float>(bi) - formantBins, freezeOn,
                            /*second=*/true);
                        e = (1.0f - morphAmt) * eA + morphAmt * eB;
                    }

                    auto& b = bands[static_cast<size_t>(bi)];
                    const float bandCarrier =
                        b.carHighpass[static_cast<size_t>(ch)].process(
                            b.carLowpass[static_cast<size_t>(ch)].process(
                                carrier));

                    // Vocoder path.
                    const float consonantLift =
                        1.0f + air * highBandWeight(bi) * 1.6f;
                    const float shaped =
                        bandCarrier * e * consonantLift * envGain;
                    vocoded += shaped * bandGain(bi);

                    // Mask path: per-band ducking, up to 96% suppression.
                    const float duck =
                        1.0f - std::min(0.96f, e * maskDepth * 3.2f);
                    masked += bandCarrier * duck * bandGain(bi);

                    envelopeSum += e;
                }

                envelopeSum *= 1.0f / static_cast<float>(kNumBands);

                const float breath =
                    air * noise.next() * (0.010f + envelopeSum * 0.085f);
                // Transient Protect: manual macro, dry transient bleed.
                // No detector exists; do not describe it as one.
                const float transientReturn =
                    dry * transientProtect * transient * 0.12f;
                vocoded = vocoded * drive + breath + transientReturn;

                float wet = vocoded;
                switch (params.mode)
                {
                    case Mode::Vocoder:
                        break;
                    case Mode::Mask:
                        wet = masked + transientReturn;
                        break;
                    case Mode::Ghost:
                        // Ghost as a mode is an intensifier on the vocoded
                        // signal; the freeze itself is the frozenEnv array,
                        // which any mode can read.
                        wet = vocoded * (1.0f + ghost * 1.95f) +
                              dry * ghost * 0.06f;
                        break;
                    case Mode::Morph:
                        // Real morph: the envelope interpolation above is
                        // the morph. wet is the vocoded signal from the
                        // morphed envelope — not a blend with the carrier.
                        wet = vocoded;
                        break;
                }

                x[n] = fastSaturate((dry * dryMix + wet * wetMix) * outGain);
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
        const auto& b = bands[static_cast<size_t>(index)];
        const bool freezeOn = params.freeze > 0.5f;
        if (second)
            return freezeOn ? b.frozenB[static_cast<size_t>(ch)]
                            : b.envB[static_cast<size_t>(ch)];
        return freezeOn ? b.frozenA[static_cast<size_t>(ch)]
                        : b.envA[static_cast<size_t>(ch)];
    }

private:
    struct Band
    {
        // One filter instance per channel: sharing a Biquad across
        // channels interleaves both channels' samples through one state
        // and corrupts the response (see the header note).
        std::array<Biquad, kMaxChannels> modLowpass, modHighpass;   // modulator path, envelope 1
        std::array<Biquad, kMaxChannels> modLowpassB, modHighpassB; // modulator path, envelope 2
        std::array<Biquad, kMaxChannels> carLowpass, carHighpass;   // carrier path
        std::array<float, kMaxChannels> envA{};
        std::array<float, kMaxChannels> envB{};
        std::array<float, kMaxChannels> frozenA{};
        std::array<float, kMaxChannels> frozenB{};
    };

    std::array<Band, kNumBands> bands;
    std::array<std::atomic<float>, kNumBands> displayBins{};
    Parameters params;
    double sr = 44100.0;
    int channels = kMaxChannels;
    int currentMidiNote = 48;
    std::array<double, kMaxChannels> phase{0.0, 0.0};
    std::array<float, kMaxChannels> previousInput{0.0f, 0.0f};
    NoiseSource noise;

    void buildBandFilters() noexcept
    {
        for (int i = 0; i < kNumBands; ++i)
        {
            const float lo = bandEdgeLo(i, sr);
            const float hi = bandEdgeHi(i, sr);
            auto& b = bands[static_cast<size_t>(i)];
            for (int ch = 0; ch < kMaxChannels; ++ch)
            {
                const size_t c = static_cast<size_t>(ch);
                b.modHighpass[c].setHighpass(lo, static_cast<float>(sr));
                b.carHighpass[c].setHighpass(lo, static_cast<float>(sr));
                b.modHighpassB[c].setHighpass(lo, static_cast<float>(sr));
                b.modLowpass[c].setLowpass(hi, static_cast<float>(sr));
                b.carLowpass[c].setLowpass(hi, static_cast<float>(sr));
                b.modLowpassB[c].setLowpass(hi, static_cast<float>(sr));
            }
        }
    }

    void analyseEnvelope(int ch, float modulator, float attackCoeff,
                         float releaseCoeff, bool freezeOn,
                         bool second) noexcept
    {
        for (int bi = 0; bi < kNumBands; ++bi)
        {
            auto& b = bands[static_cast<size_t>(bi)];
            float bandMod;
            float& env = second ? b.envB[static_cast<size_t>(ch)]
                                : b.envA[static_cast<size_t>(ch)];
            float& frozen = second ? b.frozenB[static_cast<size_t>(ch)]
                                   : b.frozenA[static_cast<size_t>(ch)];
            if (second)
                bandMod = b.modHighpassB[static_cast<size_t>(ch)].process(
                    b.modLowpassB[static_cast<size_t>(ch)].process(modulator));
            else
                bandMod = b.modHighpass[static_cast<size_t>(ch)].process(
                    b.modLowpass[static_cast<size_t>(ch)].process(modulator));

            const float mag = std::sqrt(bandMod * bandMod + 1.0e-12f);
            env = (mag > env) ? attackCoeff * env + (1.0f - attackCoeff) * mag
                              : releaseCoeff * env + (1.0f - releaseCoeff) * mag;

            if (!freezeOn)
                frozen = 0.9992f * frozen + 0.0008f * env;

            if (!second)
            {
                const float visible = freezeOn ? frozen : env;
                const float previous =
                    displayBins[static_cast<size_t>(bi)].load(
                        std::memory_order_relaxed);
                const float smoothed =
                    previous * 0.86f +
                    clampf(visible * 10.0f, 0.0f, 1.0f) * 0.14f;
                displayBins[static_cast<size_t>(bi)].store(
                    smoothed, std::memory_order_relaxed);
            }
        }
    }

    float readShiftedEnvelope(int ch, float bandPosition, bool freezeOn,
                              bool second) const noexcept
    {
        const float clamped =
            clampf(bandPosition, 0.0f, static_cast<float>(kNumBands - 1));
        const int lo = static_cast<int>(std::floor(clamped));
        const int hi = std::min(kNumBands - 1, lo + 1);
        const float t = clamped - static_cast<float>(lo);
        const auto read = [this, ch, freezeOn, second](int idx) noexcept -> float
        {
            const auto& b = bands[static_cast<size_t>(idx)];
            if (second)
                return freezeOn ? b.frozenB[static_cast<size_t>(ch)]
                                : b.envB[static_cast<size_t>(ch)];
            return freezeOn ? b.frozenA[static_cast<size_t>(ch)]
                            : b.envA[static_cast<size_t>(ch)];
        };
        return read(lo) + (read(hi) - read(lo)) * t;
    }

    // Internal MIDI carrier. Filterbank blend, as shipped in the reference's
    // Low Latency 128 engine: 0.44 saw / 0.26 sine / 0.18 pulse + sub.
    // NOTE: the reference's Ghost engine uses a different blend
    // (0.46 saw / 0.32 sine / 0.18 pulse, x0.72, + 28% dry). The paper and
    // the code disagree on which blend is canonical; Track A is
    // reconciling the numbers. This engine keeps the reference filterbank
    // blend until the reconciliation lands.
    float makeCarrierSample(float hz, float dry, int channel) noexcept
    {
        const double inc = 2.0 * 3.14159265358979 * hz / sr;
        auto& ph = phase[static_cast<size_t>(
            std::min(kMaxChannels - 1, std::max(0, channel)))];
        ph += inc;
        if (ph > 2.0 * 3.14159265358979)
            ph -= 2.0 * 3.14159265358979;

        const float saw = static_cast<float>(ph / 3.14159265358979 - 1.0);
        const float sine =
            static_cast<float>(std::sin(ph + channel * 0.17));
        const float pulse = sine >= 0.0f ? 0.55f : -0.55f;
        const float sub =
            static_cast<float>(std::sin(ph * 0.5 + channel * 0.11)) * 0.22f;
        const float internal = 0.44f * saw + 0.26f * sine + 0.18f * pulse + sub;
        const float blend = clampf(params.carrierBlend, 0.0f, 1.0f);
        return internal * blend + dry * (1.0f - blend);
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
