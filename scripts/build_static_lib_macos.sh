#!/bin/bash
# 构建 macOS 静态库（用于 Qt 或其他 C++ 项目）

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-static-macos"
OUTPUT_DIR="${PROJECT_ROOT}/output/macos-static"

echo "========================================="
echo "构建 macOS 静态库（通用库）"
echo "项目根目录: ${PROJECT_ROOT}"
echo "========================================="

# 清理旧构建
echo ""
echo "🧹 清理旧构建..."
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}"

# ========== 构建静态库 ==========
echo ""
echo "💻 构建 macOS 静态库（arm64 + x86_64 Universal Binary）..."

cmake -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DBUILD_DESKTOP=OFF \
    -DBUILD_TESTS=OFF \
    -S "${PROJECT_ROOT}" \
    -B "${BUILD_DIR}"

cmake --build "${BUILD_DIR}" --config Release -j8

# ========== 复制输出文件 ==========
echo ""
echo "📦 复制输出文件..."

# 复制静态库
cp "${BUILD_DIR}/core/src/libhxcplayer_core.a" "${OUTPUT_DIR}/"

# 复制头文件
mkdir -p "${OUTPUT_DIR}/include"
cp -r "${PROJECT_ROOT}/core/include/"* "${OUTPUT_DIR}/include/"

# 创建使用说明
cat > "${OUTPUT_DIR}/README.md" << 'EOF'
# HXCPlayer 静态库（macOS）

## 文件说明

- `libhxcplayer_core.a` - 静态库（Universal Binary: arm64 + x86_64）
- `include/` - 头文件

## 架构支持

- arm64（Apple Silicon）
- x86_64（Intel）

## 链接方法

### CMake 项目

```cmake
# 添加头文件路径
include_directories(/path/to/macos-static/include)

# 链接静态库
target_link_libraries(your_target
    /path/to/macos-static/libhxcplayer_core.a
    # FFmpeg 库
    avcodec avformat avutil swscale swresample
    # 系统框架
    "-framework CoreFoundation"
    "-framework CoreAudio"
    "-framework AudioToolbox"
)
```

### 直接编译

```bash
g++ -std=c++17 \
    -I/path/to/macos-static/include \
    your_code.cpp \
    /path/to/macos-static/libhxcplayer_core.a \
    -lavcodec -lavformat -lavutil -lswscale -lswresample \
    -framework CoreFoundation -framework CoreAudio -framework AudioToolbox \
    -o your_app
```

## 依赖要求

- FFmpeg 5.x 或更高版本（通过 Homebrew 安装）
- SoundTouch（可选，用于倍速播放）

```bash
brew install ffmpeg soundtouch
```

## 使用示例

```cpp
#include "hxc_player_core_c_bridge.h"

int main() {
    // 创建播放器
    PlayerCoreHandle* player = player_core_create();
    
    // 打开视频
    player_core_open(player, "video.mp4");
    
    // 播放
    player_core_play(player);
    
    // 释放
    player_core_release(player);
    return 0;
}
```
EOF

echo ""
echo "========================================="
echo "✅ macOS 静态库构建成功！"
echo "========================================="
echo "输出路径: ${OUTPUT_DIR}"
echo ""
echo "📋 包含的文件："
ls -lh "${OUTPUT_DIR}"
echo ""
echo "🔍 静态库架构信息："
lipo -info "${OUTPUT_DIR}/libhxcplayer_core.a"
echo ""
echo "📄 详细说明请查看："
echo "  ${OUTPUT_DIR}/README.md"
echo ""
