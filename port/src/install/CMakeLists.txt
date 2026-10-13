cmake_minimum_required(VERSION 3.20)
project(simgolf_installer LANGUAGES C)
# Build and test the installer without translated game code or launching the game.
get_filename_component(CMAKE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${CMAKE_SOURCE_DIR}/cmake/setup.cmake")
