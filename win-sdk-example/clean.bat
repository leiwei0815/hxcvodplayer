@echo off
chcp 65001 >nul 2>&1
echo ========================================
echo   清理 SDK 测试项目
echo ========================================
echo.

set PROJECT_ROOT=%~dp0

if exist "%PROJECT_ROOT%build" (
    echo [信息] 删除构建目录...
    rmdir /s /q "%PROJECT_ROOT%build"
    echo [✓] 清理完成
) else (
    echo [提示] 没有需要清理的内容
)

echo.
exit /b 0
