@echo off
chcp 65001 >nul 2>nul
REM ==========================================
REM 修复 Debug + Release STTypes.h MSVC 问题
REM ==========================================

setlocal enabledelayedexpansion

set SCRIPT_DIR=%~dp0
set HEADER_DEBUG=%SCRIPT_DIR%soundtouch-install\debug\include\soundtouch\STTypes.h
set HEADER_RELEASE=%SCRIPT_DIR%soundtouch-install\release\include\soundtouch\STTypes.h

echo ==========================================
echo 修复 SoundTouch 头文件 MSVC 兼容性
echo ==========================================
echo.

REM 修复 Debug
if exist "%HEADER_DEBUG%" (
    echo [信息] 修复 Debug 版本: %HEADER_DEBUG%
    
    REM 创建临时 PowerShell 脚本
    set TEMP_PS=%TEMP%\fix_soundtouch_%RANDOM%.ps1
    
    (
        echo $file = '%HEADER_DEBUG%'
        echo $content = Get-Content $file -Raw -Encoding UTF8
        echo.
        echo # 查找并替换问题代码段
        echo $pattern = '(?s\)(    #endif  // SOUNDTOUCH_INTEGER_SAMPLES\r?\n\r?\n)(    #if[^\}]+#endif\r?\n\r?\n)(\})'
        echo.
        echo $replacement = @'
        echo     #endif  // SOUNDTOUCH_INTEGER_SAMPLES
        echo.
        echo     // HXC: Fixed for MSVC - check if macros are defined before using them
        echo     #if defined(SOUNDTOUCH_ALLOW_SSE^)
        echo         #if SOUNDTOUCH_ALLOW_SSE
        echo             #define HXC_HAS_SSE 1
        echo         #endif
        echo     #endif
        echo     
        echo     #if defined(SOUNDTOUCH_USE_NEON^)
        echo         #if SOUNDTOUCH_USE_NEON
        echo             #define HXC_HAS_NEON 1
        echo         #endif
        echo     #endif
        echo     
        echo     #if defined(HXC_HAS_SSE^) ^|^| defined(__SSE__^) ^|^| defined(HXC_HAS_NEON^)
        echo         #if defined(SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION^)
        echo             #if SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION
        echo                 #define ST_SIMD_AVOID_UNALIGNED
        echo             #endif
        echo         #endif
        echo     #endif
        echo.
        echo }
        echo '@
        echo.
        echo $content = $content -replace $pattern, $replacement
        echo Set-Content $file -Value $content -NoNewline -Encoding UTF8
    ) > "!TEMP_PS!"
    
    powershell -NoProfile -ExecutionPolicy Bypass -File "!TEMP_PS!"
    del "!TEMP_PS!" 2>nul
    
    if !ERRORLEVEL! EQU 0 (
        echo [成功] Debug 头文件已修复
    ) else (
        echo [错误] Debug 头文件修复失败
    )
) else (
    echo [警告] Debug 头文件不存在
)

echo.

REM 修复 Release
if exist "%HEADER_RELEASE%" (
    echo [信息] 修复 Release 版本: %HEADER_RELEASE%
    
    REM 创建临时 PowerShell 脚本
    set TEMP_PS=%TEMP%\fix_soundtouch_%RANDOM%.ps1
    
    (
        echo $file = '%HEADER_RELEASE%'
        echo $content = Get-Content $file -Raw -Encoding UTF8
        echo.
        echo # 查找并替换问题代码段
        echo $pattern = '(?s\)(    #endif  // SOUNDTOUCH_INTEGER_SAMPLES\r?\n\r?\n)(    #if[^\}]+#endif\r?\n\r?\n)(\})'
        echo.
        echo $replacement = @'
        echo     #endif  // SOUNDTOUCH_INTEGER_SAMPLES
        echo.
        echo     // HXC: Fixed for MSVC - check if macros are defined before using them
        echo     #if defined(SOUNDTOUCH_ALLOW_SSE^)
        echo         #if SOUNDTOUCH_ALLOW_SSE
        echo             #define HXC_HAS_SSE 1
        echo         #endif
        echo     #endif
        echo     
        echo     #if defined(SOUNDTOUCH_USE_NEON^)
        echo         #if SOUNDTOUCH_USE_NEON
        echo             #define HXC_HAS_NEON 1
        echo         #endif
        echo     #endif
        echo     
        echo     #if defined(HXC_HAS_SSE^) ^|^| defined(__SSE__^) ^|^| defined(HXC_HAS_NEON^)
        echo         #if defined(SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION^)
        echo             #if SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION
        echo                 #define ST_SIMD_AVOID_UNALIGNED
        echo             #endif
        echo         #endif
        echo     #endif
        echo.
        echo }
        echo '@
        echo.
        echo $content = $content -replace $pattern, $replacement
        echo Set-Content $file -Value $content -NoNewline -Encoding UTF8
    ) > "!TEMP_PS!"
    
    powershell -NoProfile -ExecutionPolicy Bypass -File "!TEMP_PS!"
    del "!TEMP_PS!" 2>nul
    
    if !ERRORLEVEL! EQU 0 (
        echo [成功] Release 头文件已修复
    ) else (
        echo [错误] Release 头文件修复失败
    )
) else (
    echo [警告] Release 头文件不存在（可能还未编译）
)

echo.
echo ==========================================
echo [完成] 头文件修复完成
echo ==========================================
