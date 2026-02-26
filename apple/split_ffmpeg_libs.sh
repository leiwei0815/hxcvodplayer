#!/bin/bash

# 分离 FFmpeg fat binary 为设备和模拟器版本

set -e

PROJECT_ROOT="/Users/debug/project"
FFMPEG_LIB_DIR="$PROJECT_ROOT/YXVodPlayer/ios-third/ffmpeg-build/FFmpeg-iOS/lib"
DEVICE_LIB_DIR="$FFMPEG_LIB_DIR/device"
SIMULATOR_LIB_DIR="$FFMPEG_LIB_DIR/simulator"

echo "分离 FFmpeg fat binary..."

# 创建输出目录
mkdir -p "$DEVICE_LIB_DIR"
mkdir -p "$SIMULATOR_LIB_DIR"

# 需要处理的库
LIBS="libavcodec libavdevice libavfilter libavformat libavutil libswresample libswscale"

for lib in $LIBS; do
    echo "处理 $lib.a..."
    
    # 提取真机架构（arm64）
    if [[ "$(lipo -info "$FFMPEG_LIB_DIR/$lib.a")" =~ "arm64" ]]; then
        lipo "$FFMPEG_LIB_DIR/$lib.a" -thin arm64 -output "$DEVICE_LIB_DIR/$lib.a"
        echo "  ✅ 已提取真机版本: $DEVICE_LIB_DIR/$lib.a"
    fi
    
    # 提取模拟器架构（x86_64 和/或 arm64）
    # 对于包含多个架构的情况，需要分别提取然后合并
    ARCHS=$(lipo -info "$FFMPEG_LIB_DIR/$lib.a" 2>/dev/null || echo "")
    
    SIMULATOR_ARCHS=()
    
    if [[ "$ARCHS" =~ "x86_64" ]]; then
        SIMULATOR_ARCHS+=("x86_64")
    fi
    
    # arm64 模拟器架构需要额外判断（通过文件判断）
    # 注意：iOS 真机和模拟器的 arm64 是不同的，但 lipo 无法区分
    # 我们假设原始库是 universal binary，包含设备 arm64
    # 如果有 x86_64，说明是模拟器构建
    
    if [ ${#SIMULATOR_ARCHS[@]} -gt 0 ]; then
        # 如果只有 x86_64
        if [ ${#SIMULATOR_ARCHS[@]} -eq 1 ] && [ "${SIMULATOR_ARCHS[0]}" == "x86_64" ]; then
            lipo "$FFMPEG_LIB_DIR/$lib.a" -thin x86_64 -output "$SIMULATOR_LIB_DIR/$lib.a"
            echo "  ✅ 已提取模拟器版本 (x86_64): $SIMULATOR_LIB_DIR/$lib.a"
        else
            # 如果有多个架构，这里需要特殊处理
            echo "  ⚠️  $lib.a 包含多个架构，需要手动处理"
        fi
    fi
done

echo ""
echo "✅ FFmpeg 库分离完成！"
echo ""
echo "真机库目录: $DEVICE_LIB_DIR"
echo "模拟器库目录: $SIMULATOR_LIB_DIR"
echo ""
