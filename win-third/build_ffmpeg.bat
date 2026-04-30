@chcp 65001 >nul 2>&1
@echo off
REM ============================================================
REM FFmpeg 8.0.1 Windows 编译脚本
REM ============================================================
REM 
REM 功能：
REM   - 下载 FFmpeg 8.0.1 源码
REM   - 使用 MSYS2/MinGW64 编译 FFmpeg
REM   - 生成 DLL 动态库（可与 MSVC 链接）
REM   - 安装到 ffmpeg-install 目录
REM
REM 前置要求：
REM   1. 安装 MSYS2：https://www.msys2.org/
REM   2. 在 MSYS2 中安装编译工具：
REM      pacman -S base-devel mingw-w64-x86_64-toolchain
REM      pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-nasm yasm
REM   3. 添加 MSYS2 到 PATH：C:\msys64\mingw64\bin
REM
REM ============================================================

setlocal enabledelayedexpansion

echo ========================================
echo FFmpeg 8.0.1 Windows 编译脚本
echo ========================================
echo.

REM ========================================
REM 配置参数
REM ========================================
set FFMPEG_VERSION=8.0.1
set FFMPEG_URL=https://ffmpeg.org/releases/ffmpeg-%FFMPEG_VERSION%.tar.xz
set SOURCE_DIR=%~dp0ffmpeg-src
set BUILD_DIR=%~dp0ffmpeg-build
set INSTALL_DIR=%~dp0ffmpeg-install

REM ========================================
REM 检查 MSYS2 环境
REM ========================================
echo 检查 MSYS2 环境...

REM 检测 MSYS2 安装路径
set MSYS2_ROOT=
if exist "C:\msys64" set MSYS2_ROOT=C:\msys64
if exist "C:\msys32" set MSYS2_ROOT=C:\msys32
if exist "%USERPROFILE%\msys64" set MSYS2_ROOT=%USERPROFILE%\msys64

if "%MSYS2_ROOT%"=="" (
    echo [错误] 未找到 MSYS2 安装目录
    echo.
    echo 请先安装 MSYS2: https://www.msys2.org/
    echo 默认安装路径: C:\msys64
    echo.
    pause
    exit /b 1
)

echo ✓ 找到 MSYS2: %MSYS2_ROOT%

REM 设置 MSYS2 bash 路径
set MSYS2_BASH=%MSYS2_ROOT%\usr\bin\bash.exe
if not exist "%MSYS2_BASH%" (
    echo [错误] 未找到 bash: %MSYS2_BASH%
    echo.
    pause
    exit /b 1
)

echo ✓ 找到 bash: %MSYS2_BASH%

REM 检查必需工具
echo 检查编译工具...
"%MSYS2_BASH%" -lc "which gcc" >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [错误] 未安装 GCC
    echo.
    echo 请在 MSYS2 终端执行：
    echo   pacman -S base-devel mingw-w64-x86_64-toolchain
    echo   pacman -S mingw-w64-x86_64-nasm yasm
    echo.
    pause
    exit /b 1
)

echo ✓ 编译工具已就绪
echo.

REM ========================================
REM 第 1 步：下载 FFmpeg 源码
REM ========================================
if not exist "%SOURCE_DIR%" (
    echo [1/4] 正在下载 FFmpeg %FFMPEG_VERSION% 源码...
    echo.
    
    REM 创建临时目录
    if not exist "%~dp0temp" mkdir "%~dp0temp"
    
    REM 下载源码
    echo 下载地址: %FFMPEG_URL%
    curl -L -o "%~dp0temp\ffmpeg.tar.xz" %FFMPEG_URL%
    
    if %ERRORLEVEL% neq 0 (
        echo [错误] 下载失败
        pause
        exit /b 1
    )
    
    echo ✓ 下载完成
    echo.
    
    REM 解压源码
    echo 正在解压...
    tar -xf "%~dp0temp\ffmpeg.tar.xz" -C "%~dp0temp"
    
    REM 重命名目录
    move "%~dp0temp\ffmpeg-%FFMPEG_VERSION%" "%SOURCE_DIR%" >nul
    
    REM 清理临时文件
    del "%~dp0temp\ffmpeg.tar.xz"
    rmdir "%~dp0temp"
    
    echo ✓ 源码准备完成
    echo.
) else (
    echo [1/4] ✓ FFmpeg 源码已存在
    echo.
)

REM ========================================
REM 第 2 步：准备构建目录
REM ========================================
echo [2/4] 准备构建目录...
if exist "%BUILD_DIR%" (
    echo 清理旧的构建目录...
    rmdir /s /q "%BUILD_DIR%"
)
mkdir "%BUILD_DIR%"
echo ✓ 构建目录已准备
echo.

REM ========================================
REM 第 3 步：配置 FFmpeg
REM ========================================
echo [3/4] 配置 FFmpeg...
echo.

REM 转换 Windows 路径为 MSYS2 路径格式
REM 例如: D:\path -> /d/path
set "SOURCE_MSYS=%SOURCE_DIR%"
set "BUILD_MSYS=%BUILD_DIR%"
set "INSTALL_MSYS=%INSTALL_DIR%"

REM 替换驱动器号 (D: -> /d)
set "SOURCE_MSYS=%SOURCE_MSYS::=/%"
set "SOURCE_MSYS=%SOURCE_MSYS:\=/%"
set "SOURCE_MSYS=/%SOURCE_MSYS:~0,1%%SOURCE_MSYS:~2%"

set "BUILD_MSYS=%BUILD_MSYS::=/%"
set "BUILD_MSYS=%BUILD_MSYS:\=/%"
set "BUILD_MSYS=/%BUILD_MSYS:~0,1%%BUILD_MSYS:~2%"

set "INSTALL_MSYS=%INSTALL_MSYS::=/%"
set "INSTALL_MSYS=%INSTALL_MSYS:\=/%"
set "INSTALL_MSYS=/%INSTALL_MSYS:~0,1%%INSTALL_MSYS:~2%"

REM 转换为小写
call :lowercase SOURCE_MSYS
call :lowercase BUILD_MSYS
call :lowercase INSTALL_MSYS

echo 源码目录: %SOURCE_MSYS%
echo 构建目录: %BUILD_MSYS%
echo 安装目录: %INSTALL_MSYS%
echo.

REM 执行配置（直接在 MSYS2 bash 中运行）
"%MSYS2_BASH%" -lc "cd '%BUILD_MSYS%' && '%SOURCE_MSYS%/configure' --prefix='%INSTALL_MSYS%' --enable-shared --disable-static --disable-doc --disable-ffplay --disable-ffprobe --disable-ffmpeg --enable-avcodec --enable-avformat --enable-avutil --enable-swscale --enable-swresample --enable-network --enable-protocol=file --enable-protocol=http --enable-protocol=https --enable-protocol=crypto --enable-protocol=tcp --enable-protocol=udp --enable-decoder=h264,hevc,aac,mp3 --enable-parser=h264,hevc --enable-demuxer=mov,mpegts,flv,hls --enable-small --disable-debug --arch=x86_64"

if %ERRORLEVEL% neq 0 (
    echo.
    echo [错误] FFmpeg 配置失败
    echo.
    echo 常见问题：
    echo   1. 确保已在 MSYS2 中安装: pacman -S base-devel mingw-w64-x86_64-toolchain
    echo   2. 确保已安装: pacman -S mingw-w64-x86_64-nasm yasm
    echo   3. 尝试在 MSYS2 终端手动配置
    echo.
    pause
    exit /b 1
)

echo ✓ FFmpeg 配置完成
echo.

goto :continue_build

REM 小写转换子程序
:lowercase
set "%~1=!%~1:A=a!"
set "%~1=!%~1:B=b!"
set "%~1=!%~1:C=c!"
set "%~1=!%~1:D=d!"
set "%~1=!%~1:E=e!"
set "%~1=!%~1:F=f!"
set "%~1=!%~1:G=g!"
set "%~1=!%~1:H=h!"
set "%~1=!%~1:I=i!"
set "%~1=!%~1:J=j!"
set "%~1=!%~1:K=k!"
set "%~1=!%~1:L=l!"
set "%~1=!%~1:M=m!"
set "%~1=!%~1:N=n!"
set "%~1=!%~1:O=o!"
set "%~1=!%~1:P=p!"
set "%~1=!%~1:Q=q!"
set "%~1=!%~1:R=r!"
set "%~1=!%~1:S=s!"
set "%~1=!%~1:T=t!"
set "%~1=!%~1:U=u!"
set "%~1=!%~1:V=v!"
set "%~1=!%~1:W=w!"
set "%~1=!%~1:X=x!"
set "%~1=!%~1:Y=y!"
set "%~1=!%~1:Z=z!"
goto :eof

:continue_build

REM ========================================
REM 第 4 步：编译和安装
REM ========================================
echo [4/4] 编译 FFmpeg（这可能需要 20-40 分钟）...
echo 提示：可以打开任务管理器查看编译进度
echo.

REM 编译
"%MSYS2_BASH%" -lc "cd '%BUILD_MSYS%' && make -j%NUMBER_OF_PROCESSORS%"

if %ERRORLEVEL% neq 0 (
    echo [错误] FFmpeg 编译失败
    pause
    exit /b 1
)

echo ✓ FFmpeg 编译完成
echo.

echo 正在安装到: %INSTALL_DIR%
"%MSYS2_BASH%" -lc "cd '%BUILD_MSYS%' && make install"

if %ERRORLEVEL% neq 0 (
    echo [错误] FFmpeg 安装失败
    pause
    exit /b 1
)

echo ✓ FFmpeg 安装完成
echo.

REM ========================================
REM 生成导入库（用于 MSVC 链接）
REM ========================================
echo [额外步骤] 生成 MSVC 导入库...
echo.

REM 检查是否有 Visual Studio 环境
where lib.exe >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo 正在为 DLL 生成 .lib 导入库...
    
    cd /d "%INSTALL_DIR%\bin"
    
    for %%f in (avcodec avformat avutil swscale swresample) do (
        if exist "%%f-*.dll" (
            echo   生成 %%f.lib...
            
            REM 使用 dumpbin 导出符号
            dumpbin /exports "%%f-*.dll" > "%%f.exports"
            
            REM 创建 .def 文件
            echo EXPORTS > "%%f.def"
            
            REM 从 exports 文件提取符号（跳过头部）
            for /f "skip=19 tokens=4" %%a in (%%f.exports) do (
                if not "%%a"=="" echo %%a >> "%%f.def"
            )
            
            REM 生成 .lib
            lib /def:"%%f.def" /out:"%INSTALL_DIR%\lib\%%f.lib" /machine:x64
            
            REM 清理临时文件
            del "%%f.exports" "%%f.def" "%%f.exp" 2>nul
        )
    )
    
    echo ✓ MSVC 导入库生成完成
) else (
    echo [跳过] 未找到 Visual Studio 环境，跳过 .lib 生成
    echo   提示：如果需要 MSVC 兼容的 .lib 文件，请在 VS 开发者命令提示符中运行
)

echo.

REM ========================================
REM 完成
REM ========================================
echo ========================================
echo ✓ FFmpeg %FFMPEG_VERSION% 编译完成！
echo ========================================
echo.
echo 安装目录: %INSTALL_DIR%
echo   - 头文件: %INSTALL_DIR%\include
echo   - DLL:     %INSTALL_DIR%\bin
echo   - LIB:     %INSTALL_DIR%\lib
echo.
echo 下一步：
echo   1. 运行 build_sdl2.bat 编译 SDL2
echo   2. 运行 install_all.bat 配置项目
echo.

pause
exit /b 0
