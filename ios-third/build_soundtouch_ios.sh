#!/bin/bash

# SoundTouch iOS 静态库编译脚本
# 支持 arm64 (真机) 和 arm64 + x86_64 (模拟器)
# 输出 iOS 和 iOS Simulator 的静态库

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SOUNDTOUCH_VERSION="2.3.3"
BUILD_DIR="$SCRIPT_DIR/soundtouch-build-ios"
OUTPUT_DIR="$BUILD_DIR/SoundTouch-iOS"

echo "=========================================="
echo "SoundTouch iOS 静态库编译"
echo "=========================================="
echo "版本: $SOUNDTOUCH_VERSION"
echo "输出目录: $OUTPUT_DIR"
echo "=========================================="

# 清理之前的构建
if [ -d "$BUILD_DIR" ]; then
    echo ""
    echo "🧹 清理之前的构建..."
    rm -rf "$BUILD_DIR/build-"*
    rm -rf "$BUILD_DIR/install-"*
    rm -rf "$BUILD_DIR/SoundTouch-iOS"
    echo "✅ 清理完成"
fi

# 创建构建目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 下载 SoundTouch 源码（如果不存在）
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
fi

# 编译函数
build_soundtouch() {
    local ARCH=$1
    local PLATFORM=$2
    local SDK=$3
    local MIN_VERSION=$4
    local BUILD_SUBDIR="$BUILD_DIR/build-$PLATFORM-$ARCH"
    local INSTALL_DIR="$BUILD_DIR/install-$PLATFORM-$ARCH"
    
    echo ""
    echo "🔨 编译 SoundTouch for $PLATFORM ($ARCH)..."
    
    # 清理之前的构建
    rm -rf "$BUILD_SUBDIR"
    mkdir -p "$BUILD_SUBDIR"
    cd "$BUILD_SUBDIR"
    
    # 获取 SDK 路径
    local SDKROOT=$(xcrun --sdk $SDK --show-sdk-path)
    
    # 根据平台设置正确的版本标志
    local VERSION_FLAG=""
    if [ "$PLATFORM" = "ios" ]; then
        VERSION_FLAG="-miphoneos-version-min=$MIN_VERSION"
    else
        VERSION_FLAG="-mios-simulator-version-min=$MIN_VERSION"
    fi
    
    # 使用 CMake 编译
    cmake "$BUILD_DIR/soundtouch" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_SYSTEM_NAME=iOS \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_VERSION" \
        -DCMAKE_OSX_ARCHITECTURES="$ARCH" \
        -DCMAKE_OSX_SYSROOT="$SDKROOT" \
        -DCMAKE_C_COMPILER="$(xcrun -find -sdk $SDK clang)" \
        -DCMAKE_CXX_COMPILER="$(xcrun -find -sdk $SDK clang++)" \
        -DCMAKE_C_FLAGS="-arch $ARCH $VERSION_FLAG -isysroot $SDKROOT" \
        -DCMAKE_CXX_FLAGS="-arch $ARCH $VERSION_FLAG -isysroot $SDKROOT -stdlib=libc++" \
        -DCMAKE_SHARED_LINKER_FLAGS="-arch $ARCH $VERSION_FLAG -isysroot $SDKROOT" \
        -DCMAKE_MODULE_LINKER_FLAGS="-arch $ARCH $VERSION_FLAG -isysroot $SDKROOT" \
        -DCMAKE_EXE_LINKER_FLAGS="-arch $ARCH $VERSION_FLAG -isysroot $SDKROOT" \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    
    echo ""
    echo "🔨 编译中..."
    make -j$(sysctl -n hw.ncpu)
    
    echo ""
    echo "📦 安装到 $INSTALL_DIR..."
    make install
    
    cd "$BUILD_DIR"
}

# 编译 iOS 真机 (arm64)
build_soundtouch "arm64" "ios" "iphoneos" "12.0"

# 编译 iOS 模拟器 (arm64)
build_soundtouch "arm64" "iossimulator" "iphonesimulator" "12.0"

# 编译 iOS 模拟器 (x86_64)
build_soundtouch "x86_64" "iossimulator" "iphonesimulator" "12.0"

# 合并模拟器架构
echo ""
echo "🔗 合并模拟器架构 (arm64 + x86_64)..."
mkdir -p "$OUTPUT_DIR/ios-simulator/lib"
mkdir -p "$OUTPUT_DIR/ios-simulator/include"

# 复制头文件（两个架构的头文件相同）
cp -r "$BUILD_DIR/install-iossimulator-arm64/include/"* "$OUTPUT_DIR/ios-simulator/include/"

# 使用 lipo 合并静态库
echo "  合并 libSoundTouch.a..."
lipo -create \
    "$BUILD_DIR/install-iossimulator-arm64/lib/libSoundTouch.a" \
    "$BUILD_DIR/install-iossimulator-x86_64/lib/libSoundTouch.a" \
    -output "$OUTPUT_DIR/ios-simulator/lib/libSoundTouch.a"

# 复制真机版本
echo ""
echo "📦 复制真机版本..."
mkdir -p "$OUTPUT_DIR/ios-device/lib"
mkdir -p "$OUTPUT_DIR/ios-device/include"
cp -r "$BUILD_DIR/install-ios-arm64/include/"* "$OUTPUT_DIR/ios-device/include/"
cp "$BUILD_DIR/install-ios-arm64/lib/libSoundTouch.a" "$OUTPUT_DIR/ios-device/lib/"

echo ""
echo "✅ SoundTouch iOS 静态库编译完成！"
echo ""
echo "输出目录:"
echo "  真机 (arm64): $OUTPUT_DIR/ios-device"
echo "  模拟器 (arm64 + x86_64): $OUTPUT_DIR/ios-simulator"
echo ""
echo "库文件 (真机):"
ls -lh "$OUTPUT_DIR/ios-device/lib/"*.a
echo ""
echo "库文件 (模拟器):"
ls -lh "$OUTPUT_DIR/ios-simulator/lib/"*.a
echo ""
echo "大小统计:"
echo "  真机: $(du -sh "$OUTPUT_DIR/ios-device/lib/" | cut -f1)"
echo "  模拟器: $(du -sh "$OUTPUT_DIR/ios-simulator/lib/" | cut -f1)"
echo ""
