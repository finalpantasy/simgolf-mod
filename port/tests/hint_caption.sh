#!/usr/bin/env bash
# Golfer snapshot popup heading regression. No game launch or assets required.
set -uo pipefail
cd "$(dirname "$0")/../.."
[ -f tools/env.sh ] && . tools/env.sh
CC=${CC:-${TOOLS_EXT:-}/llvm/bin/clang}
command -v "$CC" >/dev/null 2>&1 || CC=gcc
command -v "$CC" >/dev/null 2>&1 || { echo "hint caption test: no C compiler"; exit 3; }
mkdir -p build/tmp
"$CC" -O1 -Wall -Wextra -Werror -Iport/src port/tests/hint_caption_test.c port/src/port/ui/ui_dialog_text.c -o build/tmp/hint_caption_test.exe || {
  echo "hint caption test: build failed"
  exit 3
}
build/tmp/hint_caption_test.exe
