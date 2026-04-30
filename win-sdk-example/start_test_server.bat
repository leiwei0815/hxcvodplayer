@echo off
chcp 65001 > nul
title HXCPlayer HLS AES-128 测试服务器

echo ============================================================
echo   HXCPlayer HLS AES-128 本地测试服务器
echo ============================================================
echo.

:: 检查 Python
where python >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [错误] 未找到 Python，请先安装 Python 3.8+
    echo        下载地址: https://www.python.org/downloads/
    pause
    exit /b 1
)

python --version
echo.

:: 检查并安装 cryptography
python -c "from cryptography.hazmat.primitives.ciphers import Cipher" >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [提示] 正在安装 cryptography 库...
    python -m pip install cryptography
    if %ERRORLEVEL% neq 0 (
        echo [错误] 安装 cryptography 失败，请手动执行：
        echo        pip install cryptography
        pause
        exit /b 1
    )
)

echo [OK] 依赖检查通过
echo.
echo 启动 HLS 测试服务器（端口 8765）...
echo.
echo  正确 m3u8  : http://127.0.0.1:8765/stream.m3u8
echo  错误 m3u8  : http://127.0.0.1:8765/stream_bad.m3u8
echo  key 接口   : http://127.0.0.1:8765/key （16字节binary ✅）
echo  状态诊断   : http://127.0.0.1:8765/status
echo.
echo  -------- 在播放器中点击 [SecureHLS 测试] 按钮 --------
echo  勾选"使用本地测试服务器"，填入上方地址即可测试。
echo.
echo  Ctrl+C 停止服务器
echo ============================================================

python "%~dp0hls_test_server.py" --port 8765
pause
