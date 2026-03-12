@echo off
chcp 65001 >nul 2>nul
echo ==========================================
echo Fix SoundTouch Conditional Compilation
echo ==========================================
echo.
echo This will add SoundTouch conditional compilation support
echo while preserving macOS/iOS functionality.
echo.
echo Please close Visual Studio first!
echo.
pause

echo.
echo [INFO] Backing up hxc_player_core.cpp...
copy /Y "d:\git\hxcvodplayer\core\src\hxc_player_core.cpp" "d:\git\hxcvodplayer\core\src\hxc_player_core.cpp.bak"

echo [INFO] Adding conditional compilation...
powershell -Command ^
"$file = 'd:\git\hxcvodplayer\core\src\hxc_player_core.cpp'; ^
$content = Get-Content $file -Raw -Encoding UTF8; ^
$content = $content -replace '(?m)^(#include ""hxc_player_core\.h"")','$1`r`n`r`n// SoundTouch conditional compilation`r`n#if (defined(__APPLE__) ^|^| defined(_WIN32)) ^&^& defined(HAS_SOUNDTOUCH)`r`n#include <soundtouch/SoundTouch.h>`r`n#elif defined(__APPLE__)`r`n// macOS/iOS default enable SoundTouch`r`n#include <soundtouch/SoundTouch.h>`r`n#define HAS_SOUNDTOUCH`r`n#endif'; ^
[System.IO.File]::WriteAllText($file, $content, [System.Text.UTF8Encoding]::new($false))"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo [SUCCESS] File updated successfully!
    echo.
    echo Backup saved to: hxc_player_core.cpp.bak
    echo.
    echo Now you can:
    echo   1. Reopen Visual Studio
    echo   2. Rebuild solution (Ctrl+Shift+B)
    echo.
) else (
    echo.
    echo [ERROR] Failed to update file
    echo Restoring backup...
    copy /Y "d:\git\hxcvodplayer\core\src\hxc_player_core.cpp.bak" "d:\git\hxcvodplayer\core\src\hxc_player_core.cpp"
)

pause
