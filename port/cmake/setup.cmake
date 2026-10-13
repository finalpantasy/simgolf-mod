# Installer worker (simgolf mod task-76): sgsetup.exe unpacks the player's own
# SimGolf disc (InstallShield cabinets, unshield) and maps their golf.exe and
# jgld.dll into image/ for tools/release/setup/setup.ps1. unshield and the
# zlib inflate code are in third_party (MIT and zlib licences).
set(_us "${CMAKE_SOURCE_DIR}/third_party/unshield")
set(_zl "${CMAKE_SOURCE_DIR}/third_party/zlib")
add_executable(sgsetup ${CMAKE_SOURCE_DIR}/src/install/sgsetup.c ${CMAKE_SOURCE_DIR}/src/compat/msvc/dirent.c
  ${_us}/lib/component.c ${_us}/lib/converter.c ${_us}/lib/directory.c ${_us}/lib/file.c
  ${_us}/lib/file_group.c ${_us}/lib/helper.c ${_us}/lib/libunshield.c ${_us}/lib/log.c ${_us}/lib/md5/md5c.c
  ${_zl}/adler32.c ${_zl}/crc32.c ${_zl}/inffast.c ${_zl}/inflate.c ${_zl}/inftrees.c ${_zl}/zutil.c)
target_include_directories(sgsetup PRIVATE ${CMAKE_SOURCE_DIR}/src/compat/msvc "${_us}" "${_us}/lib" "${_zl}")
target_compile_definitions(sgsetup PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
target_link_libraries(sgsetup PRIVATE bcrypt)
target_compile_options(sgsetup PRIVATE $<$<COMPILE_LANGUAGE:C>:/clang:-std=gnu11>)
set_source_files_properties(${_us}/lib/component.c ${_us}/lib/converter.c ${_us}/lib/directory.c ${_us}/lib/file.c
  ${_us}/lib/file_group.c ${_us}/lib/helper.c ${_us}/lib/libunshield.c ${_us}/lib/log.c ${_us}/lib/md5/md5c.c
  ${_zl}/adler32.c ${_zl}/crc32.c ${_zl}/inffast.c ${_zl}/inflate.c ${_zl}/inftrees.c ${_zl}/zutil.c
  PROPERTIES COMPILE_OPTIONS "-w")
# The NOP list for jgld.bin next to the exe (the browser installer's copy).
add_custom_command(TARGET sgsetup POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/web/pages/jgld-nops.json" "$<TARGET_FILE_DIR:sgsetup>"
  VERBATIM)

option(SIMGOLF_SETUP_TESTS "Build installer worker with a synthetic data manifest for tests" OFF)
if(SIMGOLF_SETUP_TESTS)
  get_target_property(_setup_sources sgsetup SOURCES)
  add_executable(sgsetup_fixture ${_setup_sources})
  target_include_directories(sgsetup_fixture PRIVATE ${CMAKE_SOURCE_DIR}/src/compat/msvc "${_us}" "${_us}/lib" "${_zl}")
  target_compile_definitions(sgsetup_fixture PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS
    SGSETUP_MANIFEST_HEADER="${CMAKE_SOURCE_DIR}/../tests/release/setup_fixture.h")
  target_compile_options(sgsetup_fixture PRIVATE $<$<COMPILE_LANGUAGE:C>:/clang:-std=gnu11>)
  target_link_libraries(sgsetup_fixture PRIVATE bcrypt)
endif()

# The updater (task-80): sgupdate.exe checks a release zip's SHA-256, unpacks it and swaps the program files.
add_executable(sgupdate ${CMAKE_SOURCE_DIR}/src/updater/sgupdate.c)
target_compile_definitions(sgupdate PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
target_compile_options(sgupdate PRIVATE $<$<COMPILE_LANGUAGE:C>:/clang:-std=gnu11>)
