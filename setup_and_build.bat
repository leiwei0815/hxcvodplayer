@echo off
chcp 65001 >nul
setlocal EnableDelayedExpansion

echo ============================================================
echo   HXCPlayer Windows 完整编译向导
echo   VS2022: D:\Program Files\Microsoft Visual Studio\2022\Community
echo   Qt5:    D:\Qt\5.15.2\msvc2019_64
echo ============================================================
echo.

REM ===== 固定路径配置 =====
set VS_VCVARS="D:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
set QT_DIR=D:\Qt\5.15.2\msvc2019_64
set PROJECT_ROOT=%~dp0
set WIN_THIRD=%PROJECT_ROOT%win-third

REM ===== 验证路径 =====
echo [检查] 验证环境...
if not exist %VS_VCVARS% (
    echo [错误] 找不到 vcvars64.bat，路径：%VS_VCVARS%
    pause & exit /b 1
)
if not exist "%QT_DIR%\bin\qmake.exe" (
    echo [错误] 找不到 Qt5，路径：%QT_DIR%
    pause & exit /b 1
)
echo [OK] VS2022 和 Qt5 验证通过
echo.

echo 请选择要执行的步骤:
echo   1. 编译 SDL2（约 5 分钟，不需要 MSYS2）
echo   2. 安装 MSYS2 并编译 FFmpeg（约 30-60 分钟）
echo   3. 编译 win-sdk（SDK DLL）
echo   4. 编译 win-sdk-example（Qt GUI Demo）
echo   5. 全部执行（1+3+4，跳过 FFmpeg，使用 vcpkg FFmpeg）
echo   6. 仅启动测试服务器（无需编译）
echo.
set /p STEP="请输入步骤编号 (1/2/3/4/5/6): "

if "%STEP%"=="1" goto BUILD_SDL2
if "%STEP%"=="2" goto INSTALL_MSYS2
if "%STEP%"=="3" goto BUILD_SDK
if "%STEP%"=="4" goto BUILD_EXAMPLE
if "%STEP%"=="5" goto BUILD_ALL_VCPKG
if "%STEP%"=="6" goto START_SERVER
echo [错误] 无效选项
pause & exit /b 1

REM ============================================================
REM 步骤1：编译 SDL2
REM ============================================================
:BUILD_SDL2
echo.
echo ============================================================
echo   步骤 1/4：编译 SDL2
echo ============================================================
call %VS_VCVARS%

set SDL2_VERSION=2.30.9
set SDL2_SRC=%WIN_THIRD%\sdl2-src
set SDL2_BUILD=%WIN_THIRD%\sdl2-build
set SDL2_INSTALL=%WIN_THIRD%\sdl2-install

REM ---- 定位 SDL2 真实源码目录（兼容 sdl2-src/ 和 sdl2-src/SDL2-x.x.x/ 两种解压结构）----
set SDL2_CMAKE_SRC=
if exist "%SDL2_SRC%\CMakeLists.txt" (
    set SDL2_CMAKE_SRC=%SDL2_SRC%
) else if exist "%SDL2_SRC%\SDL2-%SDL2_VERSION%\CMakeLists.txt" (
    set SDL2_CMAKE_SRC=%SDL2_SRC%\SDL2-%SDL2_VERSION%
) else (
    REM 查找任意子目录下的 CMakeLists.txt
    for /d %%d in ("%SDL2_SRC%\*") do (
        if exist "%%d\CMakeLists.txt" set SDL2_CMAKE_SRC=%%d
    )
)

REM 下载 SDL2（如果没有找到源码）
if "!SDL2_CMAKE_SRC!"=="" (
    echo [1/3] 下载 SDL2 %SDL2_VERSION%...
    if not exist "%SDL2_SRC%" mkdir "%SDL2_SRC%"
    curl -L -o "%SDL2_SRC%\sdl2.tar.gz" "https://github.com/libsdl-org/SDL/releases/download/release-%SDL2_VERSION%/SDL2-%SDL2_VERSION%.tar.gz"
    if !ERRORLEVEL! neq 0 (
        echo [错误] 下载失败，请检查网络或手动下载
        echo 手动方式：把 SDL2-%SDL2_VERSION%.tar.gz 解压到 %SDL2_SRC%\SDL2-%SDL2_VERSION%\
        pause & exit /b 1
    )
    tar -xzf "%SDL2_SRC%\sdl2.tar.gz" -C "%SDL2_SRC%"
    del "%SDL2_SRC%\sdl2.tar.gz"
    set SDL2_CMAKE_SRC=%SDL2_SRC%\SDL2-%SDL2_VERSION%
    echo [OK] SDL2 源码下载并解压完成
) else (
    echo [OK] SDL2 源码已存在: !SDL2_CMAKE_SRC!
)

echo [验证] SDL2 源码路径: !SDL2_CMAKE_SRC!
if not exist "!SDL2_CMAKE_SRC!\CMakeLists.txt" (
    echo [错误] 找不到 SDL2 CMakeLists.txt，目录结构异常
    pause & exit /b 1
)

REM 编译 Release
echo [2/3] 编译 SDL2 Release...
if exist "%SDL2_BUILD%\release" rd /s /q "%SDL2_BUILD%\release"
mkdir "%SDL2_BUILD%\release"
cmake "!SDL2_CMAKE_SRC!" -G "Visual Studio 17 2022" -A x64 ^
    -B "%SDL2_BUILD%\release" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX="%SDL2_INSTALL%\release" ^
    -DBUILD_SHARED_LIBS=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
if !ERRORLEVEL! neq 0 ( echo [错误] SDL2 CMake 配置失败 & pause & exit /b 1 )
cmake --build "%SDL2_BUILD%\release" --config Release --parallel
if !ERRORLEVEL! neq 0 ( echo [错误] SDL2 编译失败 & pause & exit /b 1 )
cmake --install "%SDL2_BUILD%\release" --config Release
if !ERRORLEVEL! neq 0 ( echo [错误] SDL2 安装失败 & pause & exit /b 1 )

echo [OK] SDL2 编译完成: %SDL2_INSTALL%\release
echo.

if "%STEP%"=="1" ( echo 步骤1完成！& pause & exit /b 0 )
goto BUILD_SDK

REM ============================================================
REM 步骤2：MSYS2 安装提示 + FFmpeg 编译
REM ============================================================
:INSTALL_MSYS2
echo.
echo ============================================================
echo   步骤 2：安装 MSYS2 并编译 FFmpeg
echo ============================================================
echo.
echo FFmpeg 需要 MSYS2 + MinGW64 工具链。
echo.
echo 请按以下步骤操作：
echo.
echo 1. 下载并安装 MSYS2（安装到默认路径 C:\msys64）：
echo    https://www.msys2.org/
echo    或直接运行：
echo    winget install MSYS2.MSYS2
echo.
echo 2. 安装完成后，打开 MSYS2 MinGW64 终端，运行：
echo    pacman -Syu
echo    pacman -S base-devel mingw-w64-x86_64-toolchain mingw-w64-x86_64-nasm yasm
echo.
echo 3. 完成后回来运行：
echo    cd /d "%WIN_THIRD%"
echo    build_ffmpeg.bat
echo.
echo ---- 或者（推荐替代方案）----
echo 使用 vcpkg 安装预编译的 FFmpeg（更快，约5分钟）：
echo    步骤见下方，选项 5 会自动处理
echo.
pause
exit /b 0

REM ============================================================
REM 步骤3：编译 win-sdk
REM ============================================================
:BUILD_SDK
echo.
echo ============================================================
echo   步骤 3/4：编译 win-sdk（生成 hxcplayer.dll）
echo ============================================================
call %VS_VCVARS% 2>nul

set SDK_SRC=%PROJECT_ROOT%win-sdk
set SDK_BUILD=%PROJECT_ROOT%build\win-sdk-Release
set FFMPEG_LOCAL=%WIN_THIRD%\ffmpeg-install
set SDL2_LOCAL=%WIN_THIRD%\sdl2-install\release

REM 检查 FFmpeg
if not exist "%FFMPEG_LOCAL%\include\libavcodec\avcodec.h" (
    echo [警告] 未找到本地 FFmpeg（%FFMPEG_LOCAL%）
    echo        将尝试通过 vcpkg 查找...
    set EXTRA_CMAKE=-DCMAKE_PREFIX_PATH="C:\vcpkg\installed\x64-windows"
) else (
    echo [OK] 找到本地 FFmpeg: %FFMPEG_LOCAL%
    set EXTRA_CMAKE=
)

if not exist "%SDL2_LOCAL%\include\SDL2\SDL.h" (
    echo [错误] 未找到 SDL2，请先执行步骤1编译 SDL2
    pause & exit /b 1
)
echo [OK] 找到本地 SDL2: %SDL2_LOCAL%

echo [编译] 配置 win-sdk...
if exist "%SDK_BUILD%" rd /s /q "%SDK_BUILD%"
mkdir "%SDK_BUILD%"

cmake "%PROJECT_ROOT%" ^
    -G "Visual Studio 17 2022" -A x64 ^
    -B "%SDK_BUILD%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_DESKTOP=OFF ^
    -DBUILD_SHARED_LIBS=ON ^
    -DQt5_DIR="%QT_DIR%\lib\cmake\Qt5" ^
    %EXTRA_CMAKE%

if !ERRORLEVEL! neq 0 ( echo [错误] CMake 配置失败 & pause & exit /b 1 )

echo [编译] 构建 win-sdk...
cmake --build "%SDK_BUILD%" --config Release --target hxcplayer --parallel
if !ERRORLEVEL! neq 0 ( echo [错误] win-sdk 编译失败 & pause & exit /b 1 )

REM 打包 SDK 到标准目录
set SDK_OUTPUT=%SDK_BUILD%\HXCPlayerSDK
if not exist "%SDK_OUTPUT%\include" mkdir "%SDK_OUTPUT%\include"
if not exist "%SDK_OUTPUT%\lib"     mkdir "%SDK_OUTPUT%\lib"
if not exist "%SDK_OUTPUT%\bin"     mkdir "%SDK_OUTPUT%\bin"

REM 复制头文件
xcopy /y /q "%PROJECT_ROOT%win-sdk\hxcplayer_sdk_c_api.h" "%SDK_OUTPUT%\include\"
xcopy /y /q "%PROJECT_ROOT%core\include\hxc_player_core_c_bridge.h" "%SDK_OUTPUT%\include\"
xcopy /y /q "%PROJECT_ROOT%core\include\hxc_player_types.h" "%SDK_OUTPUT%\include\"

REM 查找并复制 DLL/LIB
for /r "%SDK_BUILD%" %%f in (hxcplayer.dll) do (
    if exist "%%f" ( copy /y "%%f" "%SDK_OUTPUT%\bin\" >nul & echo [OK] 复制 %%~nxf )
)
for /r "%SDK_BUILD%" %%f in (hxcplayer.lib) do (
    if exist "%%f" ( copy /y "%%f" "%SDK_OUTPUT%\lib\" >nul & echo [OK] 复制 %%~nxf )
)

echo [OK] SDK 输出: %SDK_OUTPUT%
echo.

if "%STEP%"=="3" ( echo 步骤3完成！& pause & exit /b 0 )
goto BUILD_EXAMPLE

REM ============================================================
REM 步骤4：编译 win-sdk-example（Qt GUI Demo）
REM ============================================================
:BUILD_EXAMPLE
echo.
echo ============================================================
echo   步骤 4/4：编译 win-sdk-example（Qt GUI Demo）
echo ============================================================
call %VS_VCVARS% 2>nul

set EXAMPLE_SRC=%PROJECT_ROOT%win-sdk-example
set EXAMPLE_BUILD=%PROJECT_ROOT%build\win-sdk-example-Release
set SDK_DIR=%PROJECT_ROOT%build\win-sdk-Release\HXCPlayerSDK

if not exist "%SDK_DIR%\lib\hxcplayer.lib" (
    echo [错误] 未找到 SDK，请先执行步骤3编译 win-sdk
    echo 期望路径: %SDK_DIR%\lib\hxcplayer.lib
    pause & exit /b 1
)

REM 更新 CMakeLists 中的 SDK 路径为实际路径（通过 -D 覆盖）
echo [编译] 配置 win-sdk-example...
if exist "%EXAMPLE_BUILD%" rd /s /q "%EXAMPLE_BUILD%"
mkdir "%EXAMPLE_BUILD%"

cmake "%EXAMPLE_SRC%" ^
    -G "Visual Studio 17 2022" -A x64 ^
    -B "%EXAMPLE_BUILD%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DQt5_DIR="%QT_DIR%\lib\cmake\Qt5" ^
    -DSDK_DIR="%SDK_DIR%"

if !ERRORLEVEL! neq 0 ( echo [错误] CMake 配置失败 & pause & exit /b 1 )

echo [编译] 构建 Qt Demo...
cmake --build "%EXAMPLE_BUILD%" --config Release --parallel
if !ERRORLEVEL! neq 0 ( echo [错误] Qt Demo 编译失败 & pause & exit /b 1 )

set EXE_PATH=%EXAMPLE_BUILD%\bin\Release\SDKTestPlayer.exe
if not exist "%EXE_PATH%" set EXE_PATH=%EXAMPLE_BUILD%\bin\SDKTestPlayer.exe

REM 部署 Qt 运行时 DLL
echo [部署] 复制 Qt 运行时 DLL...
set QT_BIN=%QT_DIR%\bin
for %%f in (Qt5Core Qt5Gui Qt5Widgets) do (
    copy /y "%QT_BIN%\%%f.dll" "%EXAMPLE_BUILD%\bin\Release\" >nul 2>nul
    copy /y "%QT_BIN%\%%f.dll" "%EXAMPLE_BUILD%\bin\" >nul 2>nul
)
REM 部署平台插件
set OUT_BIN=%EXAMPLE_BUILD%\bin\Release
if not exist "%OUT_BIN%\platforms" mkdir "%OUT_BIN%\platforms"
copy /y "%QT_DIR%\plugins\platforms\qwindows.dll" "%OUT_BIN%\platforms\" >nul 2>nul

REM 复制 SDK DLL
copy /y "%SDK_DIR%\bin\hxcplayer.dll" "%OUT_BIN%\" >nul 2>nul

REM 复制 FFmpeg DLL
if exist "%WIN_THIRD%\ffmpeg-install\bin" (
    xcopy /y /q "%WIN_THIRD%\ffmpeg-install\bin\*.dll" "%OUT_BIN%\"
)
REM 复制 SDL2 DLL
if exist "%WIN_THIRD%\sdl2-install\release\bin\SDL2.dll" (
    copy /y "%WIN_THIRD%\sdl2-install\release\bin\SDL2.dll" "%OUT_BIN%\" >nul
)

echo.
echo ============================================================
echo   编译完成！
echo ============================================================
echo.
echo   可执行文件: %OUT_BIN%\SDKTestPlayer.exe
echo.
echo   直接运行:
echo     "%OUT_BIN%\SDKTestPlayer.exe"
echo.
echo   SecureHLS 测试步骤:
echo     1. 先启动测试服务器: win-sdk-example\start_test_server.bat
echo     2. 运行 SDKTestPlayer.exe
echo     3. 点击蓝色按钮「SecureHLS 测试」
echo     4. 勾选「使用本地测试服务器」→ 点「开始播放」
echo.
pause
exit /b 0

REM ============================================================
REM 步骤5：使用 vcpkg 安装 FFmpeg 后全量编译（推荐）
REM ============================================================
:BUILD_ALL_VCPKG
echo.
echo ============================================================
echo   步骤 5：通过 vcpkg 安装 FFmpeg + 全量编译
echo ============================================================
echo.
echo 正在安装 vcpkg...
if not exist "C:\vcpkg" (
    git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
    C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
)
set PATH=%PATH%;C:\vcpkg

echo 安装 FFmpeg（x64-windows）...
C:\vcpkg\vcpkg.exe install ffmpeg:x64-windows
if !ERRORLEVEL! neq 0 ( echo [错误] vcpkg 安装 FFmpeg 失败 & pause & exit /b 1 )

echo [OK] FFmpeg 已通过 vcpkg 安装
echo.

REM 复制 vcpkg FFmpeg 到 win-third 格式
set VCPKG_INSTALLED=C:\vcpkg\installed\x64-windows
set FFMPEG_INSTALL=%WIN_THIRD%\ffmpeg-install
if not exist "%FFMPEG_INSTALL%\include" mkdir "%FFMPEG_INSTALL%\include"
if not exist "%FFMPEG_INSTALL%\lib"     mkdir "%FFMPEG_INSTALL%\lib"
if not exist "%FFMPEG_INSTALL%\bin"     mkdir "%FFMPEG_INSTALL%\bin"
xcopy /y /s /q "%VCPKG_INSTALLED%\include\libav*"    "%FFMPEG_INSTALL%\include\"
xcopy /y /s /q "%VCPKG_INSTALLED%\include\libsw*"    "%FFMPEG_INSTALL%\include\"
xcopy /y /q    "%VCPKG_INSTALLED%\lib\avcodec.lib"   "%FFMPEG_INSTALL%\lib\"
xcopy /y /q    "%VCPKG_INSTALLED%\lib\avformat.lib"  "%FFMPEG_INSTALL%\lib\"
xcopy /y /q    "%VCPKG_INSTALLED%\lib\avutil.lib"    "%FFMPEG_INSTALL%\lib\"
xcopy /y /q    "%VCPKG_INSTALLED%\lib\swscale.lib"   "%FFMPEG_INSTALL%\lib\"
xcopy /y /q    "%VCPKG_INSTALLED%\lib\swresample.lib" "%FFMPEG_INSTALL%\lib\"
xcopy /y /q    "%VCPKG_INSTALLED%\bin\avcodec*.dll"  "%FFMPEG_INSTALL%\bin\"
xcopy /y /q    "%VCPKG_INSTALLED%\bin\avformat*.dll" "%FFMPEG_INSTALL%\bin\"
xcopy /y /q    "%VCPKG_INSTALLED%\bin\avutil*.dll"   "%FFMPEG_INSTALL%\bin\"
xcopy /y /q    "%VCPKG_INSTALLED%\bin\swscale*.dll"  "%FFMPEG_INSTALL%\bin\"
xcopy /y /q    "%VCPKG_INSTALLED%\bin\swresample*.dll" "%FFMPEG_INSTALL%\bin\"

set STEP=1
goto BUILD_SDL2

REM ============================================================
REM 步骤6：启动测试服务器
REM ============================================================
:START_SERVER
echo.
echo 启动 HLS AES-128 本地测试服务器...
echo.
start "HLS测试服务器" cmd /k "cd /d "%PROJECT_ROOT%win-sdk-example" && python hls_test_server.py --port 8765"
echo [OK] 测试服务器已在后台启动（端口 8765）
echo.
echo 访问地址:
echo   m3u8 (正确): http://127.0.0.1:8765/stream.m3u8
echo   m3u8 (错误): http://127.0.0.1:8765/stream_bad.m3u8
echo   key  接口:   http://127.0.0.1:8765/key
echo   诊断页面:    http://127.0.0.1:8765/status
echo.
echo 直接用 FFmpeg 命令行测试解密（如果系统有 ffmpeg）:
echo   ffmpeg -i http://127.0.0.1:8765/stream.m3u8 -c copy test_out.ts
echo.
pause
exit /b 0
