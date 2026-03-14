@chcp 65001 >nul 2>&1
@echo off
REM ============================================================
REM HXCPlayer Windows DLL SDK 构建脚本
REM ============================================================
REM 
REM 用法:
REM   build_sdk.bat              - Release 构建（默认，清理旧文件）
REM   build_sdk.bat debug        - Debug 构建（清理旧文件）
REM   build_sdk.bat --no-clean   - Release 构建（增量编译）
REM   build_sdk.bat debug --no-clean - Debug 增量编译
REM
REM ============================================================

setlocal enabledelayedexpansion

echo ========================================
echo HXCPlayer Windows DLL SDK 构建
echo ========================================
echo.

REM 配置参数
set BUILD_TYPE=Release
set BUILD_DIR=build\win-sdk-%BUILD_TYPE%
set VCPKG_ROOT=C:\vcpkg
set QT_DIR=C:\Qt\5.15.2\msvc2019_64
set CLEAN_BUILD=1

REM 检查参数
if "%1"=="debug" (
    set BUILD_TYPE=Debug
    set BUILD_DIR=build\win-sdk-%BUILD_TYPE%
    echo [配置] 构建类型: Debug
) else if "%1"=="--no-clean" (
    set CLEAN_BUILD=0
    echo [配置] 保留现有构建目录
    echo [配置] 构建类型: Release
) else (
    echo [配置] 构建类型: Release
)

REM 支持 debug --no-clean 组合
if "%2"=="--no-clean" set CLEAN_BUILD=0
if "%2"=="debug" (
    set BUILD_TYPE=Debug
    set BUILD_DIR=build\win-sdk-%BUILD_TYPE%
)

echo [配置] 构建目录: %BUILD_DIR%
echo.

REM 检查必要的工具
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [错误] 未找到 CMake！请确保 CMake 已安装并添加到 PATH
    exit /b 1
)

REM 返回项目根目录
cd ..

REM ========================================
REM 清理旧的构建目录
REM ========================================
if "%CLEAN_BUILD%"=="1" (
    if exist "%BUILD_DIR%" (
        echo [清理] 正在删除旧的构建目录: %BUILD_DIR%
        rmdir /s /q "%BUILD_DIR%" >nul 2>&1
        if %ERRORLEVEL% equ 0 (
            echo [清理] ✓ 构建目录已清理
        ) else (
            echo [警告] 构建目录可能仍在使用中，尝试继续...
        )
        echo.
    )
) else (
    echo [跳过] 保留现有构建目录（增量编译）
    echo.
)

REM 创建构建目录
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM 进入构建目录
cd /d "%BUILD_DIR%"

echo ========================================
echo 第 1 步: 配置 CMake
echo ========================================
echo.

REM 配置 CMake（构建 DLL）
cmake ..\.. ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DCMAKE_PREFIX_PATH="%VCPKG_ROOT%\installed\x64-windows;%QT_DIR%" ^
    -DQt5_DIR="%QT_DIR%\lib\cmake\Qt5" ^
    -DBUILD_SHARED_LIBS=ON ^
    -DBUILD_DESKTOP=OFF

if %ERRORLEVEL% neq 0 (
    echo [错误] CMake 配置失败！
    cd ..\..\win-sdk
    exit /b 1
)

echo.
echo ========================================
echo 第 2 步: 编译 DLL
echo ========================================
echo.

REM 编译
cmake --build . --config %BUILD_TYPE% --parallel

if %ERRORLEVEL% neq 0 (
    echo [错误] 编译失败！
    cd ..\..\win-sdk
    exit /b 1
)

echo.
echo ========================================
echo 第 3 步: 打包 SDK
echo ========================================
echo.

REM 打包 SDK
cmake --build . --config %BUILD_TYPE% --target package_sdk

if %ERRORLEVEL% neq 0 (
    echo [错误] 打包 SDK 失败！
    cd ..\..\win-sdk
    exit /b 1
)

cd ..\..\win-sdk

echo.
echo ========================================
echo ✅ SDK 构建成功！
echo ========================================
echo.
echo SDK 位置: %BUILD_DIR%\HXCPlayerSDK\
echo.
echo 目录结构:
echo   HXCPlayerSDK\
echo     ├── include\          (头文件)
echo     ├── lib\              (导入库 .lib)
echo     ├── bin\              (DLL 文件)
echo     ├── example\          (示例代码)
echo     ├── docs\             (文档)
echo     └── README.md
echo.

REM 显示文件列表
echo 包含的文件:
dir /b "%BUILD_DIR%\HXCPlayerSDK\include"
dir /b "%BUILD_DIR%\HXCPlayerSDK\lib"
dir /b "%BUILD_DIR%\HXCPlayerSDK\bin" | findstr /i "hxcplayer"

echo.
echo 可以将整个 HXCPlayerSDK 目录分发给用户使用
echo.

pause
