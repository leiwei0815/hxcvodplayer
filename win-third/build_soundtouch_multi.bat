@echo off
chcp 65001 >nul 2>nul
REM ==========================================
REM SoundTouch 源码编译脚本 (Debug + Release)
REM ==========================================

setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set SRC_DIR=%SCRIPT_DIR%soundtouch-src
set BUILD_DIR=%SCRIPT_DIR%soundtouch-build
set INSTALL_DIR=%SCRIPT_DIR%soundtouch-install

echo ==========================================
echo SoundTouch Windows 编译脚本 (Debug + Release)
echo ==========================================
echo.
echo 源码目录: %SRC_DIR%
echo 构建目录: %BUILD_DIR%
echo 安装目录: %INSTALL_DIR%
echo.

REM 检查源码
if not exist "%SRC_DIR%\CMakeLists.txt" (
    echo [错误] 源码未下载
    echo 请先运行: download_soundtouch.bat
    pause
    exit /b 1
)

REM 检查 CMake
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [错误] CMake 未安装
    pause
    exit /b 1
)

REM 清理旧构建
if exist "%BUILD_DIR%" (
    echo [信息] 清理旧的构建目录...
    rmdir /s /q "%BUILD_DIR%"
)
mkdir "%BUILD_DIR%"

if exist "%INSTALL_DIR%" (
    echo [信息] 清理旧的安装目录...
    rmdir /s /q "%INSTALL_DIR%"
)

REM ==========================================
REM 编译 Debug 版本
REM ==========================================
echo.
echo ==========================================
echo [步骤 1/4] 配置 Debug 版本...
echo ==========================================
echo.

mkdir "%BUILD_DIR%\debug"
cd "%BUILD_DIR%\debug"

cmake "%SRC_DIR%" ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%\debug" ^
    -DBUILD_SHARED_LIBS=OFF

if %ERRORLEVEL% NEQ 0 (
    cd "%SCRIPT_DIR%"
    echo [错误] Debug CMake 配置失败
    pause
    exit /b 1
)

echo.
echo ==========================================
echo [步骤 2/4] 编译 Debug 版本...
echo ==========================================
echo.

cmake --build . --config Debug --parallel

if %ERRORLEVEL% NEQ 0 (
    cd "%SCRIPT_DIR%"
    echo [错误] Debug 编译失败
    pause
    exit /b 1
)

echo [信息] 安装 Debug 版本...
cmake --install . --config Debug

echo [成功] Debug 版本完成
echo.

REM ==========================================
REM 编译 Release 版本
REM ==========================================
echo ==========================================
echo [步骤 3/4] 配置 Release 版本...
echo ==========================================
echo.

cd "%BUILD_DIR%"
mkdir release
cd release

cmake "%SRC_DIR%" ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%\release" ^
    -DBUILD_SHARED_LIBS=OFF

if %ERRORLEVEL% NEQ 0 (
    cd "%SCRIPT_DIR%"
    echo [错误] Release CMake 配置失败
    pause
    exit /b 1
)

echo.
echo ==========================================
echo [步骤 4/4] 编译 Release 版本...
echo ==========================================
echo.

cmake --build . --config Release --parallel

if %ERRORLEVEL% NEQ 0 (
    cd "%SCRIPT_DIR%"
    echo [错误] Release 编译失败
    pause
    exit /b 1
)

echo [信息] 安装 Release 版本...
cmake --install . --config Release

cd "%SCRIPT_DIR%"

echo.
echo ==========================================
echo [成功] SoundTouch Debug + Release 编译完成！
echo ==========================================
echo.
echo Debug 安装位置:
echo   头文件: %INSTALL_DIR%\debug\include\
echo   库文件: %INSTALL_DIR%\debug\lib\SoundTouch.lib
echo.
echo Release 安装位置:
echo   头文件: %INSTALL_DIR%\release\include\
echo   库文件: %INSTALL_DIR%\release\lib\SoundTouch.lib
echo.
echo 下一步:
echo   运行 configure_project_multi.bat 配置项目
echo.

pause
