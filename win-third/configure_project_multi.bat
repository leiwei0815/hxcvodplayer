@echo off
chcp 65001 >nul 2>nul
REM ==========================================
REM 配置项目使用 Debug + Release SoundTouch
REM ==========================================

setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set SOUNDTOUCH_DEBUG=%SCRIPT_DIR%soundtouch-install\debug
set SOUNDTOUCH_RELEASE=%SCRIPT_DIR%soundtouch-install\release
set PROJECT_DIR=%SCRIPT_DIR%..

echo ==========================================
echo 配置 HXCVodPlayer - Debug + Release
echo ==========================================
echo.

REM 检查 Debug 版本
if not exist "%SOUNDTOUCH_DEBUG%\include\soundtouch\SoundTouch.h" (
    echo [错误] SoundTouch Debug 版本未编译！
    echo.
    echo 请先运行: build_soundtouch_multi.bat
    echo.
    pause
    exit /b 1
)
echo [✓] SoundTouch Debug: %SOUNDTOUCH_DEBUG%

REM 检查 Release 版本
if not exist "%SOUNDTOUCH_RELEASE%\include\soundtouch\SoundTouch.h" (
    echo [错误] SoundTouch Release 版本未编译！
    echo.
    echo 请先运行: build_soundtouch_multi.bat
    echo.
    pause
    exit /b 1
)
echo [✓] SoundTouch Release: %SOUNDTOUCH_RELEASE%
echo.

REM 修复头文件
echo [信息] 修复头文件...
call "%SCRIPT_DIR%fix_soundtouch_headers_multi.bat" >nul 2>&1
echo.

REM ==========================================
REM 生成 Debug 项目
REM ==========================================
echo ==========================================
echo 生成 Debug 项目
echo ==========================================
echo.

cd "%PROJECT_DIR%"
set BUILD_DIR=build\vs2022_debug
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%" 2>nul
)
mkdir "%BUILD_DIR%" 2>nul
cd "%BUILD_DIR%"

cmake ..\.. ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_PREFIX_PATH="C:\vcpkg\installed\x64-windows;C:\Qt\5.15.2\msvc2019_64;%SOUNDTOUCH_DEBUG%" ^
    -DQt5_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" ^
    -DSOUNDTOUCH_INCLUDE_DIR="%SOUNDTOUCH_DEBUG%\include" ^
    -DSOUNDTOUCH_LIBRARY="%SOUNDTOUCH_DEBUG%\lib\SoundTouch.lib" ^
    -DBUILD_DESKTOP=ON

if %ERRORLEVEL% NEQ 0 (
    cd "%PROJECT_DIR%"
    echo [错误] Debug CMake 配置失败
    pause
    exit /b 1
)

cd "%PROJECT_DIR%"
echo [成功] Debug 项目生成完成
echo.

REM ==========================================
REM 生成 Release 项目
REM ==========================================
echo ==========================================
echo 生成 Release 项目
echo ==========================================
echo.

set BUILD_DIR=build\vs2022_release
if exist "%BUILD_DIR%" (
    rmdir /s /q "%BUILD_DIR%" 2>nul
)
mkdir "%BUILD_DIR%" 2>nul
cd "%BUILD_DIR%"

cmake ..\.. ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="C:\vcpkg\installed\x64-windows;C:\Qt\5.15.2\msvc2019_64;%SOUNDTOUCH_RELEASE%" ^
    -DQt5_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" ^
    -DSOUNDTOUCH_INCLUDE_DIR="%SOUNDTOUCH_RELEASE%\include" ^
    -DSOUNDTOUCH_LIBRARY="%SOUNDTOUCH_RELEASE%\lib\SoundTouch.lib" ^
    -DBUILD_DESKTOP=ON

if %ERRORLEVEL% NEQ 0 (
    cd "%PROJECT_DIR%"
    echo [错误] Release CMake 配置失败
    pause
    exit /b 1
)

cd "%PROJECT_DIR%"

echo.
echo ==========================================
echo [成功] 项目配置完成！
echo ==========================================
echo.
echo Debug 项目: build\vs2022_debug\HXCVodPlayer.sln
echo Release 项目: build\vs2022_release\HXCVodPlayer.sln
echo.

choice /C 123 /M "打开哪个项目: 1=Debug, 2=Release, 3=不打开"
if !ERRORLEVEL! EQU 1 (
    start build\vs2022_debug\HXCVodPlayer.sln
) else if !ERRORLEVEL! EQU 2 (
    start build\vs2022_release\HXCVodPlayer.sln
)

echo.
pause
