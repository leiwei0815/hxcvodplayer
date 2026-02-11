#!/bin/bash

# 在 Xcode 中打开项目的便捷脚本

set -e

echo "========================================="
echo "打开 YXVodPlayer Xcode 项目"
echo "========================================="

# 切换到项目根目录
cd "$(dirname "$0")/.."

# 检查 Xcode 项目是否存在
if [ ! -d "build/xcode/YXVodPlayer.xcodeproj" ]; then
    echo "Xcode 项目不存在，正在生成..."
    
    # 创建构建目录
    mkdir -p build/xcode
    cd build/xcode
    
    # 生成 Xcode 项目
    cmake ../.. -G Xcode -DCMAKE_BUILD_TYPE=Debug -DBUILD_DESKTOP=ON
    
    if [ $? -ne 0 ]; then
        echo "错误: Xcode 项目生成失败"
        exit 1
    fi
    
    cd ../..
    echo "Xcode 项目生成成功！"
fi

# 检查测试视频
if [ ! -d "test_videos" ]; then
    echo ""
    echo "生成测试视频..."
    ./scripts/generate_test_video.sh
fi

# 打开 Xcode
echo ""
echo "正在打开 Xcode..."
open build/xcode/YXVodPlayer.xcodeproj

echo ""
echo "========================================="
echo "Xcode 已打开！"
echo "========================================="
echo ""
echo "调试提示:"
echo "1. 选择 'YXVodPlayer' scheme"
echo "2. 按 ⌘R 运行"
echo "3. 或按 ⌘⇧B 进行静态分析"
echo ""
echo "详细调试指南: XCODE_GUIDE.md"
echo "========================================="
