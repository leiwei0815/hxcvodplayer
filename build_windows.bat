@echo off
chcp 65001 >nul 2>nul
REM HXCVodPlayer Windows 构建脚本
REM 使用方法: build_windows.bat [command] [build_type]
REM 命令: vs2019, vs2022, build, clean
REM 类型: debug, release

setlocal enabledelayedexpansion

set COMMAND=%1
set BUILD_TYPE=%2

if "%COMMAND%"=="" set COMMAND=vs2022
if "%BUILD_TYPE%"=="" set BUILD_TYPE=release

echo =========================================
echo HXCVodPlayer Windows 构建脚本
echo 命令: %COMMAND%
echo 类型: %BUILD_TYPE%
echo =========================================
echo.

REM 检查 CMake
where cmake >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo [错误] CMake 未安装或未添加到 PATH
    echo 请从 https://cmake.org/download/ 下载安装 CMake
    exit /b 1
)

REM 检查 vcpkg（推荐的 Windows 依赖管理工具）
if "%VCPKG_ROOT%"=="" (
    echo [警告] VCPKG_ROOT 环境变量未设置
    echo 推荐安装 vcpkg 来管理依赖: https://github.com/microsoft/vcpkg
    echo.
) else (
    echo [信息] 检测到 vcpkg: %VCPKG_ROOT%
    set CMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
    echo [信息] 使用 vcpkg toolchain: !CMAKE_TOOLCHAIN_FILE!
    echo.
)

REM 设置构建类型
set CMAKE_BUILD_TYPE=Release
if "%BUILD_TYPE%"=="debug" set CMAKE_BUILD_TYPE=Debug

goto :%COMMAND%
if %ERRORLEVEL% NEQ 0 (
    echo [错误] 未知命令: %COMMAND%
    echo 用法: %0 {vs2019^|vs2022^|build^|clean} [debug^|release]
    exit /b 1
)

:vs2019
echo [信息] 生成 Visual Studio 2019 项目...
set BUILD_DIR=build\vs2019_%BUILD_TYPE%

REM 保存项目根目录（在 cd 之前）
set PROJECT_ROOT=%~dp0

REM 自动清理旧的构建目录
if exist "%BUILD_DIR%" (
    echo [清理] 删除旧的构建目录: %BUILD_DIR%
    rmdir /s /q "%BUILD_DIR%" >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        echo [清理] ✓ 构建目录已清理
    ) else (
        echo [警告] 构建目录可能仍在使用中，尝试继续...
    )
    echo.
)

mkdir "%BUILD_DIR%" 2>nul
cd "%BUILD_DIR%"

REM 检查本地 Qt 安装
if exist "C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" (
    echo [信息] 使用本地 Qt: C:\Qt\5.15.2\msvc2019_64
    set "Qt5_DIR=C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5"
)

if "%CMAKE_TOOLCHAIN_FILE%"=="" (
    cmake ..\.. ^
        -G "Visual Studio 16 2019" ^
        -A x64 ^
        -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE% ^
        -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64" ^
        -DQt5_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" ^
        -DBUILD_DESKTOP=ON
) else (
    cmake ..\.. ^
        -G "Visual Studio 16 2019" ^
        -A x64 ^
        -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE% ^
        -DCMAKE_TOOLCHAIN_FILE="!CMAKE_TOOLCHAIN_FILE!" ^
        -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64" ^
        -DQt5_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" ^
        -DBUILD_DESKTOP=ON
)

if %ERRORLEVEL% NEQ 0 (
    cd ..\..
    echo.
    echo [错误] CMake 配置失败！
    echo.
    echo 请确保已安装以下依赖:
    echo   1. Qt5 (https://www.qt.io/download)
    echo   2. FFmpeg (通过 vcpkg: vcpkg install ffmpeg:x64-windows)
    echo   3. SDL2 (通过 vcpkg: vcpkg install sdl2:x64-windows)
    echo   4. SoundTouch (可选，通过 vcpkg: vcpkg install soundtouch:x64-windows)
    echo.
    echo 或者使用 vcpkg 一次性安装所有依赖:
    echo   vcpkg install ffmpeg:x64-windows sdl2:x64-windows soundtouch:x64-windows qt5-base:x64-windows qt5-multimedia:x64-windows
    echo.
    exit /b 1
)

cd ..\..
echo.
echo [成功] Visual Studio 2019 项目生成完成！
echo 项目位置: %BUILD_DIR%\HXCVodPlayer.sln
echo 运行: start %BUILD_DIR%\HXCVodPlayer.sln
echo.
goto end

:vs2022
echo [信息] 生成 Visual Studio 2022 项目...
set BUILD_DIR=build\vs2022_%BUILD_TYPE%

REM 保存项目根目录（在 cd 之前）
set PROJECT_ROOT=%~dp0

REM 自动清理旧的构建目录
if exist "%BUILD_DIR%" (
    echo [清理] 删除旧的构建目录: %BUILD_DIR%
    rmdir /s /q "%BUILD_DIR%" >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        echo [清理] ✓ 构建目录已清理
    ) else (
        echo [警告] 构建目录可能仍在使用中，尝试继续...
    )
    echo.
)

mkdir "%BUILD_DIR%" 2>nul
cd "%BUILD_DIR%"

REM 设置本地库目录（使用保存的项目根目录）
set WIN_THIRD_DIR=%PROJECT_ROOT%win-third

if "%BUILD_TYPE%"=="debug" (
    set SOUNDTOUCH_DIR=%WIN_THIRD_DIR%\soundtouch-install\debug
    set SDL2_DIR=%WIN_THIRD_DIR%\sdl2-install\debug
) else (
    set SOUNDTOUCH_DIR=%WIN_THIRD_DIR%\soundtouch-install\release
    set SDL2_DIR=%WIN_THIRD_DIR%\sdl2-install\release
)

set FFMPEG_DIR=%WIN_THIRD_DIR%\ffmpeg-install

echo [信息] 使用本地编译的库:
echo   Qt:         C:\Qt\5.15.2\msvc2019_64
echo   FFmpeg:     !FFMPEG_DIR!
echo   SDL2:       !SDL2_DIR!
echo   SoundTouch: !SOUNDTOUCH_DIR!
echo.

REM 构建 CMAKE_PREFIX_PATH，本地库优先
set "CMAKE_PREFIX_PATH=!FFMPEG_DIR!;!SDL2_DIR!;!SOUNDTOUCH_DIR!;C:\Qt\5.15.2\msvc2019_64"

REM 检查必需库
set MISSING_LIBS=
if not exist "!FFMPEG_DIR!\include" (
    echo [警告] 未找到本地 FFmpeg
    echo [提示] 运行 'cd win-third ^&^& .\build_ffmpeg_wsl.sh' 编译 FFmpeg
    set MISSING_LIBS=!MISSING_LIBS! FFmpeg
)
if not exist "!SDL2_DIR!\include" (
    echo [警告] 未找到本地 SDL2
    echo [提示] 运行 'cd win-third ^&^& .\build_sdl2.bat' 编译 SDL2
    set MISSING_LIBS=!MISSING_LIBS! SDL2
)
if not exist "!SOUNDTOUCH_DIR!\include" (
    echo [警告] 未找到本地 SoundTouch
    echo [提示] 运行 'cd win-third ^&^& .\install_all.bat' 编译 SoundTouch
    set MISSING_LIBS=!MISSING_LIBS! SoundTouch
)

if not "!MISSING_LIBS!"=="" (
    echo.
    echo [警告] 缺少本地库:!MISSING_LIBS!
    echo [信息] 将尝试使用 vcpkg 的库作为后备
    echo.
    if not "%VCPKG_ROOT%"=="" (
        set "CMAKE_PREFIX_PATH=!CMAKE_PREFIX_PATH!;C:\vcpkg\installed\x64-windows"
    )
)

cmake ..\.. ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE% ^
    -DCMAKE_PREFIX_PATH="!CMAKE_PREFIX_PATH!" ^
    -DQt5_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" ^
    -DBUILD_DESKTOP=ON

REM 注意：这里故意不使用 CMAKE_TOOLCHAIN_FILE，让本地库优先

if %ERRORLEVEL% NEQ 0 (
    cd ..\..
    echo.
    echo [错误] CMake 配置失败！
    echo.
    echo 请确保已安装以下依赖:
    echo   1. Qt5 (https://www.qt.io/download)
    echo   2. FFmpeg (通过 vcpkg: vcpkg install ffmpeg:x64-windows)
    echo   3. SDL2 (通过 vcpkg: vcpkg install sdl2:x64-windows)
    echo   4. SoundTouch (通过 'cd win-third ^&^& .\install_all.bat' 编译)
    echo.
    exit /b 1
)

cd ..\..
echo.
echo [成功] Visual Studio 2022 项目生成完成！
echo 项目位置: %BUILD_DIR%\HXCVodPlayer.sln
echo 运行: start %BUILD_DIR%\HXCVodPlayer.sln
echo.
goto end

:build
echo [信息] 构建 Windows Desktop 版本...
set BUILD_DIR=build\windows_%BUILD_TYPE%

REM 保存项目根目录（在 cd 之前）
set PROJECT_ROOT=%~dp0

REM 自动清理旧的构建目录
if exist "%BUILD_DIR%" (
    echo [清理] 删除旧的构建目录: %BUILD_DIR%
    rmdir /s /q "%BUILD_DIR%" >nul 2>&1
    if %ERRORLEVEL% equ 0 (
        echo [清理] ✓ 构建目录已清理
    ) else (
        echo [警告] 构建目录可能仍在使用中，尝试继续...
    )
    echo.
)

mkdir "%BUILD_DIR%" 2>nul
cd "%BUILD_DIR%"

echo [信息] 运行 CMake 配置...
if "%CMAKE_TOOLCHAIN_FILE%"=="" (
    cmake ..\.. ^
        -G "Visual Studio 17 2022" ^
        -A x64 ^
        -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE% ^
        -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64" ^
        -DQt5_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" ^
        -DBUILD_DESKTOP=ON
) else (
    cmake ..\.. ^
        -G "Visual Studio 17 2022" ^
        -A x64 ^
        -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE% ^
        -DCMAKE_TOOLCHAIN_FILE="!CMAKE_TOOLCHAIN_FILE!" ^
        -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64" ^
        -DQt5_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" ^
        -DBUILD_DESKTOP=ON
)

if %ERRORLEVEL% NEQ 0 (
    cd ..\..
    echo [错误] CMake 配置失败！
    exit /b 1
)

echo [信息] 编译中...
cmake --build . --config %CMAKE_BUILD_TYPE% --parallel

if %ERRORLEVEL% NEQ 0 (
    cd ..\..
    echo [错误] 编译失败！
    exit /b 1
)

cd ..\..
echo.
echo [成功] Windows Desktop 版本构建完成！
echo 可执行文件: %BUILD_DIR%\bin\%CMAKE_BUILD_TYPE%\HXCVodPlayer.exe
echo.
goto end

:clean
echo [信息] 清理构建文件...
if exist build (
    rmdir /s /q build
    echo [信息] 清理完成
) else (
    echo [信息] 无需清理
)
goto end

:end
endlocal
exit /b 0
