// Vox8 DSP core — framework-free, plain C++.
//
// Read this first if you are new to the core:
// - Vox8Core.h: band layout, RBJ biquads, shared helpers.
// - Vox8Params.h: the parameter contract both engines share.
// - Vox8Filterbank.h: Low Latency 128 filterbank engine (this step).
// - Vox8Ghost.h: Spectral Ghost 1024 STFT/OLA backend (Step 2).
//   Goertzel analyseBins() with per-band stride, frozen[] envelope
//   array, all four modes. Resynthesis is FFT cross-synthesis
//   (redesign landed 2026-11-06, Decision 5): windowed carrier frame
//   → 1024-pt FFT → per-bin gain from the envelope (log-interpolated,
//   formant-shifted, morph-interpolated), carrier phase kept → IFFT
//   → window → OLA. The 0.72 carrier floor is gone (silence in →
//   silence out); Mask is per-bin ducking with multiplier 16.0
//   (redesign calibration). All 107,646 Ghost checks pass — see the
//   header and docs/ARCHITECTURE.md for the measured numbers.
//
// The no-framework rule: nothing under Source/DSP/ may include a framework
// header (JUCE, iPlug2, VST3 SDK, AU headers) or a host type. The core
// takes plain float buffers and a MIDI note number, and returns plain float
// buffers. The iPlug2 shell (to be written after the core is proven) is the
// thin wrapper: it maps IParams onto Vox8Params, routes MIDI note-ons to
// setMidiNote(), and hands host buffers to process().
//
// Prove it with the tests, not with a DAW:
//
//   cd Tests && make test      # build + run the standalone suite
//   cd Tests && make check     # framework-freedom grep + no-alloc grep
//   cd Tests && make bench     # per-engine CPU/latency micro-benchmark
//
// The tests compile only the DSP headers, with no framework on the include
// path. If a test needs a framework to build, the core is wrong, not the
// test.
