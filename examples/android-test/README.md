# Android 测试项目

HXCPlayer Android 测试应用，演示如何在 Android 平台使用播放器。

## 项目状态

🚧 **开发中** - 基础框架已创建

## 架构设计

### 1. 核心层（C++）
- 使用现有的 `core/` 目录中的播放器核心
- FFmpeg 解封装和解码
- SoundTouch 变速处理

### 2. JNI 桥接层（C++/JNI）
- `HXCPlayerJNI.cpp` - Java 和 C++ 之间的桥接
- 提供 Java 可调用的 Native 方法

### 3. Java/Kotlin 层
- `HXCPlayerControl.kt` - 播放器控制类
- `HXCPlayerView.kt` - 视频渲染视图（使用 SurfaceView）
- 使用 Android MediaCodec 进行硬件解码
- 使用 AudioTrack 进行音频播放

### 4. UI 层
- `MainActivity.kt` - 主界面
- 播放控制、进度条、速度调节等

## 快速开始

由于 Android 项目结构较复杂，建议使用以下方式之一：

### 方案 A：使用 Android Studio 创建（推荐）

1. 打开 Android Studio
2. 创建新项目：
   - Template: Empty Activity
   - Name: HXCPlayerAndroidTest  
   - Package: com.hxcplayer.test
   - Language: Kotlin
   - Minimum SDK: API 24 (Android 7.0)

3. 配置 CMake 和 NDK：
   ```gradle
   android {
       ...
       externalNativeBuild {
           cmake {
               path file('../../core/CMakeLists.txt')
           }
       }
   }
   ```

4. 添加依赖库：
   - FFmpeg (预编译或自行编译)
   - SoundTouch (预编译或自行编译)

### 方案 B：使用现有项目模板

我可以为你创建完整的项目文件，但需要注意：
- Android 项目文件众多（50+ 个文件）
- 需要配置 NDK 和 CMake
- 需要编译 Android 版本的 FFmpeg 和 SoundTouch

## 下一步

**请告诉我你希望采用哪种方案：**

1. **方案 1（推荐）**：我提供详细的配置指南和关键代码，你在 Android Studio 中手动创建项目
   - ✅ 学习过程更清晰
   - ✅ 可以根据需要调整
   - ⏱️ 需要一些手动配置

2. **方案 2**：我创建完整的项目结构和所有必要文件
   - ✅ 开箱即用
   - ⏱️ 创建文件较多，需要更多时间
   - ⚠️ 需要手动编译 Android 版 FFmpeg/SoundTouch

3. **方案 3**：先创建一个简化版本
   - 使用 Android MediaPlayer（不依赖 FFmpeg）
   - 快速验证 UI 和基础功能
   - 后续再集成 FFmpeg

请告诉我你的选择，我会相应地提供帮助！

## 相关资源

- [Android NDK 开发](https://developer.android.com/ndk)
- [FFmpeg Android 编译](https://github.com/tanersener/mobile-ffmpeg)
- [Android MediaCodec](https://developer.android.com/reference/android/media/MediaCodec)
