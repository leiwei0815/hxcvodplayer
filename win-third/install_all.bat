@echo off
chcp 65001 >nul 2>nul
echo ==========================================
echo SoundTouch Debug + Release 一键编译安装
echo ==========================================
echo.
echo 此脚本会:
echo   1. 下载 SoundTouch 源码
echo   2. 编译 Debug + Release 静态库
echo   3. 配置 Debug + Release 项目
echo   4. 打开 Visual Studio 项目
echo.
echo 预计时间: 10-15 分钟
echo.
echo 优势: 不需要 ATL/MFC 组件！
echo.
pause

REM 步骤 1: 编译 SoundTouch
call build_soundtouch_multi.bat
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [错误] SoundTouch 编译失败
    pause
    exit /b 1
)

echo.
echo.
pause

REM 步骤 2: 配置项目
call configure_project_multi.bat

echo.
echo ==========================================
echo [完成] 所有步骤已完成！
echo ==========================================
echo.
echo 现在可以在 Visual Studio 中编译运行项目了
echo   - Debug 使用 Debug 版 SoundTouch
echo   - Release 使用 Release 版 SoundTouch
echo.
echo 倍速播放功能已启用！
echo.
