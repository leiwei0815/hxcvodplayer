#!/bin/bash

# NDK 环境检查脚本

echo "=========================================="
echo "Android NDK 环境检查"
echo "=========================================="
echo ""

# 检查 NDK 环境变量
if [ -z "$ANDROID_NDK" ]; then
    echo "⚠️  ANDROID_NDK 环境变量未设置"
    echo ""
    
    # 尝试自动查找
    if [ -d "$HOME/Library/Android/sdk/ndk" ]; then
        NDK_PATH=$(ls -d $HOME/Library/Android/sdk/ndk/* | tail -1)
        echo "✅ 自动检测到 NDK: $NDK_PATH"
        export ANDROID_NDK="$NDK_PATH"
    else
        echo "❌ 未找到 NDK 安装"
        exit 1
    fi
else
    NDK_PATH="$ANDROID_NDK"
    echo "✅ ANDROID_NDK: $NDK_PATH"
fi

echo ""
echo "📋 NDK 信息:"
echo "----------------------------------------"

# 检查 NDK 版本
if [ -f "$NDK_PATH/source.properties" ]; then
    echo "版本信息:"
    grep "Pkg.Revision" "$NDK_PATH/source.properties" | sed 's/Pkg.Revision = /  版本: /'
    grep "Pkg.Desc" "$NDK_PATH/source.properties" | sed 's/Pkg.Desc = /  描述: /'
else
    echo "  未找到 source.properties"
fi

echo ""
echo "📂 目录结构:"
echo "  NDK 路径: $NDK_PATH"
ls -d "$NDK_PATH"/* 2>/dev/null | head -10 | sed 's/^/  /'

echo ""
echo "🔧 工具链检查:"
echo "----------------------------------------"

TOOLCHAIN="$NDK_PATH/toolchains/llvm/prebuilt/darwin-x86_64"

# 检查工具链目录
if [ -d "$TOOLCHAIN" ]; then
    echo "✅ 工具链目录存在: $TOOLCHAIN"
else
    echo "❌ 工具链目录不存在: $TOOLCHAIN"
    echo ""
    echo "可能的原因:"
    echo "  1. NDK 版本过旧或过新"
    echo "  2. 安装不完整"
    exit 1
fi

echo ""
echo "🔨 编译器检查:"
echo "----------------------------------------"

# 检查 clang
CLANG="$TOOLCHAIN/bin/aarch64-linux-android24-clang"
if [ -f "$CLANG" ]; then
    echo "✅ clang 存在: $CLANG"
    echo "   版本:"
    "$CLANG" --version 2>&1 | head -3 | sed 's/^/   /'
else
    echo "❌ clang 不存在: $CLANG"
fi

echo ""

# 检查 llvm 工具
LLVM_AR="$TOOLCHAIN/bin/llvm-ar"
LLVM_RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
LLVM_STRIP="$TOOLCHAIN/bin/llvm-strip"

if [ -f "$LLVM_AR" ]; then
    echo "✅ llvm-ar: $LLVM_AR"
else
    echo "❌ llvm-ar 不存在"
fi

if [ -f "$LLVM_RANLIB" ]; then
    echo "✅ llvm-ranlib: $LLVM_RANLIB"
else
    echo "❌ llvm-ranlib 不存在"
fi

if [ -f "$LLVM_STRIP" ]; then
    echo "✅ llvm-strip: $LLVM_STRIP"
else
    echo "❌ llvm-strip 不存在"
fi

echo ""
echo "📦 CMake Android 工具链:"
echo "----------------------------------------"

CMAKE_TOOLCHAIN="$NDK_PATH/build/cmake/android.toolchain.cmake"
if [ -f "$CMAKE_TOOLCHAIN" ]; then
    echo "✅ CMake 工具链文件存在"
    echo "   路径: $CMAKE_TOOLCHAIN"
else
    echo "❌ CMake 工具链文件不存在"
fi

echo ""
echo "🧪 测试编译:"
echo "----------------------------------------"

# 创建测试文件
TEST_DIR=$(mktemp -d)
TEST_FILE="$TEST_DIR/test.c"

cat > "$TEST_FILE" << 'EOF'
#include <stdio.h>
int main() {
    printf("Hello Android NDK!\n");
    return 0;
}
EOF

echo "测试编译简单的 C 程序..."
if "$CLANG" -o "$TEST_DIR/test" "$TEST_FILE" 2>&1 | head -5; then
    if [ -f "$TEST_DIR/test" ]; then
        echo "✅ 编译成功!"
        ls -lh "$TEST_DIR/test"
    else
        echo "⚠️  编译命令执行了，但未生成可执行文件"
    fi
else
    echo "❌ 编译失败"
fi

# 清理
rm -rf "$TEST_DIR"

echo ""
echo "=========================================="
echo "✅ 检查完成"
echo "=========================================="
echo ""

if [ -f "$CLANG" ] && [ -f "$LLVM_AR" ] && [ -f "$CMAKE_TOOLCHAIN" ]; then
    echo "✅ NDK 环境配置正确，可以开始编译！"
    echo ""
    echo "下一步："
    echo "  cd /Users/debug/project/YXVodPlayer/android-third"
    echo "  ./build_all.sh"
else
    echo "⚠️  NDK 环境存在问题，请检查以上输出"
fi

echo ""
