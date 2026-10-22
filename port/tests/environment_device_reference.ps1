param(
  [Parameter(Mandatory=$true)][string]$Compiler,
  [Parameter(Mandatory=$true)][string]$SDL2Root
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$out = Join-Path $root 'build/tests/device-reference-x86'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out 'environment_device_reference_test.exe'
& $Compiler -target i686-pc-windows-msvc -std=c++17 -O1 -D_CRT_SECURE_NO_WARNINGS `
  "-I$root/port/src" "-I$SDL2Root/include" `
  "$PSScriptRoot/environment_device_reference_test.cpp" "$SDL2Root/lib/x86/SDL2.lib" -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Copy-Item -LiteralPath "$SDL2Root/lib/x86/SDL2.dll" -Destination $out -Force
$env:SIMGOLF_MUTE = '1'
& $exe
exit $LASTEXITCODE
