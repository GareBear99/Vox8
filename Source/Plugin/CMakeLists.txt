# Vox8 iPlug2 shell — CMake build.
#
# iPlug2 pin: master @ d82b2e99 (see ../.. docs/ARCHITECTURE.md "iPlug2 pin").
# The framework lives OUTSIDE this repo: clone iPlug2 next to the Vox8
# checkout (or point IPLUG2_DIR at it) and fetch the VST3 SDK with
#   <iplug2>/Dependencies/IPlug/download-vst3-sdk.sh
# (CLAP SDK too, for the CLAP target: download-clap-sdks.sh).
#
# Layout on disk:
#   <parent>/Vox8        this repo
#   <parent>/iPlug2      framework, pinned at d82b2e99
#
# Formats:
#   macOS / Windows: VST3 + AU + CLAP (if the CLAP SDK is present) + Standalone app.
#   Linux (this VM): VST3 only, headless (NO_IGRAPHICS). At the pinned
#     commit the CMake path has no working Linux IGraphics platform
#     (IGraphics/Platforms/IGraphicsLinux.cpp is an 11-line stub) and no
#     VST3 Linux entry point wired in Scripts/cmake/VST3.cmake, so the
#     Linux build adds the SDK's linuxmain.cpp itself and builds the
#     DSP-only VST3. The editor is macOS/Windows only until iPlug2's
#     Linux graphics lands; the editor code itself is in Vox8.cpp and
#     follows the IPlugEffect example pattern line for line.

cmake_minimum_required(VERSION 3.14)
project(Vox8 VERSION 0.1.0)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # iPlug2's deploy step copies build/out/<name>.vst3, but its VST3.cmake
  # never creates that bundle on Linux, so the deploy fails. Turn it off
  # here; the Linux branch below assembles the bundle itself.
  set(IPLUG_DEPLOY_PLUGINS OFF CACHE BOOL "Deploy built plugins" FORCE)
endif()

if(NOT DEFINED IPLUG2_DIR)
  set(IPLUG2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../iPlug2" CACHE PATH "iPlug2 root directory (pinned at d82b2e99)")
endif()

include(${IPLUG2_DIR}/iPlug2.cmake)
find_package(iPlug2 REQUIRED)

# Sample type: float at the shell boundary (the DSP core is float).
set(VOX8_DEFINES SAMPLE_TYPE_FLOAT)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Headless VST3: no editor on Linux at this iPlug2 pin (see header above).
  iplug_add_plugin(${PROJECT_NAME}
    SOURCES
      Vox8.cpp
      Vox8.h
      resources/resource.h
    RESOURCES
      resources/fonts/Roboto-Regular.ttf
    FORMATS VST3
    UI NONE
    DEFINES NO_IGRAPHICS ${VOX8_DEFINES}
  )
  # VST3.cmake does not wire a Linux entry point; the SDK ships one.
  target_sources(Vox8-vst3 PRIVATE
    ${IPLUG2_DIR}/Dependencies/IPlug/VST3_SDK/public.sdk/source/main/linuxmain.cpp)
  # Assemble the standard Linux VST3 bundle layout ourselves:
  #   out/Vox8.vst3/Contents/x86_64-linux/Vox8.so
  add_custom_command(TARGET Vox8-vst3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
      "${CMAKE_BINARY_DIR}/out/Vox8.vst3/Contents/x86_64-linux"
    COMMAND ${CMAKE_COMMAND} -E copy
      "$<TARGET_FILE:Vox8-vst3>"
      "${CMAKE_BINARY_DIR}/out/Vox8.vst3/Contents/x86_64-linux/Vox8.so"
    COMMENT "Assembling Linux VST3 bundle out/Vox8.vst3")
else()
  iplug_add_plugin(${PROJECT_NAME}
    SOURCES
      Vox8.cpp
      Vox8.h
      resources/resource.h
    RESOURCES
      resources/fonts/Roboto-Regular.ttf
    FORMATS VST3 AU APP CLAP
    DEFINES ${VOX8_DEFINES}
  )
endif()

# iPlug2's VST3.cmake omits base/source/timer.cpp from the SDK sources,
# but pluginfactory.cpp references Steinberg::InjectCreateTimerFunction
# from it — so the module links (shared libs allow undefined symbols)
# yet fails to load under RTLD_NOW, and pluginval reports 0 plugins.
# Link the SDK file into the VST3 target on every platform.
if(TARGET Vox8-vst3)
  target_sources(Vox8-vst3 PRIVATE
    ${IPLUG2_DIR}/Dependencies/IPlug/VST3_SDK/base/source/timer.cpp)
endif()

# The DSP core lives in ../DSP (plain C++, no framework includes). Every
# format target gets it through the shared base library.
target_include_directories(_Vox8-base INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/../DSP)

# Shell test: drives the real plugin class (MakePlug + ProcessBlock) with
# no host, and checks the filterbank DSP through the shell path against
# the standalone engine. Needs the VST3 API defines; no editor.
add_executable(Vox8-shell-test
  Tests/test_shell.cpp
  Vox8.cpp
  Vox8.h
)
iplug_add_target(Vox8-shell-test PUBLIC
  INCLUDE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../DSP
  LINK iPlug2::VST3
  DEFINE VST3_API NO_IGRAPHICS ${VOX8_DEFINES}
)
# The test is an executable, so unlike the shared-lib VST3 target it must
# resolve every symbol: the SDK's timer.cpp (referenced by pluginfactory).
target_sources(Vox8-shell-test PRIVATE
  ${IPLUG2_DIR}/Dependencies/IPlug/VST3_SDK/base/source/timer.cpp)
