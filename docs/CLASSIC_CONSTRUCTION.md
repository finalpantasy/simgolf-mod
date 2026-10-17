# Classic construction controls and sharp legacy text

The classic construction wheel and original trays are restored by default with
the modern top stats, notifications and Options. Options > Gameplay > Look of
the game > Original construction menu switches live and saves the preference.
Turn it off to use the combined Course palette, path-material picker and lamp
picker from 0.1.18. The original HUD remains available through `ui.hud=original`.

The existing optional HD art pack provides the faithful 4x versions of
`Interface/3mainLowerLeft.pcx`, `Interface/BuildingPanel.pcx` and
`Interface/BaseTerrainPanel.pcx`, including their alpha sheets. Select HD
textures to use these sheets. Without that pack, original artwork still works.
No original game assets are included in the source change.

`ui.original_bottom=0` restores the modern construction palette.
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
