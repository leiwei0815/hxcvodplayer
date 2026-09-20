#!/bin/bash

# Android 第三方库统一编译脚本
# 编译 OpenSSL、curl、FFmpeg 和 SoundTouch 的 Android 库
# TLS 对齐 iOS enable-openssl：FFmpeg/curl 依赖 OpenSSL

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=========================================="
echo "Android 第三方库编译"
echo "=========================================="
echo ""

echo "📦 1/4 编译 OpenSSL (TLS/SSL 支持)..."
bash "$SCRIPT_DIR/build_openssl_android.sh"

echo ""
echo "=========================================="
echo ""

echo "📦 2/4 编译 curl (HTTP 下载支持)..."
bash "$SCRIPT_DIR/build_curl_android.sh"

echo ""
echo "=========================================="
echo ""

echo "📦 3/4 编译 FFmpeg..."
bash "$SCRIPT_DIR/build_ffmpeg_android.sh"

echo ""
echo "=========================================="
echo ""

echo "📦 4/4 编译 SoundTouch..."
bash "$SCRIPT_DIR/build_soundtouch_android.sh"

echo ""
echo "=========================================="
echo "✅ Android 第三方库编译完成！"
echo "=========================================="
echo ""
echo "OpenSSL 输出:"
echo "  $SCRIPT_DIR/openssl-build-android/OpenSSL-Android/"
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
