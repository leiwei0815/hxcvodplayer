# YXVodPlayer Windows Dependencies Installation Script
# Automatically install all required dependencies using vcpkg

param(
    [switch]$SkipVcpkgSetup = $false,
    [switch]$SkipOptional = $false,
    [string]$VcpkgRoot = "C:\vcpkg"
)

$ErrorActionPreference = "Stop"

Write-Host "==========================================" -ForegroundColor Green
Write-Host "HXCVodPlayer Windows Dependencies Installer" -ForegroundColor Green  
Write-Host "==========================================" -ForegroundColor Green
Write-Host ""

# Check if running as Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "[WARNING] Recommended to run as Administrator" -ForegroundColor Yellow
    Write-Host ""
}

# Check Git
Write-Host "[CHECK] Git..." -ForegroundColor Cyan
if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host "[ERROR] Git not installed" -ForegroundColor Red
    Write-Host "Download: https://git-scm.com/download/win" -ForegroundColor Yellow
    exit 1
}
Write-Host "[OK] Git installed" -ForegroundColor Green
Write-Host ""

# Setup vcpkg
if (-not $SkipVcpkgSetup) {
    Write-Host "[STEP 1] Setup vcpkg..." -ForegroundColor Cyan
    
    if (Test-Path $VcpkgRoot) {
        Write-Host "[INFO] vcpkg exists at $VcpkgRoot" -ForegroundColor Yellow
        Write-Host "[INFO] Updating vcpkg..." -ForegroundColor Cyan
        Push-Location $VcpkgRoot
        git pull
        .\bootstrap-vcpkg.bat
        Pop-Location
    } else {
        Write-Host "[INFO] Installing vcpkg to $VcpkgRoot..." -ForegroundColor Cyan
        $vcpkgParent = Split-Path $VcpkgRoot -Parent
        if (-not (Test-Path $vcpkgParent)) {
            New-Item -ItemType Directory -Path $vcpkgParent -Force | Out-Null
        }
        
        Push-Location $vcpkgParent
        git clone https://github.com/microsoft/vcpkg.git
        Pop-Location
        
        Push-Location $VcpkgRoot
        .\bootstrap-vcpkg.bat
        Pop-Location
    }
    
    if ($isAdmin) {
        Write-Host "[INFO] Setting VCPKG_ROOT..." -ForegroundColor Cyan
        [System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $VcpkgRoot, [System.EnvironmentVariableTarget]::Machine)
        $env:VCPKG_ROOT = $VcpkgRoot
        Write-Host "[OK] VCPKG_ROOT = $VcpkgRoot" -ForegroundColor Green
    } else {
        Write-Host "[WARNING] Need admin rights for environment variable" -ForegroundColor Yellow
    }
    Write-Host ""
}

# Check vcpkg
if (-not (Test-Path "$VcpkgRoot\vcpkg.exe")) {
    Write-Host "[ERROR] vcpkg not found at $VcpkgRoot" -ForegroundColor Red
    exit 1
}

# Integrate vcpkg
Write-Host "[STEP 2] Integrate vcpkg to Visual Studio..." -ForegroundColor Cyan
Push-Location $VcpkgRoot
.\vcpkg integrate install
Pop-Location
Write-Host ""

# Install dependencies
Write-Host "[STEP 3] Install dependencies..." -ForegroundColor Cyan
Write-Host "[INFO] Qt5 may take 30-60 minutes..." -ForegroundColor Yellow
Write-Host ""

# Required packages
$required = @(
    "ffmpeg:x64-windows",
    "sdl2:x64-windows",
    "qt5-base:x64-windows",
    "qt5-multimedia:x64-windows"
)

# Optional packages (SoundTouch needs ATL/MFC component)
$optional = @(
    "soundtouch:x64-windows"
)

$failed = @()

# Install required
foreach ($pkg in $required) {
    Write-Host "[INSTALL] $pkg (required)..." -ForegroundColor Cyan
    Push-Location $VcpkgRoot
    .\vcpkg install $pkg
    $code = $LASTEXITCODE
    Pop-Location
    
    if ($code -eq 0) {
        Write-Host "[OK] $pkg" -ForegroundColor Green
    } else {
        Write-Host "[FAILED] $pkg" -ForegroundColor Red
        $failed += $pkg
    }
    Write-Host ""
}

# Check required failures
if ($failed.Count -gt 0) {
    Write-Host "==========================================" -ForegroundColor Red
    Write-Host "[ERROR] Required packages failed:" -ForegroundColor Red
    foreach ($pkg in $failed) {
        Write-Host "  - $pkg" -ForegroundColor Red
    }
    Write-Host "==========================================" -ForegroundColor Red
    exit 1
}

# Install optional
if (-not $SkipOptional) {
    foreach ($pkg in $optional) {
        Write-Host "[INSTALL] $pkg (optional)..." -ForegroundColor Cyan
        Write-Host "[INFO] Requires Visual Studio ATL/MFC component" -ForegroundColor Yellow
        Push-Location $VcpkgRoot
        .\vcpkg install $pkg
        $code = $LASTEXITCODE
        Pop-Location
        
        if ($code -eq 0) {
            Write-Host "[OK] $pkg" -ForegroundColor Green
        } else {
            Write-Host "[WARNING] $pkg failed (optional)" -ForegroundColor Yellow
            Write-Host "[INFO] Playback rate control will be disabled" -ForegroundColor Yellow
            Write-Host "[INFO] To install: Run .\install_atlmfc.bat" -ForegroundColor Yellow
        }
        Write-Host ""
    }
}

# Verify
Write-Host "[STEP 4] Verify installation..." -ForegroundColor Cyan
Push-Location $VcpkgRoot
Write-Host ""
.\vcpkg list | Select-String -Pattern "(ffmpeg|sdl2|soundtouch|qt5)"
Pop-Location
Write-Host ""

# Success
Write-Host "==========================================" -ForegroundColor Green
Write-Host "[SUCCESS] Installation complete!" -ForegroundColor Green
Write-Host "==========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. .\build_windows.bat vs2022" -ForegroundColor White
Write-Host "  2. Open Visual Studio project" -ForegroundColor White
Write-Host ""
Write-Host "See: README_WINDOWS.md" -ForegroundColor Yellow
Write-Host ""
