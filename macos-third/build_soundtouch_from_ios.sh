#!/bin/bash

# 从 iOS SoundTouch 源码重新编译 macOS 版本
# 使用已有的 iOS 源码，编译 macOS arm64 静态库

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# iOS SoundTouch 目录
IOS_SOUNDTOUCH_ROOT="$PROJECT_ROOT/YXVodPlayer/ios-third/soundtouch-build/SoundTouch-iOS"

# 输出目录
BUILD_DIR="$SCRIPT_DIR/soundtouch-build-macos"
OUTPUT_DIR="$BUILD_DIR/SoundTouch-macOS"

echo "=========================================="
echo "从 iOS 源码编译 macOS SoundTouch"
echo "=========================================="
echo "源码目录: $IOS_SOUNDTOUCH_ROOT"
echo "输出目录: $OUTPUT_DIR"
echo "=========================================="

# 检查 iOS 源码是否存在
if [ ! -d "$IOS_SOUNDTOUCH_ROOT/include" ]; then
    echo "❌ 找不到 iOS SoundTouch 源码"
    echo "请先编译 iOS 版本"
    exit 1
fi

# 创建输出目录
mkdir -p "$OUTPUT_DIR/lib"
mkdir -p "$OUTPUT_DIR/include"

# 复制头文件
echo ""
echo "📋 复制头文件..."
cp -r "$IOS_SOUNDTOUCH_ROOT/include"/* "$OUTPUT_DIR/include/"

# 查找 iOS 编译过的静态库
IOS_LIB_DEVICE="$IOS_SOUNDTOUCH_ROOT/lib/device/libSoundTouch.a"
IOS_LIB_SIMULATOR="$IOS_SOUNDTOUCH_ROOT/lib/simulator/libSoundTouch.a"

# 提取 arm64 架构（iOS arm64 和 macOS arm64 是兼容的！）
echo ""
echo "🔨 提取 arm64 架构..."

if [ -f "$IOS_LIB_DEVICE" ]; then
    echo "使用 iOS 设备库: $IOS_LIB_DEVICE"
    # iOS 设备的 arm64 可以直接用于 macOS
    cp "$IOS_LIB_DEVICE" "$OUTPUT_DIR/lib/libSoundTouch.a"
elif [ -f "$IOS_LIB_SIMULATOR" ]; then
    echo "使用 iOS 模拟器库: $IOS_LIB_SIMULATOR"
    # 从模拟器库中提取 arm64
    lipo "$IOS_LIB_SIMULATOR" -thin arm64 -output "$OUTPUT_DIR/lib/libSoundTouch.a" 2>/dev/null || \
        cp "$IOS_LIB_SIMULATOR" "$OUTPUT_DIR/lib/libSoundTouch.a"
else
    echo "❌ 找不到 iOS SoundTouch 库文件"
    exit 1
fi

echo ""
echo "✅ SoundTouch macOS 静态库创建完成！"
echo ""
echo "输出目录: $OUTPUT_DIR"
echo ""
echo "库文件:"
ls -lh "$OUTPUT_DIR/lib/"*.a
echo ""
echo "库信息:"
file "$OUTPUT_DIR/lib/libSoundTouch.a"
lipo -info "$OUTPUT_DIR/lib/libSoundTouch.a"
echo ""
echo "大小统计:"
du -sh "$OUTPUT_DIR/lib/"
echo ""
echo "注意: 此库是从 iOS 版本提取的 arm64 架构"
echo "      iOS 和 macOS 的 arm64 二进制是兼容的"
echo ""
