# Windows PowerShell 5.1+, local only. Run with the game closed.
[CmdletBinding()]
param(
    [string]$InstallDir = $PSScriptRoot,
    [string]$LogPath,
    [string[]]$SavePath = @(),
    [switch]$IncludeSaveCompanions,
    [string]$OutputDirectory = [Environment]::GetFolderPath('Desktop'),
    [switch]$PreviewOnly
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($IncludeSaveCompanions -and -not $SavePath.Count) { throw '-IncludeSaveCompanions requires at least one explicit -SavePath.' }
function Resolve-FileSystemPath([string]$Path) {
    $provider = $null
    $drive = $null
    $resolved = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path, [ref]$provider, [ref]$drive)
    if ($provider.Name -ne 'FileSystem') { throw 'Use a filesystem path.' }
    return $resolved
}
function Resolve-NativePath([string]$Path, [string]$Base) {
    # Windows rooted paths (\logs) still depend on the host's current drive.
    # Drive-relative paths (C:logs) also depend on a per-drive directory. Only
    # the base drive is known; never guess another drive's hidden directory.
    if ($Path -match '^([A-Za-z]):(?![\\/])') {
        $drive = $Matches[1] + ':'
        if (-not $Base.StartsWith($drive + '\', [StringComparison]::OrdinalIgnoreCase) -and
            -not $Base.StartsWith($drive + '/', [StringComparison]::OrdinalIgnoreCase)) {
            throw "Cannot resolve drive-relative path '$Path' against '$Base'. Use a full path such as C:\folder\file."
        }
        $Path = [IO.Path]::Combine($Base, $Path.Substring(2))
    } elseif ($Path -match '^[\\/](?![\\/])') {
        $Path = [IO.Path]::Combine([IO.Path]::GetPathRoot($Base), $Path.TrimStart('\', '/'))
    } elseif (-not [IO.Path]::IsPathRooted($Path)) {
        $Path = [IO.Path]::Combine($Base, $Path)
    }
    # Header paths may refer to a disconnected drive. Normalize lexically;
    # do not require a matching PowerShell drive or inspect the directory.
    return [IO.Path]::GetFullPath($Path)
}
$installEntry = Get-Item -LiteralPath (Resolve-FileSystemPath $InstallDir)
if (-not $installEntry.PSIsContainer) { throw 'InstallDir must be a folder.' }
$install = $installEntry.FullName
# The native host sets its current directory to the executable folder before --log.
if (-not $LogPath) { $LogPath = 'golf.log' }
$LogPath = Resolve-NativePath $LogPath $install
$OutputDirectory = Resolve-FileSystemPath $OutputDirectory
$items = New-Object 'System.Collections.Generic.List[object]'
$roots = New-Object 'System.Collections.Generic.List[object]'
function Add-Root([string]$Path, [string]$Label) {
    # Never redact a relative word or an empty pattern.
    if ($Path -and [IO.Path]::IsPathRooted($Path)) {
        $trimmed = $Path.TrimEnd('\', '/')
        if ($trimmed) { $roots.Add(@{ Path = $trimmed; Label = $Label }) }
    }
}
Add-Root $install '<INSTALL>'
Add-Root ([IO.Path]::GetDirectoryName($LogPath)) '<LOGS>'
Add-Root $env:USERPROFILE '<USER>'
$iniGame = $null
# Only inspect the game path for redaction; never bundle configuration files.
$ini = Join-Path $install 'golf.ini'
if (Test-Path -LiteralPath $ini -PathType Leaf) {
    foreach ($line in [IO.File]::ReadAllLines($ini)) {
        if ($line -match '^\s*game\s*=\s*(.+?)\s*$') {
            $game = $Matches[1].Trim('"')
            $iniGame = Resolve-NativePath $game $install
            Add-Root $iniGame '<GAME>'
        }
    }
}
# Match the host's environment/config/default precedence. --game is learned
# from log headers below. A compiled fallback cannot be inferred here.
$effectiveGame = $iniGame
if ($env:SIMGOLF_GAME) { $effectiveGame = Resolve-NativePath $env:SIMGOLF_GAME $install }
if (-not $effectiveGame -and (Test-Path -LiteralPath (Join-Path $install 'game') -PathType Container)) {
    $effectiveGame = Join-Path $install 'game'
}
Add-Root $effectiveGame '<GAME>'
function Add-SaveEnvironmentRoot([string]$GameRoot) {
    if ($env:SIMGOLF_USER_DIR) {
        if ($GameRoot) { Add-Root (Resolve-NativePath $env:SIMGOLF_USER_DIR $GameRoot) '<SAVES>' }
        elseif ($env:SIMGOLF_USER_DIR -match '^(?:[A-Za-z]:[\\/]|[\\/]{2})') {
            Add-Root (Resolve-NativePath $env:SIMGOLF_USER_DIR $install) '<SAVES>'
        }
    }
}
# The host resolves SIMGOLF_USER_DIR after changing into the game folder.
Add-SaveEnvironmentRoot $effectiveGame
function Add-File([string]$Path, [string]$Name, [bool]$Binary) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $entry = Get-Item -LiteralPath $Path
    # Do not follow links/junctions in any source path.
    $part = $entry
    while ($part) {
        if ($part.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Linked input refused: $Path" }
        if ($part -is [IO.FileInfo]) { $part = $part.Directory }
        else { $part = $part.Parent }
    }
    if ($entry.Length -gt $(if ($Binary) { 64MB } else { 8MB })) {
        Write-Warning "Skipped oversized file: $Path"; return
    }
    $items.Add(@{ Source = $entry.FullName; Name = $Name; Bytes = $entry.Length; Binary = $Binary })
}
Add-File $LogPath 'golf.log' $false
1..5 | ForEach-Object { Add-File "$LogPath.$_" "golf.log.$_" $false }
Add-File "$LogPath.crash.txt" 'golf.log.crash.txt' $false
Add-File (Join-Path $install 'version.txt') 'version.txt' $false
$i = 0
foreach ($save in $SavePath) {
    $resolved = Get-Item -LiteralPath (Resolve-FileSystemPath $save)
    if ($resolved.PSIsContainer -or $resolved.Extension -ine '.sve') { throw 'Select individual .sve files, not folders.' }
    Add-Root $resolved.DirectoryName '<SAVES>'
    ++$i
    # Do not expose player/course names from filenames.
    $saveName = "save-{0:D2}.sve" -f $i
    $before = $items.Count
    Add-File $resolved.FullName $saveName $true
    if ($IncludeSaveCompanions -and $items.Count -gt $before) {
        # Exact game-owned suffixes only. Preserve bytes (including CRC-bound
        # content) and keep the normalized save basename for reproduction.
        foreach ($suffix in @('.camera-tiles', '.course-lights', '.path-styles', '.living-club.json')) {
            Add-File ($resolved.FullName + $suffix) ($saveName + $suffix) $true
        }
    }
}
function Find-TextRoots([string]$Text) {
    foreach ($match in [regex]::Matches($Text, '(?m)^host: (game folder|saves and snapshots in) ([^\r\n]+)')) {
        Add-Root $match.Groups[2].Value $(if ($match.Groups[1].Value -eq 'game folder') { '<GAME>' } else { '<SAVES>' })
        if ($match.Groups[1].Value -eq 'game folder') { Add-SaveEnvironmentRoot $match.Groups[2].Value }
    }
}
function Redact([string]$Text) {
    foreach ($root in ($roots | Sort-Object { $_.Path.Length } -Descending)) {
        foreach ($variant in @($root.Path, $root.Path.Replace('\', '/'), $root.Path.Replace('/', '\'))) {
            $Text = [regex]::Replace($Text, [regex]::Escape($variant), $root.Label, [Text.RegularExpressions.RegexOptions]::IgnoreCase)
        }
    }
    # Omit raw stack words in shared reports; the original remains untouched.
    return [regex]::Replace($Text, '(?m)^stack [^\r\n]*(\r?\n|$)', '')
}
Write-Host 'Local diagnostic ZIP preview (close the game before continuing):'
foreach ($item in $items) { Write-Host ("  {0} ({1} bytes) <- {2}" -f $item.Name, $item.Bytes, $item.Source) }
Write-Host 'Also includes manifest.json: collection time, OS version, process architecture, file sizes, and hashes of installed game executables.'
Write-Host 'No upload is performed. Config files, screenshots, and memory dumps are not selected. Stack lines are removed from exported text.'
Write-Host 'Known install/user/game/save paths are redacted. Logs may still contain private text; inspect the ZIP before sharing.'
if ($SavePath.Count) { Write-Warning 'Selected saves and any opted-in companions are copied verbatim and may contain player/course names and other private game data. They cannot be redacted.' }
if ($PreviewOnly) { return }
if ((Read-Host 'Type EXPORT to create this ZIP, or anything else to cancel') -cne 'EXPORT') { Write-Host 'Cancelled; no ZIP created.'; return }
if (-not (Test-Path -LiteralPath $OutputDirectory -PathType Container)) { throw 'OutputDirectory must already exist.' }
$token = [Guid]::NewGuid().ToString('N')
$stage = Join-Path ([IO.Path]::GetTempPath()) "simgolf-diagnostics-$token"
$zip = Join-Path $OutputDirectory ("SimGolf-diagnostics-{0}-{1}.zip" -f [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'), $token.Substring(0,8))
try {
    # Discover roots across all selected, bounded text inputs before exporting
    # any of them. Cache the same content that will be redacted, once only.
    $textContents = @{}
    foreach ($item in $items) {
        if (-not $item.Binary) {
            $textContents[$item.Name] = [IO.File]::ReadAllText($item.Source)
            Find-TextRoots $textContents[$item.Name]
        }
    }
    New-Item -ItemType Directory -Path $stage | Out-Null
    foreach ($item in $items) {
        $target = Join-Path $stage $item.Name
        if ($item.Binary) { [IO.File]::Copy($item.Source, $target, $false) }
        else { [IO.File]::WriteAllText($target, (Redact $textContents[$item.Name]), (New-Object Text.UTF8Encoding($false))) }
    }
    $hashes = @()
    foreach ($name in @('golf.exe', 'Sim Golf Remastered.exe', 'SDL2.dll')) {
        $path = Join-Path $install $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $hashes += @{ name = $name; sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
        }
    }
    $manifest = [ordered]@{
        schema_version = 1
        collected_utc = [DateTime]::UtcNow.ToString('o')
        os_version = [Environment]::OSVersion.Version.ToString()
        collector_process_bits = [IntPtr]::Size * 8
        saves_included = @($items | Where-Object { $_.Binary -and $_.Name.EndsWith('.sve') }).Count
        save_companions_included = @($items | Where-Object { $_.Binary -and -not $_.Name.EndsWith('.sve') }).Count
        raw_stack_included = $false
        files = @($items | ForEach-Object { @{ name = $_.Name; source_bytes = $_.Bytes; binary = $_.Binary } })
        binary_sha256 = $hashes
    }
    [IO.File]::WriteAllText((Join-Path $stage 'manifest.json'), ($manifest | ConvertTo-Json -Depth 5))
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -ErrorAction Stop
    Write-Host "Created $zip. Open and inspect it before choosing whether to share it."
} catch {
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    throw
} finally {
    if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
}

# Keep browser interaction outside export cleanup/error handling: failure to
# launch a browser must never remove a successfully created diagnostic ZIP.
$issuesUrl = 'https://github.com/finalpantasy/simgolf-mod/issues'
Write-Host "Report a problem at $issuesUrl"
Write-Host '1. Open and inspect the ZIP first. GitHub issues and attachments are public; remove private information before sharing.'
Write-Host '2. Open the issues page and sign in to GitHub (or create an account).'
Write-Host '3. Search existing issues for the same problem. Add your details to a matching issue, or choose New issue.'
Write-Host '4. Give it a short title. Describe steps to reproduce, expected and actual results, and your game version from version.txt.'
Write-Host '5. Drag the reviewed ZIP into the issue description (or comment). Wait until the attachment finishes uploading and its link appears.'
Write-Host '6. Review the text and attachment, then submit the issue (or comment). Nothing is submitted by this script.'
if ((Read-Host 'Type OPEN to open the GitHub issues page, or press Enter to finish without opening a browser') -ceq 'OPEN') {
    try {
        Start-Process -FilePath $issuesUrl -ErrorAction Stop
    } catch {
        Write-Warning "Could not open the browser. Your ZIP is still at $zip. Open $issuesUrl manually."
    }
}
