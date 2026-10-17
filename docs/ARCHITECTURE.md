# Architecture

## Layers
1. Game code: golf.exe v1.02 and jgld.dll decompiled by Ghidra, translated mechanically to C (`port/tools/translate`), generated into `build/gen` at build time. Never committed, never hand-edited.
2. Overrides: hand-written replacements in `port/src/` tagged `// @override FUN_xxxxxxxx`; small text fixes in the translator json `patches`. All our mods live here or in new platform code.
3. Runtime: images loaded at their original addresses (`port/src/port/runtime.c`), CRT replacements, Win32 subset (`win32.c`, `gdi.c`), terrain renderer (`port/src/platform/terrain_*.cpp`), sound.
4. Platform backends: browser (Emscripten + JSPI, upstream) and native Windows (ours, task-2): a 32-bit build that reserves the original image ranges (0x400000.., 0x10000000..) and links the host exe elsewhere.

## Resolution plan (task-3)
The game draws an 800x600 frame through jgld's GDI calls and an OpenGL terrain. Step 1 scales that frame to any window size with integer/sharp filtering. Step 2 raises the logical canvas (widescreen, more course visible) by overriding the functions that hard-code 800x600, and renders terrain at native resolution.

## References (upstream/, read only)
- simgolfRE: VC6 matching decompilation of v1.03, Win11 shim findings, Frida/A-B tooling.
- simgolf-native: clean-room Rust port; docs/FORMATS.md, TERRAIN.md, GAMELOGIC.md are the best file-format and rules notes.
- simgolf-wine-fix: Terrain.dll GDI/OpenGL sync notes.
