<#
.SYNOPSIS
    Build script for c2t on Windows using MSYS2 / MinGW-w64 and Ninja.

.DESCRIPTION
    Automatically locates MSYS2 UCRT64/MINGW64 toolchain paths, configures CMake,
    and builds the c2t Windows executable.

.PARAMETER BuildType
    CMake build configuration (Release, Debug, MinSizeRel). Default: Release

.PARAMETER Clean
    If specified, deletes the build directory before building.

.PARAMETER MsysPath
    Path to MSYS2 root directory. Default: C:\msys64

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Clean -BuildType Release
    .\build.ps1 -BuildType Debug
#>

[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug", "MinSizeRel", "RelWithDebInfo")]
    [string]$BuildType = "Release",

    [switch]$Clean,

    [string]$MsysPath = "C:\msys64"
)

$ErrorActionPreference = "Stop"

Write-Host "=========================================" -ForegroundColor Cyan
Write-Host " Building c2t on Windows ($BuildType)" -ForegroundColor Cyan
Write-Host "=========================================" -ForegroundColor Cyan

# 1. Setup Toolchain Paths
$PossiblePaths = @(
    Join-Path $MsysPath "ucrt64\bin",
    Join-Path $MsysPath "mingw64\bin",
    Join-Path $MsysPath "usr\bin"
)

$AddedPaths = @()
foreach ($p in $PossiblePaths) {
    if (Test-Path $p) {
        if (-not ($env:PATH -split ';' -contains $p)) {
            $env:PATH = "$p;" + $env:PATH
            $AddedPaths += $p
        }
    }
}

# 2. Check for required tools
$MissingTools = @()
foreach ($tool in @("cmake", "gcc", "ninja")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        $MissingTools += $tool
    }
}

if ($MissingTools.Count -gt 0) {
    Write-Host "`n[ERROR] Missing required build tools: $($MissingTools -join ', ')" -ForegroundColor Red
    Write-Host "Please ensure you have installed the MSYS2 UCRT64 toolchain:" -ForegroundColor Yellow
    Write-Host "  pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja" -ForegroundColor White
    Exit 1
}

$BuildDir = "build/windows"

# 3. Clean if requested
if ($Clean) {
    if (Test-Path $BuildDir) {
        Write-Host "`n[INFO] Cleaning build directory ($BuildDir)..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force $BuildDir
    }
}

# 4. Configure CMake
Write-Host "`n[1/2] Configuring CMake..." -ForegroundColor Green
$CmakeConfigArgs = @(
    "-S", ".",
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$BuildType"
)

& cmake @CmakeConfigArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] CMake configuration failed." -ForegroundColor Red
    Exit $LASTEXITCODE
}

# 5. Build
Write-Host "`n[2/2] Compiling with Ninja..." -ForegroundColor Green
$CmakeBuildArgs = @(
    "--build", $BuildDir,
    "--config", $BuildType
)

& cmake @CmakeBuildArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] Build failed." -ForegroundColor Red
    Exit $LASTEXITCODE
}

# 6. Summary
$ExePath = Join-Path $BuildDir "c2t.exe"
if (Test-Path $ExePath) {
    $ExeItem = Get-Item $ExePath
    $SizeKB = [math]::Round($ExeItem.Length / 1KB, 2)
    Write-Host "`n=========================================" -ForegroundColor Green
    Write-Host " Build succeeded!" -ForegroundColor Green
    Write-Host " Output: $ExePath ($SizeKB KB)" -ForegroundColor White
    Write-Host "=========================================" -ForegroundColor Green
} else {
    Write-Host "`n[WARNING] Build finished but $ExePath not found." -ForegroundColor Yellow
}
