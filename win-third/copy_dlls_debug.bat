@chcp 65001 >nul 2>&1
@echo off
echo ==========================================
echo 复制运行时 DLL 到 Debug 输出目录
echo ==========================================
echo.

set OUTPUT_DIR=d:\git\hxcvodplayer\build\vs2022_debug\bin\Debug
set WIN_THIRD=%~dp0
set SDL2_BIN=%WIN_THIRD%sdl2-install\release\bin
set FFMPEG_BIN=%WIN_THIRD%ffmpeg-install\bin
set QT_BIN=C:\Qt\5.15.2\msvc2019_64\bin
set QT_PLUGINS=C:\Qt\5.15.2\msvc2019_64\plugins

if not exist "%OUTPUT_DIR%" (
    echo [错误] 输出目录不存在，请先编译项目
    pause
    exit /b 1
)

echo 使用本地编译的库:
echo   SDL2:   %SDL2_BIN%
echo   FFmpeg: %FFMPEG_BIN%
echo   Qt:     %QT_BIN%
echo.
echo 目标: %OUTPUT_DIR%
echo.

echo [1/4] 复制 SDL2.dll...
if exist "%SDL2_BIN%\SDL2.dll" (
    copy /Y "%SDL2_BIN%\SDL2.dll" "%OUTPUT_DIR%\" >nul 2>&1
    if exist "%OUTPUT_DIR%\SDL2.dll" (echo     ✓ SDL2.dll ^(Release 版本^)) else (echo     ✗ 失败)
) else (
    echo     ✗ 未找到 SDL2.dll
    echo     提示: 运行 build_sdl2.bat 编译 SDL2
)

echo [2/4] 复制 FFmpeg DLL...
if exist "%FFMPEG_BIN%" (
    for %%f in (%FFMPEG_BIN%\av*.dll %FFMPEG_BIN%\sw*.dll) do (
        copy /Y "%%f" "%OUTPUT_DIR%\" >nul 2>&1
        for %%n in (%%~nxf) do (
            if exist "%OUTPUT_DIR%\%%n" (echo     ✓ %%n ^(本地编译^)) else (echo     ✗ %%n)
        )
    )
) else (
    echo     ✗ 未找到本地编译的 FFmpeg DLL
    echo     提示: 运行 build_ffmpeg_wsl.sh 编译 FFmpeg
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
