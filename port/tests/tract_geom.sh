#!/usr/bin/env bash
# Land-sale tract map numbering regression (issue 110). No game launch or assets required.
set -uo pipefail
cd "$(dirname "$0")/../.."
[ -f tools/env.sh ] && . tools/env.sh
CC=${CC:-${TOOLS_EXT:-}/llvm/bin/clang}
command -v "$CC" >/dev/null 2>&1 || CC=gcc
command -v "$CC" >/dev/null 2>&1 || { echo "tract geom test: no C compiler"; exit 3; }
mkdir -p build/tmp
"$CC" -O1 -Wall -Wextra -Werror -Iport/src port/tests/tract_geom_test.c -o build/tmp/tract_geom_test.exe || {
  echo "tract geom test: build failed"
  exit 3
}
build/tmp/tract_geom_test.exe
