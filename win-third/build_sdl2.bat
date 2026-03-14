@chcp 65001 >nul 2>&1
@echo off
REM ============================================================
REM SDL2 2.30.x Windows 编译脚本
REM ============================================================
REM 
REM 功能：
REM   - 下载 SDL2 最新稳定版源码
REM   - 使用 CMake + MSVC 编译 SDL2
REM   - 生成 DLL 动态库
REM   - 安装到 sdl2-install 目录
REM
REM 前置要求：
REM   1. CMake 3.15+
REM   2. Visual Studio 2019/2022
REM
REM ============================================================

setlocal enabledelayedexpansion

echo ========================================
echo SDL2 Windows 编译脚本
echo ========================================
echo.

REM ========================================
REM 配置参数
REM ========================================
set SDL2_VERSION=2.30.9
set SDL2_URL=https://github.com/libsdl-org/SDL/releases/download/release-%SDL2_VERSION%/SDL2-%SDL2_VERSION%.tar.gz
set SOURCE_DIR=%~dp0sdl2-src
set BUILD_DIR=%~dp0sdl2-build
set INSTALL_DIR=%~dp0sdl2-install

REM 检查 CMake
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [错误] 未找到 CMake，请先安装 CMake
    echo 下载地址: https://cmake.org/download/
    pause
    exit /b 1
)

REM ========================================
REM 第 1 步：下载 SDL2 源码
REM ========================================
if not exist "%SOURCE_DIR%" (
    echo [1/4] 正在下载 SDL2 %SDL2_VERSION% 源码...
    echo.
    
    REM 创建临时目录
    if not exist "%~dp0temp" mkdir "%~dp0temp"
    
    REM 下载源码
    echo 下载地址: %SDL2_URL%
    curl -L -o "%~dp0temp\sdl2.tar.gz" %SDL2_URL%
    
    if %ERRORLEVEL% neq 0 (
        echo [错误] 下载失败
        echo.
        echo 备选方案：
        echo   1. 手动下载: https://github.com/libsdl-org/SDL/releases
        echo   2. 解压到: %SOURCE_DIR%
        echo   3. 重新运行此脚本
        pause
        exit /b 1
    )
    
    echo ✓ 下载完成
    echo.
    
    REM 解压源码
    echo 正在解压...
    tar -xzf "%~dp0temp\sdl2.tar.gz" -C "%~dp0temp"
    
    REM 重命名目录
    move "%~dp0temp\SDL2-%SDL2_VERSION%" "%SOURCE_DIR%" >nul
    
    REM 清理临时文件
    del "%~dp0temp\sdl2.tar.gz"
    rmdir "%~dp0temp"
    
    echo ✓ 源码准备完成
    echo.
) else (
    echo [1/4] ✓ SDL2 源码已存在
    echo.
)

REM ========================================
REM 第 2 步：配置 Debug 构建
REM ========================================
echo [2/4] 配置 SDL2 (Debug)...
echo.

set BUILD_DIR_DEBUG=%BUILD_DIR%\debug
set INSTALL_DIR_DEBUG=%INSTALL_DIR%\debug

if exist "%BUILD_DIR_DEBUG%" (
    rmdir /s /q "%BUILD_DIR_DEBUG%"
)
mkdir "%BUILD_DIR_DEBUG%"
cd /d "%BUILD_DIR_DEBUG%"

cmake "%SOURCE_DIR%" ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR_DEBUG%" ^
    -DBUILD_SHARED_LIBS=ON ^
    -DSDL_STATIC=OFF ^
    -DSDL_TEST=OFF

if %ERRORLEVEL% neq 0 (
    echo [错误] SDL2 Debug 配置失败
    pause
    exit /b 1
)

echo ✓ SDL2 Debug 配置完成
echo.

REM ========================================
REM 第 3 步：编译 Debug
REM ========================================
echo [3/4] 编译 SDL2 (Debug)...
cmake --build . --config Debug --parallel

if %ERRORLEVEL% neq 0 (
    echo [错误] SDL2 Debug 编译失败
    pause
    exit /b 1
)

echo ✓ SDL2 Debug 编译完成
echo.

REM 安装 Debug
echo 正在安装 Debug 到: %INSTALL_DIR_DEBUG%
cmake --install . --config Debug

if %ERRORLEVEL% neq 0 (
    echo [错误] SDL2 Debug 安装失败
    pause
    exit /b 1
)

echo ✓ SDL2 Debug 安装完成
echo.

REM ========================================
REM 第 4 步：配置 Release 构建
REM ========================================
echo [4/4] 配置 SDL2 (Release)...
echo.

set BUILD_DIR_RELEASE=%BUILD_DIR%\release
set INSTALL_DIR_RELEASE=%INSTALL_DIR%\release

if exist "%BUILD_DIR_RELEASE%" (
    rmdir /s /q "%BUILD_DIR_RELEASE%"
)
mkdir "%BUILD_DIR_RELEASE%"
cd /d "%BUILD_DIR_RELEASE%"

cmake "%SOURCE_DIR%" ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR_RELEASE%" ^
    -DBUILD_SHARED_LIBS=ON ^
    -DSDL_STATIC=OFF ^
    -DSDL_TEST=OFF

if %ERRORLEVEL% neq 0 (
    echo [错误] SDL2 Release 配置失败
    pause
    exit /b 1
)

echo ✓ SDL2 Release 配置完成
echo.

REM ========================================
REM 第 5 步：编译 Release
REM ========================================
echo [5/4] 编译 SDL2 (Release)...
cmake --build . --config Release --parallel

if %ERRORLEVEL% neq 0 (
    echo [错误] SDL2 Release 编译失败
    pause
    exit /b 1
)

echo ✓ SDL2 Release 编译完成
echo.

REM 安装 Release
echo 正在安装 Release 到: %INSTALL_DIR_RELEASE%
cmake --install . --config Release

if %ERRORLEVEL% neq 0 (
    echo [错误] SDL2 Release 安装失败
    pause
    exit /b 1
)

echo ✓ SDL2 Release 安装完成
echo.

REM ========================================
REM 完成
REM ========================================
echo ========================================
echo ✓ SDL2 %SDL2_VERSION% 编译完成！
echo ========================================
echo.
echo 安装目录:
echo   Debug:   %INSTALL_DIR_DEBUG%
echo   Release: %INSTALL_DIR_RELEASE%
echo.
echo 目录结构:
echo   - 头文件: include\SDL2\*.h
echo   - DLL:    bin\SDL2.dll
echo   - LIB:    lib\SDL2.lib
echo.
echo 下一步：
echo   1. 如果还没编译 FFmpeg，运行 build_ffmpeg.bat
echo   2. 运行 install_all.bat 配置项目
echo.

pause
exit /b 0
