#!/bin/bash

# mbedTLS Android 编译脚本
# mbedTLS 是一个轻量级的 TLS/SSL 库，比 OpenSSL 更适合移动平台

set -e

# 配置
MBEDTLS_VERSION="2.28.9"
MBEDTLS_TARBALL="mbedtls-${MBEDTLS_VERSION}.tar.gz"
# 使用 GitHub archive URL（更可靠）
MBEDTLS_URL="https://github.com/Mbed-TLS/mbedtls/archive/refs/tags/v${MBEDTLS_VERSION}.tar.gz"
BUILD_DIR="$(pwd)/mbedtls-build-android"
OUTPUT_DIR="$BUILD_DIR/mbedTLS-Android"
API_LEVEL=24

# 检查 NDK
if [ -z "$ANDROID_NDK" ]; then
    if [ -d "$HOME/Library/Android/sdk/ndk" ]; then
        # 查找最新版本的 NDK
        ANDROID_NDK=$(ls -d "$HOME/Library/Android/sdk/ndk"/* | sort -V | tail -1)
        echo "✅ 自动检测到 NDK: $ANDROID_NDK"
    else
        echo "❌ 错误: 未找到 Android NDK"
        echo "请设置 ANDROID_NDK 环境变量或安装 Android NDK"
        exit 1
    fi
fi

NDK_PATH="$ANDROID_NDK"
TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/darwin-x86_64"

echo "=========================================="
echo "mbedTLS Android 动态库编译"
echo "=========================================="
echo "版本: $MBEDTLS_VERSION"
echo "NDK 路径: $NDK_PATH"
echo "输出目录: $OUTPUT_DIR"
echo "=========================================="
echo ""

# 创建构建目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 下载源码
if [ ! -f "$MBEDTLS_TARBALL" ]; then
    echo "📥 下载 mbedTLS $MBEDTLS_VERSION..."
    curl -L -o "$MBEDTLS_TARBALL" "$MBEDTLS_URL"
    
    # 验证下载是否成功
    if [ ! -s "$MBEDTLS_TARBALL" ]; then
        echo "❌ 下载失败，文件为空"
        rm -f "$MBEDTLS_TARBALL"
        exit 1
    fi
    
    # 检查文件类型
    file "$MBEDTLS_TARBALL"
fi

# 解压源码（每次重新解压，确保干净）
rm -rf mbedtls
rm -rf "mbedtls-${MBEDTLS_VERSION}"
echo "📦 解压源码..."
tar -xzf "$MBEDTLS_TARBALL"

# GitHub archive 解压后目录名为 mbedtls-VERSION，需要重命名
if [ -d "mbedtls-${MBEDTLS_VERSION}" ]; then
    mv "mbedtls-${MBEDTLS_VERSION}" mbedtls
else
    echo "❌ 错误: 解压后未找到 mbedtls-${MBEDTLS_VERSION} 目录"
    ls -la
    exit 1
fi

# 编译函数
build_mbedtls() {
    local ARCH=$1
    local ABI=$2
    
    echo ""
    echo "🔨 编译 mbedTLS for Android $ABI..."
    
    local INSTALL_DIR="$BUILD_DIR/install-$ABI"
    local BUILD_ABI_DIR="$BUILD_DIR/build-$ABI"
    
    # 清理之前的构建
    rm -rf "$BUILD_ABI_DIR"
    mkdir -p "$BUILD_ABI_DIR"
    cd "$BUILD_ABI_DIR"
    
    # 设置编译器
    local ANDROID_ABI=""
    local CMAKE_SYSTEM_PROCESSOR=""
    
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
    
    echo "  目标架构: $ABI"
    echo "  安装目录: $INSTALL_DIR"
    
    # 使用 CMake 编译
    cmake "$BUILD_DIR/mbedtls" \
        -DCMAKE_TOOLCHAIN_FILE="$NDK_PATH/build/cmake/android.toolchain.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DANDROID_ABI=$ANDROID_ABI \
        -DANDROID_PLATFORM=android-$API_LEVEL \
        -DANDROID_NDK="$NDK_PATH" \
        -DANDROID_STL=c++_shared \
        -DUSE_SHARED_MBEDTLS_LIBRARY=ON \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_ANDROID_ARCH_ABI=$ANDROID_ABI \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_SYSTEM_VERSION=$API_LEVEL \
        -DCMAKE_ANDROID_NDK="$NDK_PATH" \
        -DENABLE_TESTING=OFF \
        -DENABLE_PROGRAMS=OFF \
        -Wno-dev
    
    echo ""
    echo "🔨 编译中..."
    make -j$(sysctl -n hw.ncpu)
    
    echo ""
    echo "📦 安装到 $INSTALL_DIR..."
    make install
    
    cd "$BUILD_DIR"
}

# 编译 arm64-v8a
build_mbedtls "aarch64" "arm64-v8a"

# 组织输出目录
echo ""
echo "📦 组织输出文件..."

for ABI in "arm64-v8a"; do
    mkdir -p "$OUTPUT_DIR/$ABI/lib"
    mkdir -p "$OUTPUT_DIR/$ABI/include"
    
    # 复制头文件
    cp -r "$BUILD_DIR/install-$ABI/include/"* "$OUTPUT_DIR/$ABI/include/" 2>/dev/null || true
    
    # 复制动态库（.so）或静态库（.a）
    if ls "$BUILD_DIR/install-$ABI/lib/"*.so >/dev/null 2>&1; then
        cp "$BUILD_DIR/install-$ABI/lib/"*.so "$OUTPUT_DIR/$ABI/lib/"
    elif ls "$BUILD_DIR/install-$ABI/lib/"*.a >/dev/null 2>&1; then
        echo "  ⚠️  $ABI: 编译出静态库而非动态库，复制静态库"
        cp "$BUILD_DIR/install-$ABI/lib/"*.a "$OUTPUT_DIR/$ABI/lib/"
    fi
done

echo ""
echo "✅ mbedTLS Android 动态库编译完成！"
echo ""
echo "输出目录:"
echo "  arm64-v8a: $OUTPUT_DIR/arm64-v8a"
echo ""
echo "库文件 (arm64-v8a):"
ls -lh "$OUTPUT_DIR/arm64-v8a/lib/"*.so 2>/dev/null || echo "  (未找到库文件)"
echo ""
