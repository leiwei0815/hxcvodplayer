@echo off
chcp 65001 >nul 2>nul
echo ==========================================
echo Install Visual Studio ATL/MFC Component
echo ==========================================
echo.
echo SoundTouch requires ATL/MFC component which is not installed.
echo.
echo Please follow these steps:
echo.
echo 1. Open "Visual Studio Installer"
echo 2. Click "Modify" on Visual Studio 2022
echo 3. Go to "Individual components" tab
echo 4. Search for "ATL" or "MFC"
echo 5. Check these components:
echo    - C++ ATL for latest v143 build tools (x86 ^& x64)
echo    - C++ MFC for latest v143 build tools (x86 ^& x64)
echo 6. Click "Modify" to install
echo.
echo After installation, run: .\setup_windows_deps.ps1
echo.
pause
start "" "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vs_installer.exe"
