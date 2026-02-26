#!/bin/bash

# FFmpeg macOS 静态库编译脚本
# 编译最小化配置的 FFmpeg，仅包含常用解码器
# 输出 arm64 架构的静态库

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
FFMPEG_VERSION="8.0.1"
BUILD_DIR="$SCRIPT_DIR/ffmpeg-build-macos"
FFMPEG_DIR="$BUILD_DIR/ffmpeg-$FFMPEG_VERSION"
OUTPUT_DIR="$BUILD_DIR/FFmpeg-macOS"

echo "=========================================="
echo "FFmpeg macOS 静态库编译"
echo "=========================================="
echo "版本: $FFMPEG_VERSION"
echo "输出目录: $OUTPUT_DIR"
echo "=========================================="

# 创建构建目录
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# 下载 FFmpeg 源码（如果不存在）
if [ ! -d "$FFMPEG_DIR" ]; then
    echo ""
    echo "📥 下载 FFmpeg $FFMPEG_VERSION 源码..."
    
    if [ ! -f "ffmpeg-$FFMPEG_VERSION.tar.xz" ]; then
        curl -L "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz" -o "ffmpeg-$FFMPEG_VERSION.tar.xz"
    fi
    
    echo "📦 解压源码..."
    tar -xf "ffmpeg-$FFMPEG_VERSION.tar.xz"
fi

# 进入源码目录
cd "$FFMPEG_DIR"

echo ""
echo "🔨 配置 FFmpeg..."

# 配置编译选项
./configure \
    --prefix="$OUTPUT_DIR" \
    --arch=arm64 \
    --target-os=darwin \
    --enable-cross-compile \
    --cc=clang \
    --extra-cflags="-arch arm64 -mmacosx-version-min=11.0" \
    --extra-ldflags="-arch arm64 -mmacosx-version-min=11.0" \
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
    --enable-securetransport \
    --disable-avdevice \
    --disable-avfilter \
    --disable-protocols \
    --enable-protocol=file \
    --enable-protocol=http \
    --enable-protocol=https \
    --enable-protocol=tcp \
    --enable-protocol=tls \
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
    --enable-hwaccel=h264_videotoolbox \
    --enable-hwaccel=hevc_videotoolbox \
    --enable-videotoolbox \
    --disable-iconv \
    --disable-bzlib \
    --disable-zlib \
    --disable-lzma

echo ""
echo "🔨 编译 FFmpeg..."
make -j$(sysctl -n hw.ncpu)

echo ""
echo "📦 安装到输出目录..."
make install

echo ""
echo "✅ FFmpeg macOS 静态库编译完成！"
echo ""
echo "输出目录: $OUTPUT_DIR"
echo ""
echo "库文件:"
ls -lh "$OUTPUT_DIR/lib/"*.a
echo ""
echo "大小统计:"
du -sh "$OUTPUT_DIR/lib/"
echo ""
