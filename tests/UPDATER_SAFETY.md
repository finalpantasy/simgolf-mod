# Updater backup-pruning regression

On Windows with an installed clang-cl x86 toolchain, run:

    $env:SIMGOLF_TEST_C_COMPILER = 'C:/path/to/clang-cl.exe'
    python tests/test_updater_backup_pruning.py

This compiles the real updater with a frozen clock and synthetic program bytes.
It verifies that three successful same-second updates retain only the current
completed backup, including after a lower suffix is reused, and preserve an
unmarked recovery backup. It also checks that the retained backup contains the
immediate previous program and both installed launcher names contain the update.
No game or staged program is launched. It requires no game assets or network.

This regression does not validate D3D, graphical/gameplay behavior, concurrent
updater invocations, or a full game build.
