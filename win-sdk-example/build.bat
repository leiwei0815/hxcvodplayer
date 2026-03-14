@echo off
chcp 65001 >nul 2>&1
setlocal EnableDelayedExpansion

echo ========================================
echo   HXCPlayer SDK 测试项目构建工具
echo ========================================
echo.

set PROJECT_ROOT=%~dp0
set SDK_DIR=%PROJECT_ROOT%..\build\win-sdk-Release\HXCPlayerSDK
set BUILD_DIR=%PROJECT_ROOT%build
set QT_DIR=C:\Qt\5.15.2\msvc2019_64

REM 检查 SDK 是否存在
if not exist "%SDK_DIR%" (
    echo [错误] SDK 未找到: %SDK_DIR%
    echo.
    echo 请先构建 SDK:
    echo   cd win-sdk
    echo   build_sdk.bat
    echo.
    exit /b 1
)

echo [✓] SDK 已找到: %SDK_DIR%
echo.

REM 创建并清理构建目录
if exist "%BUILD_DIR%" (
    echo [信息] 清理旧的构建目录...
    rmdir /s /q "%BUILD_DIR%"
)
mkdir "%BUILD_DIR%"

REM 配置项目
echo [信息] 配置 CMake 项目...
cd "%BUILD_DIR%"

cmake .. ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DQt5_DIR="%QT_DIR%\lib\cmake\Qt5"

if !ERRORLEVEL! neq 0 (
    echo [错误] CMake 配置失败
    cd "%PROJECT_ROOT%"
    exit /b 1
)

echo.
echo [✓] CMake 配置成功
echo.

REM 构建项目
echo [信息] 构建项目...
cmake --build . --config Release

if !ERRORLEVEL! neq 0 (
    echo [错误] 构建失败
    cd "%PROJECT_ROOT%"
    exit /b 1
)

cd "%PROJECT_ROOT%"

echo.
echo ========================================
echo   构建成功！
echo ========================================
echo.
echo 可执行文件位置:
echo   %BUILD_DIR%\bin\Release\SDKTestPlayer.exe
echo.
echo 运行测试:
echo   run.bat
echo.
echo 或者打开 Visual Studio 项目:
echo   %BUILD_DIR%\SDKTestPlayer.sln
echo.

exit /b 0
