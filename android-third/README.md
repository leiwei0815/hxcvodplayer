# Android 第三方库编译

本目录包含用于编译 Android 平台 FFmpeg 和 SoundTouch 静态库的脚本。

## 编译的库

### 1. FFmpeg 8.0.1
- **arm64-v8a**: 64位 ARM 架构（真机 + Apple Silicon Mac 模拟器）
- **armeabi-v7a**: 32位 ARM 架构（兼容旧设备）
- **x86_64**: 64位 x86 架构（Intel Mac 模拟器）

### 2. SoundTouch 2.3.3
- **arm64-v8a**: 64位 ARM 架构（真机 + Apple Silicon Mac 模拟器）
- **armeabi-v7a**: 32位 ARM 架构（兼容旧设备）
- **x86_64**: 64位 x86 架构（Intel Mac 模拟器）

## 系统要求

### 必需工具
- **macOS** 或 **Linux**
- **Android NDK** r25c 或更高版本
- **CMake** 3.22.1 或更高版本
- **curl** (下载源码)
- **tar** (解压源码)

### 安装 Android NDK

**方式 1：通过 Android Studio**
1. 打开 Android Studio
2. Tools -> SDK Manager
3. SDK Tools 标签
4. 勾选 "NDK (Side by side)" 和 "CMake"
5. 点击 "Apply" 下载安装

**方式 2：手动下载**
1. 访问 https://developer.android.com/ndk/downloads
2. 下载适合你系统的 NDK
3. 解压到目标目录
4. 设置环境变量：
   ```bash
   export ANDROID_NDK=$HOME/Library/Android/sdk/ndk/25.2.9519653
   ```

## 快速开始

### 编译所有库

```bash
cd android-third
./build_all.sh
```

### 单独编译

```bash
# 仅编译 FFmpeg
./build_ffmpeg_android.sh

# 仅编译 SoundTouch
./build_soundtouch_android.sh
```

## 输出目录结构

```
android-third/
├── ffmpeg-build-android/
│   └── FFmpeg-Android/
│       ├── arm64-v8a/
│       │   ├── include/
│       │   └── lib/
│       │       ├── libavcodec.a
│       │       ├── libavformat.a
│       │       ├── libavutil.a
│       │       ├── libswresample.a
│       │       └── libswscale.a
│       ├── armeabi-v7a/
│       │   ├── include/
│       │   └── lib/
│       │       ├── libavcodec.a
│       │       ├── libavformat.a
│       │       ├── libavutil.a
│       │       ├── libswresample.a
│       │       └── libswscale.a
│       └── x86_64/
│           ├── include/
│           └── lib/
│               ├── libavcodec.a
│               ├── libavformat.a
│               ├── libavutil.a
│               ├── libswresample.a
│               └── libswscale.a
└── soundtouch-build-android/
    └── SoundTouch-Android/
        ├── arm64-v8a/
        │   ├── include/
        │   └── lib/
        │       └── libSoundTouch.a
        ├── armeabi-v7a/
        │   ├── include/
        │   └── lib/
        │       └── libSoundTouch.a
        └── x86_64/
            ├── include/
            └── lib/
                └── libSoundTouch.a
```

## 编译配置

### FFmpeg 配置

- **最小 API 级别**: Android 7.0 (API 24)
- **启用的解码器**: H.264, HEVC, MPEG4, AAC, MP3, PCM, PNG, MJPEG
- **启用的协议**: file, http, https, tcp, tls
- **硬件加速**: MediaCodec (H.264/HEVC)
- **优化**: `-Os` (体积优化)

### SoundTouch 配置

- **最小 API 级别**: Android 7.0 (API 24)
- **编译类型**: Release
- **库类型**: 静态库 (`.a`)
- **C++ 标准库**: libc++_shared

## 架构说明

### arm64-v8a (64位 ARM)
- ✅ **真机**: 2017 年后的大多数 Android 设备
- ✅ **模拟器**: Apple Silicon Mac (M1/M2) 上的 Android 模拟器
- 更好的性能
- 主流架构

### armeabi-v7a (32位 ARM)
- 支持旧设备（2011-2017）
- 兼容性好
- 库体积较小

### x86_64 (64位 x86)
- ✅ **模拟器**: Intel Mac 上的 Android 模拟器
- 适用于在 Intel Mac 上进行开发和测试
- 不用于真机部署

## 集成到 Android 项目

编译完成后，更新 Android 项目的 `CMakeLists.txt`：

```cmake
# 设置第三方库路径
set(ANDROID_THIRD_ROOT "${PROJECT_ROOT}/android-third")
set(FFMPEG_ROOT "${ANDROID_THIRD_ROOT}/ffmpeg-build-android/FFmpeg-Android/${ANDROID_ABI}")
set(SOUNDTOUCH_ROOT "${ANDROID_THIRD_ROOT}/soundtouch-build-android/SoundTouch-Android/${ANDROID_ABI}")

# 包含头文件
include_directories(
    ${FFMPEG_ROOT}/include
    ${SOUNDTOUCH_ROOT}/include
)

# 链接静态库
add_library(avcodec STATIC IMPORTED)
set_target_properties(avcodec PROPERTIES IMPORTED_LOCATION ${FFMPEG_ROOT}/lib/libavcodec.a)

add_library(avformat STATIC IMPORTED)
set_target_properties(avformat PROPERTIES IMPORTED_LOCATION ${FFMPEG_ROOT}/lib/libavformat.a)

# ... 其他库

# 链接到项目
target_link_libraries(hxcplayer
    avcodec
    avformat
    avutil
    swscale
    swresample
    soundtouch
    # ... 系统库
)
```

## 编译时间

在 Apple Silicon Mac 上：
- **FFmpeg**: 约 45-60 分钟（三个架构）
- **SoundTouch**: 约 5-8 分钟（三个架构）
- **总计**: 约 50-70 分钟

在 Intel Mac 上可能需要更长时间。首次编译会下载源码，后续重新编译会更快。

## 清理

```bash
# 清理 FFmpeg 编译目录
rm -rf ffmpeg-build-android/

# 清理 SoundTouch 编译目录
rm -rf soundtouch-build-android/

# 清理所有
rm -rf ffmpeg-build-android/ soundtouch-build-android/
```

## 问题排查

### 1. NDK 未找到

**错误**: `❌ 错误: 未找到 Android NDK`

**解决**:
```bash
# 设置 ANDROID_NDK 环境变量
export ANDROID_NDK=$HOME/Library/Android/sdk/ndk/25.2.9519653

# 或者在脚本中修改 NDK_PATH
```

### 2. 编译失败

**错误**: `make: *** [target] Error 1`

**解决**:
- 确保 NDK 版本 >= r25c
- 检查磁盘空间（至少 10GB）
- 查看详细错误日志

### 3. CMake 版本过低

**错误**: `CMake 3.22 or higher is required`

**解决**:
```bash
brew upgrade cmake
```

## 版本更新

修改脚本中的版本号：

```bash
# build_ffmpeg_android.sh
FFMPEG_VERSION="8.0.1"  # 修改为新版本

# build_soundtouch_android.sh
SOUNDTOUCH_VERSION="2.3.3"  # 修改为新版本
```

## 注意事项

1. **编译产物很大**: FFmpeg + SoundTouch 每个架构约 30-40 MB，三个架构共约 90-120 MB，已在 `.gitignore` 中排除
2. **首次编译需要下载**: 源码包会自动下载（FFmpeg ~13MB, SoundTouch ~500KB）
3. **网络要求**: 需要访问 ffmpeg.org 和 surina.net
4. **NDK 兼容性**: 建议使用 NDK r25c 或更高版本
5. **架构选择**: 
   - **Apple Silicon Mac**: 只需编译 `arm64-v8a`（真机和模拟器都支持）
   - **Intel Mac**: 需要编译 `arm64-v8a`（真机）和 `x86_64`（模拟器）
   - **仅真机发布**: 可以只编译 `arm64-v8a` 和 `armeabi-v7a`

## 相关文档

- [Android NDK 文档](https://developer.android.com/ndk)
- [FFmpeg 官方文档](https://ffmpeg.org/documentation.html)
- [SoundTouch 官方文档](https://www.surina.net/soundtouch/)
- [CMake Android 工具链](https://developer.android.com/ndk/guides/cmake)
