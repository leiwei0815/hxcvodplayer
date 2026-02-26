# HXCPlayer 测试项目示例

本目录包含两个示例项目，演示如何使用 `HXCPlayer.xcframework`：

- **ios-test**: iOS 模拟器测试应用
- **macos-test**: macOS 测试应用

## 📋 前置要求

### 1. 构建 XCFramework

在运行测试项目之前，必须先构建 `HXCPlayer.xcframework`：

```bash
cd /Users/debug/project/YXVodPlayer/apple
./build_xcframework_simple.sh
```

构建成功后，XCFramework 将位于：
```
apple/build_xcframework/HXCPlayer.xcframework
```

### 2. 安装 XcodeGen（可选）

如果项目使用 `project.yml` 配置文件，需要安装 XcodeGen：

```bash
brew install xcodegen
```

然后在项目目录中生成 `.xcodeproj`：

```bash
# iOS 项目
cd examples/ios-test
xcodegen generate

# macOS 项目
cd examples/macos-test
xcodegen generate
```

## 🚀 运行测试项目

### iOS 模拟器项目

```bash
cd examples/ios-test

# 如果使用 XcodeGen
xcodegen generate

# 打开项目
open HXCPlayerIOSTest.xcodeproj

# 或直接运行
xcodebuild -project HXCPlayerIOSTest.xcodeproj \
           -scheme HXCPlayerIOSTest \
           -destination 'platform=iOS Simulator,name=iPhone 14' \
           build
```

**注意**：目前只支持 iOS 模拟器（arm64 + x86_64），不支持真机。

### macOS 项目

```bash
cd examples/macos-test

# 如果使用 XcodeGen
xcodegen generate

# 打开项目
open HXCPlayerMacOSTest.xcodeproj

# 或直接运行
xcodebuild -project HXCPlayerMacOSTest.xcodeproj \
           -scheme HXCPlayerMacOSTest \
           build
```

**注意**：只支持 Apple Silicon (arm64) Mac。

## 📱 功能演示

两个测试项目都实现了以下功能：

### 基础播放控制
- ✅ 打开网络视频 URL
- ✅ 播放/暂停
- ✅ 停止

### 变速播放
- ✅ 1x 正常速度
- ✅ 2x 快速播放
- ✅ 使用 SoundTouch 保持音调

### 视频格式支持
- ✅ H.264/AVC（硬件加速）
- ✅ HEVC/H.265（硬件加速）
- ✅ MPEG-4
- ✅ AAC/MP3 音频

### 容器格式
- ✅ MP4
- ✅ MOV
- ✅ MKV
- ✅ HTTP/HTTPS 网络流

## 🔧 项目结构

### iOS 项目

```
ios-test/
├── project.yml              # XcodeGen 配置
├── Info.plist              # 应用信息
└── Sources/
    ├── AppDelegate.h/m     # 应用委托
    ├── SceneDelegate.h/m   # 场景委托
    ├── ViewController.h/m  # 主视图控制器（播放器 UI）
    └── main.m              # 入口
```

### macOS 项目

```
macos-test/
├── project.yml              # XcodeGen 配置
├── Info.plist              # 应用信息
└── Sources/
    ├── AppDelegate.h/m     # 应用委托
    ├── ViewController.h/m  # 主视图控制器（播放器 UI）
    ├── Main.storyboard     # 界面布局
    └── main.m              # 入口
```

## 💻 代码示例

### 基本使用

```objc
#import <HXCPlayer/HXCPlayer.h>

// 创建播放器
HXCPlayerControl *player = [[HXCPlayerControl alloc] init];

// 获取视频视图
UIView *videoView = [player videoView];  // iOS
// NSView *videoView = [player videoView];  // macOS

[self.view addSubview:videoView];

// 打开视频
[player openURL:@"http://example.com/video.mp4"];

// 播放控制
[player play];
[player pause];
[player stop];

// 变速播放
[player setPlaybackRate:2.0];  // 2倍速
[player setPlaybackRate:1.0];  // 正常速度
```

### 完整示例

查看源代码：
- iOS: `ios-test/Sources/ViewController.m`
- macOS: `macos-test/Sources/ViewController.m`

## 🐛 故障排查

### 问题：找不到 HXCPlayer.xcframework

**解决方案**：
1. 确保已经运行 `apple/build_xcframework_simple.sh`
2. 检查文件是否存在：
   ```bash
   ls -l apple/build_xcframework/HXCPlayer.xcframework
   ```

### 问题：链接错误（Symbol not found）

**解决方案**：
1. 确保 Framework 设置为 "Embed & Sign"
2. 检查 Build Settings：
   - `FRAMEWORK_SEARCH_PATHS` 应包含 `$(PROJECT_DIR)/../../apple/build_xcframework`
   - `LD_RUNPATH_SEARCH_PATHS` 应包含 `@executable_path/Frameworks`

### 问题：macOS 项目无法运行

**解决方案**：
1. 确保使用 Apple Silicon (arm64) Mac
2. 检查 Build Settings：
   - `MACOSX_DEPLOYMENT_TARGET` >= 11.0
   - `VALID_ARCHS` = arm64

### 问题：iOS 真机无法运行

**原因**：当前 `HXCPlayer.xcframework` 只包含模拟器版本（iOS Simulator）。

**解决方案**：需要编译 iOS 真机版本的 FFmpeg 和 SoundTouch 库，然后重新构建 XCFramework。

## 📚 API 文档

### HXCPlayerControl

```objc
@interface HXCPlayerControl : NSObject

// 视频视图
- (UIView *)videoView;  // iOS
- (NSView *)videoView;  // macOS

// 播放控制
- (void)openURL:(NSString *)url;
- (void)play;
- (void)pause;
- (void)stop;

// 变速播放
- (void)setPlaybackRate:(double)rate;  // 0.5 - 2.0

@end
```

## 🎯 测试视频

项目默认使用 Google 提供的测试视频：
```
http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4
```

你可以替换为自己的视频 URL，支持：
- HTTP/HTTPS 网络视频
- 本地文件路径（file://）

## 📝 注意事项

1. **网络权限**：Info.plist 已配置 `NSAppTransportSecurity` 允许 HTTP
2. **模拟器限制**：iOS 项目只能在模拟器运行
3. **架构限制**：macOS 项目只支持 arm64（Apple Silicon）
4. **最低版本**：
   - iOS >= 13.0
   - macOS >= 11.0

## 🆘 获取帮助

如果遇到问题：
1. 检查 XCFramework 是否正确构建
2. 查看 Xcode 控制台的错误信息
3. 参考主项目的 README 文档
4. 查看完整的构建日志

## 📄 许可证

与主项目保持一致。
