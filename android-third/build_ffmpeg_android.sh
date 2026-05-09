#!/bin/bash

# FFmpeg Android 静态库编译脚本
# 含 HLS(m3u8) demuxer，与网络 http(s)/本地 file 协议配合使用
# 支持 arm64-v8a、armeabi-v7a、x86_64 架构
# arm64-v8a: 真机 + Apple Silicon 模拟器
# armeabi-v7a: 旧真机
# x86_64: Intel Mac 模拟器
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
    EXTRA_LDFLAGS="-Wl,-z,relro -Wl,-z,now -L$MBEDTLS_DIR/lib -lmbedtls -lmbedx509 -lmbedcrypto"
    
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
        --pkg-config=false \
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
        --enable-protocol=hls \
        --disable-encoders \
        --disable-decoders \
        --enable-decoder=h264 \
        --enable-decoder=hevc \
        --enable-decoder=h264_mediacodec \
        --enable-decoder=hevc_mediacodec \
        --enable-decoder=mpeg4 \
        --enable-decoder=aac \
        --enable-decoder=mp3 \
        --enable-decoder=pcm_s16le \
        --enable-decoder=png \
        --enable-decoder=mjpeg \
        --disable-muxers \
        --disable-demuxers \
        --enable-demuxer=mov \
        --enable-demuxer=mp4 \
        --enable-demuxer=m4v \
        --enable-demuxer=mpegts \
        --enable-demuxer=mpegtsraw \
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
        --disable-indevs \
        --disable-outdevs \
        --disable-filters \
        --enable-filter=scale \
        --enable-filter=format \
        --disable-hwaccels \
        --enable-hwaccel=h264_mediacodec \
        --enable-hwaccel=hevc_mediacodec \
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
    echo "🔨 编译中..."
    make -j$(sysctl -n hw.ncpu) 2>&1 | grep -v "warning:"
    
    echo ""
    echo "📦 安装到 $INSTALL_DIR..."
    make install
    
    cd "$BUILD_DIR"
}

# 编译 arm64-v8a (真机 + Apple Silicon 模拟器)
build_ffmpeg "aarch64" "arm64-v8a"

# 编译 armeabi-v7a (旧真机)
build_ffmpeg "armv7-a" "armeabi-v7a"

# 编译 x86_64 (Intel Mac 模拟器)
build_ffmpeg "x86_64" "x86_64"

# 组织输出目录
echo ""
echo "📦 组织输出文件..."

for ABI in "arm64-v8a" "armeabi-v7a" "x86_64"; do
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
