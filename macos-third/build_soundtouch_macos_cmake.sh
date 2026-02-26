#!/bin/bash

# SoundTouch macOS 静态库编译脚本（使用 CMake）
# 编译 arm64 架构的静态库

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SOUNDTOUCH_VERSION="2.3.3"
BUILD_DIR="$SCRIPT_DIR/soundtouch-build-macos"
SOUNDTOUCH_DIR="$BUILD_DIR/soundtouch-$SOUNDTOUCH_VERSION"
OUTPUT_DIR="$BUILD_DIR/SoundTouch-macOS"

echo "=========================================="
echo "SoundTouch macOS 静态库编译（CMake）"
echo "=========================================="
echo "版本: $SOUNDTOUCH_VERSION"
echo "输出目录: $OUTPUT_DIR"
echo "=========================================="

# 创建构建目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 下载 SoundTouch 源码（如果不存在）
if [ ! -d "$SOUNDTOUCH_DIR" ]; then
    echo ""
    echo "📥 下载 SoundTouch $SOUNDTOUCH_VERSION 源码..."
    
    if [ ! -f "soundtouch-$SOUNDTOUCH_VERSION.tar.gz" ]; then
        curl -L "https://codeberg.org/soundtouch/soundtouch/archive/$SOUNDTOUCH_VERSION.tar.gz" -o "soundtouch-$SOUNDTOUCH_VERSION.tar.gz"
    fi
    
    echo "📦 解压源码..."
    tar -xzf "soundtouch-$SOUNDTOUCH_VERSION.tar.gz"
    mv soundtouch "$SOUNDTOUCH_DIR"
fi

# 创建 CMake 构建目录
mkdir -p "$SOUNDTOUCH_DIR/build"
cd "$SOUNDTOUCH_DIR/build"

echo ""
echo "🔨 配置 SoundTouch (CMake)..."

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_INSTALL_PREFIX="$OUTPUT_DIR" \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_SHARED_LIBS=OFF \
    -DOPENMP=OFF

echo ""
echo "🔨 编译 SoundTouch..."
cmake --build . --config Release -j$(sysctl -n hw.ncpu)

echo ""
echo "📦 安装到输出目录..."
cmake --install . --config Release

echo ""
echo "✅ SoundTouch macOS 静态库编译完成！"
echo ""
echo "输出目录: $OUTPUT_DIR"
echo ""
echo "库文件:"
ls -lh "$OUTPUT_DIR/lib/"*.a 2>/dev/null || ls -lh "$OUTPUT_DIR/lib/"
echo ""
echo "大小统计:"
du -sh "$OUTPUT_DIR/lib/"
echo ""
