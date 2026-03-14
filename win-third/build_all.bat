@chcp 65001 >nul 2>&1
@echo off
REM ============================================================
REM Windows 第三方库一键编译脚本
REM ============================================================
REM 
REM 功能：自动编译所有 Windows 平台需要的第三方库
REM   1. SDL2 2.30.x
REM   2. SoundTouch (Debug + Release)
REM   3. FFmpeg 8.0.1
REM   4. 配置 CMake 项目
REM
REM ============================================================

setlocal enabledelayedexpansion

echo ========================================
echo Windows 第三方库一键编译
echo ========================================
echo.
echo 将依次编译：
echo   [1] SDL2 2.30.x
echo   [2] SoundTouch
echo   [3] FFmpeg 8.0.1
echo   [4] 配置项目
echo.
echo 预计时间：30-60 分钟（取决于 CPU 性能）
echo.

choice /C YN /M "是否继续"
if %ERRORLEVEL% neq 1 (
    echo 已取消
    exit /b 0
)

echo.
echo ========================================

REM 记录开始时间
set START_TIME=%TIME%

REM ========================================
REM 第 1 步：编译 SDL2
REM ========================================
echo.
echo ========================================
echo [1/4] 编译 SDL2
echo ========================================
echo.

call build_sdl2.bat
if %ERRORLEVEL% neq 0 (
    echo [错误] SDL2 编译失败
    pause
    exit /b 1
)

echo.
echo ✓ SDL2 编译完成
echo.

REM ========================================
REM 第 2 步：编译 SoundTouch
REM ========================================
echo.
echo ========================================
echo [2/4] 编译 SoundTouch
echo ========================================
echo.

REM 下载 SoundTouch 源码（如果需要）
if not exist "soundtouch-src" (
    echo 正在下载 SoundTouch 源码...
    call download_soundtouch.bat
    if %ERRORLEVEL% neq 0 (
        echo [错误] SoundTouch 下载失败
        pause
        exit /b 1
    )
)

REM 编译 SoundTouch
call build_soundtouch_multi.bat
if %ERRORLEVEL% neq 0 (
    echo [错误] SoundTouch 编译失败
    pause
    exit /b 1
)

REM 修复 SoundTouch 头文件
call fix_soundtouch_headers_multi.bat
if %ERRORLEVEL% neq 0 (
    echo [错误] SoundTouch 头文件修复失败
    pause
    exit /b 1
)

echo.
echo ✓ SoundTouch 编译完成
echo.

REM ========================================
REM 第 3 步：编译 FFmpeg
REM ========================================
echo.
echo ========================================
echo [3/4] 编译 FFmpeg
echo ========================================
echo.

call build_ffmpeg.bat
if %ERRORLEVEL% neq 0 (
    echo [错误] FFmpeg 编译失败
    echo.
    echo 提示：FFmpeg 编译需要 MSYS2 环境
    echo   如果未安装，请参考 build_ffmpeg.bat 中的说明
    pause
    exit /b 1
)

echo.
echo ✓ FFmpeg 编译完成
echo.

REM ========================================
REM 第 4 步：配置项目
REM ========================================
echo.
echo ========================================
echo [4/4] 配置 CMake 项目
echo ========================================
echo.

REM 配置 Debug
echo 配置 Debug 构建...
call configure_project_multi.bat
if %ERRORLEVEL% neq 0 (
    echo [警告] 项目配置失败，但库已编译完成
    echo   你可以稍后手动运行 configure_project_multi.bat
)

echo.
echo ✓ 项目配置完成
echo.

REM ========================================
REM 完成
REM ========================================
set END_TIME=%TIME%

echo.
echo ========================================
echo ✓ 所有第三方库编译完成！
echo ========================================
echo.
echo 开始时间: %START_TIME%
echo 结束时间: %END_TIME%
echo.
echo 已编译的库：
echo   ✓ SDL2       -> sdl2-install\
echo   ✓ SoundTouch -> soundtouch-install\
echo   ✓ FFmpeg     -> ffmpeg-install\
echo.
echo 构建目录：
echo   - Debug:   ..\build\vs2022_debug\
echo   - Release: ..\build\vs2022_release\
echo.
echo 下一步：
echo   1. 使用 Visual Studio 打开生成的 .sln 文件
echo   2. 或运行 rebuild_debug.bat 进行命令行编译
echo.
echo 提示：所有第三方库都已本地化，不再依赖 vcpkg！
echo.

pause
exit /b 0
