#!/usr/bin/env bash
# Portable source/failure-path checks only. This is not Windows/gameplay QA.
set -euo pipefail
cd "$(dirname "$0")/.."
python3 -m unittest discover -s port/tests -p 'test_*.py' -v
python3 -m unittest discover -s tests -p 'test_updater_safety.py' -v
python3 -m unittest discover -s tests -p 'test_original_ui_docs.py' -v
python3 -m unittest discover -s tests/diagnostics -p 'test_*.py' -v
python3 port/tests/save_group_test.py
python3 port/tests/living_dormant_test.py
python3 port/tests/terrain_recovery.py
python3 port/tests/water_recovery.py
python3 port/tests/ui_text_recovery.py
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
for test in hd_art_dialog_cutoff hd_art_preview_pixels hud_tuck_policy reports_handoff_policy; do
  "${CC:-cc}" -O1 -Wall -Wextra -Werror -Iport/src "port/tests/${test}_test.c" -o "$tmp/$test"
  "$tmp/$test"
done
bash -n tools/release/package.sh
echo 'PASS: portable safety checks. Windows/x86, PowerShell, D3D and gameplay validation remain required.'
