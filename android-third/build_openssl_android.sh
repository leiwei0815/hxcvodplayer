#!/bin/bash

# OpenSSL Android 动态库编译脚本
# 对齐 iOS enable-openssl：FFmpeg HTTPS/TLS 走 OpenSSL，替代 mbedTLS。
# 默认仅 arm64-v8a；全架构: export ANDROID_ABIS="arm64-v8a armeabi-v7a x86_64"

set -e

ANDROID_ABIS="${ANDROID_ABIS:-arm64-v8a}"
OPENSSL_VERSION="${OPENSSL_VERSION:-3.0.16}"
OPENSSL_TARBALL="openssl-${OPENSSL_VERSION}.tar.gz"
API_LEVEL=24

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=ndk_env.sh
source "$SCRIPT_DIR/ndk_env.sh"

BUILD_DIR="$SCRIPT_DIR/openssl-build-android"
OUTPUT_DIR="$BUILD_DIR/OpenSSL-Android"

hxc_detect_ndk
HOST_TAG=$(hxc_ndk_llvm_prebuilt "$NDK_PATH")
TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/$HOST_TAG"

echo "=========================================="
echo "OpenSSL Android 动态库编译"
echo "=========================================="
echo "版本: $OPENSSL_VERSION"
echo "NDK: $NDK_PATH"
echo "Host tag: $HOST_TAG"
echo "输出: $OUTPUT_DIR"
echo "=========================================="

if [ ! -d "$NDK_PATH" ]; then
    echo "❌ 错误: NDK 路径不存在: $NDK_PATH"
    exit 1
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

download_openssl() {
    local url
    for url in \
        "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/${OPENSSL_TARBALL}" \
        "https://www.openssl.org/source/${OPENSSL_TARBALL}"
    do
        echo "📥 下载 OpenSSL $OPENSSL_VERSION ..."
        echo "   $url"
        if curl -fL --retry 3 -o "$OPENSSL_TARBALL" "$url"; then
            if [ -s "$OPENSSL_TARBALL" ]; then
                return 0
            fi
        fi
        rm -f "$OPENSSL_TARBALL"
    done
    echo "❌ OpenSSL 源码下载失败"
    exit 1
}

if [ ! -f "$OPENSSL_TARBALL" ]; then
    download_openssl
fi

rm -rf "openssl-${OPENSSL_VERSION}"
echo "📦 解压源码..."
tar -xzf "$OPENSSL_TARBALL"

flatten_shared_lib() {
    local src_dir=$1
    local dest_dir=$2
    local stem=$3
    local lib="lib${stem}.so"
    mkdir -p "$dest_dir"
    if [ -e "$src_dir/$lib" ]; then
        cp -L "$src_dir/$lib" "$dest_dir/$lib"
        return 0
    fi
    local ver
    ver=$(ls "$src_dir"/lib"${stem}".so.* 2>/dev/null | head -1 || true)
    if [ -n "$ver" ] && [ -e "$ver" ]; then
        cp -L "$ver" "$dest_dir/$lib"
        return 0
    fi
    echo "❌ 未找到 $lib（目录: $src_dir）"
    ls -la "$src_dir" || true
    return 1
}

openssl_libdir() {
    local prefix=$1
    if ls "$prefix/lib64"/libssl.so* >/dev/null 2>&1; then
        echo "$prefix/lib64"
    else
        echo "$prefix/lib"
    fi
}

build_openssl() {
    local ABI=$1
    local TARGET=$2
    local SRC="$BUILD_DIR/openssl-${OPENSSL_VERSION}"
    local BUILD_ABI_DIR="$BUILD_DIR/build-$ABI"
    local INSTALL_DIR="$BUILD_DIR/install-$ABI"

    echo ""
    echo "🔨 编译 OpenSSL for Android $ABI ($TARGET)..."

    rm -rf "$BUILD_ABI_DIR" "$INSTALL_DIR"
    mkdir -p "$BUILD_ABI_DIR"
    cp -R "$SRC/." "$BUILD_ABI_DIR/"
    cd "$BUILD_ABI_DIR"

    export ANDROID_NDK_ROOT="$NDK_PATH"
    export PATH="$TOOLCHAIN/bin:$PATH"
    unset CC CXX CFLAGS CXXFLAGS LDFLAGS CPPFLAGS

    ./Configure "$TARGET" \
        -D__ANDROID_API__=$API_LEVEL \
        --prefix="$INSTALL_DIR" \
        --openssldir="$INSTALL_DIR" \
        shared \
        no-tests \
        no-module

    make -j"$(hxc_nproc)"
    make install_sw

    local LIBDIR
    LIBDIR=$(openssl_libdir "$INSTALL_DIR")
    mkdir -p "$OUTPUT_DIR/$ABI/include" "$OUTPUT_DIR/$ABI/lib"
    cp -R "$INSTALL_DIR/include/"* "$OUTPUT_DIR/$ABI/include/"
    flatten_shared_lib "$LIBDIR" "$OUTPUT_DIR/$ABI/lib" ssl
    flatten_shared_lib "$LIBDIR" "$OUTPUT_DIR/$ABI/lib" crypto

    echo "✅ $ABI: $(ls -1 "$OUTPUT_DIR/$ABI/lib")"
    cd "$BUILD_DIR"
}

for _abi in $ANDROID_ABIS; do
    case "$_abi" in
        arm64-v8a) build_openssl "arm64-v8a" "android-arm64" ;;
        armeabi-v7a) build_openssl "armeabi-v7a" "android-arm" ;;
        x86_64) build_openssl "x86_64" "android-x86_64" ;;
        *) echo "❌ 不支持的 ABI: $_abi"; exit 1 ;;
    esac
done

echo ""
echo "✅ OpenSSL Android 动态库编译完成！"
echo "输出目录: $OUTPUT_DIR"
for ABI in $ANDROID_ABIS; do
    echo "  $ABI:"
    ls -lh "$OUTPUT_DIR/$ABI/lib/"*.so
done
