@echo off
chcp 65001 >nul 2>nul
echo ==========================================
echo 修复 SoundTouch MSVC 编译错误 v2
echo ==========================================
echo.

set HEADER_FILE=%~dp0soundtouch-install\include\soundtouch\STTypes.h

if not exist "%HEADER_FILE%" (
    echo [错误] 找不到 STTypes.h 文件
    echo 请先编译 SoundTouch
    pause
    exit /b 1
)

echo [信息] 修复文件: %HEADER_FILE%
echo.

powershell -Command ^
"$file = '%HEADER_FILE%'; ^
$content = Get-Content $file -Raw -Encoding UTF8; ^
$pattern1 = '#if \(\(SOUNDTOUCH_ALLOW_SSE\).*SOUNDTOUCH_USE_NEON\)\)'; ^
$pattern2 = '#if \(defined\(SOUNDTOUCH_ALLOW_SSE\).*SOUNDTOUCH_USE_NEON\)\)'; ^
if ($content -match $pattern1 -or $content -match $pattern2) { ^
    $newLine = '#if defined(SOUNDTOUCH_ALLOW_SSE) ^|^| defined(__SSE__) ^|^| defined(SOUNDTOUCH_USE_NEON)'; ^
    $content = $content -replace $pattern1, $newLine; ^
    $content = $content -replace $pattern2, $newLine; ^
    $content = $content -replace '#if defined\(SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION\).*SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION', '#if defined(SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION)'; ^
    $utf8 = New-Object System.Text.UTF8Encoding $false; ^
    [System.IO.File]::WriteAllText($file, $content, $utf8); ^
    Write-Host '[成功] 文件已修复'; ^
    exit 0; ^
} else { ^
    Write-Host '[警告] 未找到需要修复的模式，可能已修复'; ^
    Write-Host '正在使用直接替换方式...'; ^
    $lines = Get-Content $file -Encoding UTF8; ^
    for ($i = 0; $i -lt $lines.Count; $i++) { ^
        if ($lines[$i] -match 'SOUNDTOUCH_ALLOW_SSE.*SOUNDTOUCH_USE_NEON') { ^
            $lines[$i] = '    #if defined(SOUNDTOUCH_ALLOW_SSE) ^|^| defined(__SSE__) ^|^| defined(SOUNDTOUCH_USE_NEON)'; ^
            Write-Host \"修复第 $($i+1) 行\"; ^
        } ^
        if ($lines[$i] -match '#if.*SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION.*\^&\^&') { ^
            $lines[$i] = '        #if defined(SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION)'; ^
            Write-Host \"修复第 $($i+1) 行\"; ^
        } ^
    } ^
    $utf8 = New-Object System.Text.UTF8Encoding $false; ^
    [System.IO.File]::WriteAllLines($file, $lines, $utf8); ^
    Write-Host '[完成] 直接替换完成'; ^
}"

echo.
echo [完成] 修复完成！
echo.
echo 验证修复:
type "%HEADER_FILE%" | findstr /N "SOUNDTOUCH_ALLOW_SSE"
echo.
echo 现在可以重新编译项目了
echo.
pause
