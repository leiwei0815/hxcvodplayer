#!/bin/bash

# 快速测试脚本

set -e

echo "========================================="
echo "YXVodPlayer 快速测试"
echo "========================================="

# 切换到项目根目录
cd "$(dirname "$0")/.."

# 检查构建
if [ ! -f "build/desktop_release/bin/YXVodPlayer" ]; then
    echo "播放器未构建，正在构建..."
    ./build.sh desktop release
fi

# 生成测试视频
if [ ! -d "test_videos" ]; then
    echo "生成测试视频..."
    chmod +x scripts/generate_test_video.sh
    ./scripts/generate_test_video.sh
fi

# 运行播放器
echo ""
echo "启动播放器..."
echo "尝试打开测试视频: test_videos/test1_pattern.mp4"
echo ""

if [ -f "test_videos/test1_pattern.mp4" ]; then
    ./build/desktop_release/bin/YXVodPlayer test_videos/test1_pattern.mp4
else
    echo "警告: 测试视频未找到，启动空播放器"
    ./build/desktop_release/bin/YXVodPlayer
fi
