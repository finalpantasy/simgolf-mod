#!/usr/bin/env bash
# Pure Save Name art-cutoff regression. No game launch or assets required.
set -uo pipefail
cd "$(dirname "$0")/../.."
[ -f tools/env.sh ] && . tools/env.sh
CC=${CC:-$TOOLS_EXT/llvm/bin/clang}
command -v "$CC" >/dev/null 2>&1 || CC=gcc
command -v "$CC" >/dev/null 2>&1 || { echo "Save Name art-cutoff test: no C compiler"; exit 3; }
mkdir -p build/tmp
"$CC" -O1 -Wall -Wextra -Werror -Iport/src port/tests/hd_art_dialog_cutoff_test.c -o build/tmp/hd_art_dialog_cutoff_test.exe || {
  echo "Save Name art-cutoff test: build failed"
  exit 3
}
build/tmp/hd_art_dialog_cutoff_test.exe
