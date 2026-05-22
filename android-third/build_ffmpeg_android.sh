#!/bin/bash

# FFmpeg Android 静态库编译脚本
# 含 HLS(m3u8) demuxer，与网络 http(s)/本地 file 协议配合使用
# 默认仅编译 arm64-v8a（发布/CI）
# 全架构: export ANDROID_ABIS="arm64-v8a armeabi-v7a x86_64"
ANDROID_ABIS="${ANDROID_ABIS:-arm64-v8a}"
# 输出静态库用于 NDK 集成

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG_VERSION="8.0.1"
BUILD_DIR="$SCRIPT_DIR/ffmpeg-build-android"
OUTPUT_DIR="$BUILD_DIR/FFmpeg-Android"

# Android NDK 路径（请根据实际情况修改）
if [ -z "$ANDROID_NDK" ]; then
    # 尝试常见的 NDK 路径
    if [ -d "$HOME/Library/Android/sdk/ndk" ]; then
        NDK_PATH=$(ls -d $HOME/Library/Android/sdk/ndk/* | tail -1)
    elif [ -d "/usr/local/android-sdk/ndk" ]; then
        NDK_PATH=$(ls -d /usr/local/android-sdk/ndk/* | tail -1)
    else
        echo "❌ 错误: 未找到 Android NDK"
        echo "请设置 ANDROID_NDK 环境变量或安装 Android NDK"
        echo "例如: export ANDROID_NDK=\$HOME/Library/Android/sdk/ndk/25.2.9519653"
        exit 1
    fi
else
    NDK_PATH="$ANDROID_NDK"
fi

echo "=========================================="
echo "FFmpeg Android 静态库编译"
echo "=========================================="
echo "版本: $FFMPEG_VERSION"
echo "NDK 路径: $NDK_PATH"
echo "输出目录: $OUTPUT_DIR"
echo "=========================================="

# 检查 NDK 是否存在
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
    rm -rf "$BUILD_DIR/FFmpeg-Android"
    echo "✅ 清理完成"
fi

# 创建构建目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 下载 FFmpeg 源码（如果不存在）
FFMPEG_TARBALL="ffmpeg-$FFMPEG_VERSION.tar.xz"
if [ ! -f "$FFMPEG_TARBALL" ]; then
    echo ""
    echo "📥 下载 FFmpeg $FFMPEG_VERSION 源码..."
    curl -L "https://ffmpeg.org/releases/$FFMPEG_TARBALL" -o "$FFMPEG_TARBALL"
fi

# 解压源码
if [ ! -d "ffmpeg-$FFMPEG_VERSION" ]; then
    echo "📦 解压源码..."
    tar -xf "$FFMPEG_TARBALL"
fi

# 编译函数
build_ffmpeg() {
    local ARCH=$1
    local ABI=$2
    local API_LEVEL=24
    local BUILD_SUBDIR="$BUILD_DIR/build-$ABI"
    local INSTALL_DIR="$BUILD_DIR/install-$ABI"
    
    # mbedTLS 路径
    local MBEDTLS_DIR="$SCRIPT_DIR/mbedtls-build-android/mbedTLS-Android/$ABI"
    
    echo ""
    echo "🔨 编译 FFmpeg for Android $ABI..."
    
    # 检查 mbedTLS 是否存在
    if [ ! -d "$MBEDTLS_DIR" ]; then
        echo "❌ 错误: 未找到 mbedTLS 库: $MBEDTLS_DIR"
        echo "请先运行: ./build_mbedtls_android.sh"
        exit 1
    fi
    
    # 清理之前的构建
    rm -rf "$BUILD_SUBDIR"
    mkdir -p "$BUILD_SUBDIR"
    cp -r "ffmpeg-$FFMPEG_VERSION"/* "$BUILD_SUBDIR/"
    cd "$BUILD_SUBDIR"
    
    # 设置工具链路径
    local TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/darwin-x86_64"
    local SYSROOT="$TOOLCHAIN/sysroot"
    
    # 根据架构设置编译器前缀和特殊标志
    local CROSS_PREFIX
    local CC
    local CXX
    local AR
    local RANLIB
    local STRIP
    local CPU_TYPE
    local EXTRA_CONFIGURE_FLAGS=""
    local EXTRA_CFLAGS=""
    local EXTRA_LDFLAGS=""
    
    case $ABI in
        "arm64-v8a")
            CROSS_PREFIX="aarch64-linux-android"
            CC="${TOOLCHAIN}/bin/${CROSS_PREFIX}${API_LEVEL}-clang"
            CXX="${TOOLCHAIN}/bin/${CROSS_PREFIX}${API_LEVEL}-clang++"
            AR="${TOOLCHAIN}/bin/llvm-ar"
            RANLIB="${TOOLCHAIN}/bin/llvm-ranlib"
            STRIP="${TOOLCHAIN}/bin/llvm-strip"
            CPU_TYPE="generic"
            # 禁用 NEON 和所有汇编优化，避免符号缺失问题
            EXTRA_CONFIGURE_FLAGS="--disable-neon --disable-asm"
            ;;
        "armeabi-v7a")
            CROSS_PREFIX="armv7a-linux-androideabi"
            CC="${TOOLCHAIN}/bin/${CROSS_PREFIX}${API_LEVEL}-clang"
            CXX="${TOOLCHAIN}/bin/${CROSS_PREFIX}${API_LEVEL}-clang++"
            AR="${TOOLCHAIN}/bin/llvm-ar"
            RANLIB="${TOOLCHAIN}/bin/llvm-ranlib"
            STRIP="${TOOLCHAIN}/bin/llvm-strip"
            CPU_TYPE="generic"
            # 禁用 NEON 和所有汇编优化，避免符号缺失问题
            EXTRA_CONFIGURE_FLAGS="--disable-neon --disable-asm"
            ;;
        "x86_64")
            CROSS_PREFIX="x86_64-linux-android"
            CC="${TOOLCHAIN}/bin/${CROSS_PREFIX}${API_LEVEL}-clang"
            CXX="${TOOLCHAIN}/bin/${CROSS_PREFIX}${API_LEVEL}-clang++"
            AR="${TOOLCHAIN}/bin/llvm-ar"
            RANLIB="${TOOLCHAIN}/bin/llvm-ranlib"
            STRIP="${TOOLCHAIN}/bin/llvm-strip"
            CPU_TYPE="x86-64"
            # x86_64 禁用汇编优化（避免 nasm 依赖）
            EXTRA_CONFIGURE_FLAGS="--disable-x86asm --disable-asm"
            ;;
    esac
    
    # 设置编译和链接标志（清除所有可能的环境变量干扰）
    EXTRA_CFLAGS="-fPIC -DANDROID -D__ANDROID_API__=$API_LEVEL -I$MBEDTLS_DIR/include"
    EXTRA_LDFLAGS="-Wl,-z,relro -Wl,-z,now -L$MBEDTLS_DIR/lib -lmbedtls -lmbedx509 -lmbedcrypto -landroid -lmediandk"
    
    # 清除可能干扰编译的环境变量
    unset CFLAGS CXXFLAGS LDFLAGS CPPFLAGS PKG_CONFIG_PATH
    
    # 配置编译选项
    ./configure \
        --prefix="$INSTALL_DIR" \
        --arch=$ARCH \
        --cpu=$CPU_TYPE \
        --target-os=android \
        --enable-cross-compile \
        --cross-prefix="${TOOLCHAIN}/bin/llvm-" \
        --cc="$CC" \
        --cxx="$CXX" \
        --ar="$AR" \
        --ranlib="$RANLIB" \
        --strip="$STRIP" \
        --sysroot="$SYSROOT" \
        --extra-cflags="$EXTRA_CFLAGS" \
        --extra-ldflags="$EXTRA_LDFLAGS" \
        --pkg-config=/usr/bin/false \
        --enable-shared \
        --disable-static \
        --enable-network \
        --disable-programs \
        --disable-doc \
        --disable-htmlpages \
        --disable-manpages \
        --disable-podpages \
        --disable-txtpages \
        --disable-debug \
        --enable-optimizations \
        --enable-small \
        --disable-avdevice \
        --disable-avfilter \
        --disable-protocols \
        --enable-protocol=file \
        --enable-protocol=http \
        --enable-protocol=https \
        --enable-protocol=crypto \
        --enable-protocol=httpproxy \
        --enable-protocol=tcp \
        --enable-protocol=tls \
        --enable-protocol=pipe \
        --disable-demuxers \
        --enable-demuxer=mov \
        --enable-demuxer=mpegts \
        --enable-demuxer=hls \
        --enable-demuxer=avi \
        --enable-demuxer=matroska \
        --enable-demuxer=flv \
        --enable-demuxer=aac \
        --enable-demuxer=mp3 \
        --enable-demuxer=wav \
        --disable-parsers \
        --enable-parser=h264 \
        --enable-parser=hevc \
        --enable-parser=mpeg4video \
        --enable-parser=aac \
        --enable-parser=mpegaudio \
        --disable-bsfs \
        --enable-bsf=h264_mp4toannexb \
        --enable-bsf=hevc_mp4toannexb \
        --enable-bsf=aac_adtstoasc \
        --disable-decoders \
        --enable-decoder=h264 \
        --enable-decoder=hevc \
        --enable-decoder=mpeg4 \
        --enable-decoder=aac \
        --enable-decoder=mp3 \
        --enable-decoder=pcm_s16le \
        --disable-encoders \
        --disable-muxers \
        --disable-filters \
        --disable-hwaccels \
        --enable-hwaccel=mediacodec \
        --enable-decoder=h264_mediacodec \
        --enable-decoder=hevc_mediacodec \
        --disable-indevs \
        --disable-outdevs \
        --enable-mediacodec \
        --enable-jni \
        --enable-mbedtls \
        --enable-version3 \
        --disable-iconv \
        --disable-bzlib \
        --disable-zlib \
        --disable-lzma \
        $EXTRA_CONFIGURE_FLAGS

    echo ""
    echo "🔎 configure 结果快照（Android/MediaCodec 关键宏）..."
    if [ -f "$BUILD_SUBDIR/ffbuild/config.mak" ]; then
        grep -E "^(CONFIG_(ANDROID|JNI|MEDIACODEC|MEDIANDK|H264_MEDIACODEC_DECODER|HEVC_MEDIACODEC_DECODER|AAC_MEDIACODEC_DECODER|MP3_MEDIACODEC_DECODER|MPEG4_MEDIACODEC_DECODER))=" "$BUILD_SUBDIR/ffbuild/config.mak" || true
    fi
    local CFG_LOG="$BUILD_SUBDIR/ffbuild/config.log"
    if [ ! -f "$CFG_LOG" ] && [ -f "$BUILD_SUBDIR/config.log" ]; then
        CFG_LOG="$BUILD_SUBDIR/config.log"
    fi
    if [ -f "$CFG_LOG" ]; then
        echo "🔎 configure 检测日志（mediacodec/mediandk/jni）..."
        grep -Ei "mediacodec|mediandk|jni|did not match anything|not found|disabled .*mediacodec|requires" "$CFG_LOG" | tail -n 300 || true
    fi

    echo ""
    echo "🔎 校验 FFmpeg mediacodec 组件宏..."
    local CFG_COMP="$BUILD_SUBDIR/config_components.h"
    local CFG_MAIN="$BUILD_SUBDIR/config.h"
    if [ ! -f "$CFG_COMP" ] || [ ! -f "$CFG_MAIN" ]; then
        echo "❌ 错误: 缺少配置文件: $CFG_COMP 或 $CFG_MAIN"
        exit 1
    fi

    if ! grep -q "^#define CONFIG_MEDIACODEC 1" "$CFG_COMP" "$CFG_MAIN"; then
        echo "❌ 错误: CONFIG_MEDIACODEC 未启用"
        exit 1
    fi

    if ! grep -q "^#define CONFIG_JNI 1" "$CFG_COMP" "$CFG_MAIN"; then
        echo "❌ 错误: CONFIG_JNI 未启用（Android mediacodec 依赖 JNI）"
        exit 1
    fi

    local H264_MEDIACODEC_OK=0
    local HEVC_MEDIACODEC_OK=0
    if grep -qE "^#define (CONFIG_MEDIACODEC_HWACCEL|CONFIG_H264_MEDIACODEC_DECODER|CONFIG_H264_MEDIACODEC_HWACCEL) 1" "$CFG_COMP" "$CFG_MAIN"; then
        H264_MEDIACODEC_OK=1
    fi
    if grep -qE "^#define (CONFIG_MEDIACODEC_HWACCEL|CONFIG_HEVC_MEDIACODEC_DECODER|CONFIG_HEVC_MEDIACODEC_HWACCEL) 1" "$CFG_COMP" "$CFG_MAIN"; then
        HEVC_MEDIACODEC_OK=1
    fi

    if [ "$H264_MEDIACODEC_OK" -ne 1 ]; then
        echo "❌ 错误: H264 mediacodec path 未启用（decoder/hwaccel 均缺失）"
        grep -E "^#define .*MEDIACODEC.* 1" "$CFG_COMP" "$CFG_MAIN" || true
        exit 1
    else
        echo "✅ H264 mediacodec path 已启用"
    fi
    if [ "$HEVC_MEDIACODEC_OK" -ne 1 ]; then
        echo "❌ 错误: HEVC mediacodec path 未启用（decoder/hwaccel 均缺失）"
        grep -E "^#define .*MEDIACODEC.* 1" "$CFG_COMP" "$CFG_MAIN" || true
        exit 1
    else
        echo "✅ HEVC mediacodec path 已启用"
    fi
    echo "✅ mediacodec 组件校验完成"
    
    echo ""
    echo "🔨 编译中..."
    local JOBS
    JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu)
    make -j"${JOBS}" 2>&1 | grep -v "warning:"
    
    echo ""
    echo "📦 安装到 $INSTALL_DIR..."
    make install
    
    cd "$BUILD_DIR"
}

for _abi in $ANDROID_ABIS; do
  case "$_abi" in
    arm64-v8a) build_ffmpeg "aarch64" "arm64-v8a" ;;
    armeabi-v7a) build_ffmpeg "armv7-a" "armeabi-v7a" ;;
    x86_64) build_ffmpeg "x86_64" "x86_64" ;;
    *) echo "❌ 不支持的 ABI: $_abi"; exit 1 ;;
  esac
done

# 组织输出目录
echo ""
echo "📦 组织输出文件..."

for ABI in $ANDROID_ABIS; do
    mkdir -p "$OUTPUT_DIR/$ABI/lib"
    mkdir -p "$OUTPUT_DIR/$ABI/include"
    
    # 复制头文件
    cp -r "$BUILD_DIR/install-$ABI/include/"* "$OUTPUT_DIR/$ABI/include/"
    
    # 复制动态库（.so）
    cp "$BUILD_DIR/install-$ABI/lib/"*.so "$OUTPUT_DIR/$ABI/lib/"
done

echo ""
echo "✅ FFmpeg Android 动态库编译完成！"
echo ""
echo "输出目录 (ANDROID_ABIS=$ANDROID_ABIS):"
for ABI in $ANDROID_ABIS; do
  echo "  $ABI: $OUTPUT_DIR/$ABI"
  ls -lh "$OUTPUT_DIR/$ABI/lib/"*.so 2>/dev/null || echo "    (无 .so)"
  echo "  大小: $(du -sh "$OUTPUT_DIR/$ABI/lib/" 2>/dev/null | cut -f1)"
done
echo ""
