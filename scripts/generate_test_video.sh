#!/bin/bash

# 生成测试视频脚本

echo "生成测试视频..."

OUTPUT_DIR="test_videos"
mkdir -p "$OUTPUT_DIR"

# 检查 ffmpeg
if ! command -v ffmpeg &> /dev/null; then
    echo "错误: 需要安装 ffmpeg"
    exit 1
fi

# 生成测试视频 1: 简单的彩色测试图案 (10秒)
echo "生成测试视频 1: 彩色测试图案..."
ffmpeg -f lavfi -i testsrc=duration=10:size=1280x720:rate=30 \
       -f lavfi -i sine=frequency=1000:duration=10 \
       -pix_fmt yuv420p \
       -c:v libx264 -preset fast \
       -c:a aac \
       -y "$OUTPUT_DIR/test1_pattern.mp4" 2>&1 | grep -E "(error|duration|Stream)" || true

# 生成测试视频 2: 纯色背景 + 移动的文字 (5秒)
echo "生成测试视频 2: 纯色背景..."
ffmpeg -f lavfi -i color=c=blue:s=1280x720:d=5 \
       -f lavfi -i sine=frequency=440:duration=5 \
       -vf "drawtext=text='YXVodPlayer Test':fontsize=60:fontcolor=white:x=(w-text_w)/2:y=(h-text_h)/2" \
       -pix_fmt yuv420p \
       -c:v libx264 -preset fast \
       -c:a aac \
       -y "$OUTPUT_DIR/test2_simple.mp4" 2>&1 | grep -E "(error|duration|Stream)" || true

# 生成测试视频 3: 只有视频，没有音频
echo "生成测试视频 3: 仅视频..."
ffmpeg -f lavfi -i testsrc=duration=5:size=640x480:rate=30 \
       -pix_fmt yuv420p \
       -c:v libx264 -preset fast \
       -y "$OUTPUT_DIR/test3_video_only.mp4" 2>&1 | grep -E "(error|duration|Stream)" || true

# 生成测试视频 4: 不同分辨率
echo "生成测试视频 4: 4K 分辨率..."
ffmpeg -f lavfi -i testsrc=duration=3:size=3840x2160:rate=30 \
       -f lavfi -i sine=frequency=800:duration=3 \
       -pix_fmt yuv420p \
       -c:v libx264 -preset ultrafast \
       -c:a aac \
       -y "$OUTPUT_DIR/test4_4k.mp4" 2>&1 | grep -E "(error|duration|Stream)" || true

echo ""
echo "测试视频生成完成!"
echo "视频保存在: $OUTPUT_DIR/"
ls -lh "$OUTPUT_DIR/"
