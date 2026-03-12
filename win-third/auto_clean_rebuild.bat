@echo off
chcp 65001 >nul 2>nul
echo ==========================================
echo 自动清理并重新构建（无需确认）
echo ==========================================
echo.

set PROJECT_ROOT=d:\git\hxcvodplayer
set BUILD_DIR=%PROJECT_ROOT%\build\vs2022_debug

echo [1/6] 删除 VS 缓存和 IntelliSense 数据库...
if exist "%BUILD_DIR%\.vs" (
    rmdir /s /q "%BUILD_DIR%\.vs" 2>nul
    echo     ✓ .vs 目录已删除
)

echo.
echo [2/6] 删除 CMake 缓存...
if exist "%BUILD_DIR%\CMakeCache.txt" del /f /q "%BUILD_DIR%\CMakeCache.txt" 2>nul
if exist "%BUILD_DIR%\CMakeFiles" rmdir /s /q "%BUILD_DIR%\CMakeFiles" 2>nul
echo     ✓ CMake 缓存已删除

echo.
echo [3/6] 删除所有项目构建文件...
for /d %%d in ("%BUILD_DIR%\*") do (
    if exist "%%d\*.dir" rmdir /s /q "%%d\*.dir" 2>nul
    if exist "%%d\*.vcxproj" del /f /q "%%d\*.vcxproj*" 2>nul
)
if exist "%BUILD_DIR%\*.sln" del /f /q "%BUILD_DIR%\*.sln" 2>nul
echo     ✓ 项目文件已删除

echo.
echo [4/6] 删除编译输出...
if exist "%BUILD_DIR%\core" rmdir /s /q "%BUILD_DIR%\core" 2>nul
if exist "%BUILD_DIR%\desktop" rmdir /s /q "%BUILD_DIR%\desktop" 2>nul
if exist "%BUILD_DIR%\bin" rmdir /s /q "%BUILD_DIR%\bin" 2>nul
if exist "%BUILD_DIR%\lib" rmdir /s /q "%BUILD_DIR%\lib" 2>nul
echo     ✓ 编译输出已删除

echo.
echo [5/6] 重新配置项目...
cd /d "%BUILD_DIR%"

cmake "%PROJECT_ROOT%" ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_PREFIX_PATH="C:\vcpkg\installed\x64-windows;C:\Qt\5.15.2\msvc2019_64" ^
    -DQt5_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" ^
    -DBUILD_DESKTOP=ON

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [错误] CMake 配置失败
    cd "%PROJECT_ROOT%"
    exit /b 1
)

echo.
echo [6/6] 验证配置...
findstr /C:"SoundTouch found" CMakeCache.txt >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo     ✓ SoundTouch 已找到
) else (
    echo     ✗ SoundTouch 未找到
)

cd "%PROJECT_ROOT%"

echo.
echo ==========================================
echo [成功] 清理和配置完成！
echo ==========================================
echo.
echo 项目位置: %BUILD_DIR%\HXCVodPlayer.sln
echo.
echo 下一步:
echo   1. 打开 Visual Studio
echo   2. 打开上述 .sln 文件
echo   3. 生成 ^> 清理解决方案
echo   4. 生成 ^> 重新生成解决方案
echo.
