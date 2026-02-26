#!/bin/bash

# iOS 第三方库统一编译脚本
# 编译 FFmpeg 和 SoundTouch 的 iOS 静态库

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=========================================="
echo "iOS 第三方库编译"
echo "=========================================="
echo ""

# 编译 FFmpeg
echo "📦 1/2 编译 FFmpeg..."
bash "$SCRIPT_DIR/build_ffmpeg_ios.sh"

echo ""
echo "=========================================="
echo ""

# 编译 SoundTouch
echo "📦 2/2 编译 SoundTouch..."
bash "$SCRIPT_DIR/build_soundtouch_ios.sh"

echo ""
echo "=========================================="
echo "✅ iOS 第三方库编译完成！"
echo "=========================================="
echo ""
echo "FFmpeg 输出:"
echo "  $SCRIPT_DIR/ffmpeg-build-ios/FFmpeg-iOS/"
echo ""
echo "SoundTouch 输出:"
echo "  $SCRIPT_DIR/soundtouch-build-ios/SoundTouch-iOS/"
echo ""
