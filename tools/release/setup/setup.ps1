# SimGolf Remastered setup. Double-click Setup.cmd (it runs this file).
#
# Asks for YOUR copy of the game (a disc image or installed folder, and the v1.02 no-CD zip),
# checks it, unpacks it next to our program, optionally downloads the HD art
# pack, writes golf.ini and makes a desktop shortcut. Nothing is uploaded;
# the only download is the optional HD art pack from GitHub.
#
# For testing and scripted installs every question has a parameter:
#   setup.ps1 -Dir C:\Games\SimGolf -Disc SimGolf.iso -NoCd nocd.zip -Hd no|yes|<folder> -NoShortcut -Quiet
param(
  [string]$Dir = "",
  [string]$Disc = "",
  [string]$GameDir = "",         # existing original SimGolf folder, instead of -Disc
  [string]$NoCd = "",
  [string]$Hd = "",              # "yes" (download), "no", or a folder holding the pack parts you already downloaded
  [string]$HdUrl = "https://github.com/finalpantasy/simgolf-hd-art/releases/latest/download",
  [switch]$NoShortcut,
  [switch]$Quiet                 # no dialogs: every answer must come from parameters
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms, System.IO.Compression.FileSystem
[System.Windows.Forms.Application]::EnableVisualStyles()
# GitHub only speaks TLS 1.2+; Windows PowerShell 5.1 may default to older protocols.
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
# Hidden top-most owner so every dialog opens in front of the console window, not behind it.
$owner = New-Object System.Windows.Forms.Form -Property @{ TopMost = $true; ShowInTaskbar = $false; WindowState = "Minimized" }

$here = $PSScriptRoot
$app = Join-Path $here "app"
$NOCD_SHA1 = "6f05a3ae3b5050ed82dc0010c03842a2e0ab166b"
$title = "SimGolf Remastered setup"

function Say($t) { Write-Host $t }
function Info($t) { if (-not $Quiet) { [void][System.Windows.Forms.MessageBox]::Show($owner, $t, $title, "OK", "Information") } else { Say $t } }
function Die($t) {
  Write-Host ""
  Write-Host "Problem: $t" -ForegroundColor Red
  if (-not $Quiet) { [void][System.Windows.Forms.MessageBox]::Show($owner, $t, "$title - problem", "OK", "Error") }
  exit 1
}
function Ask($t) {
  if ($Quiet) { return $true }
  return [System.Windows.Forms.MessageBox]::Show($owner, $t, $title, "YesNo", "Question") -eq "Yes"
}
function Pick($text, $filter) {
  if ($Quiet) { Die "A required file was not supplied. Use -NoCd and either -Disc or -GameDir with -Quiet." }
  $d = New-Object System.Windows.Forms.OpenFileDialog
  $d.Title = $text
  $d.Filter = $filter
  if ($d.ShowDialog($owner) -ne "OK") { Die "Setup was cancelled." }
  return $d.FileName
}
function Pick-Folder($text, $start) {
  if ($Quiet) { Die "A required folder was not supplied." }
  $d = New-Object System.Windows.Forms.FolderBrowserDialog
  $d.Description = $text
  $d.SelectedPath = $start
  if ($d.ShowDialog($owner) -ne "OK") { Die "Setup was cancelled." }
  return $d.SelectedPath
}
function Sha1-Of($path) { (Get-FileHash -Algorithm SHA1 -LiteralPath $path).Hash.ToLower() }

if (-not (Test-Path -LiteralPath (Join-Path $app "golf.exe"))) { Die "The 'app' folder is missing next to setup.ps1. Unzip the whole download first, then run Setup.cmd." }
if ($Disc -and $GameDir) { Die "Choose either -Disc or -GameDir, not both." }
if ($Quiet -and (-not $NoCd -or (-not $Disc -and -not $GameDir))) { Die "With -Quiet, supply -NoCd and either -Disc or -GameDir." }

Say "=== $title ==="
if (-not $Quiet) {
  Info ("This installs SimGolf Remastered on your PC.`n`nYou need your own copy of the game:`n  1. a SimGolf disc image OR an existing installed SimGolf folder`n  2. the version 1.02 'no-CD' golf.exe (a .zip, or the bare golf.exe)`n`nThe remaster runs the 1.02 program code. A 1.03 golf.exe cannot replace it, even when using a 1.03 install folder.`n`nSee INSTALL.txt. Nothing from your copy leaves your PC.")
}

# ---- where to install
if (-not $Dir) {
  $Dir = Join-Path $env:LOCALAPPDATA "Programs\SimGolf Remastered"
  if (-not $Quiet) {
    if (-not (Ask "Install to:`n$Dir`n`nYes = use this folder, No = choose another.")) { $Dir = Pick-Folder "Choose an empty folder for SimGolf Remastered" $env:USERPROFILE }
  }
}
$Dir = [System.IO.Path]::GetFullPath($Dir)
$ancestor = $Dir
while ($ancestor) {
  if (Test-Path -LiteralPath $ancestor) {
    if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { Die "Choose an install folder without junctions or symbolic links." }
  }
  $ancestor = [IO.Path]::GetDirectoryName($ancestor)
}
if (Test-Path -LiteralPath $Dir) {
  $links = Get-ChildItem -LiteralPath $Dir -Force -Recurse -Attributes ReparsePoint
  if ($links) { Die "Choose an install folder without junctions or symbolic links." }
}
if ((Test-Path -LiteralPath (Join-Path $Dir "game")) -and -not $Quiet) {
  if (-not (Ask "SimGolf is already installed in:`n$Dir`n`nInstall again over it? (Your saved games are kept.)")) { exit 0 }
}
$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("simgolf-setup-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force $tmp | Out-Null

try {
  # ---- choose the data source
  if (-not $Disc -and -not $GameDir) {
    $choice = [System.Windows.Forms.MessageBox]::Show($owner, "Where should setup get your game files?`n`nYes = an existing SimGolf install folder`nNo = a disc image`nCancel = stop setup", $title, "YesNoCancel", "Question")
    if ($choice -eq "Cancel") { Die "Setup was cancelled." }
    if ($choice -eq "Yes") { $GameDir = Pick-Folder "Choose the existing SimGolf folder containing its art, sound and data" $env:USERPROFILE }
    else { $Disc = Pick "Choose the SimGolf disc image (.iso, or the .7z/.zip it came in)" "Disc image or archive|*.iso;*.bin;*.img;*.mdf;*.7z;*.zip;*.rar|All files|*.*" }
  }
  if ($GameDir) {
    $GameDir = [IO.Path]::GetFullPath($GameDir).TrimEnd('\', '/')
    $target = $Dir.TrimEnd('\', '/')
    if ($GameDir -ieq $target -or $target.StartsWith($GameDir + '\', [StringComparison]::OrdinalIgnoreCase) -or $GameDir.StartsWith($target + '\', [StringComparison]::OrdinalIgnoreCase)) {
      Die "Choose a separate install folder outside the existing SimGolf folder."
    }
    Say "Checking the existing SimGolf files..."
    $checkLog = Join-Path $tmp "folder-check.log"
    $checkErr = Join-Path $tmp "folder-check.err"
    $checked = Start-Process -FilePath (Join-Path $app "sgsetup.exe") -ArgumentList @("check-folder", "`"$GameDir`"") -NoNewWindow -Wait -PassThru -RedirectStandardOutput $checkLog -RedirectStandardError $checkErr
    if ($checked.ExitCode -ne 0) { Die ((Get-Content $checkErr -Raw) + "`nUse a clean matching folder or the disc image instead.") }
  }
  # ---- the no-CD golf.exe
  if (-not $NoCd) { $NoCd = Pick "Choose the version 1.02 no-CD golf.exe (the remaster runs 1.02 program code), or its zip" "No-CD golf.exe or zip|*.zip;*.exe|All files|*.*" }
  if (-not (Test-Path -LiteralPath $NoCd)) { Die "Cannot find $NoCd" }
  $exe = Join-Path $tmp "golf.exe"
  if ($NoCd -match '\.zip$') {
    $zip = [System.IO.Compression.ZipFile]::OpenRead($NoCd)
    try {
      $e = $zip.Entries | Where-Object { $_.Name -ieq "golf.exe" } | Select-Object -First 1
      if (-not $e) { Die "There is no golf.exe inside $([IO.Path]::GetFileName($NoCd)). Is it the right zip?" }
      [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $exe, $true)
    } finally { $zip.Dispose() }
  } else { Copy-Item -LiteralPath $NoCd $exe }
  $sha = Sha1-Of $exe
  if ($sha -ne $NOCD_SHA1) {
    Die "That golf.exe is not the version 1.02 no-CD file this needs (its fingerprint is $sha, expected $NOCD_SHA1). The remaster runs the 1.02 program code; a 1.03 golf.exe cannot replace it. See INSTALL.txt."
  }
  Say "golf.exe checked: version 1.02 no-CD."

  # ---- the disc image
  if (-not $GameDir) {
  if (-not (Test-Path -LiteralPath $Disc)) { Die "Cannot find $Disc" }
  if ($Disc -match '\.(7z|zip|rar)$') {
    Say "Opening the archive (this can take a minute)..."
    $x = Join-Path $tmp "disc"
    New-Item -ItemType Directory -Force $x | Out-Null
    # Windows PowerShell 5.1 can turn native stderr into a terminating error
    # under Stop, before our recovery message runs. Capture it outside the pipeline.
    $recovery = "Extract the game archive with 7-Zip (free), then run Setup again and choose the .iso or .bin inside. Keep choosing your version 1.02 no-CD file separately."
    try {
      $archiveProcess = Start-Process -FilePath "$env:SystemRoot\System32\tar.exe" -ArgumentList @('-xf', "`"$Disc`"", '-C', "`"$x`"") -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $tmp 'archive.log') -RedirectStandardError (Join-Path $tmp 'archive.err')
    } catch {
      Die "Windows could not start its archive extractor. $recovery"
    }
    if ($archiveProcess.ExitCode -ne 0) { Die "Windows could not open $([IO.Path]::GetFileName($Disc)). Some Windows versions cannot read its compression format. $recovery" }
    $img = Get-ChildItem $x -Recurse -File | Where-Object { $_.Extension -match '^\.(iso|bin|img|mdf)$' } | Sort-Object Length -Descending | Select-Object -First 1
    if (-not $img) { Die "There is no disc image (.iso or .bin) inside $([IO.Path]::GetFileName($Disc))." }
    $Disc = $img.FullName
  }
  }

  # ---- our program files
  Say "Copying the game program to $Dir ..."
  & (Join-Path $here "copy_app.ps1") -App $app -Dir $Dir   # keeps the player's mods.ini (task-114i)

  # ---- unpack the player's own game
  Say $(if ($GameDir) { "Copying your checked SimGolf files..." } else { "Unpacking your SimGolf disc (about a minute)..." })
  $sg = Join-Path $Dir "sgsetup.exe"
  $log = Join-Path $tmp "sgsetup.log"
  $err = Join-Path $tmp "sgsetup.err"
  $installArgs = if ($GameDir) { @("install", "--game-dir", "`"$GameDir`"", "`"$exe`"", "`"$Dir`"") } else { @("install", "`"$Disc`"", "`"$exe`"", "`"$Dir`"") }
  $p = Start-Process -FilePath $sg -ArgumentList $installArgs -NoNewWindow -Wait -PassThru -RedirectStandardOutput $log -RedirectStandardError $err
  $msg = (Get-Content $err -ErrorAction SilentlyContinue | Where-Object { $_ -like "error:*" } | Select-Object -Last 1)
  if ($p.ExitCode -ne 0) {
    if ($msg) { Die ((Get-Content $err -Raw) + "`n`nSee INSTALL.txt, 'If something goes wrong'.") }
    Die "Installing the game data failed (code $($p.ExitCode))."
  }
  Say ((Get-Content $log | Select-Object -Last 1))
  # The program needs the no-CD golf.exe's name? No: the game code is read from image\golf.bin.
  # Keep golf_nocd.exe out of the install: it is only used to build image\golf.bin.
  Remove-Item -LiteralPath (Join-Path $Dir "sgsetup.exe"), (Join-Path $Dir "jgld-nops.json") -Force -ErrorAction SilentlyContinue

  # ---- settings
  $ini = Join-Path $Dir "golf.ini"
  if (-not (Test-Path -LiteralPath $ini)) { Copy-Item -LiteralPath (Join-Path $Dir "golf.ini.template") -Destination $ini }
  Remove-Item -LiteralPath (Join-Path $Dir "golf.ini.template") -Force -ErrorAction SilentlyContinue

  # ---- HD art pack (separate download)
  $artDir = Join-Path $Dir "art\upscaled"
  if (-not $Hd -and -not $Quiet) {
    $Hd = if (Ask "Download the HD art pack now (recommended, about 2.6 GB, needs 3 GB free)?`n`nIt makes the whole game sharp at 1080p and 4K. Without it the game looks like the 2002 original. You can install it later by running Setup again or following INSTALL.txt.`n`nIt is an unofficial fan-made upscale of the game's own art (see README).") { "yes" } else { "no" }
  }
  if ($Hd -and $Hd -ne "no") {
    $parts = Join-Path $tmp "hdparts"
    New-Item -ItemType Directory -Force $parts | Out-Null
    if ($Hd -eq "yes") {
      Say "Downloading the HD art pack list..."
      Invoke-WebRequest -UseBasicParsing "$HdUrl/parts.json" -OutFile (Join-Path $parts "parts.json")
      $src = $null
    } else {
      if (-not (Test-Path (Join-Path $Hd "parts.json"))) { Die "There is no parts.json in $Hd. Put all the pack files (parts.json and the simgolf-hd-art-part*.zip files) in that folder." }
      Copy-Item (Join-Path $Hd "parts.json") $parts
      $src = $Hd
    }
    $pj = Get-Content (Join-Path $parts "parts.json") -Raw | ConvertFrom-Json
    $need = [math]::Ceiling(($pj.total_bytes * 2) / 1GB)
    $drive = (Get-Item $Dir).PSDrive
    if ($drive.Free -lt ($pj.total_bytes * 2.1)) { Die "Not enough free space for the HD art pack: it needs about $need GB free on $($drive.Name):. Free some space and run Setup again, or choose 'no' for HD art." }
    New-Item -ItemType Directory -Force $artDir | Out-Null
    $n = 0
    foreach ($part in $pj.parts) {
      $n++
      $f = $null
      if ($src) {
        $f = Join-Path $src $part.file
        if (-not (Test-Path $f)) { Die "Missing $($part.file) in $src." }
      } else {
        $f = Join-Path $parts $part.file
        Say "Downloading HD art part $n of $($pj.parts.Count) ($([math]::Round($part.bytes / 1MB)) MB)... a failed download can be resumed by running Setup again."
        & "$env:SystemRoot\System32\curl.exe" -L --fail --retry 5 -C - -o $f "$HdUrl/$($part.file)"
        if ($LASTEXITCODE -ne 0) { Die "Downloading $($part.file) failed. Check your internet connection and run Setup again." }
      }
      Say "Checking $($part.file) ..."
      $h = (Get-FileHash -Algorithm SHA256 -LiteralPath $f).Hash.ToLower()
      if ($h -ne $part.sha256) { Die "$($part.file) is damaged (checksum mismatch). Delete it and run Setup again." }
      Say "Unpacking HD art part $n of $($pj.parts.Count) (a few minutes)..."
      & "$env:SystemRoot\System32\tar.exe" -xf $f -C $artDir
      if ($LASTEXITCODE -ne 0) { Die "Unpacking $($part.file) failed." }
      if (-not $src) { Remove-Item $f -Force }
    }
    if (-not (Test-Path (Join-Path $artDir "index.tsv"))) { Die "The HD art pack unpacked without an index.tsv; it may be the wrong pack." }
    Say "HD art installed."
  }

  # ---- shortcut and version
  $ver = Join-Path $Dir "version.txt"
  if (-not $NoShortcut) {
    $w = New-Object -ComObject WScript.Shell
    $desk = [Environment]::GetFolderPath("Desktop")
    $lnk = $w.CreateShortcut((Join-Path $desk "SimGolf Remastered.lnk"))
    $lnk.TargetPath = Join-Path $Dir "Sim Golf Remastered.exe"
    $lnk.WorkingDirectory = $Dir
    $lnk.Description = "SimGolf Remastered"
    $lnk.IconLocation = (Join-Path $Dir "Sim Golf Remastered.exe") + ",0"
    $lnk.Save()
    Say "Desktop shortcut made."
  }
  Say ""
  Say "Done. Start the game with the 'SimGolf Remastered' shortcut, or run $Dir\Sim Golf Remastered.exe"
  if (-not $Quiet) {
    if (Ask "SimGolf Remastered is installed.`n`nStart it now?") { Start-Process (Join-Path $Dir "Sim Golf Remastered.exe") -WorkingDirectory $Dir }
  }
} finally {
  Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
