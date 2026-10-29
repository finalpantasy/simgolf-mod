# Native Windows build of the translated game (simgolf mod, task-2).
# Included by CMakeLists.txt when not configuring with emcmake; use the preset:
#   cmake --preset native && cmake --build --preset native   (or tools/build_native.sh)
# Output: build/native/golf.exe with SDL2.dll, image/ (golf.bin, jgld.bin from
# build/gen/images) and fonts/ next to it. Nothing here is game data in git:
# the images are copied from the local translator output at build time.

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "The native build is 32-bit Windows only: cmake --preset native")
endif()

set(SIMGOLF_GEN "${CMAKE_SOURCE_DIR}/../build/gen" CACHE PATH "Translator output")
set(SIMGOLF_GAME "${CMAKE_SOURCE_DIR}/../game" CACHE PATH "Default game folder (golf.ini or --game override it)")
set(SIMGOLF_ART "art/upscaled" CACHE PATH "Default HD art folder (golf.ini art_dir= overrides it)")
set(SIMGOLF_IMAGES "${SIMGOLF_GEN}/images" CACHE PATH "Module images (golf.bin, jgld.bin)")
set(SIMGOLF_PATH_STYLE_ASSETS "${CMAKE_SOURCE_DIR}/../art_local/path-styles/pack" CACHE PATH "Optional compiled path-style pack")
if(DEFINED ENV{TOOLS_EXT})
  set(_tools "$ENV{TOOLS_EXT}")
else()
  set(_tools "C:/simgolf-tools")
endif()
set(SIMGOLF_SDL2 "${_tools}/SDL2-2.32.10" CACHE PATH "SDL2 VC development release")

if(NOT EXISTS "${SIMGOLF_GEN}/golf/golf_protos.h")
  message(FATAL_ERROR "No translated game in ${SIMGOLF_GEN}: run tools/build_web.sh first")
endif()
if(NOT EXISTS "${SIMGOLF_SDL2}/include/SDL.h")
  message(FATAL_ERROR "SDL2 not found at ${SIMGOLF_SDL2}: run tools/install_native_tools.sh")
endif()

file(GLOB SIMGOLF_GEN_SRC CONFIGURE_DEPENDS "${SIMGOLF_GEN}/*/*.c")
file(GLOB SIMGOLF_GOLF_OVERRIDES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/port/golf/*.c")
file(GLOB SIMGOLF_JGLD_OVERRIDES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/port/jgld/*.c")

# Release builds (task-76, preset "release"): no developer paths inside the exe.
option(SIMGOLF_RELEASE "Strip build-machine paths from the binaries" OFF)
if(SIMGOLF_RELEASE)
  get_filename_component(_root "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
  string(REPLACE "/" "\\" _rootw "${_root}")
  add_compile_options("/clang:-ffile-prefix-map=${_root}=." "/clang:-ffile-prefix-map=${_rootw}=.")
  add_link_options(/PDBALTPATH:golf.pdb)
endif()
add_executable(golf WIN32
  src/port/port_main.c src/port/runtime.c src/port/crt.c src/port/win32.c src/port/gdi.c
  src/port/imports.c
  src/port/host_win32.c src/port/fs_native.c src/port/harness.c src/port/bridge.c src/compat/msvc/dirent.c
  src/platform/terrain_dll.cpp src/platform/terrain_gl.cpp src/platform/terrain_bridge.cpp
  src/platform/terrain_hd.cpp
  src/platform/sound.cpp src/platform/sound_native.cpp
  ${SIMGOLF_GEN_SRC} ${SIMGOLF_GOLF_OVERRIDES} ${SIMGOLF_JGLD_OVERRIDES})

# Compile the icon separately so clang-cl-only C/C++ flags do not reach llvm-rc.
# The existing x86 toolchain already supplies CMAKE_RC_COMPILER.
set(_app_icon_res "${CMAKE_CURRENT_BINARY_DIR}/app_icon.res")
add_custom_command(OUTPUT "${_app_icon_res}"
  COMMAND "${CMAKE_RC_COMPILER}" /nologo
    /I "${CMAKE_SOURCE_DIR}/src/port" /I "${CMAKE_SOURCE_DIR}/../data/ui"
    /FO "${_app_icon_res}" "${CMAKE_SOURCE_DIR}/src/port/app_icon.rc"
  DEPENDS src/port/app_icon.rc src/port/app_icon.h "${CMAKE_SOURCE_DIR}/../data/ui/app_icon.ico"
  VERBATIM)
set_source_files_properties("${_app_icon_res}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
target_sources(golf PRIVATE "${_app_icon_res}")

# task-3: sharp scaling, borderless full screen, render thread (any resolution, any frame rate)
target_sources(golf PRIVATE src/port/hd_native.c src/port/hd_terrain_native.c src/port/legacy_text_native.c)
target_sources(golf PRIVATE src/port/course_lamps.c src/port/course_lamps_art.c src/port/course_lamps_game.c src/port/course_lamps_render.c)
add_custom_target(course_lights_files ALL
 COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/data"
 COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/../data/course-lights.csv" "${CMAKE_BINARY_DIR}/data/course-lights.csv"
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/../data/course-lamps" "${CMAKE_BINARY_DIR}/data/course-lamps")
target_sources(golf PRIVATE src/port/path_styles.c)
if(EXISTS "${SIMGOLF_PATH_STYLE_ASSETS}/index.tsv")
  target_compile_definitions(golf PRIVATE SIMGOLF_PATH_STYLE_ASSETS=1)
  add_custom_target(path_style_files ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/ui/path-styles"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SIMGOLF_PATH_STYLE_ASSETS}" "${CMAKE_BINARY_DIR}/ui/path-styles"
    VERBATIM)
endif()
target_sources(golf PRIVATE src/port/environment.c src/port/weather_state.c src/port/weather_audio.c)
target_sources(golf PRIVATE src/port/weather_climate.c src/port/atmosphere.c)
add_custom_target(weather_climate_files ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/data"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/../data/lighting-windows.csv" "${CMAKE_BINARY_DIR}/data/lighting-windows.csv"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/data"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/../data/weather-atmosphere.csv" "${CMAKE_BINARY_DIR}/data/weather-atmosphere.csv"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/data"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/../data/weather-climate.csv" "${CMAKE_BINARY_DIR}/data/weather-climate.csv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/../data/weather-surfaces.csv" "${CMAKE_BINARY_DIR}/data/weather-surfaces.csv"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/../data/weather-air.csv" "${CMAKE_BINARY_DIR}/data/weather-air.csv"
  VERBATIM)
target_sources(golf PRIVATE src/port/environment_fx.cpp)
target_sources(golf PRIVATE src/port/environment_receiver.c)
set_property(SOURCE src/port/environment_fx.cpp APPEND PROPERTY COMPILE_OPTIONS /EHsc)
target_sources(golf PRIVATE src/port/course_relief.c src/port/course_light.c src/port/cast_shadow.c src/platform/terrain_lighting.cpp)
target_sources(golf PRIVATE src/port/scenery_reflect.c src/port/rainbow.c)
set_property(SOURCE src/platform/terrain_lighting.cpp APPEND PROPERTY COMPILE_OPTIONS /EHsc)
add_executable(environment_tests "${CMAKE_SOURCE_DIR}/../tests/native/test_environment.c" src/port/environment.c src/port/atmosphere.c)
target_include_directories(environment_tests PRIVATE src "${SIMGOLF_SDL2}/include")
target_compile_options(environment_tests PRIVATE /clang:-std=gnu11)
target_link_libraries(environment_tests PRIVATE "${SIMGOLF_SDL2}/lib/x86/SDL2.lib")
# task-56: smooth zoom (the glide between golf's zoom levels)
target_sources(golf PRIVATE src/port/hd_zoom.c)
# task-17: the art pack (4x upscaled art at screen resolution; PNGs through WIC)
target_sources(golf PRIVATE src/port/hd_art.c)
# task-78: realistic water (Direct3D 11 pass in the presenter; shaders and numbers in ui/)
target_sources(golf PRIVATE src/port/water_native.c)
add_custom_target(water_files ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/ui"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/../data/ui/water.hlsl" "${CMAKE_SOURCE_DIR}/../data/ui/water.txt" "${CMAKE_BINARY_DIR}/ui"
  VERBATIM)
# task-25: the ui.font switch (settings, face map, blending) behind gdi.c's text path
target_sources(golf PRIVATE src/port/ui_font.c)

# Release version shown on the start menu (task-80), from tools/release/VERSION.
set(SIMGOLF_VERSION "dev")
if(EXISTS "${CMAKE_SOURCE_DIR}/../tools/release/VERSION")
  file(STRINGS "${CMAKE_SOURCE_DIR}/../tools/release/VERSION" SIMGOLF_VERSION LIMIT_COUNT 1)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/../tools/release/VERSION")
endif()
target_compile_definitions(golf PRIVATE SIMGOLF_PORT_FS _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _USE_MATH_DEFINES
  "SIMGOLF_DEFAULT_GAME=\"${SIMGOLF_GAME}\"" "SIMGOLF_DEFAULT_ART_DIR=\"${SIMGOLF_ART}\""
  "SIMGOLF_VERSION=\"${SIMGOLF_VERSION}\"")
target_include_directories(golf PRIVATE src third_party/stb src/compat/msvc "${SIMGOLF_SDL2}/include")
add_executable(roster_tests ../tests/native/test_roster.c src/port/golf/roster.c)
target_include_directories(roster_tests PRIVATE src)
target_compile_definitions(roster_tests PRIVATE _CRT_SECURE_NO_WARNINGS ROSTER_TEST_IMAGE)
# Mod platform (task-11): sources, defaults and the modctl_core / mods_tests tools.
include(cmake/mods.cmake)
# Frosted Mint UI kit and course HUD (task-26/30): kit sources, ui_tests, ui_gallery.
include(cmake/ui.cmake)
# Round HD golf ball (task-41): sources and ui/ball.txt.
include(cmake/ball.cmake)
# Installer worker sgsetup.exe (task-76): unpacks the player's own disc.
include(cmake/setup.cmake)

# Same semantics as the browser build (docs/porting.md): wrapping signed and
# pointer arithmetic, no strict aliasing, null checks kept, loops may spin.
set(_sem /clang:-fwrapv /clang:-fwrapv-pointer /clang:-fno-strict-aliasing
  /clang:-fno-delete-null-pointer-checks /clang:-fno-finite-loops)
target_compile_options(golf PRIVATE ${_sem} -gline-tables-only
  $<$<COMPILE_LANGUAGE:C>:/clang:-std=gnu11> -Wno-unused-function -Wno-unused-parameter)

foreach(f IN LISTS SIMGOLF_GEN_SRC)
  get_filename_component(d "${f}" DIRECTORY)
  set_property(SOURCE "${f}" APPEND PROPERTY INCLUDE_DIRECTORIES "${d}")
endforeach()
set_property(SOURCE ${SIMGOLF_GOLF_OVERRIDES} APPEND PROPERTY INCLUDE_DIRECTORIES "${SIMGOLF_GEN}/golf")
set_property(SOURCE ${SIMGOLF_JGLD_OVERRIDES} APPEND PROPERTY INCLUDE_DIRECTORIES "${SIMGOLF_GEN}/jgld")
# Generated code: -O1 and no warnings, as in the browser build. Locals start
# at zero: the decompile reads some it never assigns (stack slots Ghidra could
# not follow, e.g. FUN_00462020 on the course-site screen). wasm locals are
# zero there; x86 stack garbage crashed.
set_source_files_properties(${SIMGOLF_GEN_SRC} ${SIMGOLF_GOLF_OVERRIDES} ${SIMGOLF_JGLD_OVERRIDES}
  PROPERTIES COMPILE_OPTIONS "-w;/O1;/clang:-ftrivial-auto-var-init=zero;-Wno-error=int-conversion;-Wno-error=incompatible-pointer-types;-Wno-error=implicit-function-declaration;-Wno-error=incompatible-function-pointer-types;-Wno-error=return-type")

# Link: the exe sits at 0x60000000, away from the images at 0x00400000 and
# 0x10000000 (host_win32.c); SDL2.dll loads on first use, after the ranges are
# committed. No large-address awareness: the game compares pointers as signed
# ints, so every address must stay below 2 GB.
target_link_options(golf PRIVATE /BASE:0x60000000 /DYNAMICBASE:NO /LARGEADDRESSAWARE:NO
  /ENTRY:mainCRTStartup /DELAYLOAD:SDL2.dll /DEBUG /OPT:REF /OPT:ICF)
target_link_libraries(golf PRIVATE "${SIMGOLF_SDL2}/lib/x86/SDL2.lib" delayimp.lib winmm.lib user32.lib ws2_32.lib windowscodecs.lib ole32.lib uuid.lib)

# Runtime files next to the exe.
add_custom_command(TARGET golf POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SIMGOLF_SDL2}/lib/x86/SDL2.dll" "$<TARGET_FILE_DIR:golf>"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:golf>/image" "$<TARGET_FILE_DIR:golf>/fonts"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SIMGOLF_IMAGES}/golf.bin" "${SIMGOLF_IMAGES}/jgld.bin" "$<TARGET_FILE_DIR:golf>/image"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/third_party/fonts" "$<TARGET_FILE_DIR:golf>/fonts"
  VERBATIM)
# task-25: Plus Jakarta Sans (static cuts, OFL) and its face map next to the exe, for
# ui.font=plus_jakarta. A target of its own so editing data/ui/font_map.txt re-copies it.
set(_pj "${CMAKE_SOURCE_DIR}/../data/fonts/plus-jakarta-sans")
if(EXISTS "${CMAKE_SOURCE_DIR}/../art_local/weather/rain-light.wav")
  add_custom_target(weather_audio_files ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${CMAKE_SOURCE_DIR}/../art_local/weather"
      "$<TARGET_FILE_DIR:golf>/audio/weather")
endif()
add_custom_target(ui_font_files ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/fonts/plus-jakarta-sans"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${_pj}/PlusJakartaSans-Regular.ttf" "${_pj}/PlusJakartaSans-Medium.ttf" "${_pj}/PlusJakartaSans-SemiBold.ttf"
    "${_pj}/PlusJakartaSans-Bold.ttf" "${_pj}/PlusJakartaSans-ExtraBold.ttf" "${_pj}/OFL.txt"
    "${CMAKE_SOURCE_DIR}/../data/ui/font_map.txt"
    "${CMAKE_BINARY_DIR}/fonts/plus-jakarta-sans"
  VERBATIM)

# Issue62: selection geometry is independent of game memory and prices.
add_executable(place_drag_tests ../tests/native/place_drag_shape.c)
target_include_directories(place_drag_tests PRIVATE src)
