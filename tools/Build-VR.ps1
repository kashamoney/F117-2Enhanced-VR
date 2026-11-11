[CmdletBinding()]
param([string]$MsysRoot = 'C:\msys64', [switch]$DesktopOnly)
$ErrorActionPreference = 'Stop'
$repoPath = Split-Path -Parent $PSScriptRoot
$binPath = Join-Path $MsysRoot 'mingw64\bin'
if (-not (Test-Path -LiteralPath (Join-Path $binPath 'gcc.exe'))) {
    throw 'Install MSYS2 and its mingw64 GCC, CMake, Ninja, SDL3 and openxr-sdk packages. See the README VR section.'
}
$env:PATH = $binPath + ';' + $env:PATH
$buildPath = Join-Path $repoPath $(if ($DesktopOnly) { 'f117port\build-desktop' } else { 'f117port\build-vr' })
$xrEnabled = if ($DesktopOnly) { 'OFF' } else { 'ON' }
& cmake -S (Join-Path $repoPath 'f117port') -B $buildPath -G Ninja '-DCMAKE_C_COMPILER=gcc' '-DCMAKE_CXX_COMPILER=g++' '-DCMAKE_BUILD_TYPE=Release' '-DF117_LAUNCHER=OFF' "-DF117_OPENXR=$xrEnabled"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& cmake --build $buildPath --parallel 12
if ($LASTEXITCODE -ne 0) { throw 'Compilation failed.' }
& ctest --test-dir $buildPath --output-on-failure -R '^(chain|keymap|tick_rate|check_missing_files|platform|video_selftest|compose|enh_interp_rules|enh_wide_geometry|enh_vr_math)$'
if ($LASTEXITCODE -ne 0) { throw 'Build checks failed.' }
$exeName = if ($DesktopOnly) { 'f117-enhanced.exe' } else { 'f117-enhanced-vr.exe' }
Write-Output "Ready: $(Join-Path $buildPath $exeName)"
