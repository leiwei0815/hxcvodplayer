@echo off
chcp 65001 >nul 2>&1
echo ========================================
echo   测试 SDK 依赖复制逻辑
echo ========================================
echo.

REM 模拟调用 copy_dependencies.bat
set TEST_DIR=%TEMP%\sdk_test
if not exist "%TEST_DIR%" mkdir "%TEST_DIR%"

echo 测试目录: %TEST_DIR%
echo.

cd /d "%~dp0..\win-sdk"
call copy_dependencies.bat "%TEST_DIR%" Release "C:\vcpkg"

echo.
echo ========================================
echo 测试结果
echo ========================================
echo.

echo 已复制的 DLL:
dir /b "%TEST_DIR%\*.dll" 2>nul

echo.
echo avcodec 文件大小:
for %%f in ("%TEST_DIR%\avcodec-*.dll") do (
    set SIZE=%%~zf
    set /a SIZE_MB=!SIZE! / 1048576
    echo   %%~nxf: !SIZE_MB! MB
)

echo.
echo 清理测试目录...
rmdir /s /q "%TEST_DIR%" 2>nul

pause
