# HXCPlayer 示例项目快速指南

## ✅ 已修复的问题

1. **YAML 格式错误**: 修复了 `project.yml` 中 `@executable_path` 需要用引号包裹的问题
2. **iOS API 错误**: 修复了 `AppDelegate.m` 中 `UIConnectionOptions` 类型错误（应为 `UISceneConnectionOptions`）

## 🚀 快速开始

### 方式 1：直接打开 Xcode 项目（推荐）

```bash
# iOS 项目
open /Users/debug/project/YXVodPlayer/examples/ios-test/HXCPlayerIOSTest.xcodeproj

# macOS 项目
open /Users/debug/project/YXVodPlayer/examples/macos-test/HXCPlayerMacOSTest.xcodeproj
```

### 方式 2：命令行编译

**iOS 模拟器：**
```bash
cd /Users/debug/project/YXVodPlayer/examples/ios-test
xcodebuild -project HXCPlayerIOSTest.xcodeproj \
           -scheme HXCPlayerIOSTest \
           -destination 'platform=iOS Simulator,name=iPhone 17' \
           build
```

**macOS：**
```bash
cd /Users/debug/project/YXVodPlayer/examples/macos-test
xcodebuild -project HXCPlayerMacOSTest.xcodeproj \
           -scheme HXCPlayerMacOSTest \
           build
```

## 📱 功能演示

两个测试项目实现了：
- ✅ 播放网络视频（Big Buck Bunny 测试视频）
- ✅ 播放/暂停控制
- ✅ 2x 变速播放（使用 SoundTouch 保持音调）
- ✅ 状态显示

## 🔧 如果需要重新生成项目

如果你修改了 `project.yml`，需要重新生成 `.xcodeproj`：

```bash
# iOS
cd examples/ios-test
xcodegen generate

# macOS
cd examples/macos-test
xcodegen generate
```

## 📝 代码示例位置

- **iOS**: `examples/ios-test/Sources/ViewController.m`
- **macOS**: `examples/macos-test/Sources/ViewController.m`

这两个文件展示了如何完整地使用 `HXCPlayer.xcframework`。

## ✅ 验证状态

- ✅ iOS 项目已成功编译
- ✅ XCFramework 依赖正确链接
- ✅ 所有源文件已创建
- ✅ 项目配置已修复

## 🎯 下一步

直接在 Xcode 中运行项目：
1. 打开 `.xcodeproj` 文件
2. 选择模拟器/Mac 作为目标
3. 点击运行按钮（Cmd+R）
4. 观看视频播放！

更多详细信息请查看：`examples/README.md`
