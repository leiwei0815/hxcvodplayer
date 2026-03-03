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
- **Android 原生版**：ANativeWindow + OpenSL ES ✅
- **Desktop 版**：Qt5 + SDL2 ✅

## 🚀 立即开始

### macOS 原生版

**1. 编译第三方库（首次构建）：**
```bash
cd macos-third
./build_all.sh
```

**2. 构建 Xcode 项目：**
```bash
cd apple/macos
./build.sh
# 会自动打开 Xcode 项目
```

或直接在 Xcode 中打开：
```bash
open apple/macos/build/HXCPlayer-macOS.xcodeproj
```

### iOS 版

**1. 编译第三方库（首次构建）：**
```bash
cd ios-third
./build_all.sh
```

**2. 构建 Xcode 项目：**
```bash
cd apple/ios
./build_ios.sh
# 会自动打开 Xcode 项目
```

或直接在 Xcode 中打开：
```bash
open apple/ios/build/YXVodPlayer-iOS.xcodeproj
```

**3. 或构建 XCFramework（供其他项目集成）：**
```bash
cd apple
./build_xcframework.sh
```

生成的 XCFramework 位于：`apple/build_xcframework/HXCPlayer.xcframework`

### Android 版

**1. 编译第三方库（首次构建）：**
```bash
cd android-third
./build_all.sh
```

**2. 构建 APK（可直接安装到真机）：**
```bash
cd examples/android-test
./build_apk.sh
```

**3. 或构建 AAR 库（供其他项目集成）：**
```bash
cd android/library
./build_aar.sh release --copy-libs
```

或者在 Android Studio 中打开 `examples/android-test` 项目。

### Desktop 版（Qt）

```bash
mkdir build_desktop && cd build_desktop
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8
open bin/YXVodPlayer.app
```

## 特性

- 🎬 支持多种视频格式（MP4、MKV、AVI、FLV 等）
- 🎵 音视频同步播放
- ⚡ 变速播放（0.5x - 2.0x，保持音调）
- 🖼️ 视频显示模式（适应/填充）
- 🖥️ 跨平台支持（macOS、iOS、Android、Windows）
- 🌐 网络流媒体支持（HTTP、HTTPS、HLS、302 重定向）
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
- **Android**：`HXCPlayerControl` 类（Kotlin + JNI）
  - 视频：`ANativeWindow` 直接渲染到 `SurfaceView`
  - 音频：`OpenSL ES` 低延迟音频输出
  - 网络：FFmpeg + mbedTLS（支持 HTTPS 和 302 重定向）
- **Desktop**：Qt5 UI + SDL2 渲染

## 依赖

### 核心依赖
- FFmpeg 6.x+
- SoundTouch
- CMake 3.15+
- C++17

### 平台依赖
- **macOS/iOS**：Xcode 14+、iOS 13.0+、macOS 11.0+
- **Android**：Android Studio、NDK 25+、API 24+
- **Desktop**：Qt5、SDL2

## 构建

### macOS 原生版

**1. 编译第三方库（首次构建）：**
```bash
cd macos-third
./build_all.sh
```

这会自动下载并编译以下库（支持 arm64 和 x86_64）：
- FFmpeg 6.x（静态库）
- SoundTouch（静态库）

**2. 构建 Xcode 项目：**
```bash
cd apple/macos
./build.sh
```

生成的库位于：
- `macos-third/ffmpeg-build-macos/FFmpeg-macOS/`
- `macos-third/soundtouch-build-macos/SoundTouch-macOS/`

### iOS 版

**1. 编译第三方库（首次构建）：**
```bash
cd ios-third
./build_all.sh
```

这会自动下载并编译以下库（支持 arm64 和 arm64-simulator）：
- FFmpeg 6.x（静态库）
- SoundTouch（静态库）

**2. 构建 Xcode 项目：**
```bash
cd apple/ios
./build_ios.sh
```

生成的库位于：
- `ios-third/ffmpeg-build-ios/FFmpeg-iOS/`
- `ios-third/soundtouch-build-ios/SoundTouch-iOS/`

**3. 或构建 XCFramework（供其他项目集成）：**
```bash
cd apple
./build_xcframework.sh
```

这会将 iOS 和 macOS 的静态库打包成统一的 XCFramework：
- `apple/build_xcframework/HXCPlayer.xcframework`

XCFramework 包含：
- iOS 设备（arm64）
- iOS 模拟器（arm64、x86_64）
- macOS（arm64、x86_64）

### Android 版

**1. 编译第三方库（首次构建）：**
```bash
cd android-third
./build_all.sh
```

这会自动下载并编译以下库（支持 arm64-v8a、armeabi-v7a、x86_64）：
- FFmpeg 6.x（启用网络、HTTPS、HLS 支持）
- SoundTouch（变速播放）
- mbedTLS（HTTPS 和 SSL 支持）

**2. 构建测试 APK：**
```bash
cd examples/android-test
./build_apk.sh
```

**3. 或构建 AAR 库供其他项目使用：**
```bash
cd android/library
./build_aar.sh release --copy-libs
```

生成的 AAR 位于：`android/hxcplayer/build/outputs/aar/`

### Desktop 版

安装依赖：
```bash
# macOS
brew install qt@5 sdl2 ffmpeg soundtouch

# Ubuntu/Debian
sudo apt install qtbase5-dev libsdl2-dev libavcodec-dev \
  libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
  libsoundtouch-dev
```

构建：
```bash
mkdir build_desktop && cd build_desktop
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8
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
│   ├── macos/             # macOS 特定代码
│   └── build_xcframework.sh  # XCFramework 构建脚本
├── android/                # Android 平台
│   ├── hxcplayer/         # Android 库模块（AAR）
│   └── library/           # AAR 构建项目
├── desktop/               # Desktop (Qt5) 实现
├── examples/
│   ├── ios-test/          # iOS 测试应用
│   ├── macos-test/        # macOS 测试应用
│   └── android-test/      # Android 测试应用
├── macos-third/           # macOS 第三方库编译脚本
├── ios-third/             # iOS 第三方库编译脚本
├── android-third/         # Android 第三方库编译脚本
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

### Android

```kotlin
import com.hxcplayer.HXCPlayerControl

// 创建播放器（在 Activity 或 Fragment 中）
val player = HXCPlayerControl(this)

// 将播放器的 videoView 添加到布局中
val videoContainer = findViewById<FrameLayout>(R.id.videoContainer)
videoContainer.addView(player.videoView)

// 打开视频（支持本地文件和网络 URL）
player.openURL("http://example.com/video.mp4")

// 控制播放
player.play()
player.pause()
player.stop()

// 设置播放速率（0.5x - 2.0x）
player.setPlaybackRate(1.5f)

// 设置音量（0.0 - 1.0）
player.setVolume(0.8f)

// 设置画面比例模式
player.setAspectRatioMode(0)  // 0: FIT（保持比例，黑边）
player.setAspectRatioMode(1)  // 1: FILL（填充，可能裁剪）

// 跳转
player.seekTo(60.0)  // 跳转到 60 秒

// 获取播放状态
val duration = player.getDuration()
val position = player.getCurrentPosition()

// 清理资源（在 onDestroy 中调用）
player.release()
```

## License

MIT
