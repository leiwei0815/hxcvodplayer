@echo off
chcp 65001 >nul 2>nul
echo ==========================================
echo Install SoundTouch After ATL/MFC
echo ==========================================
echo.
echo This script will install SoundTouch after you have installed ATL/MFC.
echo.
echo Prerequisites:
echo   - Visual Studio ATL/MFC components installed
echo   - vcpkg installed at C:\vcpkg
echo.
echo Press any key to start installing SoundTouch...
pause >nul
echo.

cd C:\vcpkg

echo [INFO] Installing soundtouch:x64-windows...
echo.

vcpkg install soundtouch:x64-windows

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ==========================================
    echo [SUCCESS] SoundTouch installed!
    echo ==========================================
    echo.
    echo Next step:
    echo   Run: .\build_windows.bat vs2022
    echo.
) else (
    echo.
    echo ==========================================
    echo [ERROR] SoundTouch installation failed
    echo ==========================================
    echo.
    echo Please check:
    echo   1. ATL/MFC is installed correctly
    echo   2. vcpkg is up to date (git pull in C:\vcpkg)
    echo.
)

pause
