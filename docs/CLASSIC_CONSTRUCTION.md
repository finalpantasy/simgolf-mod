# Classic construction controls and sharp legacy text

The remastered construction menu is the default. Options > Gameplay > Look of
the game > Original construction menu enables the optional classic wheel and
Course/Buildings trays with modern top stats. The switch is live and saved.
Leave it off for the combined Course palette, path-material picker and lamp
picker. The original HUD remains available through `ui.hud=original`.

The mistaken built-in classic-menu default in 0.1.19 is ignored on upgrade;
explicit player choices in mods.ini or Options remain honored.

The existing optional HD art pack provides the faithful 4x versions of
`Interface/3mainLowerLeft.pcx`, `Interface/BuildingPanel.pcx` and
`Interface/BaseTerrainPanel.pcx`, including their alpha sheets. Select HD
textures to use these sheets. Without that pack, original artwork still works.
No original game assets are included in the source change.

`ui.original_bottom=0` keeps the default remastered construction palette.
`ui.original_bottom=1` opts into the classic construction wheel and trays.
`ui.hd_legacy_text=0` restores the previous low-resolution bitmap text for A/B.
`ui.font=original` retains the original font faces while allowing native-size
text replay. `ui.font=plus_jakarta` uses the existing modern font option.

Windows now records both GDI font paths and validates text against the actual
presented frame. That includes direct JGLD frame copies that bypass BitBlt.
Glyphs retain their actual font, original placement, tabular digits, color,
baseline and clip. Intact bitmap pixels are removed from a private frame copy,
then text is rasterized at display resolution on the render thread. Overpainted
or uncaptured text retains its bitmap fallback. Immutable records are published
with their frame, separating moving course text from fixed interface text.

This changes presentation and construction control layout only. Prices,
progression, placement rules, saves and original geometry stay unchanged.
