# HXCPlayer - macOS Xcode 项目构建指南

## 概述

本指南介绍如何为 macOS 项目生成 Xcode 项目，并在 Xcode 中进行开发和调试。

## 快速开始

### 1. 生成 Xcode 项目

在 `src/macos` 目录下运行构建脚本：

```bash
cd src/macos
./build.sh
```

脚本会自动：
- 清理旧的构建目录
- 生成 Xcode 项目
- 打开 Xcode

### 2. 在 Xcode 中运行

1. 打开 `build/HXCPlayer-macOS.xcodeproj`
2. 选择 Scheme：`HXCPlayer-macOS`
3. 点击运行按钮（⌘R）或选择 `Product > Run`

## 脚本说明

### build.sh
主构建脚本，用于生成 Xcode 项目。

**功能：**
- 清理旧的构建目录
- 使用 CMake 生成 Xcode 项目
- 自动打开 Xcode

**使用方法：**
```bash
./build.sh
```

### open_xcode.sh
快速打开已存在的 Xcode 项目。

**功能：**
- 检查 Xcode 项目是否存在
- 打开 Xcode 项目

**使用方法：**
```bash
./open_xcode.sh
```

如果项目不存在，会提示运行 `build.sh` 先生成项目。

## 手动生成 Xcode 项目

如果需要手动生成项目：

```bash
cd src/macos
mkdir -p build
cd build
cmake -G Xcode ..
open HXCPlayer-macOS.xcodeproj
```

## 命令行编译（可选）

如果不想在 Xcode 中编译，可以使用命令行：

```bash
cd src/macos/build
xcodebuild -project HXCPlayer-macOS.xcodeproj \
           -scheme HXCPlayer-macOS \
           -configuration Debug
```

编译成功后，应用位于：
```
build/bin/Debug/HXCPlayer-macOS.app
```

运行应用：
```bash
open build/bin/Debug/HXCPlayer-macOS.app
```

## 在 Xcode 中的开发

### 项目结构

生成的 Xcode 项目包含以下主要部分：

- **Source Files**: 源代码文件（.mm, .cpp）
- **Header Files**: 头文件（.h）
- **Resources**: 资源文件（Info.plist）
- **Frameworks**: 链接的框架和库

### 调试

1. 在 Xcode 中打开项目
2. 在代码中设置断点
3. 点击运行（⌘R）
4. 应用会在断点处暂停，可以查看变量和调用栈

### 修改代码

直接在 Xcode 中修改代码：
- `.h` 头文件
- `.mm` Objective-C++ 实现文件
- `.cpp` C++ 实现文件

修改后点击运行，Xcode 会自动重新编译。

## 注意事项

1. **首次构建可能较慢**：CMake 需要检测编译器和依赖库
2. **清理构建**：如果遇到问题，删除 `build` 目录重新生成
3. **依赖库路径**：确保 FFmpeg 和 SoundTouch 已通过 Homebrew 安装
4. **Xcode 版本**：建议使用 Xcode 14 或更高版本

## 常见问题

### Q: CMake 报错找不到编译器
**A:** 确保已安装 Xcode Command Line Tools：
```bash
xcode-select --install
```

### Q: 找不到 FFmpeg 或 SoundTouch
**A:** 通过 Homebrew 安装：
```bash
brew install ffmpeg soundtouch sdl2
```

### Q: Xcode 项目无法打开
**A:** 删除 `build` 目录，重新运行 `build.sh`

### Q: 编译错误
**A:** 
1. 清理项目：Product > Clean Build Folder (⇧⌘K)
2. 删除 `build` 目录
3. 重新生成项目

## 项目特点

- **纯 Cocoa 实现**：使用原生 macOS 框架
- **AVFoundation 渲染**：视频使用 `AVSampleBufferDisplayLayer`
- **AudioQueue 音频**：音频使用 `AudioQueue`
- **SoundTouch 变速**：支持倍速播放
- **CVDisplayLink 同步**：精确的视频帧同步

## 相关文档

- `README.md` - 项目总体说明
- `快速使用指南.md` - 快速开始指南
- `macOS纯Cocoa项目完成总结.md` - 项目实现总结
