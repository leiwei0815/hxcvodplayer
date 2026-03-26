#!/bin/bash

# Android 第三方库统一编译脚本
# 编译 mbedTLS、FFmpeg 和 SoundTouch 的 Android 库

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=========================================="
echo "Android 第三方库编译"
echo "=========================================="
echo ""

# 编译 mbedTLS
echo "📦 1/4 编译 mbedTLS (TLS/SSL 支持)..."
bash "$SCRIPT_DIR/build_mbedtls_android.sh"

echo ""
echo "=========================================="
echo ""

# 编译 curl
echo "📦 2/4 编译 curl (HTTP 下载支持)..."
bash "$SCRIPT_DIR/build_curl_android.sh"

echo ""
echo "=========================================="
echo ""

# 编译 FFmpeg
echo "📦 3/4 编译 FFmpeg..."
bash "$SCRIPT_DIR/build_ffmpeg_android.sh"

echo ""
echo "=========================================="
echo ""

# 编译 SoundTouch
echo "📦 4/4 编译 SoundTouch..."
bash "$SCRIPT_DIR/build_soundtouch_android.sh"

echo ""
echo "=========================================="
echo "✅ Android 第三方库编译完成！"
echo "=========================================="
echo ""
echo "mbedTLS 输出:"
echo "  $SCRIPT_DIR/mbedtls-build-android/mbedTLS-Android/"
echo ""
echo "curl 输出:"
echo "  $SCRIPT_DIR/curl-build-android/curl-Android/"
echo ""
echo "FFmpeg 输出:"
echo "  $SCRIPT_DIR/ffmpeg-build-android/FFmpeg-Android/"
echo ""
echo "SoundTouch 输出:"
echo "  $SCRIPT_DIR/soundtouch-build-android/SoundTouch-Android/"
echo ""
