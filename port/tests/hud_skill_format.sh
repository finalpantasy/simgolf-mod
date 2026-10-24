#!/usr/bin/env bash
# Pure HUD skill-format regression. No game launch or assets required.
set -uo pipefail
cd "$(dirname "$0")/../.."
[ -f tools/env.sh ] && . tools/env.sh
CC=${CC:-$TOOLS_EXT/llvm/bin/clang}
command -v "$CC" >/dev/null 2>&1 || CC=gcc
command -v "$CC" >/dev/null 2>&1 || { echo "HUD skill-format test: no C compiler"; exit 3; }
mkdir -p build/tmp
"$CC" -O1 -Wall -Wextra -Werror -Iport/src port/tests/hud_skill_format_test.c -o build/tmp/hud_skill_format_test.exe || {
  echo "HUD skill-format test: build failed"
  exit 3
}
build/tmp/hud_skill_format_test.exe
