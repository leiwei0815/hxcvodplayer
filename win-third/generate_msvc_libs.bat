@chcp 65001 >nul 2>&1
@echo off
REM ============================================================
REM 为 FFmpeg DLL 生成 MSVC 导入库
REM ============================================================

setlocal enabledelayedexpansion

echo ========================================
echo 生成 MSVC 导入库
echo ========================================
echo.

REM 检查 Visual Studio 工具
where lib.exe >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [错误] 未找到 lib.exe
    echo.
    echo 请在 "Visual Studio 开发者命令提示符" 中运行此脚本
    echo   开始菜单 -^> Visual Studio 2022 -^> x64 Native Tools Command Prompt
    echo.
    pause
    exit /b 1
)

where dumpbin.exe >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [错误] 未找到 dumpbin.exe
    echo 请在 Visual Studio 开发者命令提示符中运行
    pause
    exit /b 1
)

echo ✓ Visual Studio 工具检测成功
echo.

REM 设置路径
set INSTALL_DIR=%~dp0ffmpeg-install
set BIN_DIR=%INSTALL_DIR%\bin
set LIB_DIR=%INSTALL_DIR%\lib

if not exist "%BIN_DIR%" (
    echo [错误] 找不到 FFmpeg bin 目录: %BIN_DIR%
    echo 请先运行 build_ffmpeg_wsl.sh 编译 FFmpeg
    pause
    exit /b 1
)

if not exist "%LIB_DIR%" mkdir "%LIB_DIR%"

echo FFmpeg 目录: %INSTALL_DIR%
echo   BIN: %BIN_DIR%
echo   LIB: %LIB_DIR%
echo.
echo 开始处理 DLL...
echo.

cd /d "%BIN_DIR%"

REM 处理 avcodec
call :process_lib avcodec 62

REM 处理 avformat
call :process_lib avformat 62

REM 处理 avutil
call :process_lib avutil 60

REM 处理 swscale
call :process_lib swscale 9

REM 处理 swresample
call :process_lib swresample 6

REM 处理 avdevice (可选)
call :process_lib avdevice 62

REM 处理 avfilter (可选)
call :process_lib avfilter 11

echo.
echo ========================================
echo ✓ MSVC 导入库生成完成
echo ========================================
echo.
echo 生成的文件:
if exist "%LIB_DIR%\*.lib" (
    dir /b "%LIB_DIR%\*.lib"
) else (
    echo   (未生成任何 .lib 文件)
)
echo.
echo 现在可以在 MSVC 项目中使用这些 .lib 文件了
echo.

pause
exit /b 0

REM ============================================================
REM 子程序：处理单个库
REM 参数：%1 = 库名 (例如 avcodec)
REM        %2 = 版本号 (例如 62)
REM ============================================================
:process_lib
set LIB_NAME=%1
set LIB_VERSION=%2
set DLL_NAME=%LIB_NAME%-%LIB_VERSION%

echo 处理 %LIB_NAME%-%LIB_VERSION%...

REM 查找对应的 DLL 文件
if not exist "%DLL_NAME%.dll" (
    echo   - 跳过: %DLL_NAME% ^(未找到 DLL^)
    echo.
    goto :eof
)

echo   找到: %DLL_NAME%.dll

REM 导出符号
dumpbin /exports "%DLL_NAME%.dll" > "%LIB_NAME%.exports" 2>nul

if %ERRORLEVEL% neq 0 (
    echo   ✗ 失败: 无法导出符号
    del "%LIB_NAME%.exports" 2>nul
    echo.
    goto :eof
)

REM 创建 DEF 文件
echo LIBRARY %DLL_NAME% > "%LIB_NAME%.def"
echo EXPORTS >> "%LIB_NAME%.def"

REM 提取函数名（跳过前 19 行头部，取第 4 列）
for /f "skip=19 tokens=4" %%S in (%LIB_NAME%.exports) do (
    if not "%%S"=="" (
        if not "%%S"=="name" (
            echo %%S >> "%LIB_NAME%.def"
        )
    )
)

REM 生成 LIB（不带版本号的 .lib 文件）
lib /def:"%LIB_NAME%.def" /out:"%LIB_DIR%\%LIB_NAME%.lib" /machine:x64 >nul 2>&1

if %ERRORLEVEL% equ 0 (
    echo   ✓ 生成: %LIB_DIR%\%LIB_NAME%.lib
) else (
    echo   ✗ 失败: %LIB_NAME%.lib
)

REM 清理临时文件
del "%LIB_NAME%.exports" "%LIB_NAME%.def" "%LIB_NAME%.exp" 2>nul

echo.
goto :eof
