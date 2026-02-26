# iOS 第三方库编译

本目录包含用于编译 iOS 平台 FFmpeg 和 SoundTouch 静态库的脚本。

## 编译的库

### 1. FFmpeg 8.0.1
- **真机**: arm64
- **模拟器**: arm64 + x86_64 (通用二进制)

### 2. SoundTouch 2.3.3
- **真机**: arm64
- **模拟器**: arm64 + x86_64 (通用二进制)

## 快速开始

### 编译所有库

```bash
cd ios-third
./build_all.sh
```

### 单独编译

```bash
# 仅编译 FFmpeg
./build_ffmpeg_ios.sh

# 仅编译 SoundTouch
./build_soundtouch_ios.sh
```

## 输出目录结构

```
ios-third/
├── ffmpeg-build-ios/
│   └── FFmpeg-iOS/
│       ├── ios-device/          # 真机版本 (arm64)
│       │   ├── include/
│       │   └── lib/
│       │       ├── libavcodec.a
│       │       ├── libavformat.a
│       │       ├── libavutil.a
│       │       ├── libswresample.a
│       │       └── libswscale.a
│       └── ios-simulator/       # 模拟器版本 (arm64 + x86_64)
│           ├── include/
│           └── lib/
│               ├── libavcodec.a
│               ├── libavformat.a
│               ├── libavutil.a
│               ├── libswresample.a
│               └── libswscale.a
└── soundtouch-build-ios/
    └── SoundTouch-iOS/
        ├── ios-device/          # 真机版本 (arm64)
        │   ├── include/
        │   └── lib/
        │       └── libSoundTouch.a
        └── ios-simulator/       # 模拟器版本 (arm64 + x86_64)
            ├── include/
            └── lib/
                └── libSoundTouch.a
```

## 系统要求

- macOS 11.0+
- Xcode 12.0+
- Xcode Command Line Tools
- CMake 3.15+ (用于 SoundTouch)

安装 CMake：
```bash
brew install cmake
```

## 编译配置

### FFmpeg 配置

- 最小部署目标: iOS 12.0
- 启用的解码器: H.264, HEVC, MPEG4, AAC, MP3, PCM, PNG, MJPEG
- 启用的协议: file, http, https, tcp, tls
- 启用的硬件加速: VideoToolbox
- 优化: `-Os` (体积优化)
- Bitcode: 已启用

### SoundTouch 配置

- 最小部署目标: iOS 12.0
- 编译类型: Release
- 库类型: 静态库 (`.a`)
- Bitcode: 已启用
- C++ 标准库: libc++

## 架构说明

### 真机 (iOS Device)
- **arm64**: 支持 iPhone 5s 及更新设备

### 模拟器 (iOS Simulator)
- **arm64**: Apple Silicon Mac 上的模拟器
- **x86_64**: Intel Mac 上的模拟器
- 使用 `lipo` 工具合并为通用二进制文件

## 使用编译好的库

编译完成后，可以在 Xcode 项目中链接这些静态库：

### 链接设置

```cmake
# iOS 真机
link_directories(
    ${PROJECT_ROOT}/ios-third/ffmpeg-build-ios/FFmpeg-iOS/ios-device/lib
    ${PROJECT_ROOT}/ios-third/soundtouch-build-ios/SoundTouch-iOS/ios-device/lib
)

# iOS 模拟器
link_directories(
    ${PROJECT_ROOT}/ios-third/ffmpeg-build-ios/FFmpeg-iOS/ios-simulator/lib
    ${PROJECT_ROOT}/ios-third/soundtouch-build-ios/SoundTouch-iOS/ios-simulator/lib
)
```

### 头文件路径

```cmake
include_directories(
    ${PROJECT_ROOT}/ios-third/ffmpeg-build-ios/FFmpeg-iOS/ios-device/include
    ${PROJECT_ROOT}/ios-third/soundtouch-build-ios/SoundTouch-iOS/ios-device/include
)
```

## 编译时间

在 Apple Silicon Mac 上：
- FFmpeg: 约 15-20 分钟
- SoundTouch: 约 2-3 分钟
- **总计**: 约 20-25 分钟

## 清理

```bash
# 清理 FFmpeg 编译目录
rm -rf ffmpeg-build-ios/

# 清理 SoundTouch 编译目录
rm -rf soundtouch-build-ios/

# 清理所有
rm -rf ffmpeg-build-ios/ soundtouch-build-ios/
```

## 问题排查

### 1. SDK 未找到

**错误**: `xcrun: error: SDK "iphoneos" cannot be located`

**解决**:
```bash
sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer
xcodebuild -showsdks
```

### 2. CMake 版本过低

**错误**: `CMake 3.15 or higher is required`

**解决**:
```bash
brew upgrade cmake
```

### 3. 编译失败

- 确保有足够的磁盘空间（至少 5GB）
- 检查 Xcode Command Line Tools 是否安装：
  ```bash
  xcode-select --install
  ```

## 版本更新

修改脚本中的版本号即可：

```bash
# build_ffmpeg_ios.sh
FFMPEG_VERSION="8.0.1"  # 修改为新版本

# build_soundtouch_ios.sh
SOUNDTOUCH_VERSION="2.3.3"  # 修改为新版本
```

## 注意事项

1. **编译产物很大**: FFmpeg + SoundTouch 约 50-60 MB，已在 `.gitignore` 中排除
2. **首次编译需要下载**: 源码包会自动下载（FFmpeg ~13MB, SoundTouch ~500KB）
3. **网络要求**: 需要访问 ffmpeg.org 和 surina.net
4. **架构支持**: 不包含 armv7/armv7s（iOS 11 以下设备）
5. **Bitcode**: 已启用，支持 App Store 提交

## 相关文档

- [FFmpeg 官方文档](https://ffmpeg.org/documentation.html)
- [SoundTouch 官方文档](https://www.surina.net/soundtouch/)
- [iOS 交叉编译指南](https://developer.apple.com/documentation/)
