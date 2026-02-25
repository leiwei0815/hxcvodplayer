# YXVodPlayer iOS

iOS 播放器项目，使用 FFmpeg 解码，iOS 原生音视频渲染。

## 📋 项目特点

- **底层解码**：使用 FFmpeg 进行音视频解复用和解码（复用 `src/core` 代码）
- **原生渲染**：
  - 视频：使用 `AVSampleBufferDisplayLayer` 进行硬件加速渲染
  - 音频：使用 `AudioQueue` 进行音频播放
- **倍速播放**：使用 SoundTouch 库实现 0.5x ~ 2.0x 倍速播放（保持音调）
- **支持平台**：iOS 13.0+ (模拟器 + 真机)

## 🛠️ 环境要求

- macOS 12.0+
- Xcode 14.0+
- CMake 3.20+
- iOS 开发者账号（真机调试）

## 📦 第三方库

项目已在 `ios-third` 目录下预编译了以下静态库：

- **FFmpeg**：`ios-third/ffmpeg-build/FFmpeg-iOS/`
  - libavcodec.a
  - libavformat.a
  - libavutil.a
  - libswscale.a
  - libswresample.a

- **SoundTouch**：`ios-third/soundtouch-build/SoundTouch-iOS/`
  - libSoundTouch.a (支持模拟器和真机)

## 🚀 构建和运行

### 方法 1: 使用构建脚本（推荐）

```bash
cd src/ios

# 构建模拟器版本
./build_ios.sh simulator

# 或者构建真机版本
./build_ios.sh device
```

脚本会自动生成 Xcode 项目，位置：`src/ios/build/ios/YXVodPlayer-iOS.xcodeproj`

### 方法 2: 手动构建

```bash
cd src/ios/build

# 创建构建目录
mkdir -p ios
cd ios

# 生成 Xcode 项目（模拟器）
cmake .. -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"

# 或生成 Xcode 项目（真机）
cmake .. -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64
```

### 打开 Xcode 并运行

```bash
open src/ios/build/ios/YXVodPlayer-iOS.xcodeproj
```

在 Xcode 中：
1. 选择目标设备（模拟器或真机）
2. 如果是真机，在 "Signing & Capabilities" 中配置开发者证书和 Team
3. 点击 ▶️ Run 按钮

## 📱 功能说明

### 播放控制

- **播放/暂停**：点击播放按钮
- **进度条**：拖动进度条跳转到指定位置
- **倍速播放**：点击倍速按钮切换（1.0x → 1.5x → 2.0x → 0.5x）
- **音量控制**：拖动音量滑块调节音量

### 自动播放

应用启动后会自动播放测试视频（网络 URL），你可以在 `PlayerViewController.mm` 的 `openTestVideo` 方法中修改测试 URL：

```objc
- (void)openTestVideo {
    NSString *urlString = @"YOUR_VIDEO_URL_HERE";
    [_player openURL:urlString];
    [_player play];
}
```

## 🏗️ 项目结构

```
src/ios/
├── PlayerCore_iOS.h          # iOS 播放器核心头文件
├── PlayerCore_iOS.mm         # iOS 播放器核心实现（桥接 C++）
├── PlayerViewController.h    # 视图控制器头文件
├── PlayerViewController.mm   # 视图控制器实现（UI）
├── build/
│   ├── CMakeLists.txt        # CMake 配置
│   ├── Info.plist            # 应用信息配置
│   ├── AppDelegate.h/mm      # 应用代理
│   └── main.mm               # 应用入口
└── build_ios.sh              # 构建脚本
```

## 🔧 技术实现

### 架构设计

```
┌─────────────────────────────────────────┐
│          PlayerViewController           │  ← UI 层（Swift/ObjC）
│    (播放控制、进度显示、UI 交互)       │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│          PlayerCore_iOS                 │  ← 桥接层（ObjC++）
│   (Objective-C++ 封装，系统渲染)        │
│   - AVSampleBufferDisplayLayer (视频)  │
│   - AudioQueue (音频)                   │
│   - SoundTouch (倍速)                   │
└──────────────┬──────────────────────────┘
               │
┌──────────────▼──────────────────────────┐
│          yxplayer::PlayerCore           │  ← 核心层（C++）
│   (FFmpeg 解码、帧队列、同步控制)      │
└─────────────────────────────────────────┘
```

### 关键特性

1. **无 SDL 依赖**：iOS 平台使用 `NO_SDL` 宏禁用 SDL，所有音视频渲染由 iOS 原生 API 处理

2. **视频渲染**：
   - 从 `video_queue_` 获取解码后的 `AVFrame`
   - 转换为 `CVPixelBuffer`（NV12 格式）
   - 创建 `CMSampleBuffer` 并送入 `AVSampleBufferDisplayLayer`
   - 使用 `CADisplayLink` 同步刷新（60 FPS）

3. **音频渲染**：
   - 从 `audio_queue_` 获取解码后的 `AVFrame`
   - 经过 SoundTouch 处理（倍速）
   - 填充到 `AudioQueueBuffer` 并播放

4. **音画同步**：
   - 音频作为主时钟（`AudioMaster`）
   - 视频根据 PTS 和主时钟进行同步
   - 支持丢帧和等待策略

## ⚠️ 注意事项

1. **网络权限**：`Info.plist` 中已配置 `NSAppTransportSecurity` 允许 HTTP 访问
2. **开发者证书**：真机运行需要配置有效的开发者证书和 Team
3. **静态库架构**：SoundTouch 和 FFmpeg 已编译为支持 `x86_64`（Intel 模拟器）、`arm64`（Apple Silicon 模拟器和真机）
4. **内存管理**：视频帧较大，注意及时释放 `CVPixelBuffer` 和 `CMSampleBuffer`

## 🐛 常见问题

### 1. 编译错误：找不到 FFmpeg 或 SoundTouch 头文件

检查 `ios-third` 目录是否包含已编译的库文件：
```bash
ls ios-third/ffmpeg-build/FFmpeg-iOS/lib/
ls ios-third/soundtouch-build/SoundTouch-iOS/lib/
```

### 2. 真机运行提示签名错误

在 Xcode 的 "Signing & Capabilities" 中：
- 选择你的 Team
- 修改 Bundle Identifier（如果冲突）

### 3. 视频不显示或音频无声

查看 Xcode 控制台日志，检查：
- 解码器是否正常初始化
- 队列是否有数据
- 视频层是否正确添加到视图

## 📄 许可证

本项目使用的开源库：
- FFmpeg: LGPL/GPL
- SoundTouch: LGPL
