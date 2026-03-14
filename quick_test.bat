@echo off
chcp 65001 >nul 2>&1
echo ========================================
echo   快速测试脚本
echo ========================================
echo.

set PROJECT_ROOT=%~dp0

echo [1/3] 重新构建 SDK...
cd "%PROJECT_ROOT%win-sdk"
call build_sdk.bat
if %ERRORLEVEL% neq 0 (
    echo [错误] SDK 构建失败
    pause
    exit /b 1
)

echo.
echo [2/3] 清理并构建 example...
cd "%PROJECT_ROOT%win-sdk-example"
call clean.bat
call build.bat
if %ERRORLEVEL% neq 0 (
    echo [错误] Example 构建失败
    pause
    exit /b 1
)

echo.
echo [3/3] 运行测试...
echo.
echo ========================================
echo 请在程序中测试视频播放
echo 关闭程序后查看日志文件
echo ========================================
echo.
call run.bat

pause
