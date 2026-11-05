// iPlug2 Linux patches (VM-only, NOT upstream).
//
// At the pinned commit d82b2e99 iPlug2's Linux support in the CMake path
// is unfinished. Two gaps block ANY iPlug2 plugin build on Linux, and
// one latent bug aborts every plugin construction on a fortify-enabled
// toolchain (Ubuntu GCC defaults _FORTIFY_SOURCE=3 at -O2):
//
// 1. No Linux timer: IPlug/IPlugTimer.h ends `#else #error NOT
//    IMPLEMENTED` and IPlug/IPlugTimer.cpp has no OS_LINUX branch, so no
//    iPlug2 code compiles on Linux at all (the header is included by
//    IPlugAPIBase.h unconditionally). Fix: minimal Timer_impl on a
//    std::thread that sleeps intervalMs between callbacks. Stop() is
//    safe to call from the timer thread itself (detaches instead of
//    joining). The timer is only used for main-thread idle callbacks;
//    the headless Linux VST3 never creates one in normal use, so this
//    is a compile-enabler, not a behaviour change.
// 2. snprintf size bug in CurrentTime() (IPlug/IPlugLogger.h): the
//    timezone suffix is written with snprintf(&cStr[i], 32, ...) —
//    the full buffer size, not the remaining 32-i bytes. Only 4 chars
//    are ever written, so it is harmless on macOS/Windows, but fortify
//    aborts the process at plugin construction. Fix: pass 32-i.
//    This is a genuine upstream bug, not Linux-specific; it just only
//    fires where fortify is on.
//
// Apply to a clean iPlug2 checkout at d82b2e99 with:
//   cd <iplug2> && git apply <vox8-repo>/Source/Plugin/Linux/iplug2-linux-d82b2e99.patch
//
// The macOS/Windows builds do not need this patch (no fortify, native
// timers). Send both fixes upstream if the Linux CMake path is ever
// revived; do not carry them silently.
