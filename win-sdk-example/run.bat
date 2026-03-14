@echo off
chcp 65001 >nul 2>&1
setlocal

set PROJECT_ROOT=%~dp0
set BUILD_DIR=%PROJECT_ROOT%build
set EXE_PATH=%BUILD_DIR%\bin\Release\SDKTestPlayer.exe

if not exist "%EXE_PATH%" (
    echo [错误] 可执行文件未找到: %EXE_PATH%
    echo.
    echo 请先构建项目:
    echo   build.bat
    echo.
    exit /b 1
)

echo [信息] 运行 SDK 测试程序...
echo.

start "" "%EXE_PATH%"

exit /b 0
