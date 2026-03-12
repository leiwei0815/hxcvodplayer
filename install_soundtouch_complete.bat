@echo off
chcp 65001 >nul 2>nul
echo ==========================================
echo 安装 SoundTouch 完整指南
echo ==========================================
echo.
echo SoundTouch 用于倍速播放功能（0.5x - 2.0x）
echo 当前项目可以正常播放，只是没有倍速功能
echo.
echo 问题：ATL/MFC 安装在旧版 MSVC 14.16，但需要 14.44
echo.
echo ==========================================
echo 步骤 1: 卸载旧版 MSVC 工具链（可选）
echo ==========================================
echo.
echo 1. 打开 Visual Studio Installer
echo 2. 点击 "修改" Visual Studio 2022
echo 3. 转到 "单个组件" 选项卡
echo 4. 搜索 "MSVC v141"
echo 5. 取消勾选旧版本工具链
echo 6. 点击 "修改"
echo.
pause
echo.

echo ==========================================
echo 步骤 2: 为当前版本安装 ATL/MFC
echo ==========================================
echo.
echo 1. 在 Visual Studio Installer 中
echo 2. "单个组件" 选项卡
echo 3. 搜索 "ATL"
echo 4. 确保勾选：
echo    [✓] 用于最新 v143 生成工具的 C++ ATL (x86 和 x64)
echo    [✓] 用于最新 v143 生成工具的 C++ MFC (x86 和 x64)
echo.
echo 注意：确保是 v143（最新），不是 v141 或 v142
echo.
pause
echo.

echo 正在打开 Visual Studio Installer...
start "" "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vs_installer.exe"
echo.
echo 安装完成后，按任意键继续安装 SoundTouch...
pause >nul
echo.

echo ==========================================
echo 步骤 3: 安装 SoundTouch
echo ==========================================
echo.
echo 正在安装 SoundTouch...
echo 这可能需要几分钟...
echo.

cd C:\vcpkg
vcpkg install soundtouch:x64-windows

if %ERRORLEVEL% EQU 0 (
    echo.
    echo ==========================================
    echo [成功] SoundTouch 安装完成！
    echo ==========================================
    echo.
    echo 步骤 4: 重新生成项目
    echo.
    choice /C YN /M "是否现在重新生成项目"
    if !ERRORLEVEL! EQU 1 (
        cd /d d:\git\hxcvodplayer
        rmdir /s /q build\vs2022_release
        call build_windows.bat vs2022 release
        echo.
        echo 请在 Visual Studio 中重新加载并编译项目
        echo.
        choice /C YN /M "是否打开 Visual Studio"
        if !ERRORLEVEL! EQU 1 (
            start build\vs2022_release\YXVodPlayer.sln
        )
    )
) else (
    echo.
    echo ==========================================
    echo [失败] SoundTouch 安装失败
    echo ==========================================
    echo.
    echo 可能的原因：
    echo 1. ATL/MFC 未正确安装到 v143 版本
    echo 2. 网络问题
    echo.
    echo 请检查错误信息，然后重试
)

echo.
pause
