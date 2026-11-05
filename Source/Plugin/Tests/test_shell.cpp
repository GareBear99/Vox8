// test_shell.cpp — Vox8 iPlug2 shell test (no host).
//
// Drives the real plugin class (MakePlug + ProcessBlock) and checks the
// filterbank DSP through the shell path against the standalone engine:
// the shell must be bit-identical to the engine given the same input,
// parameters and MIDI note — it adds routing, not maths.
//
// Run: ./Vox8-shell-test  (must print ALL SHELL TESTS PASSED)
// Exit code 0 on pass, 1 on any failure.

#include "Vox8.h"
#include "Vox8Filterbank.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace iplug
{
  // Defined in IPlug_include_in_plug_src.h (pulled in by Vox8.cpp).
  Plugin* MakePlug(const InstanceInfo& info);
}

static int gChecks = 0;
static int gFailures = 0;

#define CHECK(cond) do { ++gChecks; if (!(cond)) { ++gFailures; \
  std::printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

#define CHECK_NEAR(a, b, tol) do { ++gChecks; \
  const double _d = std::fabs(static_cast<double>(a) - static_cast<double>(b)); \
  if (!(_d <= (tol))) { ++gFailures; \
    std::printf("FAIL line %d: |%s - %s| = %g > %g\n", __LINE__, #a, #b, _d, static_cast<double>(tol)); } } while (0)

static float rms(const std::vector<float>& v)
{
  double s = 0.0;
  for (float x : v)
    s += static_cast<double>(x) * x;
  return static_cast<float>(std::sqrt(s / static_cast<double>(v.size())));
}

// Run `nFrames` of `input` through the shell plugin and return the output.
// Resets the engine first, so each run starts from the same state as a
// freshly prepared standalone engine (the comparison target).
static std::vector<float> runShell(Vox8* plug, const std::vector<float>& input,
                                   const std::vector<float>* sidechain, int nFrames)
{
  plug->OnReset();
  std::vector<float> inL(input), inR(input);
  std::vector<float> outL(nFrames, 0.0f), outR(nFrames, 0.0f);
  std::vector<float> scL, scR;
  float* ins[4] = {inL.data(), inR.data(), nullptr, nullptr};
  float* outs[2] = {outL.data(), outR.data()};
  if (sidechain)
  {
    scL = *sidechain;
    scR = *sidechain;
    ins[2] = scL.data();
    ins[3] = scR.data();
  }
  plug->ProcessBlock(ins, outs, nFrames);
  std::vector<float> out(nFrames * 2);
  for (int i = 0; i < nFrames; ++i)
  {
    out[2 * i] = outL[i];
    out[2 * i + 1] = outR[i];
  }
  return out;
}

// Same input through the standalone engine, same parameters and MIDI note.
static std::vector<float> runEngine(const vox8::Parameters& p, int midiNote,
                                    const std::vector<float>& input,
                                    const std::vector<float>* sidechain, int nFrames,
                                    double sampleRate)
{
  vox8::FilterbankEngine engine;
  engine.prepare(sampleRate, 2);
  engine.setMidiNote(midiNote);
  engine.setParameters(p);
  std::vector<float> inL(input), inR(input);
  std::vector<float> scL, scR;
  float* io[2] = {inL.data(), inR.data()};
  const float* sc[2] = {nullptr, nullptr};
  if (sidechain)
  {
    scL = *sidechain;
    scR = *sidechain;
    sc[0] = scL.data();
    sc[1] = scR.data();
  }
  engine.process(io, 2, nFrames, sidechain ? sc : nullptr, sidechain ? 2 : 0);
  std::vector<float> out(nFrames * 2);
  for (int i = 0; i < nFrames; ++i)
  {
    out[2 * i] = inL[i];
    out[2 * i + 1] = inR[i];
  }
  return out;
}

int main()
{
  const double kSr = 48000.0;
  const int kFrames = 4096;

  // --- Construction + defaults -------------------------------------------
  iplug::Plugin* base = iplug::MakePlug(iplug::InstanceInfo());
  CHECK(base != nullptr);
  Vox8* plug = dynamic_cast<Vox8*>(base);
  CHECK(plug != nullptr);
  CHECK(plug->NParams() == kNumParams);
  CHECK(plug->GetParam(kMode)->Value() == 0.0);
  CHECK_NEAR(plug->GetParam(kMix)->Value(), 1.0, 1e-9);
  CHECK_NEAR(plug->GetParam(kCarrierBlend)->Value(), 0.65, 1e-9);
  CHECK_NEAR(plug->GetParam(kMaskDepth)->Value(), 0.5, 1e-9);
  CHECK_NEAR(plug->GetParam(kClarity)->Value(), 0.55, 1e-9);
  CHECK_NEAR(plug->GetParam(kFormantShift)->Value(), 0.0, 1e-9);
  CHECK_NEAR(plug->GetParam(kGhost)->Value(), 0.0, 1e-9);
  CHECK_NEAR(plug->GetParam(kAir)->Value(), 0.15, 1e-9);
  CHECK_NEAR(plug->GetParam(kFreeze)->Value(), 0.0, 1e-9);
  CHECK_NEAR(plug->GetParam(kTransientProtect)->Value(), 0.35, 1e-9);
  CHECK_NEAR(plug->GetParam(kStereoWidth)->Value(), 1.0, 1e-9);
  CHECK_NEAR(plug->GetParam(kQuality)->Value(), 0.75, 1e-9);
  CHECK_NEAR(plug->GetParam(kOutputGainDb)->Value(), 0.0, 1e-9);
  CHECK_NEAR(plug->GetParam(kMorph)->Value(), 0.5, 1e-9);
  CHECK_NEAR(plug->GetParam(kEngineMode)->Value(), 0.0, 1e-9);
  CHECK(plug->GetMidiNote() == 48);

  plug->TestSetup(kSr, 4, 2);

  // --- Test signals --------------------------------------------------------
  std::vector<float> sine(kFrames);
  std::vector<float> sine2(kFrames);
  for (int i = 0; i < kFrames; ++i)
  {
    const double t = static_cast<double>(i) / kSr;
    sine[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 1000.0 * t));
    sine2[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979 * 3000.0 * t));
  }

  // --- Shell DSP == standalone engine, bit-identical ----------------------
  // (a) Default Vocoder, no sidechain.
  {
    const std::vector<float> shellOut = runShell(plug, sine, nullptr, kFrames);
    const vox8::Parameters p; // defaults
    const std::vector<float> engOut = runEngine(p, 48, sine, nullptr, kFrames, kSr);
    CHECK(shellOut.size() == engOut.size());
    double maxDiff = 0.0;
    for (size_t i = 0; i < shellOut.size(); ++i)
      maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(shellOut[i] - engOut[i])));
    CHECK_NEAR(maxDiff, 0.0, 0.0);
    std::printf("shell-vs-engine vocoder max diff: %g\n", maxDiff);
    CHECK(rms(shellOut) > 0.001f); // the carrier is audible, not silence
  }

  // (b) Morph through the sidechain: morph=0 follows the sidechain,
  // morph=1 follows the main input.
  {
    plug->GetParam(kMode)->Set(3.0); // Morph
    plug->GetParam(kMorph)->Set(0.0);
    const std::vector<float> out0 = runShell(plug, sine, &sine2, kFrames);
    plug->GetParam(kMorph)->Set(1.0);
    const std::vector<float> out1 = runShell(plug, sine, &sine2, kFrames);
    vox8::Parameters p;
    p.mode = vox8::Mode::Morph;
    p.morph = 0.0f;
    const std::vector<float> eng0 = runEngine(p, 48, sine, &sine2, kFrames, kSr);
    p.morph = 1.0f;
    const std::vector<float> eng1 = runEngine(p, 48, sine, &sine2, kFrames, kSr);
    double d0 = 0.0, d1 = 0.0;
    for (size_t i = 0; i < out0.size(); ++i)
    {
      d0 = std::max(d0, static_cast<double>(std::fabs(out0[i] - eng0[i])));
      d1 = std::max(d1, static_cast<double>(std::fabs(out1[i] - eng1[i])));
    }
    CHECK_NEAR(d0, 0.0, 0.0);
    CHECK_NEAR(d1, 0.0, 0.0);
    // And the two morph endpoints really differ through the shell.
    double endDiff = 0.0;
    for (size_t i = 0; i < out0.size(); ++i)
      endDiff = std::max(endDiff, static_cast<double>(std::fabs(out0[i] - out1[i])));
    CHECK(endDiff > 0.01);
    std::printf("shell-vs-engine morph max diff: %g / %g; endpoints differ by %g\n", d0, d1, endDiff);
    plug->GetParam(kMode)->Set(0.0);
    plug->GetParam(kMorph)->Set(0.5);
  }

  // (c) Mask mode through the shell, bit-identical to the engine.
  {
    plug->GetParam(kMode)->Set(1.0); // Mask
    plug->GetParam(kMaskDepth)->Set(0.8);
    const std::vector<float> shellOut = runShell(plug, sine, nullptr, kFrames);
    vox8::Parameters p;
    p.mode = vox8::Mode::Mask;
    p.maskDepth = 0.8f;
    const std::vector<float> engOut = runEngine(p, 48, sine, nullptr, kFrames, kSr);
    double maxDiff = 0.0;
    for (size_t i = 0; i < shellOut.size(); ++i)
      maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(shellOut[i] - engOut[i])));
    CHECK_NEAR(maxDiff, 0.0, 0.0);
    plug->GetParam(kMode)->Set(0.0);
    plug->GetParam(kMaskDepth)->Set(0.5);
  }

  // --- MIDI: latest NoteOn wins, carrier follows ---------------------------
  {
    iplug::IMidiMsg noteOn;
    noteOn.MakeNoteOnMsg(69, 100, 0);
    plug->ProcessMidiMsg(noteOn);
    CHECK(plug->GetMidiNote() == 69);
    const std::vector<float> outA = runShell(plug, sine, nullptr, kFrames);
    vox8::Parameters p;
    const std::vector<float> engA = runEngine(p, 69, sine, nullptr, kFrames, kSr);
    double maxDiff = 0.0;
    for (size_t i = 0; i < outA.size(); ++i)
      maxDiff = std::max(maxDiff, static_cast<double>(std::fabs(outA[i] - engA[i])));
    CHECK_NEAR(maxDiff, 0.0, 0.0);
    // NoteOff does not change the note (carrier sustains at the last note).
    iplug::IMidiMsg noteOff;
    noteOff.MakeNoteOffMsg(69, 0);
    plug->ProcessMidiMsg(noteOff);
    CHECK(plug->GetMidiNote() == 69);
  }

  // --- State round-trip: params survive Serialize/Unserialize --------------
  {
    plug->GetParam(kClarity)->Set(0.9);
    plug->GetParam(kFormantShift)->Set(-12.0);
    plug->GetParam(kMorph)->Set(0.25);
    IByteChunk chunk;
    CHECK(plug->SerializeState(chunk) == true);
    plug->GetParam(kClarity)->Set(0.1);
    plug->GetParam(kFormantShift)->Set(12.0);
    plug->GetParam(kMorph)->Set(0.75);
    plug->UnserializeState(chunk, 0);
    CHECK_NEAR(plug->GetParam(kClarity)->Value(), 0.9, 1e-9);
    CHECK_NEAR(plug->GetParam(kFormantShift)->Value(), -12.0, 1e-9);
    CHECK_NEAR(plug->GetParam(kMorph)->Value(), 0.25, 1e-9);
    plug->GetParam(kClarity)->Set(0.55);
    plug->GetParam(kFormantShift)->Set(0.0);
    plug->GetParam(kMorph)->Set(0.5);
  }

  // --- Engine params the shell hands over match the IParams ---------------
  {
    plug->GetParam(kMode)->Set(2.0); // Ghost mode (filterbank intensifier)
    plug->GetParam(kGhost)->Set(0.7);
    const std::vector<float> out = runShell(plug, sine, nullptr, kFrames);
    const vox8::Parameters handed = plug->GetEngineParams();
    CHECK(handed.mode == vox8::Mode::Ghost);
    CHECK_NEAR(handed.ghost, 0.7f, 1e-6);
    CHECK(handed.engineMode == 0); // Ghost ENGINE still parked; mode 2 is the intensifier
    CHECK(rms(out) > 0.001f);
    plug->GetParam(kMode)->Set(0.0);
    plug->GetParam(kGhost)->Set(0.0);
  }

  delete base;

  std::printf("shell tests: %d checks, %d failures\n", gChecks, gFailures);
  if (gFailures == 0)
    std::printf("ALL SHELL TESTS PASSED\n");
  return gFailures == 0 ? 0 : 1;
}
