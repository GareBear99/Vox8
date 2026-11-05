// config.h — Vox8 iPlug2 plugin configuration.
//
// Vox8 is a spectral vocoder (it processes audio; it does not synthesise
// singing). FreeVox8 / Vox8, Gary Doman (GareBear99 / TizWildin).
// iPlug2 pin: master @ d82b2e99 (verified by a Linux VST3 build, 2026-11-05).

#define PLUG_NAME "Vox8"
#define PLUG_MFR "TizWildin"
#define PLUG_VERSION_HEX 0x00010000
#define PLUG_VERSION_STR "0.1.0"
#define PLUG_UNIQUE_ID 'Vox8'
#define PLUG_MFR_ID 'TizW'
#define PLUG_URL_STR "https://github.com/GareBear99/Vox8"
#define PLUG_EMAIL_STR "gdoman99@gmail.com"
#define PLUG_COPYRIGHT_STR "Copyright 2026 Gary Doman"
#define PLUG_CLASS_NAME Vox8

#define BUNDLE_NAME "Vox8"
#define BUNDLE_MFR "TizWildin"
#define BUNDLE_DOMAIN "com"

#define SHARED_RESOURCES_SUBPATH "Vox8"

// iPlug2's Linux branch defines neither BUNDLE_ID nor APP_GROUP_ID
// (IPlug_include_in_plug_hdr.h leaves them as TODO on OS_LINUX), but
// MakeConfig() takes both unconditionally — so define them here, Linux
// only, in the same shape as the macOS definition. No effect on Mac/Win.
#if defined OS_LINUX
#define BUNDLE_ID "com.TizWildin.vst3.Vox8"
#define APP_GROUP_ID "group.com.TizWildin.Vox8"
#endif

// Main stereo in/out plus a stereo sidechain bus (modulator 2 for Morph).
// Matches the IPlugSideChain example layout: bus 0 = main, bus 1 = sidechain.
#define PLUG_CHANNEL_IO "\
1-1 \
1.1-1 \
1.2-1 \
1.2-2 \
2.1-1 \
2.1-2 \
2-2 \
2.2-2"

#define PLUG_LATENCY 0
#define PLUG_TYPE 0
#define PLUG_DOES_MIDI_IN 1
#define PLUG_DOES_MIDI_OUT 0
#define PLUG_DOES_MPE 0
#define PLUG_DOES_STATE_CHUNKS 0
// The Linux VM build is headless (NO_IGRAPHICS): it genuinely has no
// editor, so it must say so — otherwise a host asks for an editor view
// the build cannot provide and crashes. macOS/Windows keep the editor.
#if defined OS_LINUX
#define PLUG_HAS_UI 0
#else
#define PLUG_HAS_UI 1
#endif
#define PLUG_WIDTH 720
#define PLUG_HEIGHT 420
#define PLUG_FPS 60
#define PLUG_SHARED_RESOURCES 0
#define PLUG_HOST_RESIZE 0

#define AUV2_ENTRY Vox8_Entry
#define AUV2_ENTRY_STR "Vox8_Entry"
#define AUV2_FACTORY Vox8_Factory
#define AUV2_VIEW_CLASS Vox8_View
#define AUV2_VIEW_CLASS_STR "Vox8_View"

#define AAX_TYPE_IDS 'VX81', 'VX82'
#define AAX_TYPE_IDS_AUDIOSUITE 'VXA1', 'VXA2'
#define AAX_PLUG_MFR_STR "TizW"
#define AAX_PLUG_NAME_STR "Vox8\nVOX8"
#define AAX_PLUG_CATEGORY_STR "Effect"
#define AAX_DOES_AUDIOSUITE 1

#define VST3_SUBCATEGORY "Fx"

#define CLAP_MANUAL_URL "https://github.com/GareBear99/Vox8"
#define CLAP_SUPPORT_URL "https://github.com/GareBear99/Vox8"
#define CLAP_DESCRIPTION "Spectral vocoder: Low Latency 128 filterbank engine with Ghost freeze, Morph and dynamic spectral Mask"
#define CLAP_FEATURES "audio-effect", "vocoder"

#define APP_NUM_CHANNELS 2
#define APP_N_VECTOR_WAIT 0
#define APP_MULT 1
#define APP_COPY_AUV3 0
#define APP_SIGNAL_VECTOR_SIZE 64

#define ROBOTO_FN "Roboto-Regular.ttf"
