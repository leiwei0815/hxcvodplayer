@echo off
chcp 65001 >nul 2>nul
echo ==========================================
echo 强制清理并重新生成项目
echo ==========================================
echo.

set PROJECT_ROOT=d:\git\hxcvodplayer
set BUILD_DIR=%PROJECT_ROOT%\build\vs2022_debug

echo [1/4] 关闭 Visual Studio...
echo 请手动关闭 Visual Studio，然后按任意键继续...
pause >nul

echo.
echo [2/4] 清理构建目录...
if exist "%BUILD_DIR%\CMakeFiles" (
    rmdir /s /q "%BUILD_DIR%\CMakeFiles"
    echo     清理 CMakeFiles
)
if exist "%BUILD_DIR%\.vs" (
    rmdir /s /q "%BUILD_DIR%\.vs"
    echo     清理 .vs
)
if exist "%BUILD_DIR%\core" (
    rmdir /s /q "%BUILD_DIR%\core"
    echo     清理 core
)
if exist "%BUILD_DIR%\desktop" (
    rmdir /s /q "%BUILD_DIR%\desktop"
    echo     清理 desktop
)
del /q "%BUILD_DIR%\*.vcxproj" 2>nul
del /q "%BUILD_DIR%\*.vcxproj.filters" 2>nul
del /q "%BUILD_DIR%\CMakeCache.txt" 2>nul

echo.
echo [3/4] 重新配置项目...
cd "%BUILD_DIR%"
cmake ..\.. ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_PREFIX_PATH="C:\vcpkg\installed\x64-windows;C:\Qt\5.15.2\msvc2019_64;D:/git/hxcvodplayer/win-third/soundtouch-install/debug" ^
    -DQt5_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5" ^
    -DSOUNDTOUCH_INCLUDE_DIR="D:/git/hxcvodplayer/win-third/soundtouch-install/debug/include" ^
    -DSOUNDTOUCH_LIBRARY="D:/git/hxcvodplayer/win-third/soundtouch-install/debug/lib/SoundTouch.lib" ^
    -DBUILD_DESKTOP=ON

if %ERRORLEVEL% NEQ 0 (
    echo [错误] CMake 配置失败
    pause
    exit /b 1
)

echo.
echo [4/4] 完成！
echo.
echo 现在可以打开 Visual Studio 项目了：
echo %BUILD_DIR%\HXCVodPlayer.sln
echo.
echo 在 VS 中：
echo   1. 重新生成解决方案 (Ctrl+Shift+B)
echo   2. 运行 (F5)
echo.

choice /C YN /M "是否打开 Visual Studio 项目"
if %ERRORLEVEL% EQU 1 (
    start "" "%BUILD_DIR%\HXCVodPlayer.sln"
)
