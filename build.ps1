$ErrorActionPreference = "Stop"

Write-Host "=== MoneroMiner Build Script ===" -ForegroundColor Cyan
Write-Host ""

# Find Visual Studio
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -property installationPath
} else {
    $vsPath = "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community"
}

if (-not (Test-Path $vsPath)) {
    Write-Host "ERROR: Visual Studio not found!" -ForegroundColor Red
    exit 1
}

Write-Host "Found Visual Studio at: $vsPath" -ForegroundColor Green

# Find MSBuild
$msbuild = Join-Path $vsPath "MSBuild\Current\Bin\amd64\MSBuild.exe"
if (-not (Test-Path $msbuild)) {
    $msbuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
}

Write-Host "MSBuild found: $msbuild" -ForegroundColor Green
Write-Host ""

# Default configuration
$configuration = "Release"
$platform = "x64"

# Parse command line args
if ($args -contains "-debug") { $configuration = "Debug" }
if ($args -contains "-x86") { $platform = "Win32" }

Write-Host "Building: $configuration | $platform" -ForegroundColor Yellow
Write-Host ""

# Clean
Write-Host "Cleaning..." -ForegroundColor Yellow
Remove-Item -Recurse -Force "$platform\$configuration" -ErrorAction SilentlyContinue

# Build solution
& $msbuild "MoneroMiner.sln" "/p:Configuration=$configuration" "/p:Platform=$platform" /m /v:minimal

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "=== BUILD SUCCESSFUL ===" -ForegroundColor Green
    $exe = "$platform\$configuration\MoneroMiner.exe"
    if (Test-Path $exe) {
        Write-Host "Output: $exe" -ForegroundColor Cyan
    }
} else {
    Write-Host ""
    Write-Host "=== BUILD FAILED ===" -ForegroundColor Red
    exit 1
}
