# Builds the whole Antioch Suite on Windows and installs the VST3s.
#
# Run from the Antioch folder, in PowerShell:
#     powershell -ExecutionPolicy Bypass -File .\build-windows.ps1
# Run PowerShell as administrator to install straight into C:\Program Files\Common Files\VST3;
# otherwise the plugins are left in .\dist for you to copy there.
#
# Needs: Visual Studio 2022 (or its Build Tools) with "Desktop development with C++",
# CMake 3.22 or newer, Git, and an internet connection the first time (CMake fetches JUCE).

param(
    [string[]] $Plugins = @("Antioch", "CauseAndEffect", "Correspondence", "Kybalion", "Vibration", "Mentalism",
                            "Ouroboros", "Lapis", "EmeraldTablet", "Azoth", "PrimaMateria", "SolveEtCoagula", "Quintessence"),
    [string] $BuildDir = "build-win",
    [switch] $NoInstall
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

function Fail ($message) { Write-Host ""; Write-Host "  $message" -ForegroundColor Red; Write-Host ""; exit 1 }

# ---- Tools ----
if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Fail "Git isn't installed (or not on PATH): https://git-scm.com/download/win" }
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { Fail "CMake isn't installed (or not on PATH): https://cmake.org/download/ (tick 'Add CMake to the PATH')" }
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { Fail "Visual Studio 2022 isn't installed: https://visualstudio.microsoft.com/downloads/ (Community, with 'Desktop development with C++')" }
$vs = & $vswhere -latest -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { Fail "Visual Studio 2022 is installed, but without the C++ tools: add 'Desktop development with C++' in the Visual Studio Installer" }
Write-Host "Visual Studio: $vs"
Write-Host "CMake:         $((cmake --version | Select-Object -First 1))"

# ---- Configure and build ----
Write-Host ""
Write-Host "Configuring (the first time, CMake downloads JUCE: a few minutes)..." -ForegroundColor Cyan
cmake -B $BuildDir -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { Fail "Configuring failed (see above)" }

$failed = @()
foreach ($p in $Plugins) {
    Write-Host ""
    Write-Host "Building $p..." -ForegroundColor Cyan
    cmake --build $BuildDir --config Release --target "${p}_VST3" --parallel
    if ($LASTEXITCODE -ne 0) { $failed += $p }
}
if ($failed.Count -gt 0) { Fail ("These didn't build: " + ($failed -join ", ") + ". Send me the errors above.") }

# ---- Collect and install ----
$dist = Join-Path $PSScriptRoot "dist"
if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory $dist | Out-Null
foreach ($p in $Plugins) {
    Copy-Item -Recurse (Join-Path $BuildDir "plugins\$p\${p}_artefacts\Release\VST3\*.vst3") $dist
}
Write-Host ""
Write-Host "Built $($Plugins.Count) plugins into $dist" -ForegroundColor Green

if ($NoInstall) { exit 0 }
$target = Join-Path $env:CommonProgramFiles "VST3"
$admin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
    Write-Host "Not running as administrator: copy the .vst3 folders from $dist into $target yourself" -ForegroundColor Yellow
    Write-Host "(or run PowerShell as administrator and run this script again). Close FL Studio first."
    exit 0
}
if (Get-Process -Name "FL64", "FL" -ErrorAction SilentlyContinue) { Fail "FL Studio is running: close it, then run this again (it keeps the plugins locked)" }
New-Item -ItemType Directory $target -Force | Out-Null
Get-ChildItem $dist -Filter *.vst3 | ForEach-Object {
    $dest = Join-Path $target $_.Name
    if (Test-Path $dest) { Remove-Item $dest -Recurse -Force }
    Copy-Item -Recurse $_.FullName $dest
}
Write-Host "Installed into $target. In FL Studio: Options > Manage plugins > Find plugins." -ForegroundColor Green
