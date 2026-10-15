# copy_app.ps1: copies the program files of a release (the zip's app folder) into the install folder (card task-114i).
#   copy_app.ps1 -App <zip>\app -Dir <install folder>
# The player's own mods.ini is never replaced: the release ships the documented defaults as mods.ini.default
# (always refreshed) and mods.ini is only written when the install has none yet. An older zip that still holds a
# mods.ini does not overwrite the player's either.
# Existing mod files and profiles are player-owned. Add missing defaults only.
param([Parameter(Mandatory = $true)][string]$App, [Parameter(Mandatory = $true)][string]$Dir)
$ErrorActionPreference = "Stop"
function Copy-ModDefaults([string]$Source, [string]$Destination) {
  $entry = Get-Item -LiteralPath $Source -Force
  if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw 'Linked mod defaults cannot be copied.'
  }
  $existing = Get-Item -LiteralPath $Destination -Force -ErrorAction SilentlyContinue
  if ($existing -and (($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
      -not $entry.PSIsContainer -or -not $existing.PSIsContainer)) { return }
  if ($entry.PSIsContainer) {
    if (-not $existing) { New-Item -ItemType Directory -Path $Destination -Force | Out-Null }
    Get-ChildItem -LiteralPath $Source -Force | ForEach-Object {
      Copy-ModDefaults $_.FullName (Join-Path $Destination $_.Name)
    }
  } else {
    # Never overwrite a file that appeared after the existence check either.
    [IO.File]::Copy($entry.FullName, $Destination, $false)
  }
}
New-Item -ItemType Directory -Force $Dir | Out-Null
$mine = Join-Path $Dir "mods.ini"
$had = Test-Path -LiteralPath $mine
Get-ChildItem -LiteralPath $App -Force | ForEach-Object {
  if ($_.Name -ieq "mods.ini" -and $had) { return }
  if ($_.Name -ieq "mods" -and $_.PSIsContainer) {
    Copy-ModDefaults $_.FullName (Join-Path $Dir 'mods')
    return
  }
  Copy-Item -LiteralPath $_.FullName -Destination $Dir -Recurse -Force
}
$def = Join-Path $Dir "mods.ini.default"
if (-not (Test-Path -LiteralPath $mine) -and (Test-Path -LiteralPath $def)) { Copy-Item -LiteralPath $def $mine }

# Keep the established internal payload name compatible with earlier releases.
$program = Join-Path $Dir "golf.exe"
if (Test-Path -LiteralPath $program) {
  Copy-Item -LiteralPath $program -Destination (Join-Path $Dir "Sim Golf Remastered.exe") -Force
}
