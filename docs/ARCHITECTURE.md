# YXVodPlayer 架构设计文档

## 概述

YXVodPlayer 是一个跨平台的视频播放器，基于 FFmpeg 和 ffplay 的架构设计，支持 Mac、Windows、Android 和 iOS 平台。

## 架构设计

### 1. 核心层（Core Layer）

核心层包含平台无关的播放器逻辑，完全使用 C++ 实现。

#### 1.1 主要组件

```
+------------------+
|  PlayerCore      |  播放器核心控制
+------------------+
        |
        +-- Demuxer          (解复用器)
        +-- Decoder          (解码器)
        |   +-- VideoDecoder
        |   +-- AudioDecoder
        +-- PacketQueue      (数据包队列)
        +-- FrameQueue       (帧队列)
        +-- Clock            (时钟同步)
        +-- SwrContext       (音频重采样)
        +-- SwsContext       (视频格式转换)
```

#### 1.2 核心类说明

**PlayerCore**
- 职责：播放器核心控制逻辑
- 功能：
  - 媒体文件打开/关闭
  - 播放控制（播放/暂停/停止/跳转）
  - 流管理和同步
  - 状态管理

**Decoder**
- 职责：解码音视频数据
- 实现：
  - VideoDecoder: 视频解码
  - AudioDecoder: 音频解码
- 特点：
  - 多线程解码
  - 支持硬件加速

**PacketQueue**
- 职责：缓存解复用后的数据包
- 特点：
  - 线程安全
  - 支持阻塞/非阻塞操作
  - 自动内存管理

**FrameQueue**
- 职责：缓存解码后的音视频帧
- 特点：
  - 参照 ffplay 实现
  - 高效的环形队列
  - 支持帧保留

**Clock**
- 职责：音视频同步
- 支持三种同步模式：
  - AudioMaster: 以音频为基准
  - VideoMaster: 以视频为基准
  - ExternalClock: 外部时钟

### 2. 平台抽象层（Platform Abstraction Layer）

提供平台无关的接口定义，各平台实现具体的渲染和 UI 逻辑。

```cpp
interface IVideoRenderer {
    + init()
    + render_frame()
    + resize()
    + clear()
    + destroy()
}

interface IAudioRenderer {
    + init()
    + play_audio()
    + pause()
    + set_volume()
    + destroy()
}

interface IPlayerUI {
    + show_controls()
    + update_progress()
    + update_state()
    + show_error()
}

interface IPlatformFactory {
    + create_video_renderer()
    + create_audio_renderer()
    + create_player_ui()
}
```

### 3. 平台实现层

#### 3.1 Desktop（Mac/Windows）

**技术栈**
- UI 框架: Qt5
- 视频渲染: SDL2 或 Qt Widget
- 音频输出: SDL2

**组件**
- MainWindow: 主窗口
- VideoWidget: 视频显示组件
- SDLRenderer: SDL2 渲染器

**构建系统**: CMake

#### 3.2 Android

**技术栈**
- 开发语言: Java/Kotlin + C++ (JNI)
- 视频渲染: SurfaceView / TextureView
- 硬件解码: MediaCodec
- 音频输出: AudioTrack

**组件结构**
```
android/
├── app/
│   ├── src/main/
│   │   ├── java/           # Java/Kotlin 代码
│   │   │   └── com.yx.vodplayer/
│   │   │       ├── PlayerActivity
│   │   │       ├── PlayerView
│   │   │       └── PlayerController
│   │   ├── cpp/            # JNI 代码
│   │   │   ├── player_jni.cpp
│   │   │   └── android_renderer.cpp
│   │   └── AndroidManifest.xml
│   └── build.gradle
└── build.gradle
```

**关键实现**
- JNI 桥接层连接 Java 和 C++ 核心
- 使用 MediaCodec 实现硬件解码
- SurfaceView 进行视频渲染
- AudioTrack 进行音频播放

#### 3.3 iOS

**技术栈**
- 开发语言: Objective-C/Swift + C++
- 视频渲染: Metal / OpenGL ES
- 硬件解码: VideoToolbox
- 音频输出: AVAudioEngine / AudioQueue

**组件结构**
```
ios/
├── YXVodPlayer/
│   ├── AppDelegate
│   ├── PlayerViewController
│   ├── PlayerView (Metal)
│   ├── Bridge/             # C++ 桥接
│   │   ├── PlayerBridge.h
│   │   └── PlayerBridge.mm
│   └── Resources/
└── YXVodPlayer.xcodeproj
```

**关键实现**
- Objective-C++ 桥接 C++ 核心
- VideoToolbox 硬件解码
- Metal 实现高性能渲染
- AVAudioEngine 音频播放

## 数据流

### 播放流程

```
文件输入
  ↓
解复用 (av_read_frame)
  ↓
PacketQueue (视频/音频分别缓存)
  ↓
解码线程
  ↓
FrameQueue (解码后的帧)
  ↓
同步控制 (Clock)
  ↓
渲染输出
  ├── 视频 → VideoRenderer → 屏幕
  └── 音频 → AudioRenderer → 扬声器
```

### 线程模型

1. **主线程**: UI 更新和用户交互
2. **读取线程**: 解复用，从文件读取数据包
3. **视频解码线程**: 解码视频帧
4. **音频解码线程**: 解码音频帧
5. **视频渲染线程**: 显示视频帧（可选）
6. **音频回调线程**: SDL/系统音频回调

## 同步机制

### 音视频同步策略

**参照 ffplay 实现**

1. **以音频为基准**（默认）
   - 音频连续播放
   - 视频根据音频时钟调整显示时间
   - 丢帧或延迟显示保持同步

2. **时钟计算**
   ```
   video_pts = 视频帧时间戳
   audio_pts = 音频时间戳
   delay = video_pts - audio_pts
   
   if delay > 0:
       等待 delay 时间
   else if delay < -threshold:
       丢帧
   ```

3. **时钟更新**
   - 音频时钟：音频回调时更新
   - 视频时钟：显示帧时更新
   - 外部时钟：系统时间

## 性能优化

### 1. 硬件加速

**Desktop**
- 使用 FFmpeg 的硬件加速 API
- 支持: VAAPI, VDPAU, VideoToolbox, D3D11

**Android**
- MediaCodec 硬件解码
- OpenGL ES 渲染

**iOS**
- VideoToolbox 硬件解码
- Metal 高性能渲染

### 2. 内存管理

- 使用智能指针管理 C++ 对象
- AVFrame/AVPacket 及时释放
- 队列大小限制，防止内存溢出

### 3. 多线程优化

- 解复用、解码、渲染分离
- 无锁队列优化（可选）
- 线程池复用

## 构建系统

### Desktop (CMake)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

### Android (Gradle)

```bash
cd android
./gradlew assembleRelease
```

### iOS (Xcode)

```bash
cd ios
xcodebuild -project YXVodPlayer.xcodeproj -scheme YXVodPlayer build
```

## 依赖管理

### 核心依赖

- FFmpeg 8.x
- SDL2 (Desktop)
- Qt5 (Desktop)

### 平台依赖

**Android**
- NDK r21+
- Android SDK API 21+

**iOS**
- Xcode 12+
- iOS 12.0+

## 扩展性设计

### 支持新格式

1. FFmpeg 自动支持大部分格式
2. 添加新的解码器只需更新 FFmpeg

### 支持新平台

1. 实现 IPlatformFactory 接口
2. 实现视频/音频渲染器
3. 实现 UI 层

### 插件系统（未来）

- 滤镜插件
- 字幕插件
- 特效插件

## 调试和日志

### 日志级别

- ERROR: 错误信息
- WARNING: 警告信息
- INFO: 一般信息
- DEBUG: 调试信息

### 调试工具

- FFmpeg 日志: `av_log_set_level()`
- 性能分析: 帧率、缓冲区状态
- 内存泄漏检测: Valgrind, ASan

## 测试策略

### 单元测试

- 核心组件测试
- 队列测试
- 解码器测试

### 集成测试

- 端到端播放测试
- 多格式兼容性测试
- 性能基准测试

### 平台测试

- Mac/Windows 自动化测试
- Android 真机测试
- iOS 真机测试

## 发布流程

1. 代码审查
2. 测试覆盖
3. 版本号更新
4. 编译发布版本
5. 签名和打包
6. 发布到应用商店

## 参考资料

- [FFmpeg 官方文档](https://ffmpeg.org/documentation.html)
- [ffplay 源代码](https://github.com/FFmpeg/FFmpeg/blob/master/fftools/ffplay.c)
- [SDL2 文档](https://wiki.libsdl.org/)
- [Qt 文档](https://doc.qt.io/)
