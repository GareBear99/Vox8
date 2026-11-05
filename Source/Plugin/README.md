# Vox8 iPlug2 shell

Thin iPlug2 wrapper around the framework-free DSP core in `../DSP/`.
The core never sees the framework; the shell owns parameters, MIDI and
host buffers only.

## What is wired in

- **Low Latency 128 filterbank engine** — all four modes (Vocoder, Mask,
  Ghost intensifier, real two-envelope Morph), per-band Mask, formant
  shift (envelope remap), Air, Transient Protect (manual macro, not a
  detector), internal MIDI carrier, sidechain bus (the sidechain IS
  modulator 1; the main input becomes modulator 2 for Morph).
- **15 parameters** (indices frozen — see `Vox8.h`; never reorder).
- **Minimal editor**: 14 controls (mode slider + 13 knobs), engine line,
  version + build date in the header, so a loaded build is recognisable
  at a glance.
- **State**: iPlug2 positional binary chunk (`SerializeParams` default).
  A session saved by this build loads the same parameter values back.

## What is NOT wired in

- **Spectral Ghost 1024 engine — PARKED.** The reference Ghost
  resynthesis does not vocode (Step 2 measured: output on/off 1.32× on
  bursts the envelope array tracks at 41.5×). The shell has a second
  engine slot ready (`Vox8.h` / `Vox8.cpp`, search "Ghost engine slot")
  and the `engineMode` parameter already exists with only the filterbank
  value exposed. No restructuring is needed when the redesign decision
  lands (FFT cross-synthesis vs per-band carrier filtering vs park Ghost
  as analysis-only).
- **Spectrum / mask display** — a later step, not this one.

## Build

Framework: iPlug2 `master` @ `d82b2e99` (pinned; see
`../../docs/ARCHITECTURE.md` "iPlug2 pin"). Clone it next to this repo
and fetch the VST3 SDK (CLAP SDK too, for the CLAP target):

```
git clone https://github.com/iPlug2/iPlug2.git   # then: git checkout d82b2e99
<path-to-iplug2>/Dependencies/IPlug/download-vst3-sdk.sh
<path-to-iplug2>/Dependencies/IPlug/download-clap-sdks.sh   # optional, CLAP
```

Then:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

- **macOS / Windows**: VST3 + AU + CLAP (if the CLAP SDK is present) +
  Standalone app, NanoVG editor (Metal on macOS, GL2 on Windows).
  AU + pluginval on the Mac is still to be verified.
- **Linux (this VM)**: VST3 only, headless (`NO_IGRAPHICS`). At the
  pinned commit the CMake path has no working Linux IGraphics platform
  (`IGraphics/Platforms/IGraphicsLinux.cpp` is an 11-line stub) and no
  VST3 Linux entry point wired in `Scripts/cmake/VST3.cmake`, so this
  build adds the SDK's `linuxmain.cpp` itself. The editor is
  macOS/Windows only until iPlug2's Linux graphics lands. The VST3
  load test on Linux is pluginval (strictness 10) + the Steinberg
  VST3 validator — both pass (see Verification below).
  Two framework gaps are patched in the workspace iPlug2 clone (patch
  saved in `Linux/iplug2-linux-d82b2e99.patch`, Mac/Windows do not need
  it): no Linux timer (`IPlugTimer.h` `#error NOT IMPLEMENTED`) and a
  fortify abort in `CurrentTime()` (`snprintf(&cStr[i], 32, ...)` must
  be `32 - i`). The build also links the SDK's `base/source/timer.cpp`
  into the VST3 target — iPlug2's VST3.cmake omits it, which makes the
  module unloadable under `RTLD_NOW` (pluginval reports 0 plugins).
  Build the named targets (`Vox8-vst3`, `Vox8-shell-test`), not `all`:
  `IGraphics.cmake` unconditionally creates a FlexBox object library
  that does not compile on Linux at this pin.

Sample type is `float` at the shell boundary (`SAMPLE_TYPE_FLOAT`):
the core is float, and widening to double and back would buy nothing.

## Verify (VM)

```
cmake --build build -j --target Vox8-shell-test && ./build/Vox8-shell-test
```

Drives the real plugin class with no host (`MakePlug` +
`ProcessBlock`) and checks the filterbank DSP through the shell path
against the standalone engine (bit-identical output for Vocoder, Morph
through the sidechain, and Mask), MIDI NoteOn tracking, and a state
round-trip. Must print `ALL SHELL TESTS PASSED`.

VST3 load test (Linux):

```
pluginval --strictness-level 10 --validate <build-dir>/out/Vox8.vst3
```

Verified 2026-11-05: **pluginval v1.0.4 strictness 10 SUCCESS** (25
test sections, 0 failures) and **Steinberg VST3 validator: 47 passed,
0 failed** (validator built from the downloaded SDK).

(AU / editor-open / pluginval on the Mac wait for the Mac build.)

## Files

- `config.h` — plugin identity, channel I/O (main + sidechain bus),
  formats, UI size.
- `Vox8.h` / `Vox8.cpp` — the shell. Editor code is guarded
  `#if IPLUG_EDITOR && !defined(NO_IGRAPHICS)` so the headless Linux
  build compiles the same source.
- `resources/` — `resource.h` + Roboto-Regular.ttf (Apache 2.0, as
  shipped in the iPlug2 examples).
- `Tests/test_shell.cpp` — shell test, built by `CMakeLists.txt`.
- `CMakeLists.txt` — formats per platform (see header comment).
