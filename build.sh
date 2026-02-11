#!/bin/bash

# YXVodPlayer 构建脚本
# 使用方法: ./build.sh [platform] [build_type]
# 平台: desktop, android, ios
# 类型: debug, release

set -e

PLATFORM=${1:-desktop}
BUILD_TYPE=${2:-release}

echo "========================================="
echo "YXVodPlayer 构建脚本"
echo "平台: $PLATFORM"
echo "类型: $BUILD_TYPE"
echo "========================================="

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

function print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

function print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

function print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查依赖
function check_dependencies() {
    print_info "检查依赖..."
    
    # 检查 CMake
    if ! command -v cmake &> /dev/null; then
        print_error "CMake 未安装"
        exit 1
    fi
    
    # 检查 FFmpeg
    if ! pkg-config --exists libavcodec libavformat libavutil; then
        print_error "FFmpeg 未安装或未配置 pkg-config"
        exit 1
    fi
    
    print_info "依赖检查完成"
}

# 构建 Desktop 版本
function build_desktop() {
    print_info "构建 Desktop 版本..."
    
    # 检查 SDL2
    if ! pkg-config --exists sdl2; then
        print_error "SDL2 未安装"
        exit 1
    fi
    
    # 检查 Qt5
    if [ -z "$Qt5_DIR" ]; then
        if [ "$(uname)" == "Darwin" ]; then
            # macOS
            export Qt5_DIR=/usr/local/opt/qt@5/lib/cmake/Qt5
        fi
    fi
    
    # 创建构建目录
    BUILD_DIR="build/desktop_${BUILD_TYPE}"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    # CMake 配置
    CMAKE_BUILD_TYPE="Release"
    if [ "$BUILD_TYPE" == "debug" ]; then
        CMAKE_BUILD_TYPE="Debug"
    fi
    
    print_info "运行 CMake 配置..."
    cmake ../.. \
        -DCMAKE_BUILD_TYPE=$CMAKE_BUILD_TYPE \
        -DBUILD_DESKTOP=ON
    
    # 编译
    print_info "编译中..."
    CPU_COUNT=$(getconf _NPROCESSORS_ONLN)
    make -j$CPU_COUNT
    
    cd ../..
    
    print_info "Desktop 版本构建完成！"
    print_info "可执行文件: $BUILD_DIR/bin/YXVodPlayer"
}

# 构建 Android 版本
function build_android() {
    print_info "构建 Android 版本..."
    
    # 检查 Android 开发环境
    if [ -z "$ANDROID_HOME" ] && [ -z "$ANDROID_SDK_ROOT" ]; then
        print_error "Android SDK 未配置"
        exit 1
    fi
    
    cd android
    
    # Gradle 构建
    if [ "$BUILD_TYPE" == "debug" ]; then
        ./gradlew assembleDebug
        print_info "Android Debug APK 构建完成！"
        print_info "APK 位置: android/app/build/outputs/apk/debug/"
    else
        ./gradlew assembleRelease
        print_info "Android Release APK 构建完成！"
        print_info "APK 位置: android/app/build/outputs/apk/release/"
    fi
    
    cd ..
}

# 构建 iOS 版本
function build_ios() {
    print_info "构建 iOS 版本..."
    
    # 检查 macOS
    if [ "$(uname)" != "Darwin" ]; then
        print_error "iOS 只能在 macOS 上构建"
        exit 1
    fi
    
    # 检查 Xcode
    if ! command -v xcodebuild &> /dev/null; then
        print_error "Xcode 未安装"
        exit 1
    fi
    
    cd ios
    
    # 选择配置
    CONFIGURATION="Release"
    if [ "$BUILD_TYPE" == "debug" ]; then
        CONFIGURATION="Debug"
    fi
    
    # 清理
    print_info "清理之前的构建..."
    xcodebuild clean \
        -project YXVodPlayer.xcodeproj \
        -scheme YXVodPlayer \
        -configuration $CONFIGURATION
    
    # 构建
    print_info "构建 iOS 应用..."
    xcodebuild \
        -project YXVodPlayer.xcodeproj \
        -scheme YXVodPlayer \
        -sdk iphoneos \
        -configuration $CONFIGURATION \
        build
    
    print_info "iOS 版本构建完成！"
    
    cd ..
}

# 清理构建
function clean() {
    print_info "清理构建文件..."
    
    rm -rf build/
    
    if [ -d "android" ]; then
        cd android
        ./gradlew clean || true
        cd ..
    fi
    
    if [ -d "ios" ]; then
        cd ios
        xcodebuild clean || true
        cd ..
    fi
    
    print_info "清理完成"
}

# 运行测试
function run_tests() {
    print_info "运行测试..."
    
    if [ -f "build/desktop_release/bin/YXVodPlayer_test" ]; then
        ./build/desktop_release/bin/YXVodPlayer_test
    else
        print_warn "测试程序未找到，请先构建"
    fi
}

# 主函数
function main() {
    case $PLATFORM in
        desktop)
            check_dependencies
            build_desktop
            ;;
        xcode)
            check_dependencies
            print_info "生成 Xcode 项目..."
            mkdir -p build/xcode
            cd build/xcode
            cmake ../.. -G Xcode -DCMAKE_BUILD_TYPE=Debug -DBUILD_DESKTOP=ON
            cd ../..
            print_info "Xcode 项目生成完成！"
            print_info "运行: open build/xcode/YXVodPlayer.xcodeproj"
            ;;
        android)
            build_android
            ;;
        ios)
            build_ios
            ;;
        clean)
            clean
            ;;
        test)
            run_tests
            ;;
        all)
            check_dependencies
            build_desktop
            build_android
            build_ios
            ;;
        *)
            echo "用法: $0 {desktop|xcode|android|ios|all|clean|test} [debug|release]"
            exit 1
            ;;
    esac
}

main
