#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
[ -f tools/env.sh ] && . tools/env.sh
CC=${CC:-${TOOLS_EXT:-F:/dev/tools_ext}/llvm/bin/clang}
command -v "$CC" >/dev/null 2>&1 || CC=gcc
command -v "$CC" >/dev/null 2>&1 || { echo "path queue: no C compiler"; exit 3; }
mkdir -p build/tmp
"$CC" -std=c11 -O1 -Wall -Wextra -Werror -Iport/src port/tests/path_queue_test.c -o build/tmp/path_queue_test.exe
build/tmp/path_queue_test.exe
${PYTHON:-python} port/tests/test_path_queue_patch.py
