#!/bin/bash
# 构建 HXCPlayer XCFramework（macOS + iOS）

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-xcframework"
OUTPUT_DIR="${PROJECT_ROOT}/output"

echo "========================================="
echo "构建 HXCPlayer XCFramework"
echo "项目根目录: ${PROJECT_ROOT}"
echo "========================================="

# 清理旧构建
echo ""
echo "🧹 清理旧构建..."
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}"

# ========== 构建 iOS 真机版本 ==========
echo ""
echo "📱 [1/3] 构建 iOS 真机版本（arm64）..."
cmake -G Xcode \
    -DIOS=ON \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/ios-device" \
    -S "${PROJECT_ROOT}/apple/framework" \
    -B "${BUILD_DIR}/ios-device-build"

xcodebuild -project "${BUILD_DIR}/ios-device-build/HXCPlayerFramework.xcodeproj" \
    -scheme HXCPlayer \
    -configuration Release \
    -sdk iphoneos \
    -arch arm64 \
    BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
    SKIP_INSTALL=NO \
    -derivedDataPath "${BUILD_DIR}/ios-device-derived"

# ========== 构建 iOS 模拟器版本 ==========
echo ""
echo "🖥️  [2/3] 构建 iOS 模拟器版本（arm64 + x86_64）..."
cmake -G Xcode \
    -DIOS=ON \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/ios-simulator" \
    -S "${PROJECT_ROOT}/apple/framework" \
    -B "${BUILD_DIR}/ios-simulator-build"

xcodebuild -project "${BUILD_DIR}/ios-simulator-build/HXCPlayerFramework.xcodeproj" \
    -scheme HXCPlayer \
    -configuration Release \
    -sdk iphonesimulator \
    -arch "arm64 x86_64" \
    BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
    SKIP_INSTALL=NO \
    -derivedDataPath "${BUILD_DIR}/ios-simulator-derived"

# ========== 构建 macOS 版本 ==========
echo ""
echo "💻 [3/3] 构建 macOS 版本（arm64 + x86_64）..."
cmake -G Xcode \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/macos" \
    -S "${PROJECT_ROOT}/apple/framework" \
    -B "${BUILD_DIR}/macos-build"

xcodebuild -project "${BUILD_DIR}/macos-build/HXCPlayerFramework.xcodeproj" \
    -scheme HXCPlayer \
    -configuration Release \
    -sdk macosx \
    -arch "arm64 x86_64" \
    BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
    SKIP_INSTALL=NO \
    -derivedDataPath "${BUILD_DIR}/macos-derived"

# ========== 创建 XCFramework ==========
echo ""
echo "📦 创建 XCFramework..."

# 查找生成的 framework 路径
IOS_DEVICE_FRAMEWORK=$(find "${BUILD_DIR}/ios-device-derived" -name "HXCPlayer.framework" -path "*/Release-iphoneos/*" | head -n 1)
IOS_SIMULATOR_FRAMEWORK=$(find "${BUILD_DIR}/ios-simulator-derived" -name "HXCPlayer.framework" -path "*/Release-iphonesimulator/*" | head -n 1)
MACOS_FRAMEWORK=$(find "${BUILD_DIR}/macos-derived" -name "HXCPlayer.framework" -path "*/Release/*" | head -n 1)

if [ -z "$IOS_DEVICE_FRAMEWORK" ]; then
    echo "❌ 错误: 未找到 iOS 真机 Framework"
    exit 1
fi

if [ -z "$IOS_SIMULATOR_FRAMEWORK" ]; then
    echo "❌ 错误: 未找到 iOS 模拟器 Framework"
    exit 1
fi

if [ -z "$MACOS_FRAMEWORK" ]; then
    echo "❌ 错误: 未找到 macOS Framework"
    exit 1
fi

echo "  iOS Device Framework: ${IOS_DEVICE_FRAMEWORK}"
echo "  iOS Simulator Framework: ${IOS_SIMULATOR_FRAMEWORK}"
echo "  macOS Framework: ${MACOS_FRAMEWORK}"

# 删除旧的 XCFramework
rm -rf "${OUTPUT_DIR}/HXCPlayer.xcframework"

xcodebuild -create-xcframework \
    -framework "${IOS_DEVICE_FRAMEWORK}" \
    -framework "${IOS_SIMULATOR_FRAMEWORK}" \
    -framework "${MACOS_FRAMEWORK}" \
    -output "${OUTPUT_DIR}/HXCPlayer.xcframework"

echo ""
echo "========================================="
echo "✅ XCFramework 构建成功！"
echo "========================================="
echo "输出路径: ${OUTPUT_DIR}/HXCPlayer.xcframework"
echo ""
echo "📋 包含的平台："
echo "  • iOS 真机（arm64）"
echo "  • iOS 模拟器（arm64 + x86_64）"
echo "  • macOS（arm64 + x86_64 Universal Binary）"
echo ""
echo "🔧 使用方法："
echo "  1. 在 Xcode 项目中：拖拽 HXCPlayer.xcframework 到项目"
echo "  2. General -> Frameworks, Libraries, and Embedded Content"
echo "  3. 选择 'Embed & Sign'"
echo "  4. 在代码中：import HXCPlayer"
echo ""
echo "📦 文件大小："
du -sh "${OUTPUT_DIR}/HXCPlayer.xcframework"
echo ""
