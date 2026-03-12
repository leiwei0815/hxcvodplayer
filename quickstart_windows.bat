@echo off
chcp 65001 >nul 2>nul
REM HXCVodPlayer Windows 快速开始脚本
REM 此脚本会检查并安装依赖，然后生成 Visual Studio 项目

echo ==========================================
echo HXCVodPlayer Windows 快速开始
echo ==========================================
echo.

REM 检查 PowerShell
where powershell >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [错误] PowerShell 未找到
    exit /b 1
)

REM 询问是否安装依赖
echo 是否需要安装/更新依赖? (需要 vcpkg)
echo.
choice /C YN /M "安装依赖"
if %ERRORLEVEL% EQU 1 (
    echo.
    echo [信息] 开始安装依赖...
    echo [信息] 这可能需要较长时间，请耐心等待...
    echo.
    powershell -ExecutionPolicy Bypass -File setup_windows_deps.ps1
    
    if %ERRORLEVEL% NEQ 0 (
        echo.
        echo [错误] 依赖安装失败
        pause
        exit /b 1
    )
    echo.
)

REM 生成 Visual Studio 项目
echo.
echo [信息] 生成 Visual Studio 项目...
echo.
call build_windows.bat vs2022 release

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [错误] 项目生成失败
    pause
    exit /b 1
)

REM 询问是否打开项目
echo.
choice /C YN /M "是否打开 Visual Studio 项目"
if %ERRORLEVEL% EQU 1 (
    start build\vs2022_release\YXVodPlayer.sln
)

echo.
echo [完成] 设置完成！
echo.
pause
