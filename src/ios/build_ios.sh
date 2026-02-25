#!/bin/bash

# iOS 项目构建脚本
# 使用方法: ./build_ios.sh [simulator|device]

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$SCRIPT_DIR/../../.."
BUILD_DIR="$SCRIPT_DIR/build"
IOS_BUILD_DIR="$BUILD_DIR/ios"

# 检查参数
PLATFORM="${1:-simulator}"

if [ "$PLATFORM" = "simulator" ]; then
    echo "📱 构建 iOS 模拟器版本..."
    # 只使用 arm64（Apple Silicon 模拟器）
    CMAKE_TOOLCHAIN_ARGS="-DCMAKE_OSX_SYSROOT=iphonesimulator -DCMAKE_OSX_ARCHITECTURES=arm64"
elif [ "$PLATFORM" = "device" ]; then
    echo "📱 构建 iOS 真机版本..."
    CMAKE_TOOLCHAIN_ARGS="-DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64"
else
    echo "❌ 错误: 未知的平台 '$PLATFORM'"
    echo "用法: $0 [simulator|device]"
    exit 1
fi

# 清理旧的构建目录
if [ -d "$IOS_BUILD_DIR" ]; then
    echo "🧹 清理旧的构建目录..."
    rm -rf "$IOS_BUILD_DIR"
fi

# 创建构建目录
mkdir -p "$IOS_BUILD_DIR"
cd "$IOS_BUILD_DIR"

# 生成 Xcode 项目
echo "🔨 生成 Xcode 项目..."
cmake "$BUILD_DIR" \
    -G Xcode \
    $CMAKE_TOOLCHAIN_ARGS \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY

echo ""
echo "✅ Xcode 项目生成完成！"
echo ""
echo "📂 项目位置: $IOS_BUILD_DIR/YXVodPlayer-iOS.xcodeproj"
echo ""
echo "🚀 打开 Xcode 项目:"
echo "   open $IOS_BUILD_DIR/YXVodPlayer-iOS.xcodeproj"
echo ""
echo "📝 注意事项:"
echo "   1. 在 Xcode 中选择目标设备（模拟器或真机）"
echo "   2. 如果是真机，需要配置开发者证书和 Team"
echo "   3. 点击 Run 按钮即可运行"
echo ""
