// Vox8.h — iPlug2 shell around the framework-free DSP core.
//
// The shell is a thin wrapper: it owns the iPlug2 plugin, the parameter
// list, the MIDI note tracking and the host buffers, and hands plain float
// buffers to the DSP core in Source/DSP/. The core never sees the
// framework (no iPlug2 includes under Source/DSP/), so it survives any
// future framework move.
//
// Engines:
//   Slot 0 — Low Latency 128 filterbank (Vox8Filterbank.h). Wired in.
//   Slot 1 — Spectral Ghost 1024 (Vox8Ghost.h). PARKED: the reference
//   Ghost resynthesis does not vocode (measured Step 2: output on/off
//   ratio 1.32 on bursts the envelope array tracks at 41.5x). The Ghost
//   engine is NOT wired into this shell until the redesign decision lands
//   (FFT cross-synthesis vs per-band carrier filtering vs park Ghost as
//   analysis-only). When it lands, add a GhostEngine member here and a
//   case in ProcessBlock; no restructuring is needed. The engineMode
//   parameter exists in the contract already, with only the filterbank
//   value exposed until then.
//
// Parameter indices are FROZEN before the first release: iPlug2 state is
// a positional binary chunk, so reordering an index breaks every saved
// session. Append new parameters at the end; never reuse an index.
//
// Sample type: float at the shell boundary (SAMPLE_TYPE_FLOAT). The core
// is float; widening to double at the boundary and back would buy nothing.

#pragma once

#include "IPlug_include_in_plug_hdr.h"
#include "Vox8Core.h"
#include "Vox8Filterbank.h"
#include "Vox8Params.h"

const int kNumPresets = 1;

// Frozen parameter indices — DO NOT reorder. Append only, at the end.
enum EParams
{
  kMode = 0,             // Vocoder / Mask / Ghost / Morph
  kMix = 1,              // wet/dry mix, 0..1
  kCarrierBlend = 2,     // internal carrier vs dry input, 0..1
  kMaskDepth = 3,        // Mask mode depth, 0..1
  kClarity = 4,          // envelope speed, 0..1
  kFormantShift = 5,     // semitones, -24..+24 (envelope remap, not a spectral shift)
  kGhost = 6,            // Ghost mode intensifier, 0..1
  kAir = 7,              // noise excitation for fricatives, 0..1
  kFreeze = 8,           // >0.5 holds the frozen envelope
  kTransientProtect = 9, // manual macro: dry transient bleed, 0..1 (NOT a detector)
  kStereoWidth = 10,     // 0..2, 1 = untouched
  kQuality = 11,         // drive macro, 0..1
  kOutputGainDb = 12,    // dB, -24..+24
  kMorph = 13,           // Morph amount, 0..1: 0 = modulator 1, 1 = modulator 2
  kEngineMode = 14,      // 0 = Low Latency 128 filterbank (only value until Ghost lands)
  kNumParams
};

using namespace iplug;
using namespace igraphics;

class Vox8 final : public Plugin
{
public:
  Vox8(const InstanceInfo& info);

#if IPLUG_DSP
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  void ProcessMidiMsg(const IMidiMsg& msg) override;
  void OnReset() override;
  void OnActivate(bool enable) override;
  void GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String& str) const override;
#endif

  // Test access: the live parameter struct the shell hands to the engine.
  // Not a hot-path function.
  vox8::Parameters GetEngineParams() const { return mEngineParams; }
  int GetMidiNote() const { return mMidiNote; }

  // Test setup: what a host does on load (sample rate, channel
  // connections, reset, activate). SetSampleRate and
  // SetChannelConnections are protected on the base class, so the test
  // binary goes through here instead of calling them directly.
  void TestSetup(double sampleRate, int nInChans, int nOutChans)
  {
    SetSampleRate(sampleRate);
    SetChannelConnections(ERoute::kInput, 0, nInChans, true);
    SetChannelConnections(ERoute::kOutput, 0, nOutChans, true);
    OnReset();
    OnActivate(true);
  }

private:
#if IPLUG_DSP
  // Rebuild mEngineParams from the IParams. Called every block: 15 param
  // reads per block is nothing next to 512 biquads per sample, and it
  // keeps the shell correct under automation without relying on the host
  // calling OnParamChange for every move.
  void SyncParams() noexcept;

  vox8::FilterbankEngine mFilterbank;
  // Ghost engine slot: add `vox8::GhostEngine mGhost;` here when the
  // redesign decision lands. Same parameter contract, same ProcessBlock
  // dispatch (see Vox8.cpp).
  vox8::Parameters mEngineParams;
  int mMidiNote = 48; // C3, the engine default; latest NoteOn wins.
#endif
};
