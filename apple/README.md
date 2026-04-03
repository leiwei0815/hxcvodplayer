# HXCPlayer Framework

将 HXCPlayer 打包成 XCFramework，支持 iOS 和 macOS。

## 📁 目录结构

```
apple/
├── framework/                    # Framework 构建配置
│   ├── CMakeLists.txt           # CMake 配置文件
│   └── Info.plist.in            # Framework Info.plist 模板
├── HXCVDownload/                 # HXCVD 视频下载模块（NSURLSession + Core Data）
│   └── README.md                 # 模块说明文档
├── build_framework.sh           # 构建脚本
├── FRAMEWORK_BUILD_GUIDE.md     # 详细构建指南
├── ios/                         # iOS 示例 App
├── macos/                       # macOS 示例 App
└── HXCPlayerControl.h/mm        # 核心代码
    HXCPlayerView.h/mm
```

## 🚀 快速开始

### 步骤 1: 构建 XCFramework

```bash
cd /Users/debug/project/YXVodPlayer/apple
./build_framework.sh
```

构建完成后，XCFramework 位于：
```
apple/build_framework/HXCPlayer.xcframework
```

### 步骤 2: 集成到你的项目

#### iOS 项目

1. 拖入 `HXCPlayer.xcframework` 到 Xcode 项目
2. 在 "Frameworks, Libraries, and Embedded Content" 中选择 "Embed & Sign"
3. 确保已集成 FFmpeg 和 SoundTouch（iOS 静态库）
4. 添加系统 Frameworks：
   - AVFoundation.framework
   - AudioToolbox.framework
   - CoreMedia.framework
   - CoreVideo.framework
   - UIKit.framework

#### macOS 项目

1. 拖入 `HXCPlayer.xcframework` 到 Xcode 项目
2. 在 "Frameworks, Libraries, and Embedded Content" 中选择 "Embed & Sign"
3. 确保已安装 Homebrew 依赖：
   ```bash
   brew install ffmpeg
   brew install sound-touch
   ```
4. 添加系统 Frameworks：
   - AVFoundation.framework
   - AudioToolbox.framework
   - CoreMedia.framework
   - CoreVideo.framework
   - AppKit.framework

### 步骤 3: 使用代码

```objc
#import <HXCPlayer/HXCPlayerControl.h>

// 创建播放器
HXCPlayerControl *player = [[HXCPlayerControl alloc] init];
player.delegate = self;

// 添加视频视图
[self.view addSubview:player.videoView];

// 设置约束（Auto Layout）
player.videoView.translatesAutoresizingMaskIntoConstraints = NO;
[NSLayoutConstraint activateConstraints:@[
    [player.videoView.topAnchor constraintEqualToAnchor:self.view.topAnchor],
    [player.videoView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor],
    [player.videoView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor],
    [player.videoView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor]
]];

// 播放控制
[player prepareToPlay:@"https://example.com/video.mp4"];
[player play];

// 调整播放参数
player.playbackRate = 1.5;  // 1.5 倍速
player.volume = 0.8;         // 80% 音量
player.aspectRatioMode = HXCAspectRatioModeFit;  // 适应模式

// 暂停
[player pause];

// 跳转
[player seekToPosition:60.0];  // 跳转到 60 秒

// 停止
[player stop];
```

## 📋 API 说明

### HXCPlayerControl

**属性**：
- `delegate` - 播放器代理
- `volume` - 音量 (0.0-1.0)
- `playbackRate` - 播放速度 (0.5-2.0)
- `aspectRatioMode` - 视频显示模式（Fit/Fill）
- `state` - 播放器状态（只读）
- `duration` - 视频时长（只读）
- `position` - 当前播放位置（只读）
- `videoView` - 视频视图（只读）

**方法**：
- `- (BOOL)openURL:(NSString *)url` - 打开视频
- `- (BOOL)prepareToPlay:(NSString *)url` - 准备播放
- `- (void)play` - 开始播放
- `- (void)pause` - 暂停
- `- (void)resume` - 恢复播放
- `- (void)stop` - 停止播放
- `- (void)replay` - 重新播放
- `- (void)seekToPosition:(double)position` - 跳转到指定位置

**代理方法**：
```objc
@protocol HXCPlayerControlDelegate <NSObject>
@optional
- (void)playerDidChangeState:(HXCPlayerState)state;
- (void)playerDidUpdatePosition:(double)position duration:(double)duration;
- (void)playerDidEncounterError:(NSError *)error;
@end
```

## 📦 依赖说明

### 为什么需要外部依赖？

HXCPlayer 使用**弱链接（Weak Linking）**方式引用 FFmpeg 和 SoundTouch，这意味着：

✅ **优点**：
- Framework 体积小（1-2 MB vs 100+ MB）
- 使用方可以选择依赖版本
- iOS 和 macOS 可以使用不同的依赖库
- 易于更新和维护

⚠️ **要求**：
- 使用方必须自行集成 FFmpeg 和SoundTouch
- iOS 使用静态库（.a）
- macOS 可以使用 Homebrew 动态库

### iOS 依赖库配置

你需要预编译 iOS 版本的 FFmpeg 和 SoundTouch，并添加到项目中：

**FFmpeg 静态库**：
- libavcodec.a
- libavformat.a
- libavutil.a
- libswscale.a
- libswresample.a

**SoundTouch 静态库**：
- libSoundTouch.a

**Header Search Paths**：
```
$(PROJECT_DIR)/Frameworks/ffmpeg/include
$(PROJECT_DIR)/Frameworks/soundtouch/include
```

**Library Search Paths**：
```
$(PROJECT_DIR)/Frameworks/ffmpeg/lib
$(PROJECT_DIR)/Frameworks/soundtouch/lib
```

### macOS 依赖库配置

**方式 1: 使用 Homebrew（推荐）**
```bash
brew install ffmpeg
brew install sound-touch
```

**方式 2: 使用静态库**
- 与 iOS 类似，配置 Header/Library Search Paths

## 🔧 高级配置

### 自定义 FFmpeg 配置

如果你需要自定义 FFmpeg 编译选项（例如只包含特定编解码器），请：

1. 自行编译 FFmpeg
2. 将编译好的库集成到你的项目
3. HXCPlayer 会自动使用你提供的版本

### 支持的架构

- **iOS**: arm64（设备）+ arm64/x86_64（模拟器）
- **macOS**: arm64 + x86_64（Universal Binary）

## 📝 示例项目

完整的示例项目位于：
- `apple/ios/` - iOS 示例 App
- `apple/macos/` - macOS 示例 App

这些示例展示了如何：
- 集成 XCFramework
- 配置依赖库
- 使用播放器 API
- 实现 UI 控制

## ❓ 常见问题

### Q: 为什么不把 FFmpeg 打包进 XCFramework？
A: FFmpeg 非常大（100MB+），打包进去会让 Framework 体积膨胀，且不灵活。弱链接方式更符合最佳实践。

### Q: iOS 和 macOS 能用同一个 XCFramework 吗？
A: 是的！这就是 XCFramework 的优势，一个文件同时支持多个平台。

### Q: 更新 FFmpeg 需要重新编译 Framework 吗？
A: 不需要。只要 API 兼容，可以直接替换 FFmpeg 库。

### Q: 支持 Catalyst 吗？
A: 目前不支持，但可以通过修改 CMake 配置添加 Catalyst 支持。

## 📖 更多文档

详细的构建和集成指南，请参考：
- [FRAMEWORK_BUILD_GUIDE.md](./FRAMEWORK_BUILD_GUIDE.md)

## 📄 许可证

MIT License
