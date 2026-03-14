@echo off
chcp 65001 >nul 2>&1
echo 检查 FFmpeg DLL 依赖关系...
echo.

set FFMPEG_BIN=D:\git\hxcvodplayer\win-third\ffmpeg-install\bin
set TEST_DIR=D:\git\hxcvodplayer\win-sdk-example\build\bin\Debug

echo FFmpeg DLL 位置: %FFMPEG_BIN%
echo 测试项目目录: %TEST_DIR%
echo.

REM 使用 dumpbin 检查依赖（需要 VS 开发者命令提示符）
where dumpbin >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [使用 dumpbin 检查 avcodec-62.dll 的依赖]
    echo.
    dumpbin /dependents "%FFMPEG_BIN%\avcodec-62.dll" | findstr /i "dll"
    echo.
    echo.
    echo [检查 avpriv_emms_asm 符号是否存在]
    dumpbin /exports "%FFMPEG_BIN%\avutil-60.dll" | findstr /i "emms"
) else (
    echo [警告] 未在 VS 开发者命令提示符中运行，无法使用 dumpbin
    echo 请在 "x64 Native Tools Command Prompt for VS 2022" 中运行此脚本
)

echo.
pause
