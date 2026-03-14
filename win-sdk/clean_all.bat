@chcp 65001 >nul 2>&1
@echo off
REM ============================================================
REM 清理所有构建目录
REM ============================================================

echo ========================================
echo 清理构建目录
echo ========================================
echo.

set PROJECT_ROOT=%~dp0..

echo [扫描] 查找构建目录...
echo.

REM 定义要清理的目录列表
set DIRS_TO_CLEAN=build build\win-sdk-Debug build\win-sdk-Release build\vs2022_debug build\vs2022_release

set CLEANED_COUNT=0
set SKIPPED_COUNT=0

for %%d in (%DIRS_TO_CLEAN%) do (
    if exist "%PROJECT_ROOT%\%%d" (
        echo [清理] 正在删除: %%d
        rmdir /s /q "%PROJECT_ROOT%\%%d" >nul 2>&1
        if %ERRORLEVEL% equ 0 (
            echo [清理] ✓ 已删除: %%d
            set /a CLEANED_COUNT+=1
        ) else (
            echo [警告] ✗ 无法删除: %%d （可能被占用）
            set /a SKIPPED_COUNT+=1
        )
        echo.
    )
)

echo ========================================
echo 清理完成
echo ========================================
echo 已清理: %CLEANED_COUNT% 个目录
echo 跳过: %SKIPPED_COUNT% 个目录
echo.

REM 清理 CMake 缓存
echo [扫描] 查找 CMake 缓存文件...
if exist "%PROJECT_ROOT%\CMakeCache.txt" (
    del /f /q "%PROJECT_ROOT%\CMakeCache.txt" >nul 2>&1
    echo [清理] ✓ 已删除 CMakeCache.txt
)

if exist "%PROJECT_ROOT%\CMakeFiles" (
    rmdir /s /q "%PROJECT_ROOT%\CMakeFiles" >nul 2>&1
    echo [清理] ✓ 已删除 CMakeFiles
)

echo.
echo 所有构建文件已清理！
echo.

pause
