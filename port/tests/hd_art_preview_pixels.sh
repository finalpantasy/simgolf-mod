#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/../.."
[ -f tools/env.sh ] && . tools/env.sh
CC=${CC:-$TOOLS_EXT/llvm/bin/clang}
command -v "$CC" >/dev/null 2>&1 || CC=gcc
command -v "$CC" >/dev/null 2>&1 || { echo "HD preview pixel test: no C compiler"; exit 3; }
mkdir -p build/tmp
"$CC" -O1 -Wall -Wextra -Werror -Iport/src port/tests/hd_art_preview_pixels_test.c -o build/tmp/hd_art_preview_pixels_test.exe || exit 3
build/tmp/hd_art_preview_pixels_test.exe
