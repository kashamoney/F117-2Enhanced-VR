[CmdletBinding()]
param(
    [switch]$Preview,
    [switch]$Diagnostic,
    [switch]$Check,
    [switch]$NoPrompt,
    [switch]$DumpEyes,
    [switch]$CarrierPractice,
    [switch]$TraceWorld,
    [switch]$RecordLive,
    [ValidateRange(0,4096)][int]$Resolution = 0,
    [switch]$Software,
    [switch]$OriginalPresentation,
    [ValidateSet(1,2,4,8)][int]$DrawDistance = 8,
    [switch]$OriginalDrawDistance,
    [switch]$OriginalAircraftSize,
    [switch]$LegacyWaterPoints,
    [switch]$NoWaterDetail,
    [switch]$NoShorelines,
    [switch]$NoWakes,
    [switch]$NoAtmosphere,
    [switch]$NoLandingDetail,
    [ValidateRange(0.1,10)][double]$WorldScale = 1.0,
    [string]$GameDirectory
)
$ErrorActionPreference = 'Stop'
$previousKeyScript = $env:F117_KEYS
$previousShellScript = $env:F117_SHELL_SCRIPT
$previousVrTiming = $env:F117_VR_TIMING
$previousVrCapture = $env:F117_VR_CAPTURE_LIVE
$repoPath = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$settingsPath = Join-Path $repoPath '.vr-local.json'
if ($Diagnostic -and $Check) { throw 'Choose either -Diagnostic (headset) or -Check (game files).' }
if (-not $Diagnostic) {
    if (-not $GameDirectory) { $GameDirectory = $env:F117_GAME_DIR }
    if (-not $GameDirectory -and (Test-Path -LiteralPath $settingsPath)) {
        try {
            $settings = [IO.File]::ReadAllText($settingsPath) | ConvertFrom-Json
            $GameDirectory = $settings.GameDirectory
            if ($GameDirectory -and -not [IO.Path]::IsPathRooted($GameDirectory)) {
                $GameDirectory = Join-Path $repoPath $GameDirectory
            }
        } catch { throw 'Cannot read .vr-local.json. Supply -GameDirectory to select your game folder again.' }
    }
    if (-not $GameDirectory -and (Test-Path -LiteralPath (Join-Path $repoPath 'Game\VGAME.EXE'))) {
        $GameDirectory = Join-Path $repoPath 'Game'
    }
    if (-not $GameDirectory) {
        if ($NoPrompt) { throw 'Choose your original game folder with -GameDirectory, F117_GAME_DIR, or a Game folder beside README.md.' }
        Add-Type -AssemblyName System.Windows.Forms
        $folderPicker = New-Object System.Windows.Forms.FolderBrowserDialog
        $folderPicker.Description = 'Select your original F-117A game folder (containing F117.COM, VGAME.EXE and the other game files).'
        $folderPicker.ShowNewFolderButton = $false
        try {
            if ($folderPicker.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
                Write-Output 'Game folder selection cancelled.'
                return
            }
            $GameDirectory = $folderPicker.SelectedPath
        } finally { $folderPicker.Dispose() }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $GameDirectory 'VGAME.EXE') -PathType Leaf)) {
        throw 'The selected folder does not contain VGAME.EXE. Supply -GameDirectory with your original game folder.'
    }
    $resolvedGamePath = (Resolve-Path -LiteralPath $GameDirectory).Path
    $settingsJson = @{ GameDirectory = $resolvedGamePath } | ConvertTo-Json
    [IO.File]::WriteAllText($settingsPath, $settingsJson, (New-Object Text.UTF8Encoding($false)))
}
$exePath = Join-Path $repoPath 'f117port\build-vr\f117-enhanced-vr.exe'
if (-not (Test-Path -LiteralPath $exePath)) { & (Join-Path $PSScriptRoot 'Build-VR.ps1') }
$sessionPath = Join-Path $repoPath 'work\vr-session'
New-Item -ItemType Directory -Path $sessionPath -Force | Out-Null
$gameArgs = @('--vr-resolution', $Resolution.ToString(), '--vr-world-scale', $WorldScale.ToString([Globalization.CultureInfo]::InvariantCulture))
if ($Resolution -gt 0 -and $Resolution -lt 640) { throw 'Resolution must be 0 (native) or 640..4096 pixels high per eye.' }
if ($Software) { $gameArgs += '--vr-software' }
if ($OriginalPresentation) { $gameArgs += '--vr-no-presentation' }
$gameArgs += @('--vr-draw-distance', $DrawDistance.ToString())
if ($OriginalDrawDistance) { $gameArgs += '--vr-original-distance' }
if ($OriginalAircraftSize) { $gameArgs += '--vr-original-aircraft-size' }
if ($LegacyWaterPoints) { $gameArgs += '--vr-legacy-water-points' }
if ($NoWaterDetail) { $gameArgs += '--vr-no-water-detail' }
if ($NoShorelines) { $gameArgs += '--vr-no-shorelines' }
if ($NoWakes) { $gameArgs += '--vr-no-wakes' }
if ($NoAtmosphere) { $gameArgs += '--vr-no-atmosphere' }
if ($NoLandingDetail) { $gameArgs += '--vr-no-landing-detail' }
if ($Diagnostic) {
    $gameArgs += '--vr-diagnostic'
} elseif ($Check) {
    $gameArgs = @('--headless', '--check', '--game-dir', $resolvedGamePath)
} else {
    if ($CarrierPractice) {
        $practicePath = Join-Path $repoPath ('work\vr-practice\' + [guid]::NewGuid().ToString('N').Substring(0,10) + '\game')
        & python (Join-Path $PSScriptRoot 'prepare_vr_practice.py') --game-dir $resolvedGamePath --destination $practicePath
        if ($LASTEXITCODE -ne 0) { throw 'Could not prepare the carrier practice copy.' }
        $resolvedGamePath = $practicePath
        $env:F117_KEYS = '1:31,2:03,11:39'
        $env:F117_SHELL_SCRIPT = Join-Path $repoPath 'port\notes\s54\shell_chain_libya.txt'
    }
    $gameArgs += @('--game-dir', $resolvedGamePath)
    $gameArgs += $(if ($Preview) { '--vr-preview' } else { '--vr' })
    if ($DumpEyes) { $gameArgs += '--vr-dump' }
}
Write-Output $(if ($Check) { "Checking original game files in $resolvedGamePath" } elseif ($Preview) { 'Starting desktop stereo preview. Ctrl+arrows look around.' } elseif ($Diagnostic) { 'Checking SteamVR / OpenXR headset and D3D11 adapter.' } else { 'Start SteamVR with your headset tracking, then use this window for game input.' })
Write-Output "F12: recenter seat. F11: cockpit toggle. Logs: $sessionPath"
if ($TraceWorld -or $RecordLive) { $env:F117_VR_TIMING = '1' }
if ($RecordLive) {
    $env:F117_VR_CAPTURE_LIVE = '1'
    Write-Output 'F9: record a short live stereo burst while flying. Close the game to save PNGs and timing data.'
}
Push-Location $sessionPath
try {
    & $exePath @gameArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Game exited with code $LASTEXITCODE. Check f117.log and f117-vr.log in $sessionPath."
    }
} finally {
    Pop-Location
    $env:F117_VR_TIMING = $previousVrTiming
    $env:F117_VR_CAPTURE_LIVE = $previousVrCapture
    if ($CarrierPractice) {
        $env:F117_KEYS = $previousKeyScript
        $env:F117_SHELL_SCRIPT = $previousShellScript
    }
}
