@echo off
chcp 65001 >nul 2>nul
REM HXCVodPlayer Windows 环境检查脚本
REM 检查所有必需的工具和依赖是否正确安装

setlocal enabledelayedexpansion

echo ==========================================
echo HXCVodPlayer Windows 环境检查
echo ==========================================
echo.

set "PASSED=0"
set "FAILED=0"
set "WARNING=0"

REM 检查 Git
echo [检查 1/8] Git...
where git >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    for /f "tokens=3" %%i in ('git --version') do echo [PASS] Git 已安装: %%i
    set /a PASSED+=1
) else (
    echo [FAIL] Git 未安装
    echo        请从 https://git-scm.com/download/win 下载安装
    set /a FAILED+=1
)
echo.

REM 检查 CMake
echo [检查 2/8] CMake...
where cmake >nul 2>nul
if %ERRORLEVEL% EQU 0 (
    for /f "tokens=3" %%i in ('cmake --version') do (
        echo [PASS] CMake 已安装: %%i
        set /a PASSED+=1
        goto :cmake_done
    )
) else (
    echo [FAIL] CMake 未安装
    echo        请从 https://cmake.org/download/ 下载安装
    set /a FAILED+=1
)
:cmake_done
echo.

REM 检查 Visual Studio
echo [检查 3/8] Visual Studio...
set "VS_FOUND=0"

REM 检查 VS 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe" (
    echo [PASS] Visual Studio 2022 Community 已安装
    set "VS_FOUND=1"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\devenv.exe" (
    echo [PASS] Visual Studio 2022 Professional 已安装
    set "VS_FOUND=1"
) else if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\devenv.exe" (
    echo [PASS] Visual Studio 2022 Enterprise 已安装
    set "VS_FOUND=1"
)

REM 检查 VS 2019
if !VS_FOUND! EQU 0 (
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\devenv.exe" (
        echo [PASS] Visual Studio 2019 Community 已安装
        set "VS_FOUND=1"
    ) else if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional\Common7\IDE\devenv.exe" (
        echo [PASS] Visual Studio 2019 Professional 已安装
        set "VS_FOUND=1"
    )
)

if !VS_FOUND! EQU 1 (
    set /a PASSED+=1
) else (
    echo [FAIL] Visual Studio 未安装
    echo        请从 https://visualstudio.microsoft.com/ 下载安装
    set /a FAILED+=1
)
echo.

REM 检查 vcpkg
echo [检查 4/8] vcpkg...
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\vcpkg.exe" (
        echo [PASS] vcpkg 已安装于: %VCPKG_ROOT%
        set /a PASSED+=1
    ) else (
        echo [WARN] VCPKG_ROOT 已设置但 vcpkg.exe 未找到
        echo        路径: %VCPKG_ROOT%
        set /a WARNING+=1
    )
) else (
    echo [WARN] VCPKG_ROOT 环境变量未设置
    echo        推荐安装 vcpkg 来管理依赖
    echo        运行: powershell -ExecutionPolicy Bypass -File setup_windows_deps.ps1
    set /a WARNING+=1
)
echo.

REM 检查 FFmpeg
echo [检查 5/8] FFmpeg...
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\installed\x64-windows\include\libavcodec\avcodec.h" (
        echo [PASS] FFmpeg 已通过 vcpkg 安装
        set /a PASSED+=1
    ) else (
        echo [FAIL] FFmpeg 未找到
        echo        运行: vcpkg install ffmpeg:x64-windows
        set /a FAILED+=1
    )
) else (
    echo [SKIP] 跳过（需要 vcpkg）
)
echo.

REM 检查 SDL2
echo [检查 6/8] SDL2...
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\installed\x64-windows\include\SDL2\SDL.h" (
        echo [PASS] SDL2 已通过 vcpkg 安装
        set /a PASSED+=1
    ) else (
        echo [FAIL] SDL2 未找到
        echo        运行: vcpkg install sdl2:x64-windows
        set /a FAILED+=1
    )
) else (
    echo [SKIP] 跳过（需要 vcpkg）
)
echo.

REM 检查 Qt5
echo [检查 7/8] Qt5...
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\installed\x64-windows\include\qt5\QtCore\QCoreApplication" (
        echo [PASS] Qt5 已通过 vcpkg 安装
        set /a PASSED+=1
    ) else (
        echo [FAIL] Qt5 未找到
        echo        运行: vcpkg install qt5-base:x64-windows
        set /a FAILED+=1
    )
) else (
    echo [SKIP] 跳过（需要 vcpkg）
)
echo.

REM 检查 SoundTouch（可选）
echo [检查 8/8] SoundTouch (可选)...
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\installed\x64-windows\include\soundtouch\SoundTouch.h" (
        echo [PASS] SoundTouch 已通过 vcpkg 安装
        set /a PASSED+=1
    ) else (
        echo [WARN] SoundTouch 未找到（倍速播放功能将不可用）
        echo        运行: vcpkg install soundtouch:x64-windows
        set /a WARNING+=1
    )
) else (
    echo [SKIP] 跳过（需要 vcpkg）
)
echo.

REM 总结
echo ==========================================
echo 检查总结
echo ==========================================
echo.
echo [✓] 通过: %PASSED%
if %WARNING% GTR 0 (
    echo [!] 警告: %WARNING%
)
if %FAILED% GTR 0 (
    echo [✗] 失败: %FAILED%
)
echo.

if %FAILED% EQU 0 (
    if %WARNING% EQU 0 (
        echo [成功] 所有检查通过！可以开始构建项目。
        echo.
        echo 下一步:
        echo   运行: build_windows.bat vs2022
        echo   或:   quickstart_windows.bat
    ) else (
        echo [完成] 基本环境已就绪，但有一些警告。
        echo        可以尝试构建，但可能需要解决警告项。
    )
) else (
    echo [错误] 环境检查未通过，请先安装缺失的工具。
    echo.
    echo 推荐步骤:
    echo   1. 安装 Visual Studio 2022
    echo   2. 安装 CMake
    echo   3. 运行: powershell -ExecutionPolicy Bypass -File setup_windows_deps.ps1
)
echo.

pause
endlocal
exit /b %FAILED%
