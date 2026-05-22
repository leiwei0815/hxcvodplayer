#!/bin/bash

# curl Android 编译脚本
# 用于 hxc_custom_io 的 HTTP Range 下载功能

set -e

ANDROID_ABIS="${ANDROID_ABIS:-arm64-v8a}"

# 配置
CURL_VERSION="8.5.0"
CURL_TARBALL="curl-${CURL_VERSION}.tar.gz"
CURL_URL="https://curl.se/download/${CURL_TARBALL}"
BUILD_DIR="$(pwd)/curl-build-android"
OUTPUT_DIR="$BUILD_DIR/curl-Android"
MBEDTLS_ROOT="$(pwd)/mbedtls-build-android/mbedTLS-Android"
API_LEVEL=24

# 检查 NDK
if [ -z "$ANDROID_NDK" ]; then
    if [ -d "$HOME/Library/Android/sdk/ndk" ]; then
        ANDROID_NDK=$(ls -d "$HOME/Library/Android/sdk/ndk"/* | sort -V | tail -1)
        echo "✅ 自动检测到 NDK: $ANDROID_NDK"
    else
        echo "❌ 错误: 未找到 Android NDK"
        exit 1
    fi
fi

NDK_PATH="$ANDROID_NDK"
TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/darwin-x86_64"

echo "=========================================="
echo "curl Android 编译"
echo "=========================================="
echo "版本: $CURL_VERSION"
echo "NDK: $NDK_PATH"
echo "mbedTLS: $MBEDTLS_ROOT"
echo "输出: $OUTPUT_DIR"
echo "=========================================="

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 下载源码
if [ ! -f "$CURL_TARBALL" ]; then
    echo "📥 下载 curl $CURL_VERSION..."
    curl -L -o "$CURL_TARBALL" "$CURL_URL"
fi

# 解压
rm -rf "curl-${CURL_VERSION}"
echo "📦 解压源码..."
tar -xzf "$CURL_TARBALL"

# 编译函数
build_curl() {
    local ARCH=$1
    local ABI=$2
    local HOST=$3

    echo "🔨 编译 $ABI..."

    cd "$BUILD_DIR/curl-${CURL_VERSION}"
    make clean 2>/dev/null || true

    export CC="$TOOLCHAIN/bin/${HOST}${API_LEVEL}-clang"
    export CXX="$TOOLCHAIN/bin/${HOST}${API_LEVEL}-clang++"
    export AR="$TOOLCHAIN/bin/llvm-ar"
    export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    export STRIP="$TOOLCHAIN/bin/llvm-strip"
    export CFLAGS="-I${MBEDTLS_ROOT}/${ABI}/include"
    export LDFLAGS="-L${MBEDTLS_ROOT}/${ABI}/lib"

    ./configure \
        --host=$HOST \
        --prefix="$OUTPUT_DIR/$ABI" \
        --enable-static \
        --disable-shared \
        --with-mbedtls="${MBEDTLS_ROOT}/${ABI}" \
        --without-zlib \
        --disable-ldap \
        --disable-ldaps \
        --disable-rtsp \
        --disable-dict \
        --disable-telnet \
        --disable-tftp \
        --disable-pop3 \
        --disable-imap \
        --disable-smtp \
        --disable-gopher \
        --disable-manual \
        --disable-verbose

    make -j$(sysctl -n hw.ncpu)
    make install

    echo "✅ $ABI 编译完成"
}

for _abi in $ANDROID_ABIS; do
  case "$_abi" in
    arm64-v8a) build_curl "aarch64" "arm64-v8a" "aarch64-linux-android" ;;
    armeabi-v7a) build_curl "armv7a" "armeabi-v7a" "armv7a-linux-androideabi" ;;
    x86_64) build_curl "x86_64" "x86_64" "x86_64-linux-android" ;;
    *) echo "❌ 不支持的 ABI: $_abi"; exit 1 ;;
  esac
done

echo ""
echo "✅ curl Android 编译完成！"
echo "输出目录: $OUTPUT_DIR"
