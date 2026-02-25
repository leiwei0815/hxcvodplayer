#!/bin/bash

# HXCPlayer macOS 构建脚本 - 生成 Xcode 项目

set -e

echo "========================================"
echo "HXC Player - macOS Native 构建脚本"
echo "生成 Xcode 项目"
echo "========================================"

# 进入 macOS 项目目录
cd "$(dirname "$0")"

# 清理旧的构建
if [ -d "build" ]; then
    echo "🧹 清理旧的构建目录..."
    rm -rf build
fi

# 创建构建目录
echo "📁 创建构建目录..."
mkdir -p build
cd build

# 运行 CMake 生成 Xcode 项目
echo "🔧 运行 CMake 生成 Xcode 项目..."
cmake -G Xcode ..

# 检查生成结果
if [ $? -eq 0 ]; then
    echo ""
    echo "✅ Xcode 项目生成成功！"
    echo ""
    echo "项目路径: $(pwd)/HXCPlayer-macOS.xcodeproj"
    echo ""
    echo "打开 Xcode 项目:"
    echo "  open HXCPlayer-macOS.xcodeproj"
    echo ""
    echo "或者在 Xcode 中编译并运行:"
    echo "  1. 打开 HXCPlayer-macOS.xcodeproj"
    echo "  2. 选择 Scheme: HXCPlayer-macOS"
    echo "  3. 点击 Run (⌘R) 运行"
    echo ""
    echo "命令行编译（可选）:"
    echo "  xcodebuild -project HXCPlayer-macOS.xcodeproj -scheme HXCPlayer-macOS -configuration Debug"
    echo ""
    
    # 自动打开 Xcode
    echo "🚀 打开 Xcode..."
    open HXCPlayer-macOS.xcodeproj
else
    echo ""
    echo "❌ Xcode 项目生成失败！"
    echo ""
    exit 1
fi
