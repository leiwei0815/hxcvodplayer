#!/bin/bash

# curl Android 编译脚本
# 用于 hxc_custom_io 的 HTTP Range 下载功能
# TLS 使用 OpenSSL（与 FFmpeg / iOS enable-openssl 对齐）

set -e

ANDROID_ABIS="${ANDROID_ABIS:-arm64-v8a}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=ndk_env.sh
source "$SCRIPT_DIR/ndk_env.sh"

CURL_VERSION="8.5.0"
CURL_TARBALL="curl-${CURL_VERSION}.tar.gz"
CURL_URL="https://curl.se/download/${CURL_TARBALL}"
BUILD_DIR="$SCRIPT_DIR/curl-build-android"
OUTPUT_DIR="$BUILD_DIR/curl-Android"
OPENSSL_ROOT="$SCRIPT_DIR/openssl-build-android/OpenSSL-Android"
API_LEVEL=24

hxc_detect_ndk
HOST_TAG=$(hxc_ndk_llvm_prebuilt "$NDK_PATH")
TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TAG"

echo "=========================================="
echo "curl Android 编译"
echo "=========================================="
echo "版本: $CURL_VERSION"
echo "NDK: $NDK_PATH"
echo "Host tag: $HOST_TAG"
echo "OpenSSL: $OPENSSL_ROOT"
echo "输出: $OUTPUT_DIR"
echo "=========================================="

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

if [ ! -f "$CURL_TARBALL" ]; then
    echo "📥 下载 curl $CURL_VERSION..."
    curl -L -o "$CURL_TARBALL" "$CURL_URL"
fi

rm -rf "curl-${CURL_VERSION}"
echo "📦 解压源码..."
tar -xzf "$CURL_TARBALL"

build_curl() {
    local ABI=$2
    local HOST=$3

    echo "🔨 编译 $ABI..."

    if [ ! -f "${OPENSSL_ROOT}/${ABI}/lib/libssl.so" ]; then
        echo "❌ 错误: 未找到 OpenSSL: ${OPENSSL_ROOT}/${ABI}"
        echo "请先运行: ./build_openssl_android.sh"
        exit 1
    fi

    cd "$BUILD_DIR/curl-${CURL_VERSION}"
    make clean 2>/dev/null || true

    export CC="$TOOLCHAIN/bin/${HOST}${API_LEVEL}-clang"
    export CXX="$TOOLCHAIN/bin/${HOST}${API_LEVEL}-clang++"
    export AR="$TOOLCHAIN/bin/llvm-ar"
    export RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
    export STRIP="$TOOLCHAIN/bin/llvm-strip"
    export CFLAGS="-I${OPENSSL_ROOT}/${ABI}/include"
    export LDFLAGS="-L${OPENSSL_ROOT}/${ABI}/lib"

    ./configure \
        --host=$HOST \
        --prefix="$OUTPUT_DIR/$ABI" \
        --enable-static \
        --disable-shared \
        --with-openssl="${OPENSSL_ROOT}/${ABI}" \
        --without-mbedtls \
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

    make -j"$(hxc_nproc)"
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
