# Troubleshooting

[Modding guide](README.md) / Troubleshooting

Start with `golf.log` beside the running executable. Confirm which install you launched, which profile it selected and which mods resolved. A second install or an old shortcut can make correct edits appear ineffective.

| Symptom | Check and repair |
|---|---|
| Mod does not appear | Look for `mods/<id>/mod.toml` beside the executable. Remove an extra ZIP wrapper folder. Show file extensions and check for `mod.toml.txt`. |
| Mod is listed but off | Select the intended profile, enable it, apply and restart. Check `[mods]` in `mods.ini` and player Options for an explicit off switch. |
| Broken manifest | Check TOML syntax, lowercase ID, semantic version and duplicate keys. Put top-level keys before section headers. Use `validate` if you have the helper. |
| Missing dependency | Install a compatible version separately. An explicitly disabled dependency stays off; merely installing it may not enable the dependent mod. |
| Correct JSON, no visible effect | Check the mod is loaded, the table filename and row/field IDs are exact, and a `data:` line reports the change. Use a new course for starting cash and generation changes. |
| Settings ignored | Check the key has a source-code consumer. Then check live values, `options.ini`, `mods.ini` and later mods for overrides. |
| Price seems wrong | Building prices use $100 units, then the game can apply level and site-work costs. Compare the same level and equivalent terrain. |
| Conflict warning | Inspect both operations and the resolved load order. A later exclusive operation wins; dependencies can reorder the player's list. |
| Invalid row or read-only field | Consult `data/tables/<table>.json`. Most tables have fixed rows. A schema or manifest declaration does not make new engine types work. |
| Dialogue does not appear | Check thought-event IDs, share allocation, chance and a relevant event. The Player Comments report retains original text. Test characters in the actual bubble. |
| Old save warns about missing mods | Restore the recorded mod/version or test a copy without it. The companion records identity, not a bundled copy of the mod. |
| Save won't open in the original game | Expanded map/staff saves can require newer readers. The mod companion is not the only compatibility factor. Return to a backup made before enabling those features. |
| CLI helper not found | Build the [standalone helper](SOURCE.md#build-only-the-mod-validation-helper); set `MODCTL_CORE` or use the global `--core` option. It is not shipped in the standard release. |
| CLI rejects an option | Place `--mods-dir` and `--core` before the subcommand; put `--profile` after a command that accepts it. |
| Data check can't read image | Supply the mapped `image/golf.bin` from a matching prepared install/build, not the raw `golf_nocd.exe`. |
| Source edit makes no difference | Retranslate after changing translator patches or override markers, rebuild, then verify the exact executable you launched. |
| Modern labels or buttons are blank | Use a complete package or native ALL build, including `ui_font_files`; the game target alone can omit fonts. |
| Clean native configure cannot find tests | This is a known gap in the public 0.1.16 source export. See [full-build limitations](SOURCE.md#full-game-build-prerequisites-and-current-gaps). |

## Report a reproducible problem

Include the Remastered version or source commit, mod versions, profile and resolved order, exact steps, expected result, observed result and relevant log lines. For a build failure, include the command and first meaningful error. For a visual issue, attach a real screenshot.

Reduce the reproduction in a copy of your test install: affected mod alone, then its required dependencies, then the other suspected mod. Keep the original failing log. Remove personal paths and unrelated private information before uploading evidence. Do not attach original game files or private saves to a public issue.
