# HXCPlayer - 跨平台视频播放器

基于 FFmpeg 的跨平台视频播放器，支持变速播放（SoundTouch）。

## 🎉 当前状态

✅ **核心功能已完成！**
- 核心播放引擎 100% 完成
- 音视频同步完善
- 变速播放支持（0.5x - 2.0x）
- 多平台原生渲染

✅ **平台支持**
- **macOS 原生版**：Cocoa + AVFoundation + AudioQueue ✅
- **iOS 原生版**：UIKit + AVFoundation + AudioQueue ✅
- **Windows Desktop 版**：Qt5 + SDL2 ✅
- **Linux Desktop 版**：Qt5 + SDL2 ✅

## 🚀 立即开始

### macOS 原生版

```bash
cd apple/macos
./build.sh
# 会自动打开 Xcode 项目
```

或者在 Xcode 中运行：
```bash
open apple/macos/build/HXCPlayer-macOS.xcodeproj
```

### iOS 版

```bash
cd apple/ios
./build_ios.sh
# 会自动打开 Xcode 项目
```

或者在 Xcode 中运行：
```bash
open apple/ios/build/YXVodPlayer-iOS.xcodeproj
```

### Desktop 版（Qt）

**macOS / Linux:**
```bash
mkdir build_desktop && cd build_desktop
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8
# macOS
open bin/YXVodPlayer.app
# Linux
./bin/YXVodPlayer
```

**Windows:**
```cmd
# 快速开始（推荐）
quickstart_windows.bat

# 或手动生成 Visual Studio 项目
build_windows.bat vs2022

# 详细说明请参考
README_WINDOWS.md
```

## 特性

- 🎬 支持多种视频格式（MP4、MKV、AVI、FLV 等）
- 🎵 音视频同步播放
- ⚡ 变速播放（0.5x - 2.0x，保持音调）
- 🖼️ 视频显示模式（适应/填充）
- 🖥️ 跨平台支持（macOS、iOS、Windows）
- 🎨 现代化原生 UI 界面

## 架构设计

### 核心模块（C++）
- **解复用器**：负责解析视频容器格式（FFmpeg）
- **解码器**：音频和视频解码（FFmpeg）
- **同步控制**：音视频同步机制（音频主时钟）
- **帧队列**：高效的帧缓冲管理
- **变速处理**：音频变速不变调（SoundTouch）

### 平台实现
- **macOS/iOS**：统一的 `HXCPlayerControl` 类，使用系统原生渲染
  - 视频：`AVSampleBufferDisplayLayer`
  - 音频：`AudioQueue`
  - 同步：`CVDisplayLink`（macOS）/ `CADisplayLink`（iOS）
- **Desktop**：Qt5 UI + SDL2 渲染

## 依赖

### 核心依赖
- FFmpeg 6.x+
- SoundTouch
- CMake 3.15+
- C++17

### 平台依赖
- **macOS/iOS**：Xcode 14+、iOS 13.0+、macOS 11.0+
- **Desktop (Qt)**：Qt5、SDL2
- **Windows**：Visual Studio 2019/2022、vcpkg（推荐）

## 构建

### macOS 原生版

需要先安装依赖：
```bash
brew install ffmpeg soundtouch
```

然后构建：
```bash
cd apple/macos
./build.sh
```

### iOS 版

需要预先编译 iOS 版本的 FFmpeg 和 SoundTouch 库，放置在：
- `ios-third/ffmpeg-build/FFmpeg-iOS/`
- `ios-third/soundtouch-build/SoundTouch-iOS/`

然后构建：
```bash
cd apple/ios
./build_ios.sh
```

### Desktop 版

安装依赖：
```bash
# macOS
brew install qt@5 sdl2 ffmpeg soundtouch

# Ubuntu/Debian
sudo apt install qtbase5-dev libsdl2-dev libavcodec-dev \
  libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
  libsoundtouch-dev

# Windows (使用 vcpkg)
# 运行自动安装脚本
powershell -ExecutionPolicy Bypass -File setup_windows_deps.ps1

# 或手动安装
vcpkg install ffmpeg:x64-windows sdl2:x64-windows soundtouch:x64-windows qt5-base:x64-windows qt5-multimedia:x64-windows
```

构建：
```bash
# macOS / Linux
mkdir build_desktop && cd build_desktop
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8

# Windows
# 使用快速开始脚本
quickstart_windows.bat

# 或使用 build_windows.bat
build_windows.bat vs2022 release

# 详细的 Windows 构建说明请参考 README_WINDOWS.md
```

## 目录结构

```
YXVodPlayer/
├── core/                   # 核心播放器引擎（C++）
│   ├── include/           # 公共头文件
│   └── src/               # 核心实现
├── apple/                  # Apple 平台（iOS & macOS）
│   ├── HXCPlayerControl.h/mm  # 统一播放器控制类
│   ├── ios/               # iOS 特定代码
│   └── macos/             # macOS 特定代码
├── desktop/               # Desktop (Qt5) 实现
├── ios-third/             # iOS 第三方库
├── docs/                  # 文档
└── CMakeLists.txt         # 主 CMake 配置
```

## API 使用示例

### macOS/iOS

```objc
#import "HXCPlayerControl.h"

// 创建播放器
HXCPlayerControl *player = [[HXCPlayerControl alloc] init];
player.delegate = self;

// 打开视频
[player prepareToPlay:@"path/to/video.mp4"];
[player play];

// 控制播放
player.playbackRate = 1.5;  // 1.5倍速
player.volume = 0.8;         // 80% 音量
player.aspectRatioMode = HXCAspectRatioModeFit;

// 跳转
[player seekToPosition:60.0];  // 跳转到 60 秒

// 代理回调
- (void)playerDidChangeState:(HXCPlayerState)state {
    // 处理状态变化
}
```

## License

MIT
