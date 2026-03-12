#!/bin/bash

# HXCPlayer Android AAR 构建脚本
# 位置：android/library/build_aar.sh
# 用途：构建 HXCPlayer AAR 库

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_info() { echo -e "${BLUE}ℹ️  $1${NC}"; }
print_success() { echo -e "${GREEN}✅ $1${NC}"; }
print_warning() { echo -e "${YELLOW}⚠️  $1${NC}"; }
print_error() { echo -e "${RED}❌ $1${NC}"; }
print_header() {
    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""
}

show_usage() {
    echo "用法: $0 [BUILD_TYPE] [OPTIONS]"
    echo ""
    echo "BUILD_TYPE:"
    echo "  debug      - 打包 Debug 版本 AAR"
    echo "  release    - 打包 Release 版本 AAR (默认)"
    echo "  all        - 同时打包 Debug 和 Release 版本"
    echo ""
    echo "OPTIONS:"
    echo "  --clean        - 打包前先清理项目"
    echo "  --copy-libs    - 从 android-third 复制第三方库"
    echo "  --copy-to-test - 复制 AAR 到测试项目 (examples/android-test/app/libs)"
    echo "  --help         - 显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0                              # 打包 Release 版本"
    echo "  $0 debug                        # 打包 Debug 版本"
    echo "  $0 release --copy-to-test       # 打包并复制到测试项目"
    echo "  $0 all --clean --copy-to-test   # 清理、打包、复制"
    echo ""
}

BUILD_TYPE="release"
CLEAN_BUILD=false
COPY_LIBS=false
COPY_TO_TEST=false

while [[ $# -gt 0 ]]; do
    case $1 in
        debug|release|all) BUILD_TYPE="$1"; shift ;;
        --clean) CLEAN_BUILD=true; shift ;;
        --copy-libs) COPY_LIBS=true; shift ;;
        --copy-to-test) COPY_TO_TEST=true; shift ;;
        --help|-h) show_usage; exit 0 ;;
        *) print_error "未知参数: $1"; show_usage; exit 1 ;;
    esac
done

print_header "HXCPlayer AAR 构建工具"

# 设置 JAVA_HOME
print_info "设置 Java 环境..."
if [ -z "$JAVA_HOME" ]; then
    if [ -d "/Applications/Android Studio.app/Contents/jbr/Contents/Home" ]; then
        export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
        print_success "使用 Android Studio JDK"
    elif command -v /usr/libexec/java_home &> /dev/null; then
        export JAVA_HOME=$(/usr/libexec/java_home)
        print_success "使用系统 JDK"
    else
        print_error "无法找到 Java 环境！"
        exit 1
    fi
fi

# 复制第三方库（如果需要）
if [ "$COPY_LIBS" = true ]; then
    print_info "从 android-third 复制第三方库..."
    
    TARGET_JNI="$SCRIPT_DIR/../hxcplayer/src/main/jniLibs"
    THIRD_PARTY_ROOT="$SCRIPT_DIR/../../android-third"
    
    mkdir -p "$TARGET_JNI"
    
    for ABI in "arm64-v8a" "armeabi-v7a" "x86_64"; do
        mkdir -p "$TARGET_JNI/$ABI"
        
        # 复制 FFmpeg
        if [ -d "$THIRD_PARTY_ROOT/ffmpeg-build-android/FFmpeg-Android/$ABI/lib" ]; then
            cp "$THIRD_PARTY_ROOT/ffmpeg-build-android/FFmpeg-Android/$ABI/lib/"*.so "$TARGET_JNI/$ABI/" 2>/dev/null || true
        fi
        
        # 复制 SoundTouch
        if [ -d "$THIRD_PARTY_ROOT/soundtouch-build-android/SoundTouch-Android/$ABI/lib" ]; then
            cp "$THIRD_PARTY_ROOT/soundtouch-build-android/SoundTouch-Android/$ABI/lib/"*.so "$TARGET_JNI/$ABI/" 2>/dev/null || true
        fi
        
        # 复制 mbedTLS
        if [ -d "$THIRD_PARTY_ROOT/mbedtls-build-android/mbedTLS-Android/$ABI/lib" ]; then
            cp "$THIRD_PARTY_ROOT/mbedtls-build-android/mbedTLS-Android/$ABI/lib/"*.so "$TARGET_JNI/$ABI/" 2>/dev/null || true
        fi
        
        echo "  ✅ $ABI"
    done
    
    print_success "第三方库复制完成"
fi

# 清理项目（如果需要）
if [ "$CLEAN_BUILD" = true ]; then
    print_info "清理项目..."
    ./gradlew clean
    print_success "清理完成"
fi

# 打包函数
build_aar() {
    local TYPE=$1
    local TASK=$2
    
    print_info "开始打包 $TYPE 版本..."
    echo ""
    
    if ./gradlew "$TASK"; then
        print_success "$TYPE 版本打包完成！"
        
        local AAR_FILE="../hxcplayer/build/outputs/aar/hxcplayer-${TYPE}.aar"
        if [ -f "$AAR_FILE" ]; then
            local FILE_SIZE=$(du -h "$AAR_FILE" | cut -f1)
            print_info "文件: $AAR_FILE"
            print_info "大小: $FILE_SIZE"
            
            echo ""
            print_info "AAR 内容预览:"
            unzip -l "$AAR_FILE" | grep -E "\.so$" | head -15
            echo "    ..."
        fi
        
        return 0
    else
        print_error "$TYPE 版本打包失败！"
        return 1
    fi
}

# 根据 BUILD_TYPE 执行打包
case $BUILD_TYPE in
    debug)
        print_header "打包 Debug 版本"
        build_aar "debug" ":hxcplayer:assembleDebug"
        ;;
    
    release)
        print_header "打包 Release 版本"
        build_aar "release" ":hxcplayer:assembleRelease"
        ;;
    
    all)
        print_header "打包所有版本"
        print_info "正在打包 Debug 和 Release 版本..."
        echo ""
        
        if ./gradlew :hxcplayer:assemble; then
            print_success "所有版本打包完成！"
            
            echo ""
            print_info "输出文件:"
            
            local DEBUG_AAR="../hxcplayer/build/outputs/aar/hxcplayer-debug.aar"
            if [ -f "$DEBUG_AAR" ]; then
                local DEBUG_SIZE=$(du -h "$DEBUG_AAR" | cut -f1)
                echo "  📦 Debug:   $DEBUG_AAR ($DEBUG_SIZE)"
            fi
            
            local RELEASE_AAR="../hxcplayer/build/outputs/aar/hxcplayer-release.aar"
            if [ -f "$RELEASE_AAR" ]; then
                local RELEASE_SIZE=$(du -h "$RELEASE_AAR" | cut -f1)
                echo "  📦 Release: $RELEASE_AAR ($RELEASE_SIZE)"
            fi
        else
            print_error "打包失败！"
            exit 1
        fi
        ;;
esac

print_header "打包完成"

# 复制到测试项目（如果需要）
if [ "$COPY_TO_TEST" = true ]; then
    print_info "复制 AAR 到测试项目..."
    
    TEST_LIBS_DIR="$SCRIPT_DIR/../../examples/android-test/app/libs"
    mkdir -p "$TEST_LIBS_DIR"
    
    case $BUILD_TYPE in
        debug)
            AAR_SRC="../hxcplayer/build/outputs/aar/hxcplayer-debug.aar"
            if [ -f "$AAR_SRC" ]; then
                cp "$AAR_SRC" "$TEST_LIBS_DIR/"
                print_success "已复制 Debug AAR 到: $TEST_LIBS_DIR/"
            fi
            ;;
        
        release)
            AAR_SRC="../hxcplayer/build/outputs/aar/hxcplayer-release.aar"
            if [ -f "$AAR_SRC" ]; then
                cp "$AAR_SRC" "$TEST_LIBS_DIR/"
                print_success "已复制 Release AAR 到: $TEST_LIBS_DIR/"
            fi
            ;;
        
        all)
            # 复制 Release 版本（推荐用于测试）
            AAR_SRC="../hxcplayer/build/outputs/aar/hxcplayer-release.aar"
            if [ -f "$AAR_SRC" ]; then
                cp "$AAR_SRC" "$TEST_LIBS_DIR/"
                print_success "已复制 Release AAR 到: $TEST_LIBS_DIR/"
            fi
            
            # 也复制 Debug 版本
            AAR_SRC="../hxcplayer/build/outputs/aar/hxcplayer-debug.aar"
            if [ -f "$AAR_SRC" ]; then
                cp "$AAR_SRC" "$TEST_LIBS_DIR/"
                print_success "已复制 Debug AAR 到: $TEST_LIBS_DIR/"
            fi
            ;;
    esac
    
    echo ""
    print_info "测试项目中的 AAR:"
    ls -lh "$TEST_LIBS_DIR/" | grep ".aar"
    echo ""
fi

print_info "输出目录: ../hxcplayer/build/outputs/aar/"
echo ""
print_info "使用 AAR:"
echo "  1. 复制 AAR 到项目的 libs/ 目录 (或使用 --copy-to-test)"
echo "  2. 在 build.gradle 中添加:"
echo "     implementation files('libs/hxcplayer-release.aar')"
echo ""

print_success "🎉 全部完成！"
