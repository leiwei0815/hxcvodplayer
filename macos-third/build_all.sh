#!/bin/bash

# 完整的 macOS 第三方库构建脚本
# 先编译 FFmpeg 和 SoundTouch，然后构建 XCFramework

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=========================================="
echo "macOS 第三方库完整构建流程"
echo "=========================================="

# 检查是否已经有编译好的库
FFMPEG_LIB="$SCRIPT_DIR/ffmpeg-build-macos/FFmpeg-macOS/lib/libavcodec.a"
SOUNDTOUCH_LIB="$SCRIPT_DIR/soundtouch-build-macos/SoundTouch-macOS/lib/libSoundTouch.a"

if [ -f "$FFMPEG_LIB" ] && [ -f "$SOUNDTOUCH_LIB" ]; then
    echo ""
    echo "✅ 检测到已编译的库文件"
    echo "   FFmpeg: $FFMPEG_LIB"
    echo "   SoundTouch: $SOUNDTOUCH_LIB"
    echo ""
    read -p "是否重新编译？(y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "跳过编译，使用现有库"
        exit 0
    fi
fi

# 编译 FFmpeg
echo ""
echo "=========================================="
echo "1/2: 编译 FFmpeg"
echo "=========================================="
"$SCRIPT_DIR/build_ffmpeg_macos.sh"

# 从 iOS 版本创建 SoundTouch
echo ""
echo "=========================================="
echo "2/2: 创建 SoundTouch (从 iOS 版本)"
echo "=========================================="
"$SCRIPT_DIR/build_soundtouch_from_ios.sh"

echo ""
echo "=========================================="
echo "✅ 所有库创建完成！"
echo "=========================================="
echo ""
echo "接下来可以运行："
echo "  cd $PROJECT_ROOT/apple"
echo "  ./build_xcframework_simple.sh"
echo ""
echo "来构建完全静态链接的 HXCPlayer.xcframework"
echo ""
