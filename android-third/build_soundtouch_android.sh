#!/bin/bash

# SoundTouch Android 静态库编译脚本
# 支持 arm64-v8a、armeabi-v7a、x86_64 架构
# arm64-v8a: 真机 + Apple Silicon 模拟器
# armeabi-v7a: 旧真机
# x86_64: Intel Mac 模拟器
# 输出静态库用于 NDK 集成

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SOUNDTOUCH_VERSION="2.3.3"
BUILD_DIR="$SCRIPT_DIR/soundtouch-build-android"
OUTPUT_DIR="$BUILD_DIR/SoundTouch-Android"

# Android NDK 路径
if [ -z "$ANDROID_NDK" ]; then
    if [ -d "$HOME/Library/Android/sdk/ndk" ]; then
        NDK_PATH=$(ls -d $HOME/Library/Android/sdk/ndk/* | tail -1)
    elif [ -d "/usr/local/android-sdk/ndk" ]; then
        NDK_PATH=$(ls -d /usr/local/android-sdk/ndk/* | tail -1)
    else
        echo "❌ 错误: 未找到 Android NDK"
        echo "请设置 ANDROID_NDK 环境变量"
        exit 1
    fi
else
    NDK_PATH="$ANDROID_NDK"
fi

echo "=========================================="
echo "SoundTouch Android 静态库编译"
echo "=========================================="
echo "版本: $SOUNDTOUCH_VERSION"
echo "NDK 路径: $NDK_PATH"
echo "输出目录: $OUTPUT_DIR"
echo "=========================================="

# 检查 NDK
if [ ! -d "$NDK_PATH" ]; then
    echo "❌ 错误: NDK 路径不存在: $NDK_PATH"
    exit 1
fi

# 清理之前的构建
if [ -d "$BUILD_DIR" ]; then
    echo ""
    echo "🧹 清理之前的构建..."
    rm -rf "$BUILD_DIR/build-"*
    rm -rf "$BUILD_DIR/install-"*
    rm -rf "$BUILD_DIR/SoundTouch-Android"
    echo "✅ 清理完成"
fi

# 创建构建目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 下载 SoundTouch 源码
SOUNDTOUCH_TARBALL="soundtouch-$SOUNDTOUCH_VERSION.tar.gz"
if [ ! -f "$SOUNDTOUCH_TARBALL" ]; then
    echo ""
    echo "📥 下载 SoundTouch $SOUNDTOUCH_VERSION 源码..."
    curl -L "https://www.surina.net/soundtouch/$SOUNDTOUCH_TARBALL" -o "$SOUNDTOUCH_TARBALL"
fi

# 解压源码
if [ ! -d "soundtouch" ]; then
    echo "📦 解压源码..."
    tar -xzf "$SOUNDTOUCH_TARBALL"
    
    # 修复 CMakeLists.txt 的 CMake 版本要求
    if [ -f "soundtouch/CMakeLists.txt" ]; then
        echo "🔧 修复 CMakeLists.txt CMake 版本要求..."
        sed -i.bak 's/cmake_minimum_required(VERSION 3.1)/cmake_minimum_required(VERSION 3.5)/' soundtouch/CMakeLists.txt
        rm -f soundtouch/CMakeLists.txt.bak
    fi
fi

# 编译函数
build_soundtouch() {
    local ARCH=$1
    local ABI=$2
    local API_LEVEL=24
    local BUILD_SUBDIR="$BUILD_DIR/build-$ABI"
    local INSTALL_DIR="$BUILD_DIR/install-$ABI"
    
    echo ""
    echo "🔨 编译 SoundTouch for Android $ABI..."
    
    # 清理之前的构建
    rm -rf "$BUILD_SUBDIR"
    mkdir -p "$BUILD_SUBDIR"
    cd "$BUILD_SUBDIR"
    
    # 设置工具链
    local TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/darwin-x86_64"
    
    # 根据架构设置
    local ANDROID_ABI
    local CMAKE_SYSTEM_PROCESSOR
    case $ABI in
        "arm64-v8a")
            ANDROID_ABI="arm64-v8a"
            CMAKE_SYSTEM_PROCESSOR="aarch64"
            ;;
        "armeabi-v7a")
            ANDROID_ABI="armeabi-v7a"
            CMAKE_SYSTEM_PROCESSOR="armv7-a"
            ;;
        "x86_64")
            ANDROID_ABI="x86_64"
            CMAKE_SYSTEM_PROCESSOR="x86_64"
            ;;
    esac
    
    # 使用 CMake 编译
    cmake "$BUILD_DIR/soundtouch" \
        -DCMAKE_TOOLCHAIN_FILE="$NDK_PATH/build/cmake/android.toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DANDROID_ABI=$ANDROID_ABI \
        -DANDROID_PLATFORM=android-$API_LEVEL \
        -DANDROID_NDK="$NDK_PATH" \
        -DANDROID_STL=c++_shared \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_ANDROID_ARCH_ABI=$ANDROID_ABI \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_SYSTEM_VERSION=$API_LEVEL \
        -DCMAKE_ANDROID_NDK="$NDK_PATH" \
        -Wno-dev
    
    echo ""
    echo "🔨 编译中..."
    make -j$(sysctl -n hw.ncpu)
    
    echo ""
    echo "📦 安装到 $INSTALL_DIR..."
    make install
    
    cd "$BUILD_DIR"
}

# 编译 arm64-v8a (真机 + Apple Silicon 模拟器)
build_soundtouch "aarch64" "arm64-v8a"

# 编译 armeabi-v7a (旧真机)
build_soundtouch "armv7-a" "armeabi-v7a"

# 编译 x86_64 (Intel Mac 模拟器)
build_soundtouch "x86_64" "x86_64"

# 组织输出目录
echo ""
echo "📦 组织输出文件..."

for ABI in "arm64-v8a" "armeabi-v7a" "x86_64"; do
    mkdir -p "$OUTPUT_DIR/$ABI/lib"
    mkdir -p "$OUTPUT_DIR/$ABI/include"
    
    # 复制头文件
    cp -r "$BUILD_DIR/install-$ABI/include/"* "$OUTPUT_DIR/$ABI/include/"
    
    # 复制动态库
    cp "$BUILD_DIR/install-$ABI/lib/libSoundTouch.so" "$OUTPUT_DIR/$ABI/lib/"
done

echo ""
echo "✅ SoundTouch Android 动态库编译完成！"
echo ""
echo "输出目录:"
echo "  arm64-v8a (真机 + Apple Silicon 模拟器): $OUTPUT_DIR/arm64-v8a"
echo "  armeabi-v7a (旧真机): $OUTPUT_DIR/armeabi-v7a"
echo "  x86_64 (Intel Mac 模拟器): $OUTPUT_DIR/x86_64"
echo ""
echo "库文件 (arm64-v8a):"
ls -lh "$OUTPUT_DIR/arm64-v8a/lib/"*.so
echo ""
echo "库文件 (armeabi-v7a):"
ls -lh "$OUTPUT_DIR/armeabi-v7a/lib/"*.so
echo ""
echo "库文件 (x86_64):"
ls -lh "$OUTPUT_DIR/x86_64/lib/"*.so
echo ""
echo "大小统计:"
echo "  arm64-v8a: $(du -sh "$OUTPUT_DIR/arm64-v8a/lib/" | cut -f1)"
echo "  armeabi-v7a: $(du -sh "$OUTPUT_DIR/armeabi-v7a/lib/" | cut -f1)"
echo "  x86_64: $(du -sh "$OUTPUT_DIR/x86_64/lib/" | cut -f1)"
echo ""
