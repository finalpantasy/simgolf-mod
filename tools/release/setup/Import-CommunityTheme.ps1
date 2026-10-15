# Import a compatible community theme into the game's data folder.
# The target is <GameDir>\Themes\<ThemeName>. It never changes settings or saves.
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$GameDir,
  [Parameter(Mandatory = $true)][string]$StoriesZip,
  [Parameter(Mandatory = $true)][string]$ProsAndCelebsZip,
  [string]$ThemeName = "Community"
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression

$script:TextEncoding = [System.Text.Encoding]::GetEncoding(1252, [System.Text.EncoderFallback]::ExceptionFallback,
  [System.Text.DecoderFallback]::ExceptionFallback)
$script:MaxArchiveBytes = 32MB
$script:MaxEntryBytes = 64KB
$script:MaxTotalBytes = 2MB
$script:ManifestName = ".simgolf-community-theme.json"

function Get-Sha256Bytes([byte[]]$Bytes) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace("-", "").ToLowerInvariant() }
  finally { $sha.Dispose() }
}

function Get-Sha256File([string]$Path) {
  $sha = [System.Security.Cryptography.SHA256]::Create()
  $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
  try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace("-", "").ToLowerInvariant() }
  finally { $stream.Dispose(); $sha.Dispose() }
}

function Read-ZipEntry([System.IO.Compression.ZipArchiveEntry]$Entry, [int]$Limit, [string]$Label) {
  if ($Entry.Length -lt 0 -or $Entry.Length -gt $Limit) { throw "$Label contains a file that is too large." }
  $input = $Entry.Open()
  $output = New-Object System.IO.MemoryStream
  try {
    $buffer = New-Object byte[] 8192
    $total = 0
    while (($read = $input.Read($buffer, 0, $buffer.Length)) -gt 0) {
      $total += $read
      if ($total -gt $Limit) { throw "$Label contains a file that expands beyond the allowed size." }
      $output.Write($buffer, 0, $read)
    }
    if ($total -ne $Entry.Length) { throw "$Label contains a damaged or incomplete file." }
    return ,$output.ToArray()
  } finally { $input.Dispose(); $output.Dispose() }
}

function Read-CommunityArchive([string]$Path, [string]$Label) {
  if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Cannot find the $Label ZIP." }
  $item = Get-Item -LiteralPath $Path -Force
  if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "The $Label ZIP must be a regular file, not a link." }
  if ($item.Length -lt 22 -or $item.Length -gt $script:MaxArchiveBytes) { throw "The $Label ZIP is empty or too large." }
  $bytes = [IO.File]::ReadAllBytes($item.FullName)
  $hash = Get-Sha256Bytes $bytes
  $memory = New-Object System.IO.MemoryStream
  $zip = $null
  try {
    $memory.Write($bytes, 0, $bytes.Length)
    $memory.Position = 0
    try { $zip = [System.IO.Compression.ZipArchive]::new($memory, [System.IO.Compression.ZipArchiveMode]::Read, $false) }
    catch { throw "Cannot read the $Label file as a ZIP archive." }

    $entries = @($zip.Entries)
    if ($entries.Count -eq 0) { throw "$Label ZIP contains no files." }
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $files = @{}
    $total = 0
    foreach ($entry in $entries) {
      $name = $entry.FullName
      if ([string]::IsNullOrWhiteSpace($name) -or $name.Contains("/") -or $name.Contains("\") -or
          $name.Contains(":") -or $name -match '[\x00-\x1f]' -or $name.StartsWith("/") -or
          $name -match '^[A-Za-z]:') {
        throw "$Label ZIP contains a non-flat or unsafe path."
      }
      if ($name -eq "." -or $name -eq ".." -or $name.EndsWith(".")) { throw "$Label ZIP contains an unsafe file name." }
      $unixMode = ($entry.ExternalAttributes -shr 16) -band 0xF000
      if ($unixMode -eq 0xA000) { throw "$Label ZIP contains a symbolic link." }
      if (-not $seen.Add($name)) { throw "$Label ZIP contains duplicate file names." }
      if ($entry.Length -lt 0 -or $entry.Length -gt $script:MaxEntryBytes) { throw "$Label ZIP contains a file that is too large." }
      $total += $entry.Length
      if ($total -gt $script:MaxTotalBytes) { throw "$Label ZIP expands beyond the allowed total size." }
      $files.Add($name, (Read-ZipEntry $entry $script:MaxEntryBytes $Label))
    }
    return [pscustomobject]@{ Hash = $hash; Files = $files }
  } finally {
    if ($zip) { $zip.Dispose() }
    $memory.Dispose()
  }
}

function Get-TextLines([byte[]]$Bytes, [string]$Label) {
  if ($Bytes -contains 0) { throw "$Label contains a NUL byte." }
  try { $text = $script:TextEncoding.GetString($Bytes) }
  catch { throw "$Label is not valid Windows-1252 text." }
  $hasBom = ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFF -and $Bytes[1] -eq 0xFE) -or
            ($Bytes.Length -ge 2 -and $Bytes[0] -eq 0xFE -and $Bytes[1] -eq 0xFF) -or
            ($Bytes.Length -ge 3 -and $Bytes[0] -eq 0xEF -and $Bytes[1] -eq 0xBB -and $Bytes[2] -eq 0xBF)
  if ($hasBom) { throw "$Label must not contain a text byte-order mark." }
  $lines = [regex]::Split($text, "\r\n|\n|\r")
  if ($lines.Count -gt 1 -and $lines[$lines.Count - 1] -eq "") { $lines = $lines[0..($lines.Count - 2)] }
  if ($lines.Count -gt 200) { throw "$Label contains too many lines for the game reader." }
  foreach ($line in $lines) {
    try { $lineBytes = $script:TextEncoding.GetByteCount($line) }
    catch { throw "$Label contains text outside Windows-1252." }
    if ($lineBytes -gt 247) { throw "$Label contains a line too long for the game reader." }
  }
  return ,$lines
}

function Test-Story([string]$Name, [byte[]]$Bytes) {
  if ($Name -notmatch '^[A-Za-z]{8}.+\.txt$' -or $Name -match '[<>:"/\\|?*\x00-\x1f]' -or
      $Name.EndsWith(" ") -or $Name.EndsWith(".")) {
    throw "Stories ZIP contains a file name that is not safe for the game's story list."
  }
  try { $nameBytes = $script:TextEncoding.GetByteCount($Name) }
  catch { throw "Stories ZIP contains a story file name outside Windows-1252." }
  if ($nameBytes -gt 49) { throw "Stories ZIP contains a story file name that is too long for the game." }
  $leaf = [IO.Path]::GetFileNameWithoutExtension($Name).Split('.')[0]
  if ($leaf -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') { throw "Stories ZIP contains a reserved Windows file name." }

  $lines = Get-TextLines $Bytes "Story file"
  if ($lines.Count -lt 2 -or [string]::IsNullOrWhiteSpace($lines[0])) { throw "Stories ZIP contains a story with no title or stages." }
  $replies = New-Object 'System.Collections.Generic.List[int]'
  $stage = -1
  for ($i = 1; $i -lt $lines.Count; $i++) {
    $line = $lines[$i]
    if ($line.Length -le 1) { continue }
    if ($line[0] -eq ' ') {
      if ($stage -ge 0) { $replies[$stage] = $replies[$stage] + 1 }
    } else {
      $stage++
      if ($stage -ge 4) { throw "Stories ZIP contains a story with more than four stages." }
      $replies.Add(0)
    }
  }
  if ($stage -ne 3) { throw "Stories ZIP contains a story without four stages." }
  foreach ($count in $replies) {
    if ($count -lt 2 -or $count -gt 6) { throw "Stories ZIP contains a stage with an invalid reply count." }
  }
}

function Test-Dta([string]$Name, [byte[]]$Bytes) {
  $lines = Get-TextLines $Bytes $Name
  $rows = 0
  foreach ($line in $lines) {
    if ($line.Length -le 9 -or $line[0] -eq '*') { continue }
    $fields = $line.Split([char[]]@(','), [StringSplitOptions]::None)
    if ([string]::IsNullOrWhiteSpace($fields[0])) { throw "$Name contains a row without a name." }
    try { $nameBytes = $script:TextEncoding.GetByteCount($fields[0]) }
    catch { throw "$Name contains a name outside Windows-1252." }
    if ($nameBytes -gt 31) { throw "$Name contains a name too long for the game." }
    if ($Name -ieq "celebrities.dta") {
      if (($fields.Count -ne 6 -and -not ($fields.Count -eq 7 -and $fields[6] -eq "")) -or
          $fields[1] -notmatch '^[A-K]$' -or $fields[2] -notmatch '^[0-3]$' -or
          $fields[3] -notmatch '^[0-9]$' -or $fields[4] -notmatch '^[0-9]$' -or $fields[5] -notmatch '^[0-9]$') {
        throw "celebrities.dta contains a row outside the documented field ranges."
      }
    } else {
      if ($fields.Count -ne 7 -or $fields[1] -notmatch '^[0-7]$' -or $fields[2] -notmatch '^[0-3]$' -or
          $fields[3] -notmatch '^[0-9]$' -or $fields[4] -notmatch '^[0-9]$' -or $fields[5] -notmatch '^[0-9]$' -or
          $fields[6].Length -lt 10 -or $fields[6].Substring(0, 10) -notmatch '^[0-9A-Ja-j]{10}$' -or
          $fields[6].Substring(10) -notmatch '^[ 0-9]*$') {
        throw "progolfers.dta contains a row outside the documented field ranges."
      }
    }
    $rows++
    if ($rows -gt 100) { throw "$Name contains more than 100 rows, the game limit." }
  }
  if ($rows -eq 0) { throw "$Name contains no usable rows." }
  return $rows
}

function Get-ExistingTheme([string]$Path) {
  $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
  if (-not $item) { return $null }
  if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "The target theme is a link; no files were changed." }
  if (-not $item.PSIsContainer) { throw "A file already uses the target theme name; no files were changed." }
  return $item
}

function Test-ImportedTheme([string]$Path, $Manifest, [hashtable]$Files, [string]$StoriesHash, [string]$PeopleHash) {
  if (-not (Test-Path -LiteralPath $Manifest -PathType Leaf)) { throw "The theme already exists without this importer's manifest; no files were changed." }
  try { $record = Get-Content -LiteralPath $Manifest -Raw | ConvertFrom-Json }
  catch { throw "The existing theme manifest is unreadable; no files were changed." }
  if ($record.schema -ne 1 -or $record.storiesArchiveSha256 -ne $StoriesHash -or
      $record.prosAndCelebsArchiveSha256 -ne $PeopleHash) {
    throw "The existing theme came from different archives; no files were changed."
  }
  $properties = @($record.files.PSObject.Properties)
  if ($properties.Count -ne $Files.Count) { throw "The existing theme manifest does not match its archives; no files were changed." }
  foreach ($name in $Files.Keys) {
    $property = $record.files.PSObject.Properties | Where-Object { [string]::Equals($_.Name, $name, [StringComparison]::OrdinalIgnoreCase) } | Select-Object -First 1
    $expected = Get-Sha256Bytes $Files[$name]
    if (-not $property -or $property.Value -ne $expected) { throw "The existing theme manifest does not match its archives; no files were changed." }
    $targetFile = Join-Path $Path $name
    if (-not (Test-Path -LiteralPath $targetFile -PathType Leaf) -or (Get-Sha256File $targetFile) -ne $expected) {
      throw "An imported theme file changed after installation; no files were changed."
    }
  }
}

if ($ThemeName.Length -lt 1 -or $ThemeName.Length -gt 39 -or $ThemeName -notmatch '^[A-Za-z0-9][A-Za-z0-9 _-]*$' -or
    $ThemeName.EndsWith(" ") -or $ThemeName.EndsWith(".")) {
  throw "ThemeName must be 1 to 39 letters, numbers, spaces, underscores or hyphens, starting with a letter or number."
}
if ($ThemeName -match '^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])$') { throw "ThemeName is a reserved Windows name." }

if (-not (Test-Path -LiteralPath $GameDir -PathType Container)) { throw "Cannot find the game data folder." }
$gamePath = [IO.Path]::GetFullPath((Resolve-Path -LiteralPath $GameDir).Path)
if (-not (Test-Path -LiteralPath (Join-Path $gamePath "Data") -PathType Container) -or
    -not (Test-Path -LiteralPath (Join-Path $gamePath "Themes") -PathType Container)) {
  throw "GameDir must be the game's data folder containing both Data and Themes."
}
$gameItem = Get-Item -LiteralPath $gamePath -Force
$themesPath = Join-Path $gamePath "Themes"
$themesItem = Get-Item -LiteralPath $themesPath -Force
if (($gameItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -or ($themesItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
  throw "GameDir and its Themes folder must not be links."
}

$stories = Read-CommunityArchive $StoriesZip "Stories"
$people = Read-CommunityArchive $ProsAndCelebsZip "Pros-and-Celebs"
if ($stories.Files.Count -ne 24) { throw "Stories ZIP must contain exactly 24 flat story files." }
if ($people.Files.Count -ne 2) { throw "Pros-and-Celebs ZIP must contain exactly two flat data files." }

$storyFiles = @{}
foreach ($name in $stories.Files.Keys) {
  Test-Story $name $stories.Files[$name]
  $storyFiles.Add($name, $stories.Files[$name])
}
$dtaNames = @($people.Files.Keys | ForEach-Object { $_.ToLowerInvariant() } | Sort-Object)
if (($dtaNames -join ",") -ne "celebrities.dta,progolfers.dta") {
  throw "Pros-and-Celebs ZIP must contain celebrities.dta and progolfers.dta at its root."
}
$celebrityName = @($people.Files.Keys | Where-Object { $_ -ieq "celebrities.dta" })[0]
$proName = @($people.Files.Keys | Where-Object { $_ -ieq "progolfers.dta" })[0]
$celebrities = $people.Files[$celebrityName]
$pros = $people.Files[$proName]
$celebrityCount = Test-Dta "celebrities.dta" $celebrities
$proCount = Test-Dta "progolfers.dta" $pros

$files = @{}
foreach ($name in $storyFiles.Keys) { $files.Add($name, $storyFiles[$name]) }
$files.Add("celebrities.dta", $celebrities)
$files.Add("progolfers.dta", $pros)
$themePath = Join-Path $themesPath $ThemeName
$existing = Get-ExistingTheme $themePath
if ($existing) {
  Test-ImportedTheme $themePath (Join-Path $themePath $script:ManifestName) $files $stories.Hash $people.Hash
  [pscustomobject]@{ Status = "AlreadyImported"; Theme = $ThemeName; Stories = $storyFiles.Count; Celebrities = $celebrityCount; ProGolfers = $proCount }
  return
}

$fileHashes = [ordered]@{}
foreach ($name in $files.Keys) { $fileHashes[$name] = Get-Sha256Bytes $files[$name] }
$manifest = [ordered]@{
  schema = 1
  theme = $ThemeName
  storiesArchiveSha256 = $stories.Hash
  prosAndCelebsArchiveSha256 = $people.Hash
  files = $fileHashes
}
$manifestText = ConvertTo-Json -InputObject $manifest -Depth 5 -Compress
$stagePath = Join-Path $gamePath (".simgolf-theme-import-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $stagePath | Out-Null
try {
  foreach ($name in $files.Keys) {
    [IO.File]::WriteAllBytes((Join-Path $stagePath $name), $files[$name])
  }
  $utf8 = New-Object System.Text.UTF8Encoding($false)
  [IO.File]::WriteAllText((Join-Path $stagePath $script:ManifestName), $manifestText, $utf8)
  [IO.Directory]::Move($stagePath, $themePath)
} catch {
  if ([IO.Directory]::Exists($stagePath)) { [IO.Directory]::Delete($stagePath, $true) }
  throw
}

[pscustomobject]@{ Status = "Imported"; Theme = $ThemeName; Stories = $storyFiles.Count; Celebrities = $celebrityCount; ProGolfers = $proCount }
