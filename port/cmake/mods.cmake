# Mod platform core (simgolf mod, task-11), included by native.cmake after the
# golf target. Adds the mod sources to the game and builds two small tools
# from the same sources: modctl_core.exe (the helper behind tools/modctl, so
# the CLI and the game resolve identically) and mods_tests.exe (unit tests,
# run by tools/check.sh). docs/modding/CORE.md.

set(SIMGOLF_MODS_CORE
  src/mods/semver.c src/mods/manifest.c src/mods/resolve.c src/mods/session.c
  src/mods/store.c src/mods/modmgr.c src/mods/saverec.c src/mods/relocate.c src/mods/json.c src/mods/data.c
  third_party/tomlc99/toml.c)
set_source_files_properties(third_party/tomlc99/toml.c PROPERTIES COMPILE_OPTIONS "-w")

target_sources(golf PRIVATE ${SIMGOLF_MODS_CORE} src/mods/mods_runtime.c src/mods/data_runtime.c src/mods/living_club.c src/mods/living_runtime.c)
target_include_directories(golf PRIVATE third_party)
# The repo's mods/ and data/mods.ini are the defaults; golf.ini, the command
# line and the environment override them (mods_runtime.c).
if(SIMGOLF_RELEASE)   # release exe: no build-machine paths, mods/, mods.ini and data/tables sit next to golf.exe
  target_compile_definitions(golf PRIVATE "SIMGOLF_DEFAULT_MODS=\"\"" "SIMGOLF_DEFAULT_MODS_INI=\"\"" "SIMGOLF_DEFAULT_TABLES=\"\"")
else()
  target_compile_definitions(golf PRIVATE
    "SIMGOLF_DEFAULT_MODS=\"${CMAKE_SOURCE_DIR}/../mods\""
    "SIMGOLF_DEFAULT_MODS_INI=\"${CMAKE_SOURCE_DIR}/../data/mods.ini\""
    "SIMGOLF_DEFAULT_TABLES=\"${CMAKE_SOURCE_DIR}/../data/tables\"")
endif()

add_executable(modctl_core src/mods/tool/modctl_core.c src/mods/tool/modctl_data.c ${SIMGOLF_MODS_CORE} src/compat/msvc/dirent.c)
add_executable(mods_tests "${CMAKE_SOURCE_DIR}/../tests/mods/test_mods.c" ${SIMGOLF_MODS_CORE} src/mods/mods_runtime.c
  src/compat/msvc/dirent.c)
# task-19: the data layer's unit tests (tests/mods/test_data.c).
add_executable(data_tests "${CMAKE_SOURCE_DIR}/../tests/mods/test_data.c" ${SIMGOLF_MODS_CORE} src/compat/msvc/dirent.c)
add_executable(living_tests "${CMAKE_SOURCE_DIR}/../tests/mods/test_living.c" src/mods/living_club.c src/mods/json.c src/mods/semver.c)
# task-22: the Mods screen's model (search, filter, sort, order and profile edits, load_order.toml).
add_executable(modmgr_tests "${CMAKE_SOURCE_DIR}/../tests/ui/test_modmgr.c" ${SIMGOLF_MODS_CORE} src/compat/msvc/dirent.c)
foreach(t modctl_core mods_tests data_tests modmgr_tests living_tests)
  target_include_directories(${t} PRIVATE src third_party src/compat/msvc)
  target_compile_definitions(${t} PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
  target_compile_options(${t} PRIVATE $<$<COMPILE_LANGUAGE:C>:/clang:-std=gnu11> -Wno-unused-function)
endforeach()
