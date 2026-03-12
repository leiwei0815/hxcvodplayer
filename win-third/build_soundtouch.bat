@echo off
chcp 65001 >nul 2>nul
REM ==========================================
REM SoundTouch 源码编译脚本 (Windows)
REM ==========================================

setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set SRC_DIR=%SCRIPT_DIR%soundtouch-src
set BUILD_DIR=%SCRIPT_DIR%soundtouch-build
set INSTALL_DIR=%SCRIPT_DIR%soundtouch-install

echo ==========================================
echo SoundTouch Windows 编译脚本
echo ==========================================
echo.
echo 源码目录: %SRC_DIR%
echo 构建目录: %BUILD_DIR%
echo 安装目录: %INSTALL_DIR%
echo.

REM 检查 Git
where git >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [错误] Git 未安装
    pause
    exit /b 1
)

REM 检查 CMake
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [错误] CMake 未安装
    echo 请从 https://cmake.org/download/ 下载安装
    pause
    exit /b 1
)

REM 步骤 1: 下载源码
if not exist "%SRC_DIR%\CMakeLists.txt" (
    echo ==========================================
    echo [步骤 1/4] 下载 SoundTouch 源码...
    echo ==========================================
    echo.
    
    call "%SCRIPT_DIR%download_soundtouch.bat"
    
    if %ERRORLEVEL% NEQ 0 (
        echo [错误] 下载源码失败
        pause
        exit /b 1
    )
    
    echo [成功] 源码下载完成
    echo.
) else (
    echo ==========================================
    echo [步骤 1/4] 源码已存在，跳过下载
    echo ==========================================
    echo.
    
    choice /C YN /M "是否更新到最新版本"
    if !ERRORLEVEL! EQU 1 (
        cd "%SRC_DIR%"
        git pull 2>nul
        if !ERRORLEVEL! NEQ 0 (
            echo [警告] 更新失败，使用现有版本
        )
        cd "%SCRIPT_DIR%"
    )
    echo.
)

REM 步骤 2: 配置 CMake
echo ==========================================
echo [步骤 2/4] 配置 CMake...
echo ==========================================
echo.

if exist "%BUILD_DIR%" (
    echo 清理旧的构建目录...
    rmdir /s /q "%BUILD_DIR%"
)
mkdir "%BUILD_DIR%"

cd "%BUILD_DIR%"

echo 运行 CMake 配置...
echo.

cmake "%SRC_DIR%" ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
    -DBUILD_SHARED_LIBS=OFF

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [错误] CMake 配置失败
    cd "%SCRIPT_DIR%"
    pause
    exit /b 1
)

echo.
echo [成功] CMake 配置完成
echo.

REM 步骤 3: 编译
echo ==========================================
echo [步骤 3/4] 编译 SoundTouch...
echo ==========================================
echo.

echo 编译 Release 版本...
cmake --build . --config Release --parallel

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [错误] 编译失败
    cd "%SCRIPT_DIR%"
    pause
    exit /b 1
)

echo.
echo [成功] 编译完成
echo.

REM 步骤 4: 安装
echo ==========================================
echo [步骤 4/4] 安装到目标目录...
echo ==========================================
echo.

cmake --install . --config Release

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [错误] 安装失败
    cd "%SCRIPT_DIR%"
    pause
    exit /b 1
)

cd "%SCRIPT_DIR%"

echo.
echo ==========================================
echo [成功] SoundTouch 编译安装完成！
echo ==========================================
echo.
echo 安装位置:
echo   头文件: %INSTALL_DIR%\include\
echo   库文件: %INSTALL_DIR%\lib\
echo   二进制: %INSTALL_DIR%\bin\
echo.
echo 下一步:
echo   运行 configure_project.bat 配置项目使用本地 SoundTouch
echo.

pause
