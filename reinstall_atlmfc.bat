@echo off
chcp 65001 >nul 2>nul
echo ==========================================
echo Reinstall ATL/MFC for Latest MSVC
echo ==========================================
echo.
echo Current situation:
echo   - ATL/MFC is installed for MSVC 14.16 (old)
echo   - vcpkg needs ATL/MFC for MSVC 14.44 (current)
echo.
echo Solution:
echo   Reinstall ATL/MFC component in Visual Studio Installer
echo.
echo Steps:
echo   1. Open Visual Studio Installer (will open now)
echo   2. Click "Modify" on VS 2022
echo   3. Go to "Individual components"
echo   4. UNCHECK old ATL/MFC (if visible)
echo   5. CHECK these for v143 (latest):
echo      - C++ ATL for latest v143 build tools (x86 ^& x64)
echo      - C++ MFC for latest v143 build tools (x86 ^& x64)
echo   6. Click "Modify"
echo.
echo Press any key to open VS Installer...
pause >nul

start "" "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vs_installer.exe"

echo.
echo After installation, run: .\install_soundtouch.bat
echo.
pause
