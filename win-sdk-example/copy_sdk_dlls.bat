@echo off
chcp 65001 >nul 2>&1
setlocal EnableDelayedExpansion

echo ========================================
echo   复制 SDK DLL 到测试项目
echo ========================================
echo.

set PROJECT_ROOT=%~dp0
set TEST_BIN_DIR=%PROJECT_ROOT%build\bin\Release
set SDK_BIN_DIR=%PROJECT_ROOT%..\build\win-sdk-Release\HXCPlayerSDK\bin
set LOCAL_FFMPEG=%PROJECT_ROOT%..\win-third\ffmpeg-install\bin
set LOCAL_SDL2=%PROJECT_ROOT%..\win-third\sdl2-install\release\bin

REM 创建输出目录
if not exist "%TEST_BIN_DIR%" mkdir "%TEST_BIN_DIR%"

echo [1/3] 复制 hxcplayer.dll...
if exist "%SDK_BIN_DIR%\hxcplayer.dll" (
    copy /Y "%SDK_BIN_DIR%\hxcplayer.dll" "%TEST_BIN_DIR%\" >nul
    echo   ✓ hxcplayer.dll
) else (
    echo   ✗ 未找到 hxcplayer.dll
    echo   提示: 先运行 cd ..\win-sdk ; build_sdk.bat
)

echo.
echo [2/3] 复制 SDL2.dll...
if exist "%LOCAL_SDL2%\SDL2.dll" (
    copy /Y "%LOCAL_SDL2%\SDL2.dll" "%TEST_BIN_DIR%\" >nul
    echo   ✓ SDL2.dll
) else (
    echo   ✗ 未找到 SDL2.dll
)

echo.
echo [3/3] 复制 FFmpeg DLLs...
set COPIED=0
for %%f in ("%LOCAL_FFMPEG%\av*.dll" "%LOCAL_FFMPEG%\sw*.dll") do (
    copy /Y "%%f" "%TEST_BIN_DIR%\" >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        for %%n in ("%%~nxf") do echo   ✓ %%~n
        set COPIED=1
    )
)

if !COPIED! equ 0 (
    echo   ✗ 未找到 FFmpeg DLL
)

echo.
echo ========================================
echo 复制完成！
echo ========================================
echo.
echo 输出目录: %TEST_BIN_DIR%
echo.
dir /b "%TEST_BIN_DIR%\*.dll"

exit /b 0
