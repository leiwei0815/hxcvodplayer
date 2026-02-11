# YXVodPlayer - 跨平台视频播放器

基于 FFmpeg 和 SDL2 的跨平台视频播放器，支持 Mac、Windows、Android、iOS。

## 🎉 当前状态

✅ **桌面版已完成并优化！**（Mac/Windows）
- 核心播放引擎 100% 完成
- 音视频同步完善
- Qt5 UI 完整实现
- **Xcode 调试环境配置完成**
- 编译通过，测试视频生成完成

🚧 **移动端设计完成，代码待实现**（Android/iOS）

## 🚀 立即开始

### 使用 Xcode 调试（推荐，Mac）

```bash
# 方式 1：一键启动
./scripts/open_xcode.sh

# 方式 2：手动打开
open build/xcode/YXVodPlayer.xcodeproj
# 然后在 Xcode 中按 ⌘R
```

📖 **调试指南**：
- ⭐ [READY_TO_DEBUG.md](READY_TO_DEBUG.md) - 快速开始
- [XCODE_BREAKPOINT_FIX.md](XCODE_BREAKPOINT_FIX.md) - 断点问题解决
- [HOW_TO_DEBUG_IN_XCODE.md](HOW_TO_DEBUG_IN_XCODE.md) - 详细教程

### 或命令行运行

```bash
./build.sh desktop release
open build/desktop_release/bin/YXVodPlayer.app
```

## 特性

- 🎬 支持多种视频格式（MP4、MKV、AVI、FLV 等）
- 🎵 音视频同步播放
- 🖥️ 跨平台支持（Mac、Windows、Android、iOS）
- ⚡ 基于 FFplay 架构设计
- 🎨 现代化 UI 界面

## 架构设计

### 核心模块
- **解复用器**：负责解析视频容器格式
- **解码器**：音频和视频解码
- **同步控制**：音视频同步机制
- **帧队列**：高效的帧缓冲管理
- **渲染引擎**：视频渲染和音频输出

### 平台实现
- **Mac/Windows**：使用 Qt5 + SDL2
- **Android**：使用 MediaCodec + SurfaceView
- **iOS**：使用 AVFoundation + Metal

## 依赖

- FFmpeg 8.x
- SDL2
- Qt5（桌面端）
- CMake 3.15+
- C++17

## 构建

### Mac/Windows

**使用构建脚本**（推荐）:
```bash
# 构建 Release 版本
./build.sh desktop release

# 构建 Debug 版本
./build.sh desktop debug

# 生成 Xcode 项目（Mac）
./build.sh xcode
# 或使用快捷脚本
./scripts/open_xcode.sh
```

**手动构建**:
```bash
mkdir build && cd build
cmake ..
make
```

### Android
使用 Android Studio 打开 `src/android` 目录

### iOS
使用 Xcode 打开 `src/ios/YXVodPlayer.xcodeproj`

## 目录结构

```
YXVodPlayer/
├── src/
│   ├── core/           # 核心播放器引擎
│   ├── platform/       # 平台抽象层
│   ├── desktop/        # Desktop (Qt5) 实现
│   ├── android/        # Android 实现
│   └── ios/            # iOS 实现
├── include/            # 公共头文件
├── build/              # 构建输出
└── CMakeLists.txt      # CMake 构建文件
```

## License

MIT
