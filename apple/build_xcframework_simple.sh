#!/bin/bash

# HXCPlayer XCFramework 构建脚本（一体化方案）
# 将 FFmpeg 和 SoundTouch 静态链接到 HXCPlayer 中
# 支持 iOS 模拟器 + macOS

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
FRAMEWORK_DIR="$SCRIPT_DIR/framework"
BUILD_DIR="$SCRIPT_DIR/build_xcframework"

echo "=========================================="
echo "HXCPlayer XCFramework 构建（一体化）"
echo "=========================================="
echo "项目根目录: $PROJECT_ROOT"
echo "Framework 目录: $FRAMEWORK_DIR"
echo "构建目录: $BUILD_DIR"
echo "=========================================="

# 清理旧构建
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# ==================== 构建 iOS 模拟器版 ====================
echo ""
echo "📱 构建 iOS 模拟器版..."
cd "$BUILD_DIR"
mkdir -p ios-simulator && cd ios-simulator

cmake -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DIOS=ON \
    "$FRAMEWORK_DIR"

xcodebuild archive \
    -scheme HXCPlayer \
    -archivePath "$BUILD_DIR/ios-simulator.xcarchive" \
    -destination "generic/platform=iOS Simulator" \
    SKIP_INSTALL=NO \
    BUILD_LIBRARY_FOR_DISTRIBUTION=YES

# ==================== 构建 macOS 版 ====================
echo ""
echo "💻 构建 macOS 版（仅 arm64）..."
cd "$BUILD_DIR"
mkdir -p macos && cd macos

cmake -G Xcode \
    -DCMAKE_OSX_ARCHITECTURES="arm64" \
    "$FRAMEWORK_DIR"

xcodebuild archive \
    -scheme HXCPlayer \
    -archivePath "$BUILD_DIR/macos.xcarchive" \
    -destination "generic/platform=macOS" \
    SKIP_INSTALL=NO \
    BUILD_LIBRARY_FOR_DISTRIBUTION=YES

# ==================== 创建 XCFramework ====================
echo ""
echo "📦 创建 XCFramework..."
cd "$BUILD_DIR"

xcodebuild -create-xcframework \
    -framework "$BUILD_DIR/ios-simulator.xcarchive/Products/@rpath/HXCPlayer.framework" \
    -framework "$BUILD_DIR/macos.xcarchive/Products/@rpath/HXCPlayer.framework" \
    -output "$BUILD_DIR/HXCPlayer.xcframework"

echo ""
echo "✅ HXCPlayer.xcframework 构建成功！"
echo ""
echo "输出路径: $BUILD_DIR/HXCPlayer.xcframework"
echo ""
echo "=========================================="
echo "使用说明"
echo "=========================================="
echo ""
echo "1. 集成到项目："
echo "   - 将 HXCPlayer.xcframework 拖入 Xcode 项目"
echo "   - 在 General -> Frameworks, Libraries, and Embedded Content"
echo "   - 设置为 Embed & Sign"
echo ""
echo "2. 使用示例："
echo "   #import <HXCPlayer/HXCPlayer.h>"
echo ""
echo "   HXCPlayerControl *player = [[HXCPlayerControl alloc] init];"
echo "   [self.view addSubview:player.videoView];"
echo "   [player openURL:@\"https://example.com/video.mp4\"];"
echo "   [player play];"
echo ""
echo "3. 注意事项："
echo "   - FFmpeg 和 SoundTouch 已静态链接到框架中"
echo "   - 无需额外导入其他依赖"
echo "   - 当前仅支持 iOS 模拟器和 macOS 平台"
echo "   - iOS 真机支持需要重新编译 FFmpeg for iOS device"
echo ""
