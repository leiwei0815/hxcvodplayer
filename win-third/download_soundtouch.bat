@echo off
chcp 65001 >nul 2>nul
REM ==========================================
REM SoundTouch 源码下载脚本（多种方式）
REM ==========================================

setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set SRC_DIR=%SCRIPT_DIR%soundtouch-src

echo ==========================================
echo SoundTouch 源码下载
echo ==========================================
echo.

REM 检查是否已存在
if exist "%SRC_DIR%\.git" (
    echo [信息] 源码已存在: %SRC_DIR%
    choice /C YN /M "是否重新下载"
    if !ERRORLEVEL! EQU 2 (
        echo 使用现有源码
        exit /b 0
    )
    rmdir /s /q "%SRC_DIR%"
)

echo 选择下载方式：
echo.
echo [1] GitHub (官方，需要良好网络)
echo [2] GitHub (使用代理 127.0.0.1:7890)
echo [3] 手动下载（打开浏览器下载 ZIP）
echo.
choice /C 123 /M "请选择"
set CHOICE=%ERRORLEVEL%

if %CHOICE% EQU 1 goto github_direct
if %CHOICE% EQU 2 goto github_proxy
if %CHOICE% EQU 3 goto manual_download

:github_direct
echo.
echo [方式 1] 从 GitHub 直接下载...
echo.
git clone https://github.com/soundtouch/soundtouch.git "%SRC_DIR%"
goto check_result

:github_proxy
echo.
echo [方式 2] 使用代理下载...
echo.
git -c http.proxy=http://127.0.0.1:7890 clone https://github.com/soundtouch/soundtouch.git "%SRC_DIR%"
goto check_result

:manual_download
echo.
echo [方式 3] 手动下载
echo ==========================================
echo.
echo 步骤：
echo   1. 浏览器将打开 GitHub 下载页面
echo   2. 点击绿色 "Code" 按钮
echo   3. 点击 "Download ZIP"
echo   4. 下载完成后，解压到：
echo      %SRC_DIR%
echo.
echo 按任意键打开下载页面...
pause >nul

start https://github.com/soundtouch/soundtouch

echo.
echo ==========================================
echo 等待手动下载和解压...
echo ==========================================
echo.
echo 下载并解压后，按任意键继续验证...
pause >nul

REM 检查手动解压的目录
if exist "%SRC_DIR%\CMakeLists.txt" (
    echo [成功] 源码已正确解压
    exit /b 0
)

REM 检查可能的解压路径
if exist "%SRC_DIR%\soundtouch-master\CMakeLists.txt" (
    echo [信息] 检测到 soundtouch-master 目录，移动文件...
    move "%SRC_DIR%\soundtouch-master\*" "%SRC_DIR%\"
    rmdir "%SRC_DIR%\soundtouch-master"
    echo [成功] 文件已整理
    exit /b 0
)

echo [错误] 未找到正确的源码目录
echo 请确保解压后的文件直接在：
echo %SRC_DIR%
exit /b 1

:check_result
if %ERRORLEVEL% EQU 0 (
    echo.
    echo [成功] 源码下载完成！
    echo 位置: %SRC_DIR%
    exit /b 0
) else (
    echo.
    echo [失败] 下载失败
    echo.
    echo 建议：
    echo   1. 检查网络连接
    echo   2. 尝试使用代理
    echo   3. 使用手动下载方式
    exit /b 1
)
