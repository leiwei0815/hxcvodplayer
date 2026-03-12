@echo off
chcp 65001 >nul 2>nul
echo ==========================================
echo 复制运行时 DLL 到 Debug 输出目录
echo ==========================================
echo.

set OUTPUT_DIR=d:\git\hxcvodplayer\build\vs2022_debug\bin\Debug
set VCPKG_BIN=C:\vcpkg\installed\x64-windows\bin
set QT_BIN=C:\Qt\5.15.2\msvc2019_64\bin
set QT_PLUGINS=C:\Qt\5.15.2\msvc2019_64\plugins

if not exist "%OUTPUT_DIR%" (
    echo [错误] 输出目录不存在，请先编译项目
    pause
    exit /b 1
)

echo 目标: %OUTPUT_DIR%
echo.

echo [1/4] 复制 SDL2.dll...
copy /Y "%VCPKG_BIN%\SDL2.dll" "%OUTPUT_DIR%\" >nul 2>&1
if exist "%OUTPUT_DIR%\SDL2.dll" (echo     ✓ SDL2.dll) else (echo     ✗ 失败)

echo [2/4] 复制 FFmpeg DLL...
for %%f in (avcodec-61 avformat-61 avutil-59 swscale-8 swresample-5) do (
    copy /Y "%VCPKG_BIN%\%%f.dll" "%OUTPUT_DIR%\" >nul 2>&1
    if exist "%OUTPUT_DIR%\%%f.dll" (echo     ✓ %%f.dll) else (echo     ✗ %%f.dll)
)

echo [3/4] 复制 Qt Debug DLL...
for %%f in (Qt5Cored Qt5Guid Qt5Widgetsd) do (
    copy /Y "%QT_BIN%\%%f.dll" "%OUTPUT_DIR%\" >nul 2>&1
    if exist "%OUTPUT_DIR%\%%f.dll" (echo     ✓ %%f.dll) else (echo     ✗ %%f.dll)
)

echo [4/4] 复制 Qt 平台插件...
if not exist "%OUTPUT_DIR%\platforms" mkdir "%OUTPUT_DIR%\platforms"
copy /Y "%QT_PLUGINS%\platforms\qwindowsd.dll" "%OUTPUT_DIR%\platforms\" >nul 2>&1
if exist "%OUTPUT_DIR%\platforms\qwindowsd.dll" (echo     ✓ platforms\qwindowsd.dll) else (echo     ✗ 失败)

echo.
echo ==========================================
echo [完成] 运行时 DLL 复制完成
echo ==========================================
echo.
echo 现在可以运行程序了！
echo.
pause
