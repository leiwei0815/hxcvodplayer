@echo off
chcp 65001 >nul 2>nul
REM ==========================================
REM 配置项目使用本地编译的 SoundTouch
REM ==========================================

setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set SOUNDTOUCH_DIR=%SCRIPT_DIR%soundtouch-install
set PROJECT_DIR=%SCRIPT_DIR%..

echo ==========================================
echo 配置项目使用本地 SoundTouch
echo ==========================================
echo.

REM 检查 SoundTouch 是否已编译
if not exist "%SOUNDTOUCH_DIR%\include\soundtouch\SoundTouch.h" (
    echo [错误] SoundTouch 未编译！
    echo.
    echo 请先运行: build_soundtouch.bat
    echo.
    pause
    exit /b 1
)

echo [信息] 找到 SoundTouch 安装目录
echo        %SOUNDTOUCH_DIR%
echo.

REM 修复 SoundTouch 头文件的 MSVC 兼容性问题
echo [信息] 修复 SoundTouch 头文件...
call "%SCRIPT_DIR%fix_soundtouch_headers.bat" >nul 2>&1
echo.

REM 检查库文件
if exist "%SOUNDTOUCH_DIR%\lib\SoundTouch.lib" (
    echo [✓] 找到静态库: SoundTouch.lib
) else (
    echo [警告] 未找到 SoundTouch.lib
)

if exist "%SOUNDTOUCH_DIR%\lib\soundtouch.lib" (
    echo [✓] 找到静态库: soundtouch.lib
) else (
    echo [警告] 未找到 soundtouch.lib
)
echo.

REM 清理旧的构建
echo [信息] 清理旧的构建目录...
if exist "%PROJECT_DIR%\build\vs2022_release" (
    rmdir /s /q "%PROJECT_DIR%\build\vs2022_release"
)
echo.

REM 生成新项目
echo ==========================================
echo 生成 Visual Studio 项目
echo ==========================================
echo.

cd "%PROJECT_DIR%"

set BUILD_DIR=build\vs2022_release
mkdir "%BUILD_DIR%" 2>nul
cd "%BUILD_DIR%"

echo [信息] 使用本地 Qt: C:\Qt\5.15.2\msvc2019_64
echo [信息] 使用 vcpkg 依赖: C:\vcpkg\installed\x64-windows
echo [信息] 使用本地 SoundTouch: %SOUNDTOUCH_DIR%
echo.

cmake ..\.. ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="C:\vcpkg\installed\x64-windows;C:\Qt\5.15.2\msvc2019_64;%SOUNDTOUCH_DIR%" ^
    -DQt5_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" ^
    -DSOUNDTOUCH_INCLUDE_DIR="%SOUNDTOUCH_DIR%\include" ^
    -DSOUNDTOUCH_LIBRARY="%SOUNDTOUCH_DIR%\lib\SoundTouch.lib" ^
    -DBUILD_DESKTOP=ON

if %ERRORLEVEL% NEQ 0 (
    cd "%PROJECT_DIR%"
    echo.
    echo [错误] CMake 配置失败
    pause
    exit /b 1
)

cd "%PROJECT_DIR%"

echo.
echo ==========================================
echo [成功] 项目配置完成！
echo ==========================================
echo.
echo Visual Studio 项目: build\vs2022_release\YXVodPlayer.sln
echo.

choice /C YN /M "是否打开 Visual Studio 项目"
if !ERRORLEVEL! EQU 1 (
    start build\vs2022_release\YXVodPlayer.sln
    echo.
    echo 在 Visual Studio 中：
    echo   1. 重新生成解决方案 (Ctrl+Shift+B)
    echo   2. 运行项目 (F5)
)

echo.
pause
