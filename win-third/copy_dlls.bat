@echo off
chcp 65001 >nul 2>nul
REM ==========================================
REM 复制运行时依赖 DLL 到输出目录
REM ==========================================

setlocal enabledelayedexpansion

set PROJECT_ROOT=%~dp0..
set VCPKG_ROOT=C:\vcpkg\installed\x64-windows
set QT_ROOT=C:\Qt\5.15.2\msvc2019_64

REM 检查参数
if "%~1"=="" (
    echo 用法: copy_dlls.bat [debug^|release]
    echo.
    echo 示例:
    echo   copy_dlls.bat debug
    echo   copy_dlls.bat release
    pause
    exit /b 1
)

set BUILD_TYPE=%~1

if /i "%BUILD_TYPE%"=="debug" (
    set BUILD_DIR=%PROJECT_ROOT%\build\vs2022_debug
    set OUTPUT_DIR=%BUILD_DIR%\bin\Debug
    set CONFIG=Debug
) else if /i "%BUILD_TYPE%"=="release" (
    set BUILD_DIR=%PROJECT_ROOT%\build\vs2022_release
    set OUTPUT_DIR=%BUILD_DIR%\bin\Release
    set CONFIG=Release
) else (
    echo [错误] 无效的构建类型: %BUILD_TYPE%
    echo 必须是 debug 或 release
    pause
    exit /b 1
)

echo ==========================================
echo 复制 %CONFIG% 运行时 DLL
echo ==========================================
echo.
echo 目标目录: %OUTPUT_DIR%
echo.

if not exist "%OUTPUT_DIR%" (
    echo [错误] 输出目录不存在: %OUTPUT_DIR%
    echo 请先编译项目
    pause
    exit /b 1
)

REM 复制 SDL2 DLL
echo [1/4] 复制 SDL2.dll...
if exist "%VCPKG_ROOT%\bin\SDL2.dll" (
    copy /Y "%VCPKG_ROOT%\bin\SDL2.dll" "%OUTPUT_DIR%\" >nul
    echo     ✓ SDL2.dll
) else (
    echo     ✗ 找不到 SDL2.dll
)

REM 复制 FFmpeg DLL
echo [2/4] 复制 FFmpeg DLL...
for %%f in (avcodec avformat avutil swscale swresample) do (
    if exist "%VCPKG_ROOT%\bin\%%f*.dll" (
        copy /Y "%VCPKG_ROOT%\bin\%%f*.dll" "%OUTPUT_DIR%\" >nul
        echo     ✓ %%f-*.dll
    ) else (
        echo     ✗ 找不到 %%f*.dll
    )
)

REM 复制 Qt DLL
echo [3/4] 复制 Qt DLL...
if /i "%BUILD_TYPE%"=="debug" (
    set QT_SUFFIX=d
) else (
    set QT_SUFFIX=
)

for %%f in (Core Gui Widgets Multimedia) do (
    if exist "%QT_ROOT%\bin\Qt5%%f!QT_SUFFIX!.dll" (
        copy /Y "%QT_ROOT%\bin\Qt5%%f!QT_SUFFIX!.dll" "%OUTPUT_DIR%\" >nul
        echo     ✓ Qt5%%f!QT_SUFFIX!.dll
    ) else (
        echo     ✗ 找不到 Qt5%%f!QT_SUFFIX!.dll
    )
)

REM 复制 Qt 平台插件
echo [4/4] 复制 Qt 平台插件...
if not exist "%OUTPUT_DIR%\platforms" mkdir "%OUTPUT_DIR%\platforms"
if exist "%QT_ROOT%\plugins\platforms\qwindows!QT_SUFFIX!.dll" (
    copy /Y "%QT_ROOT%\plugins\platforms\qwindows!QT_SUFFIX!.dll" "%OUTPUT_DIR%\platforms\" >nul
    echo     ✓ qwindows!QT_SUFFIX!.dll
) else (
    echo     ✗ 找不到 qwindows!QT_SUFFIX!.dll
)

echo.
echo ==========================================
echo [完成] DLL 复制完成
echo ==========================================
echo.
echo 现在可以运行程序了:
echo %OUTPUT_DIR%\HXCVodPlayer.exe
echo.
pause
