# Windows PowerShell integration test. No game files or network needed.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$script = Join-Path $repo 'tools\diagnostics\Collect-Diagnostics.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) ('simgolf-test-' + [Guid]::NewGuid().ToString('N'))
function Assert([bool]$Value, [string]$Message) { if (-not $Value) { throw $Message } }
$global:DiagnosticBrowserCalls = New-Object 'System.Collections.Generic.List[string]'
$originalGameEnv = $env:SIMGOLF_GAME
$originalSaveEnv = $env:SIMGOLF_USER_DIR
$env:SIMGOLF_GAME = $null
$env:SIMGOLF_USER_DIR = $null
$global:DiagnosticBrowserFails = $false
function global:Start-Process {
    param([string]$FilePath, [string]$ErrorAction)
    $global:DiagnosticBrowserCalls.Add($FilePath)
    if ($global:DiagnosticBrowserFails) { throw 'Mock browser launch failure' }
}
try {
    New-Item -ItemType Directory -Path $root | Out-Null
    $install = Join-Path $root 'install'
    $out = Join-Path $root 'out'
    New-Item -ItemType Directory -Path $install, $out | Out-Null
    [IO.File]::WriteAllText((Join-Path $install 'golf.log'), "host: game folder $install/game`nuser $env:USERPROFILE`n")
    [IO.File]::WriteAllText((Join-Path $install 'golf.log.crash.txt'), "exception c0000005`nstack 12345678: deadbeef`nmodule golf.exe base 60000000 offset 1234`n")
    [IO.File]::WriteAllText((Join-Path $install 'version.txt'), 'test-build')
    [IO.File]::WriteAllText((Join-Path $install 'golf.ini'), 'secret-not-to-bundle')
    $save = Join-Path $install 'private-player.sve'
    [IO.File]::WriteAllBytes($save, [byte[]](1, 2, 3, 4))
    $suffixes = @('.camera-tiles', '.course-lights', '.path-styles', '.living-club.json')
    foreach ($suffix in $suffixes) { [IO.File]::WriteAllBytes(($save + $suffix), [byte[]](5, 6, 7, 8)) }
    [IO.File]::WriteAllText(($save + '.path-styles.tmp'), 'must-not-include')
    [IO.File]::WriteAllText(($save + '.bak'), 'must-not-include')
    $rejected = $false
    try { & $script -InstallDir $install -OutputDirectory $out -IncludeSaveCompanions -PreviewOnly } catch { $rejected = $true }
    Assert $rejected 'Companion option accepted without explicit save'
    & $script -InstallDir $install -OutputDirectory $out -PreviewOnly
    Assert (@(Get-ChildItem -LiteralPath $out).Count -eq 0) 'Preview created output'
    Assert ($global:DiagnosticBrowserCalls.Count -eq 0) 'Preview opened browser'
    function global:Read-Host { return 'cancel' }
    & $script -InstallDir $install -OutputDirectory $out
    Assert (@(Get-ChildItem -LiteralPath $out).Count -eq 0) 'Cancel created output'
    Assert ($global:DiagnosticBrowserCalls.Count -eq 0) 'Cancel opened browser'
    function global:Read-Host {
        param([string]$Prompt)
        if ($Prompt.StartsWith('Type EXPORT')) { return 'EXPORT' }
        return ''
    }
    & $script -InstallDir $install -OutputDirectory $out
    $zips = @(Get-ChildItem -LiteralPath $out -Filter '*.zip')
    Assert ($zips.Count -eq 1) 'Expected one ZIP'
    Assert ($global:DiagnosticBrowserCalls.Count -eq 0) 'Default export opened browser'
    $extract = Join-Path $root 'extract-default'
    Expand-Archive -LiteralPath $zips[0].FullName -DestinationPath $extract
    Assert (-not (Test-Path -LiteralPath (Join-Path $extract 'save-01.sve'))) 'Save included without opt-in'
    Assert (-not (Test-Path -LiteralPath (Join-Path $extract 'golf.ini'))) 'Configuration leaked'
    $log = [IO.File]::ReadAllText((Join-Path $extract 'golf.log'))
    Assert (-not $log.Contains($install)) 'Install path leaked'
    if ($env:USERPROFILE) { Assert (-not $log.Contains($env:USERPROFILE)) 'User path leaked' }
    $crash = [IO.File]::ReadAllText((Join-Path $extract 'golf.log.crash.txt'))
    Assert (-not $crash.Contains('deadbeef')) 'Stack words leaked'
    Assert ($crash.Contains('module golf.exe')) 'Module evidence lost'
    & $script -InstallDir $install -OutputDirectory $out -SavePath $save
    $second = @(Get-ChildItem -LiteralPath $out -Filter '*.zip' | Where-Object { $_.Name -ne $zips[0].Name })
    Assert ($second.Count -eq 1) 'Expected second ZIP'
    $extractSave = Join-Path $root 'extract-save'
    Expand-Archive -LiteralPath $second[0].FullName -DestinationPath $extractSave
    Assert (-not (Test-Path -LiteralPath (Join-Path $extractSave 'save-01.sve.path-styles'))) 'Companion included without opt-in'
    Assert ((Get-FileHash -LiteralPath $save).Hash -eq (Get-FileHash -LiteralPath (Join-Path $extractSave 'save-01.sve')).Hash) 'Save bytes changed'
    Assert ([IO.File]::ReadAllText((Join-Path $install 'golf.log.crash.txt')).Contains('deadbeef')) 'Original crash record changed'
    $beforeZips = @(Get-ChildItem -LiteralPath $out -Filter '*.zip' | ForEach-Object { $_.Name })
    & $script -InstallDir $install -OutputDirectory $out -SavePath $save -IncludeSaveCompanions
    $third = @(Get-ChildItem -LiteralPath $out -Filter '*.zip' | Where-Object { $_.Name -notin $beforeZips })
    Assert ($third.Count -eq 1) 'Expected companion ZIP'
    $extractCompanions = Join-Path $root 'extract-companions'
    Expand-Archive -LiteralPath $third[0].FullName -DestinationPath $extractCompanions
    foreach ($suffix in $suffixes) {
        Assert ((Get-FileHash -LiteralPath ($save + $suffix)).Hash -eq (Get-FileHash -LiteralPath (Join-Path $extractCompanions ('save-01.sve' + $suffix))).Hash) 'Companion bytes changed'
    }
    Assert (-not (Test-Path -LiteralPath (Join-Path $extractCompanions 'save-01.sve.path-styles.tmp'))) 'Temporary file included'
    Assert (-not (Test-Path -LiteralPath (Join-Path $extractCompanions 'save-01.sve.bak'))) 'Backup included'
    $manifest = Get-Content -LiteralPath (Join-Path $extractCompanions 'manifest.json') -Raw | ConvertFrom-Json
    Assert ($manifest.saves_included -eq 1) 'Companions counted as saves'
    Assert ($manifest.save_companions_included -eq 4) 'Wrong companion count'
    function global:Read-Host {
        param([string]$Prompt)
        if ($Prompt.StartsWith('Type EXPORT')) { return 'EXPORT' }
        if ($Prompt.StartsWith('Type OPEN')) { return 'OPEN' }
        throw 'Unexpected prompt'
    }
    & $script -InstallDir $install -OutputDirectory $out
    Assert ($global:DiagnosticBrowserCalls.Count -eq 1) 'OPEN did not launch exactly once'
    Assert ($global:DiagnosticBrowserCalls[0] -ceq 'https://github.com/finalpantasy/simgolf-mod/issues') 'Unexpected browser URL'
    $beforeFailureZips = @(Get-ChildItem -LiteralPath $out -Filter '*.zip' | ForEach-Object { $_.Name })
    $global:DiagnosticBrowserFails = $true
    & $script -InstallDir $install -OutputDirectory $out
    $retained = @(Get-ChildItem -LiteralPath $out -Filter '*.zip' | Where-Object { $_.Name -notin $beforeFailureZips })
    Assert ($retained.Count -eq 1) 'Browser launch failure lost successful ZIP'
    Assert ($global:DiagnosticBrowserCalls.Count -eq 2) 'Failed browser call retried or skipped'
    Assert ($global:DiagnosticBrowserCalls[1] -ceq 'https://github.com/finalpantasy/simgolf-mod/issues') 'Failure used unexpected URL'
    $extractRetained = Join-Path $root 'extract-retained'
    Expand-Archive -LiteralPath $retained[0].FullName -DestinationPath $extractRetained
    Assert (Test-Path -LiteralPath (Join-Path $extractRetained 'manifest.json')) 'Retained ZIP invalid'
    # Relative filesystem arguments use PowerShell's location, while --log
    # follows the native host and is relative to the selected install folder.
    $global:DiagnosticBrowserFails = $false
    function global:Read-Host {
        param([string]$Prompt)
        if ($Prompt.StartsWith('Type EXPORT')) { return 'EXPORT' }
        return ''
    }
    $env:SIMGOLF_GAME = '..\external-game'
    $env:SIMGOLF_USER_DIR = 'session-saves'
    $externalGame = Join-Path $root 'external-game'
    $externalSaves = Join-Path $externalGame 'session-saves'
    $oldRoot = 'Z:\private-old-session\game'
    [IO.File]::WriteAllText((Join-Path $install 'relative.log'), "ordinary game text and session-saves text`nload $oldRoot/course.sve`nload $externalGame/course.sve`nload $externalSaves/course.sve`n")
    [IO.File]::WriteAllText((Join-Path $install 'relative.log.1'), "host: game folder $oldRoot`n")
    $beforeRelativeZips = @(Get-ChildItem -LiteralPath $out -Filter '*.zip' | ForEach-Object { $_.Name })
    $processDirectory = [Environment]::CurrentDirectory
    Push-Location -LiteralPath $root
    try {
        [Environment]::CurrentDirectory = [IO.Path]::GetTempPath()
        & $script -InstallDir 'install' -LogPath 'relative.log' -OutputDirectory 'out' -SavePath 'install\private-player.sve'
    } finally {
        [Environment]::CurrentDirectory = $processDirectory
        Pop-Location
    }
    $relativeZips = @(Get-ChildItem -LiteralPath $out -Filter '*.zip' | Where-Object { $_.Name -notin $beforeRelativeZips })
    Assert ($relativeZips.Count -eq 1) 'Relative output directory resolved incorrectly'
    $extractRelative = Join-Path $root 'extract-relative'
    Expand-Archive -LiteralPath $relativeZips[0].FullName -DestinationPath $extractRelative
    $relativeLog = [IO.File]::ReadAllText((Join-Path $extractRelative 'golf.log'))
    Assert ($relativeLog.Contains('ordinary game text and session-saves text')) 'Relative environment paths replaced ordinary words'
    Assert (-not $relativeLog.Contains($oldRoot)) 'Later rotated-log header failed to redact earlier log'
    Assert ([regex]::Matches($relativeLog, [regex]::Escape('<GAME>/course.sve')).Count -eq 2) 'Relative game environment path not normalized'
    Assert ($relativeLog.Contains('<SAVES>/course.sve')) 'Relative save environment path used the wrong base'
    Assert (Test-Path -LiteralPath (Join-Path $extractRelative 'save-01.sve')) 'Relative save selection resolved incorrectly'
    # Lexical Windows cases use unmounted drives deliberately: host bases must
    # win over both process and PowerShell current-drive state.
    & {
        . $script -InstallDir $install -OutputDirectory $out -PreviewOnly
        Assert ((Resolve-NativePath '\logs\golf.log' 'Q:\SimGolf') -eq 'Q:\logs\golf.log') 'Root-relative log used the process drive'
        Assert ((Resolve-NativePath '/logs/golf.log' 'Q:\SimGolf') -eq 'Q:\logs\golf.log') 'Forward-slash root used the process drive'
        Assert ((Resolve-NativePath 'Q:logs\golf.log' 'Q:\SimGolf') -eq 'Q:\SimGolf\logs\golf.log') 'Same-drive relative path used a hidden directory'
        Assert ((Resolve-NativePath 'R:\logs\golf.log' 'Q:\SimGolf') -eq 'R:\logs\golf.log') 'Fully qualified path changed drives'
        $rejectedDriveRelative = $false
        try { Resolve-NativePath 'R:logs\golf.log' 'Q:\SimGolf' | Out-Null } catch { $rejectedDriveRelative = $true }
        Assert $rejectedDriveRelative 'Ambiguous other-drive relative path was accepted'
        $roots.Clear()
        $env:SIMGOLF_USER_DIR = '\private-saves'
        Add-SaveEnvironmentRoot 'R:\game'
        Assert (@($roots | Where-Object { $_.Path -eq 'R:\private-saves' }).Count -eq 1) 'Root-relative save path used install drive'
        $roots.Clear()
        $env:SIMGOLF_USER_DIR = 'R:private-saves'
        Add-SaveEnvironmentRoot 'R:\game'
        Assert (@($roots | Where-Object { $_.Path -eq 'R:\game\private-saves' }).Count -eq 1) 'Drive-relative save path used install directory'
        $roots.Clear()
        $env:SIMGOLF_USER_DIR = '\private-saves'
        Add-SaveEnvironmentRoot $null
        Assert ($roots.Count -eq 0) 'Unknown game drive was guessed'
        $env:SIMGOLF_USER_DIR = 'R:\private-saves'
        Add-SaveEnvironmentRoot $null
        Assert (@($roots | Where-Object { $_.Path -eq 'R:\private-saves' }).Count -eq 1) 'Absolute save path required a known game drive'
    }
    Write-Host 'Collector integration tests passed'
} finally {
    $env:SIMGOLF_GAME = $originalGameEnv
    $env:SIMGOLF_USER_DIR = $originalSaveEnv
    Remove-Item Function:\Read-Host -ErrorAction SilentlyContinue
    Remove-Item Function:\Start-Process -ErrorAction SilentlyContinue
    Remove-Variable DiagnosticBrowserCalls, DiagnosticBrowserFails -Scope Global -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
