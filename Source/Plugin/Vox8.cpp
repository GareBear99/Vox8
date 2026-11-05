// Vox8.cpp — iPlug2 shell around the framework-free DSP core.
//
// The shell does three things only: parameters, MIDI, buffers. All audio
// maths lives in Source/DSP/ (plain C++, no framework includes).
//
// Real-time safety (paper doctrine, carried over from FreeEQ8): the hot
// path allocates nothing, locks nothing, touches no files and spawns no
// threads. ProcessBlock copies the input to the output and processes in
// place through the engine, which owns only fixed-size member arrays.

#include "Vox8.h"
#include "IPlug_include_in_plug_src.h"

#if IPLUG_EDITOR && !defined(NO_IGRAPHICS)
#include "IControls.h"
#endif

Vox8::Vox8(const InstanceInfo& info)
: iplug::Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
#if IPLUG_DSP
  SetChannelLabel(ERoute::kInput, 0, "Main L");
  SetChannelLabel(ERoute::kInput, 1, "Main R");
  SetChannelLabel(ERoute::kInput, 2, "SideChain L");
  SetChannelLabel(ERoute::kInput, 3, "SideChain R");
#endif

  // Defaults match vox8::Parameters exactly, so a fresh instance sounds
  // identical to the standalone engine with default parameters.
  GetParam(kMode)->InitEnum("Mode", 0, {"Vocoder", "Mask", "Ghost", "Morph"});
  GetParam(kMix)->InitDouble("Mix", 1.0, 0.0, 1.0, 0.01, "%");
  GetParam(kCarrierBlend)->InitDouble("Carrier Blend", 0.65, 0.0, 1.0, 0.01, "%");
  GetParam(kMaskDepth)->InitDouble("Mask Depth", 0.5, 0.0, 1.0, 0.01, "%");
  GetParam(kClarity)->InitDouble("Clarity", 0.55, 0.0, 1.0, 0.01, "%");
  GetParam(kFormantShift)->InitDouble("Formant Shift", 0.0, -24.0, 24.0, 0.01, "st");
  GetParam(kGhost)->InitDouble("Ghost", 0.0, 0.0, 1.0, 0.01, "%");
  GetParam(kAir)->InitDouble("Air", 0.15, 0.0, 1.0, 0.01, "%");
  GetParam(kFreeze)->InitDouble("Freeze", 0.0, 0.0, 1.0, 0.01, "%");
  GetParam(kTransientProtect)->InitDouble("Transient Protect", 0.35, 0.0, 1.0, 0.01, "%");
  GetParam(kStereoWidth)->InitDouble("Stereo Width", 1.0, 0.0, 2.0, 0.01, "x");
  GetParam(kQuality)->InitDouble("Quality", 0.75, 0.0, 1.0, 0.01, "%");
  GetParam(kOutputGainDb)->InitDouble("Output Gain", 0.0, -24.0, 24.0, 0.01, "dB");
  GetParam(kMorph)->InitDouble("Morph", 0.5, 0.0, 1.0, 0.01, "%");
  // Only the filterbank value exists until the Ghost redesign lands.
  GetParam(kEngineMode)->InitEnum("Engine", 0, {"Low Latency 128"});

#if IPLUG_EDITOR && !defined(NO_IGRAPHICS)
  mMakeGraphicsFunc = [&]() {
    return MakeGraphics(*this, PLUG_WIDTH, PLUG_HEIGHT, PLUG_FPS, GetScaleForScreen(PLUG_WIDTH, PLUG_HEIGHT));
  };

  mLayoutFunc = [&](IGraphics* pGraphics) {
    pGraphics->AttachCornerResizer(EUIResizerMode::Scale, false);
    pGraphics->AttachPanelBackground(COLOR_GRAY);
    pGraphics->LoadFont("Roboto-Regular", ROBOTO_FN);

    const IRECT bounds = pGraphics->GetBounds();
    const IRECT header = bounds.GetFromTop(48.f);
    pGraphics->AttachControl(new ITextControl(header.GetFromLeft(200.f).GetPadded(-8.f), "Vox8", IText(30)));
    pGraphics->AttachControl(new ITextControl(header.GetFromLeft(420.f).GetPadded(-8.f).GetVShifted(8.f), "Low Latency 128 filterbank vocoder", IText(14)));

    WDL_String buildInfoStr;
    GetBuildInfoStr(buildInfoStr, __DATE__, __TIME__);
    WDL_String versionStr;
    versionStr.SetFormatted(64, "Vox8 v%s — %s", PLUG_VERSION_STR, buildInfoStr.Get());
    pGraphics->AttachControl(new ITextControl(header.GetFromRight(300.f).GetPadded(-8.f), versionStr.Get(), DEFAULT_TEXT.WithAlign(EAlign::Far)));

    // Two rows of seven controls. Knobs are 64 px with the parameter
    // name underneath; the Mode control is a vertical slider (enum).
    const float cellW = 100.f;
    const float knobSize = 64.f;
    const float row1Y = 64.f;
    const float row2Y = 204.f;

    auto knobRect = [&](int col, float y) {
      return IRECT(10.f + col * cellW, y, 10.f + col * cellW + knobSize, y + knobSize);
    };

    pGraphics->AttachControl(new IVSliderControl(knobRect(0, row1Y), kMode));
    pGraphics->AttachControl(new IVKnobControl(knobRect(1, row1Y), kMix));
    pGraphics->AttachControl(new IVKnobControl(knobRect(2, row1Y), kCarrierBlend));
    pGraphics->AttachControl(new IVKnobControl(knobRect(3, row1Y), kMaskDepth));
    pGraphics->AttachControl(new IVKnobControl(knobRect(4, row1Y), kClarity));
    pGraphics->AttachControl(new IVKnobControl(knobRect(5, row1Y), kFormantShift));
    pGraphics->AttachControl(new IVKnobControl(knobRect(6, row1Y), kGhost));

    pGraphics->AttachControl(new IVKnobControl(knobRect(0, row2Y), kAir));
    pGraphics->AttachControl(new IVKnobControl(knobRect(1, row2Y), kFreeze));
    pGraphics->AttachControl(new IVKnobControl(knobRect(2, row2Y), kTransientProtect));
    pGraphics->AttachControl(new IVKnobControl(knobRect(3, row2Y), kStereoWidth));
    pGraphics->AttachControl(new IVKnobControl(knobRect(4, row2Y), kQuality));
    pGraphics->AttachControl(new IVKnobControl(knobRect(5, row2Y), kOutputGainDb));
    pGraphics->AttachControl(new IVKnobControl(knobRect(6, row2Y), kMorph));

    const IRECT footer = bounds.GetFromBottom(52.f).GetPadded(-10.f);
    pGraphics->AttachControl(new ITextControl(footer.GetFromTop(20.f), "Engine: Low Latency 128 — Spectral Ghost 1024 parked pending redesign decision", IText(13)));
    pGraphics->AttachControl(new ITextControl(footer.GetFromBottom(20.f), "Freeze holds the envelope above 0.5. Transient Protect is a manual macro, not a detector.", IText(13)));
  };
#endif
}

#if IPLUG_DSP

void Vox8::SyncParams() noexcept
{
  mEngineParams.mode = static_cast<vox8::Mode>(static_cast<int>(GetParam(kMode)->Value()));
  mEngineParams.mix = static_cast<float>(GetParam(kMix)->Value());
  mEngineParams.carrierBlend = static_cast<float>(GetParam(kCarrierBlend)->Value());
  mEngineParams.maskDepth = static_cast<float>(GetParam(kMaskDepth)->Value());
  mEngineParams.clarity = static_cast<float>(GetParam(kClarity)->Value());
  mEngineParams.formantShift = static_cast<float>(GetParam(kFormantShift)->Value());
  mEngineParams.ghost = static_cast<float>(GetParam(kGhost)->Value());
  mEngineParams.air = static_cast<float>(GetParam(kAir)->Value());
  mEngineParams.freeze = static_cast<float>(GetParam(kFreeze)->Value());
  mEngineParams.transientProtect = static_cast<float>(GetParam(kTransientProtect)->Value());
  mEngineParams.stereoWidth = static_cast<float>(GetParam(kStereoWidth)->Value());
  mEngineParams.quality = static_cast<float>(GetParam(kQuality)->Value());
  mEngineParams.outputGainDb = static_cast<float>(GetParam(kOutputGainDb)->Value());
  mEngineParams.morph = static_cast<float>(GetParam(kMorph)->Value());
  mEngineParams.engineMode = static_cast<int>(GetParam(kEngineMode)->Value());
}

void Vox8::OnReset()
{
  mFilterbank.prepare(GetSampleRate(), vox8::kMaxChannels);
  mFilterbank.setMidiNote(mMidiNote);
  SyncParams();
  mFilterbank.setParameters(mEngineParams);
}

void Vox8::OnActivate(bool enable)
{
  if (enable)
  {
    SyncParams();
    mFilterbank.setParameters(mEngineParams);
  }
}

void Vox8::GetBusName(ERoute direction, int busIdx, int nBuses, WDL_String& str) const
{
  if (direction == ERoute::kInput)
  {
    if (busIdx == 0)
      str.Set("Main Input");
    else
      str.Set("SideChain");
  }
  else
  {
    str.Set("Output");
  }
}

void Vox8::ProcessMidiMsg(const IMidiMsg& msg)
{
  if (msg.StatusMsg() == IMidiMsg::kNoteOn && msg.Velocity() > 0)
  {
    // Latest NoteOn wins; the carrier keeps sounding at the last note
    // (the engine has no note-off — same as the reference carrier).
    mMidiNote = msg.NoteNumber();
    mFilterbank.setMidiNote(mMidiNote);
  }
}

void Vox8::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  SyncParams();
  mFilterbank.setParameters(mEngineParams);
  mFilterbank.setMidiNote(mMidiNote);

  const int nChans = std::min(NOutChansConnected(), vox8::kMaxChannels);
  if (nChans <= 0 || nFrames <= 0)
    return;

  // Sidechain present (channels 2/3 connected) => it IS modulator 1;
  // the main input becomes modulator 2 for Morph. Same routing as the
  // standalone engine. Logic/GarageBand duplicate the main bus onto the
  // sidechain bus when no sidechain is routed (see the IPlugSideChain
  // example); only treat the sidechain as present when its channels are
  // actually connected.
  const float* scPtrs[vox8::kMaxChannels] = {nullptr, nullptr};
  int scChans = 0;
  if (IsChannelConnected(ERoute::kInput, 2))
  {
    scPtrs[0] = inputs[2];
    scChans = 1;
    if (IsChannelConnected(ERoute::kInput, 3))
    {
      scPtrs[1] = inputs[3];
      scChans = 2;
    }
  }
  const float* const* sidechain = scChans > 0 ? scPtrs : nullptr;

  // The engine processes in place, so copy main input to output first.
  for (int c = 0; c < nChans; ++c)
  {
    for (int s = 0; s < nFrames; ++s)
      outputs[c][s] = inputs[c][s];
  }

  // Ghost engine slot: when the redesign lands, dispatch here on
  // mEngineParams.engineMode (0 = filterbank, 1 = Ghost) and report the
  // Ghost frame latency with SetLatency(). Until then every mode runs
  // the filterbank, which has zero latency (PLUG_LATENCY 0).
  float* ioPtrs[vox8::kMaxChannels] = {nullptr, nullptr};
  for (int c = 0; c < nChans; ++c)
    ioPtrs[c] = outputs[c];
  mFilterbank.process(ioPtrs, nChans, nFrames, sidechain, scChans);
}

#endif // IPLUG_DSP
