@echo off
chcp 65001 >nul 2>nul
REM ==========================================
REM 快速复制 Debug DLL 并运行
REM ==========================================

echo 正在复制 Debug 运行时 DLL...
call "%~dp0copy_dlls.bat" debug

if %ERRORLEVEL% EQU 0 (
    echo.
    echo DLL 复制成功！
    
    choice /C YN /M "是否立即运行程序"
    if !ERRORLEVEL! EQU 1 (
        start "" "%~dp0..\build\vs2022_debug\desktop\Debug\HXCVodPlayer.exe"
    )
)
