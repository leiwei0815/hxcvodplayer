@chcp 65001 >nul 2>&1
@echo off
REM ============================================================
REM 复制 SDK 依赖 DLL 脚本
REM ============================================================

setlocal enabledelayedexpansion

set SDK_BIN_DIR=%1
set BUILD_TYPE=%2
set VCPKG_ROOT=%3

if "%SDK_BIN_DIR%"=="" (
    echo [错误] 未指定 SDK bin 目录
    exit /b 1
)

if "%BUILD_TYPE%"=="" set BUILD_TYPE=release
if "%VCPKG_ROOT%"=="" set VCPKG_ROOT=C:\vcpkg

echo ========================================
echo 复制依赖 DLL 到 SDK
echo ========================================
echo SDK bin 目录: %SDK_BIN_DIR%
echo 构建类型: %BUILD_TYPE%
echo vcpkg 路径: %VCPKG_ROOT%
echo.

REM 创建 bin 目录
if not exist "%SDK_BIN_DIR%" mkdir "%SDK_BIN_DIR%"

REM 本地库目录（统一使用 Release 版本）
set WIN_THIRD_DIR=%~dp0..\win-third
set SDL2_LOCAL=%WIN_THIRD_DIR%\sdl2-install\release\bin\SDL2.dll
set FFMPEG_LOCAL=%WIN_THIRD_DIR%\ffmpeg-install\bin

echo 使用本地编译的 Release 版本库
echo.

REM vcpkg bin 目录（作为后备）
set VCPKG_BIN=%VCPKG_ROOT%\installed\x64-windows\bin

REM ========================================
REM [1/3] 复制 SDL2
REM ========================================
echo [1/3] 复制 SDL2...

if exist "%SDL2_LOCAL%" (
    copy /Y "%SDL2_LOCAL%" "%SDK_BIN_DIR%\" >nul
    echo   ✓ SDL2.dll (本地编译 Release)
) else if exist "%VCPKG_BIN%\SDL2.dll" (
    copy /Y "%VCPKG_BIN%\SDL2.dll" "%SDK_BIN_DIR%\" >nul
    echo   ✓ SDL2.dll (vcpkg 后备)
) else (
    echo   ✗ SDL2.dll 未找到
    echo     提示: 运行 win-third\build_sdl2.bat 编译本地版本
)

echo.

REM ========================================
REM [2/3] 复制 FFmpeg
REM ========================================
echo [2/3] 复制 FFmpeg...
set FFMPEG_FOUND=0

if exist "%FFMPEG_LOCAL%" (
    echo   使用本地编译版本: %FFMPEG_LOCAL%
    echo   [验证] 检查文件大小...
    for %%f in (%FFMPEG_LOCAL%\avcodec-*.dll) do (
        set SIZE=%%~zf
        set /a SIZE_MB=!SIZE! / 1048576
        echo   - avcodec 大小: !SIZE_MB! MB
        if !SIZE_MB! LSS 11 (
            echo   ✓ 确认为本地精简版本
        ) else (
            echo   ⚠ 警告: 文件较大，可能是完整版本
        )
    )
    for %%f in (%FFMPEG_LOCAL%\av*.dll %FFMPEG_LOCAL%\sw*.dll) do (
        copy /Y "%%f" "%SDK_BIN_DIR%\" >nul
        for %%n in (%%~nxf) do (
            echo   ✓ %%n (本地编译 Release)
            set FFMPEG_FOUND=1
        )
    )
    goto :ffmpeg_done
)

REM 如果本地没找到，使用 vcpkg 后备
if exist "%VCPKG_BIN%" (
    echo   使用 vcpkg 版本: %VCPKG_BIN%
    for %%f in (avcodec avformat avutil swscale swresample) do (
        set FOUND=0
        for %%v in (62 61 60 59 58) do (
            if exist "%VCPKG_BIN%\%%f-%%v.dll" (
                copy /Y "%VCPKG_BIN%\%%f-%%v.dll" "%SDK_BIN_DIR%\" >nul
                echo   ✓ %%f-%%v.dll (vcpkg 后备)
                set FOUND=1
                set FFMPEG_FOUND=1
                goto :next_lib
            )
        )
        :next_lib
    )
    goto :ffmpeg_done
)

echo   ✗ FFmpeg 未找到
echo     提示: 运行 win-third\build_ffmpeg_wsl.sh 编译本地版本

:ffmpeg_done

if %FFMPEG_FOUND%==0 (
    echo   [警告] 未找到 FFmpeg DLL
)

echo.
echo [3/3] 复制 SoundTouch (可选)...

REM SoundTouch 是静态库，已链接到 hxcplayer.dll，不需要单独复制
echo   ⓘ SoundTouch 已静态链接到 DLL，无需复制

echo.
echo ========================================
echo 依赖 DLL 复制完成
echo ========================================
echo.

REM 列出复制的文件
echo 已复制的 DLL:
dir /b "%SDK_BIN_DIR%\*.dll" 2>nul

echo.
exit /b 0
