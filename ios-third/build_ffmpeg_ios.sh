#!/bin/bash

# FFmpeg iOS 静态库编译脚本
# 支持 arm64 (真机) 和 arm64 + x86_64 (模拟器)
# 输出 iOS 和 iOS Simulator 的静态库

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG_VERSION="8.0.1"
BUILD_DIR="$SCRIPT_DIR/ffmpeg-build-ios"
OUTPUT_DIR="$BUILD_DIR/FFmpeg-iOS"

echo "=========================================="
echo "FFmpeg iOS 静态库编译"
echo "=========================================="
echo "版本: $FFMPEG_VERSION"
echo "输出目录: $OUTPUT_DIR"
echo "=========================================="

# 清理之前的构建
if [ -d "$BUILD_DIR" ]; then
    echo ""
    echo "🧹 清理之前的构建..."
    rm -rf "$BUILD_DIR/build-"*
    rm -rf "$BUILD_DIR/install-"*
    rm -rf "$BUILD_DIR/FFmpeg-iOS"
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
    local PLATFORM=$2
    local SDK=$3
    local MIN_VERSION=$4
    local BUILD_SUBDIR="$BUILD_DIR/build-$PLATFORM-$ARCH"
    local INSTALL_DIR="$BUILD_DIR/install-$PLATFORM-$ARCH"
    
    echo ""
    echo "🔨 编译 FFmpeg for $PLATFORM ($ARCH)..."
    
    # 清理之前的构建
    rm -rf "$BUILD_SUBDIR"
    mkdir -p "$BUILD_SUBDIR"
    cp -r "ffmpeg-$FFMPEG_VERSION"/* "$BUILD_SUBDIR/"
    cd "$BUILD_SUBDIR"
    
    # 获取 SDK 路径
    local SDKROOT=$(xcrun --sdk $SDK --show-sdk-path)
    
    # 根据平台设置正确的版本标志和额外参数
    local VERSION_FLAG=""
    local EXTRA_CFLAGS=""
    local EXTRA_LDFLAGS=""
    local VIDEOTOOLBOX_FLAGS=""
    
    if [ "$PLATFORM" = "ios" ]; then
        # 真机：启用 VideoToolbox 硬件加速
        VERSION_FLAG="-miphoneos-version-min=$MIN_VERSION"
        EXTRA_CFLAGS="-arch $ARCH $VERSION_FLAG"
        EXTRA_LDFLAGS="-arch $ARCH $VERSION_FLAG"
        VIDEOTOOLBOX_FLAGS="--enable-videotoolbox --enable-hwaccel=h264_videotoolbox --enable-hwaccel=hevc_videotoolbox"
    else
        # 模拟器：禁用 VideoToolbox，使用模拟器标志
        VERSION_FLAG="-mios-simulator-version-min=$MIN_VERSION"
        EXTRA_CFLAGS="-arch $ARCH $VERSION_FLAG -target $ARCH-apple-ios$MIN_VERSION-simulator"
        EXTRA_LDFLAGS="-arch $ARCH $VERSION_FLAG -target $ARCH-apple-ios$MIN_VERSION-simulator"
        VIDEOTOOLBOX_FLAGS="--disable-videotoolbox"
    fi
    
    # x86_64 架构需要禁用汇编优化（避免需要 nasm）
    local ASM_FLAGS=""
    if [ "$ARCH" = "x86_64" ]; then
        ASM_FLAGS="--disable-x86asm"
    fi
    
    # 配置编译选项
    ./configure \
        --prefix="$INSTALL_DIR" \
        --arch=$ARCH \
        --target-os=darwin \
        --enable-cross-compile \
        --cc="xcrun -sdk $SDK clang" \
        --extra-cflags="$EXTRA_CFLAGS" \
        --extra-ldflags="$EXTRA_LDFLAGS" \
        --sysroot="$SDKROOT" \
        --disable-shared \
        --enable-static \
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
        --enable-protocol=tcp \
        --enable-protocol=tls \
        --enable-securetransport \
        --disable-encoders \
        --disable-decoders \
        --enable-decoder=h264 \
        --enable-decoder=hevc \
        --enable-decoder=mpeg4 \
        --enable-decoder=aac \
        --enable-decoder=mp3 \
        --enable-decoder=pcm_s16le \
        --enable-decoder=png \
        --enable-decoder=mjpeg \
        --disable-muxers \
        --disable-demuxers \
        --enable-demuxer=hls \
        --enable-demuxer=mov \
        --enable-demuxer=mp4 \
        --enable-demuxer=m4v \
        --enable-demuxer=mpegts \
        --enable-demuxer=mpegtsraw \
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
        $VIDEOTOOLBOX_FLAGS \
        $ASM_FLAGS \
        --disable-iconv \
        --disable-bzlib \
        --enable-zlib \
        --disable-lzma
    
    echo ""
    echo "🔨 编译中..."
    make -j$(sysctl -n hw.ncpu)
    
    echo ""
    echo "📦 安装到 $INSTALL_DIR..."
    make install
    
    cd "$BUILD_DIR"
}

# 编译 iOS 真机 (arm64)
build_ffmpeg "arm64" "ios" "iphoneos" "12.0"

# 编译 iOS 模拟器 (arm64)
build_ffmpeg "arm64" "iossimulator" "iphonesimulator" "12.0"

# 编译 iOS 模拟器 (x86_64)
build_ffmpeg "x86_64" "iossimulator" "iphonesimulator" "12.0"

# 合并模拟器架构
echo ""
echo "🔗 合并模拟器架构 (arm64 + x86_64)..."
mkdir -p "$OUTPUT_DIR/ios-simulator/lib"
mkdir -p "$OUTPUT_DIR/ios-simulator/include"

# 复制头文件（两个架构的头文件相同）
cp -r "$BUILD_DIR/install-iossimulator-arm64/include/"* "$OUTPUT_DIR/ios-simulator/include/"

# 使用 lipo 合并静态库
for lib in libavcodec.a libavformat.a libavutil.a libswresample.a libswscale.a; do
    echo "  合并 $lib..."
    lipo -create \
        "$BUILD_DIR/install-iossimulator-arm64/lib/$lib" \
        "$BUILD_DIR/install-iossimulator-x86_64/lib/$lib" \
        -output "$OUTPUT_DIR/ios-simulator/lib/$lib"
done

# 复制真机版本
echo ""
echo "📦 复制真机版本..."
mkdir -p "$OUTPUT_DIR/ios-device/lib"
mkdir -p "$OUTPUT_DIR/ios-device/include"
cp -r "$BUILD_DIR/install-ios-arm64/include/"* "$OUTPUT_DIR/ios-device/include/"
cp "$BUILD_DIR/install-ios-arm64/lib/"*.a "$OUTPUT_DIR/ios-device/lib/"

echo ""
echo "✅ FFmpeg iOS 静态库编译完成！"
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
