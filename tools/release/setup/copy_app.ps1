# copy_app.ps1: copies the program files of a release (the zip's app folder) into the install folder (card task-114i).
#   copy_app.ps1 -App <zip>\app -Dir <install folder>
# The player's own mods.ini is never replaced: the release ships the documented defaults as mods.ini.default
# (always refreshed) and mods.ini is only written when the install has none yet. An older zip that still holds a
# mods.ini does not overwrite the player's either.
param([Parameter(Mandatory = $true)][string]$App, [Parameter(Mandatory = $true)][string]$Dir)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $Dir | Out-Null
$mine = Join-Path $Dir "mods.ini"
$had = Test-Path -LiteralPath $mine
Get-ChildItem -LiteralPath $App -Force | ForEach-Object {
  if ($_.Name -ieq "mods.ini" -and $had) { return }
  Copy-Item -LiteralPath $_.FullName -Destination $Dir -Recurse -Force
}
$def = Join-Path $Dir "mods.ini.default"
if (-not (Test-Path -LiteralPath $mine) -and (Test-Path -LiteralPath $def)) { Copy-Item -LiteralPath $def $mine }

# Keep the established internal payload name compatible with earlier releases.
$program = Join-Path $Dir "golf.exe"
if (Test-Path -LiteralPath $program) {
  Copy-Item -LiteralPath $program -Destination (Join-Path $Dir "Sim Golf Remastered.exe") -Force
}
