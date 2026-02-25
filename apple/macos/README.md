# HXC Player - macOS 原生版本

纯 Cocoa 实现的 macOS 视频播放器，使用系统原生 API 进行音视频渲染。

## 项目结构

```
src/macos/
├── CMakeLists.txt           # CMake 构建配置
├── build.sh                 # 一键构建脚本
├── Info.plist               # macOS Bundle 信息
├── main.mm                  # 应用程序入口
├── AppDelegate.h/mm         # 应用代理
├── PlayerViewController.h/mm # 播放器视图控制器
└── README.md                # 本文件
```

## 技术架构

```
┌─────────────────────────────────────┐
│   PlayerViewController (UI 层)     │
│   ├─ NSButton (控制按钮)            │
│   ├─ NSSlider (进度条、音量)        │
│   └─ HXCPlayerView (视频显示)      │
├─────────────────────────────────────┤
│   HXCPlayerControl (播放器控制层)  │
│   ├─ AVSampleBufferDisplayLayer    │  ← 视频渲染
│   └─ AudioQueue                     │  ← 音频渲染
├─────────────────────────────────────┤
│   C Bridge Layer                    │  ← C 接口桥接
│   (hxc_player_core_c_bridge)       │
├─────────────────────────────────────┤
│   Core Library (解码层)            │
│   ├─ FFmpeg (解码)                 │
│   ├─ SoundTouch (变速)             │
│   └─ PlayerCore (C++)               │
└─────────────────────────────────────┘
```

## 依赖要求

### 系统要求
- macOS 10.15+
- Xcode 12.0+
- CMake 3.15+

### 第三方库
```bash
# 使用 Homebrew 安装依赖
brew install ffmpeg
brew install sound-touch
brew install cmake

# 注意：macOS 原生项目不需要 SDL2
# 音视频渲染使用 AVFoundation + AudioQueue
```

## 编译说明

### 方法一：生成 Xcode 项目（推荐）

适合在 Xcode 中开发和调试：

```bash
cd src/macos
./build.sh
```

脚本会自动生成 Xcode 项目并打开，详见 [Xcode项目构建指南.md](./Xcode项目构建指南.md)

### 方法二：使用构建脚本

直接编译生成可执行文件：

```bash
cd src/macos
mkdir -p build && cd build
cmake ..
make -j8
```

### 方法三：命令行编译 Xcode 项目

先生成 Xcode 项目，然后用命令行编译：

```bash
cd src/macos/build
cmake -G Xcode ..
xcodebuild -project HXCPlayer-macOS.xcodeproj \
           -scheme HXCPlayer-macOS \
           -configuration Debug
```

## 运行应用

```bash
# 打开应用
open build/bin/HXCPlayer-macOS.app

# 或直接运行可执行文件
./build/bin/HXCPlayer-macOS.app/Contents/MacOS/HXCPlayer-macOS
```

## 功能特性

### 基础功能
- ✅ 本地文件播放
- ✅ 网络视频播放（HTTP/HTTPS）
- ✅ 播放/暂停/停止
- ✅ 进度条拖动
- ✅ 音量控制

### 高级功能
- ✅ 变速播放（0.5x ~ 2.0x）
- ✅ 视频显示模式（适应/填充）
- ✅ 精确音视频同步
- ✅ 硬件加速解码
- ✅ 实时进度更新

### 支持格式
- **视频**: MP4, MKV, AVI, MOV, FLV, WMV 等
- **音频**: AAC, MP3, AC3, DTS 等
- **协议**: 本地文件, HTTP, HTTPS

## UI 界面

### 控制栏布局
```
┌──────────────────────────────────────────────────────────┐
│  [进度条滑块]                                             │
├──────────────────────────────────────────────────────────┤
│  [打开] [播放] [停止] [00:00/00:00] ... [音量] [速度] [适应] │
└──────────────────────────────────────────────────────────┘
```

### 按钮说明
- **打开**: 选择本地视频文件
- **播放/暂停**: 控制播放状态
- **停止**: 停止播放并重置
- **时间**: 显示当前/总时长
- **音量滑块**: 0-100% 音量控制
- **速度选择**: 0.5x, 0.75x, 1.0x, 1.25x, 1.5x, 2.0x
- **适应/填充**: 切换视频显示模式

## 代码示例

### 使用 HXCPlayerControl

```objective-c
// 创建播放器
HXCPlayerControl *player = [[HXCPlayerControl alloc] init];
player.delegate = self;

// 创建视频视图
HXCPlayerView *playerView = [[HXCPlayerView alloc] initWithFrame:frame];
[playerView setPlayer:player];
[self.view addSubview:playerView];

// 播放视频
[player prepareToPlay:@"path/to/video.mp4"];
[player play];

// 控制播放
player.playbackRate = 1.5;  // 1.5倍速
player.volume = 0.8;         // 80% 音量
player.aspectRatioMode = HXCAspectRatioModeFill;  // 填充模式

// 进度控制
[player seekToPosition:30.0];  // 跳转到 30 秒
[player pause];                 // 暂停
[player play];                  // 继续
```

### 实现代理方法

```objective-c
@interface MyViewController () <HXCPlayerControlDelegate>
@end

@implementation MyViewController

- (void)player:(HXCPlayerControl *)player didChangeState:(HXCPlayerState)state {
    switch (state) {
        case HXCPlayerStatePlaying:
            NSLog(@"正在播放");
            break;
        case HXCPlayerStatePaused:
            NSLog(@"已暂停");
            break;
        case HXCPlayerStateError:
            NSLog(@"播放错误");
            break;
        default:
            break;
    }
}

- (void)player:(HXCPlayerControl *)player didEncounterError:(NSString *)error {
    NSLog(@"错误: %@", error);
}

@end
```

## 与其他版本对比

| 特性 | macOS (纯 Cocoa) | macOS (Qt) | iOS |
|-----|-----------------|-----------|-----|
| **UI 框架** | Cocoa | Qt Widgets | UIKit |
| **视频渲染** | AVSampleBufferDisplayLayer | SDL2 | AVSampleBufferDisplayLayer |
| **音频渲染** | AudioQueue | SDL2 | AudioQueue |
| **刷新机制** | CVDisplayLink | QTimer | CADisplayLink |
| **依赖** | 仅系统框架 | Qt5, SDL2 | 仅系统框架 |
| **包大小** | 小 | 大 | 小 |
| **性能** | 优秀 | 良好 | 优秀 |

## 优势

### 1. 原生体验
- 使用 Cocoa 原生控件
- 符合 macOS Human Interface Guidelines
- 系统级别的硬件加速

### 2. 轻量级
- 无第三方 UI 框架依赖
- 应用包体积小
- 启动速度快

### 3. 高性能
- CVDisplayLink 精确刷新
- AVFoundation 硬件加速
- 高效的音视频同步

### 4. 易于维护
- 代码结构清晰
- 与 iOS 版本架构一致
- 易于扩展和定制

## 调试

### 方法一：在 Xcode 中调试（推荐）

1. 生成并打开 Xcode 项目：
   ```bash
   cd src/macos
   ./build.sh
   ```

2. 在代码中设置断点（点击行号左侧）

3. 点击运行按钮（⌘R）或选择 `Product > Run`

4. 应用会在断点处暂停，可以：
   - 查看变量值
   - 单步执行（F6）
   - 步入函数（F7）
   - 查看调用栈

详细说明见 [Xcode项目构建指南.md](./Xcode项目构建指南.md)

### 方法二：启用详细日志

在 `HXCPlayerControl.mm` 中取消注释日志语句：
```objective-c
// NSLog(@"[视频] PTS: %.2f, 主时钟: %.2f", currentPTS, masterClock);
```

### 方法三：使用 Instruments 性能分析

在 Xcode 中：
1. 选择 `Product > Profile` (⌘I)
2. 选择分析工具（如 Time Profiler、Allocations）
3. 点击录制按钮开始分析

## 常见问题

### Q: 编译时找不到 FFmpeg 或 SoundTouch？
A: 确保通过 Homebrew 安装了依赖：
```bash
brew install ffmpeg sound-touch
```

### Q: 视频有声音但没有画面？
A: 检查视频格式是否支持，尝试使用 H.264 编码的 MP4 文件。

### Q: 播放卡顿？
A: 可能是网络问题或视频码率过高，尝试降低播放速度或使用本地文件。

### Q: 如何添加自定义功能？
A: 参考 `PlayerViewController.mm` 中的实现，通过 `HXCPlayerControl` 的 API 进行扩展。

## 开发路线图

- [ ] 支持字幕加载
- [ ] 播放列表管理
- [ ] 音轨/字幕选择
- [ ] 截图功能
- [ ] 播放历史记录
- [ ] 快捷键支持

## 许可证

本项目遵循与主项目相同的许可证。

## 联系方式

如有问题或建议，请提交 Issue。

---

**创建日期**: 2026-02-25  
**版本**: 1.0  
**状态**: ✅ 完成
