@echo off
chcp 65001 >nul 2>&1
setlocal EnableDelayedExpansion

echo ========================================
echo   复制 MinGW FFmpeg 运行时依赖
echo ========================================
echo.

set TEST_DIR=D:\git\hxcvodplayer\win-sdk-example\build\bin\Debug
set FFMPEG_BIN=D:\git\hxcvodplayer\win-third\ffmpeg-install\bin

if not exist "%TEST_DIR%" (
    echo [错误] Debug 目录不存在: %TEST_DIR%
    echo 请先在 Visual Studio 中编译 Debug 版本
    pause
    exit /b 1
)

echo 目标目录: %TEST_DIR%
echo.

REM ========================================
REM 1. 复制 FFmpeg DLLs
REM ========================================
echo [1/3] 复制 FFmpeg DLLs...
if exist "%FFMPEG_BIN%" (
    copy /Y "%FFMPEG_BIN%\av*.dll" "%TEST_DIR%\" >nul 2>&1
    copy /Y "%FFMPEG_BIN%\sw*.dll" "%TEST_DIR%\" >nul 2>&1
    echo   ✓ FFmpeg DLLs 已复制
) else (
    echo   ✗ 未找到 FFmpeg bin 目录
)

REM ========================================
REM 2. 复制 SDL2
REM ========================================
echo.
echo [2/3] 复制 SDL2.dll...
set SDL2_DLL=D:\git\hxcvodplayer\win-third\sdl2-install\release\bin\SDL2.dll
if exist "%SDL2_DLL%" (
    copy /Y "%SDL2_DLL%" "%TEST_DIR%\" >nul 2>&1
    echo   ✓ SDL2.dll 已复制
) else (
    echo   ✗ 未找到 SDL2.dll
)

REM ========================================
REM 3. 查找并复制 MinGW 运行时 DLL
REM ========================================
echo.
echo [3/3] 查找 MinGW 运行时库...
echo.

REM 常见的 MinGW DLL 位置
set MINGW_PATHS=C:\msys64\mingw64\bin;C:\msys64\ucrt64\bin;C:\mingw64\bin

REM 需要的 MinGW 运行时 DLL
set MINGW_DLLS=libgcc_s_seh-1.dll libwinpthread-1.dll libstdc++-6.dll

for %%p in (%MINGW_PATHS%) do (
    if exist "%%p" (
        echo 检查: %%p
        for %%d in (%MINGW_DLLS%) do (
            if exist "%%p\%%d" (
                copy /Y "%%p\%%d" "%TEST_DIR%\" >nul 2>&1
                echo   ✓ 复制 %%d
            )
        )
    )
)

echo.
echo ========================================
echo 已复制的所有 DLL:
echo ========================================
dir /b "%TEST_DIR%\*.dll"

echo.
echo [完成] 现在可以尝试运行程序
echo.
pause
