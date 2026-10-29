# Windows application icon

- Source geometry: the existing project-authored UI_ICON_GOLF_BALL in port/src/port/ui/ui_icons.c, unchanged. The Frosted Mint icon set is identified in ui_icons.h as the hand-drawn project mockup paths. The coordinator's ui-fonts manifest records that these paths were written for this project and do not come from an icon pack.
- Colors: existing data/ui/frosted_mint.toml accent.color (#0E9F6E) and text.on_accent (#FFFFFF), on a rounded badge with transparent corners.
- Purpose: supply the missing executable, shortcut and running-window icon using existing UI artwork. The HD artwork proposed in issue #106 is not included.
- Licence: derived from this project's MIT-licensed UI code. No original game image, reporter attachment, third-party icon pack or private player file is used.
- Tool: port/tools/make_app_icon.c calls the production ui_icon_raster and writes standard 32-bit ICO DIB frames at 16, 20, 24, 32, 48, 64, 128 and 256 pixels. Its optional PNG preview uses the existing ui_png_write and shows transparency over white.
- Cost: zero; local conversion only.

From the repository root, create build/tmp, then rebuild with an installed C compiler:

    clang -std=c11 -O2 -Iport/src port/tools/make_app_icon.c port/src/port/ui/ui_icons.c port/src/port/ui/ui_png.c -o build/tmp/make_app_icon.exe
    build/tmp/make_app_icon.exe data/ui/app_icon.ico build/tmp/app_icon.png

On toolchains that require it, add -lm to the compile command. The ICO is committed so ordinary native builds need no new image-conversion dependency. Windows resource and extraction checks are in port/tests/test_native_icon.py.
