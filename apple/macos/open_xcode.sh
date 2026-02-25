#!/bin/bash

# 快速打开 Xcode 项目脚本

cd "$(dirname "$0")"

if [ -d "build/HXCPlayer-macOS.xcodeproj" ]; then
    echo "🚀 打开 Xcode 项目..."
    open build/HXCPlayer-macOS.xcodeproj
else
    echo "❌ Xcode 项目不存在！"
    echo ""
    echo "请先运行构建脚本生成项目:"
    echo "  ./build.sh"
    echo ""
    exit 1
fi
