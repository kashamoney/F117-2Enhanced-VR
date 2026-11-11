[CmdletBinding()]
param(
    [switch]$Preview,
    [switch]$Diagnostic,
    [switch]$DumpEyes,
    [switch]$CarrierPractice,
    [ValidateRange(640,4096)][int]$Resolution = 4096,
    [ValidateRange(0.1,10)][double]$WorldScale = 1.0,
    [string]$GameDirectory
)
$ErrorActionPreference = 'Stop'
$previousKeyScript = $env:F117_KEYS
$previousShellScript = $env:F117_SHELL_SCRIPT
$repoPath = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $repoPath 'f117port\build-vr\f117-enhanced-vr.exe'
if (-not (Test-Path -LiteralPath $exePath)) { & (Join-Path $PSScriptRoot 'Build-VR.ps1') }
if (-not $GameDirectory) { $GameDirectory = Join-Path (Split-Path -Parent $repoPath) 'F117-Enhanced-play\Game' }
$sessionPath = Join-Path $repoPath 'work\vr-session'
New-Item -ItemType Directory -Path $sessionPath -Force | Out-Null
$gameArgs = @('--vr-resolution', $Resolution.ToString(), '--vr-world-scale', $WorldScale.ToString([Globalization.CultureInfo]::InvariantCulture))
if ($Diagnostic) {
    $gameArgs += '--vr-diagnostic'
} else {
    $resolvedGamePath = (Resolve-Path -LiteralPath $GameDirectory).Path
    if (-not (Test-Path -LiteralPath (Join-Path $resolvedGamePath 'VGAME.EXE'))) { throw 'GameDirectory must contain the original game files.' }
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
Write-Output $(if ($Preview) { 'Starting desktop stereo preview. Ctrl+arrows look around.' } elseif ($Diagnostic) { 'Checking SteamVR / OpenXR headset and D3D11 adapter.' } else { 'Start SteamVR with your headset tracking, then use this window for game input.' })
Write-Output "F12: recenter seat. F11: cockpit toggle. Logs: $sessionPath"
Push-Location $sessionPath
try {
    & $exePath @gameArgs
    if ($LASTEXITCODE -ne 0) {
        throw "Game exited with code $LASTEXITCODE. Check f117-vr.log and SteamVR. If SteamVR reports Oasis_UnlockRequired, follow the Oasis headset unlock procedure."
    }
} finally {
    Pop-Location
    if ($CarrierPractice) {
        $env:F117_KEYS = $previousKeyScript
        $env:F117_SHELL_SCRIPT = $previousShellScript
    }
}
