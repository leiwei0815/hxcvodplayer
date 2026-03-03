# HXCPlayer Android Library

这是 HXCPlayer 的 Android 库构建项目，用于生成可复用的 AAR 库文件。

## 📂 目录结构

```
android/
├── hxcplayer/          # 播放器库源码（Kotlin + JNI + C++）
│   ├── src/
│   │   └── main/
│   │       ├── java/com/hxcplayer/   # Kotlin 源码
│   │       ├── cpp/                  # JNI 和 C++ 源码
│   │       └── jniLibs/              # 第三方 .so 库
│   └── build.gradle
│
└── library/            # AAR 构建项目
    ├── build_aar.sh    # 打包脚本
    ├── build.gradle    # 根配置
    ├── settings.gradle # 项目配置
    └── gradle/         # Gradle wrapper
```

## 🚀 快速开始

### 1. 打包 AAR

```bash
cd android/library

# 打包 Release 版本（推荐）
./build_aar.sh

# 打包 Debug 版本
./build_aar.sh debug

# 同时打包两个版本
./build_aar.sh all

# 清理后打包
./build_aar.sh release --clean

# 从 android-third 复制库后打包
./build_aar.sh release --copy-libs
```

### 2. 查看输出

AAR 文件位置：
```
android/hxcplayer/build/outputs/aar/
├── hxcplayer-debug.aar      # Debug 版本
└── hxcplayer-release.aar    # Release 版本
```

### 3. 使用 AAR

将 AAR 文件复制到你的 Android 项目：

```gradle
// build.gradle
dependencies {
    implementation files('libs/hxcplayer-release.aar')
}
```

详细使用文档：[HXCPlayer_AAR_Usage_Guide.md](../../HXCPlayer_AAR_Usage_Guide.md)

## 🔧 开发说明

### 源码位置

- **Kotlin 代码**：`hxcplayer/src/main/java/com/hxcplayer/`
- **JNI 桥接**：`hxcplayer/src/main/cpp/hxcplayer_jni.cpp`
- **Android 实现**：`hxcplayer/src/main/cpp/android_player.cpp`
- **Native 库**：`hxcplayer/src/main/jniLibs/`

### 依赖的第三方库

AAR 包含以下第三方库（自动打包到 `jniLibs`）：

- **FFmpeg** (libavcodec, libavformat, libavutil, libswscale, libswresample)
- **SoundTouch** (libSoundTouch.so)
- **mbedTLS** (libmbedtls, libmbedx509, libmbedcrypto)

这些库从 `../../android-third/` 编译生成，需要先编译第三方库：

```bash
cd ../../android-third
./build_all.sh
```

### 更新第三方库

```bash
# 1. 编译第三方库
cd ../../android-third
./build_all.sh

# 2. 复制到 hxcplayer
cd ../android/library
./build_aar.sh release --copy-libs
```

## 📦 AAR 内容

打包后的 AAR 包含：

```
hxcplayer-release.aar
├── classes.jar                 # Kotlin 编译后的类文件
├── jni/
│   ├── arm64-v8a/             # 64位 ARM (主流设备)
│   │   ├── libhxcplayer.so
│   │   ├── libavcodec.so
│   │   ├── libavformat.so
│   │   ├── libSoundTouch.so
│   │   ├── libmbedtls.so
│   │   └── ...
│   ├── armeabi-v7a/           # 32位 ARM (旧设备)
│   └── x86_64/                # 模拟器
├── AndroidManifest.xml        # 权限声明
└── res/                       # 资源文件（如果有）
```

## 🛠️ 构建选项

### 基本命令

```bash
./build_aar.sh [BUILD_TYPE] [OPTIONS]
```

### BUILD_TYPE

- `debug` - Debug 版本（包含调试符号）
- `release` - Release 版本（优化，默认）
- `all` - 同时构建两个版本

### OPTIONS

- `--clean` - 构建前清理项目
- `--copy-libs` - 从 android-third 复制第三方库
- `--help` - 显示帮助信息

### 示例

```bash
# 完整流程：复制库 + 清理 + 打包
./build_aar.sh release --copy-libs --clean

# 快速打包（库已存在）
./build_aar.sh

# 打包所有版本
./build_aar.sh all
```

## 📊 性能指标

### AAR 大小

- **Release AAR**: ~6.2 MB
- **Debug AAR**: ~6.5 MB

### 支持架构

- ✅ arm64-v8a (64位 ARM)
- ✅ armeabi-v7a (32位 ARM)
- ✅ x86_64 (模拟器)

### 系统要求

- **最低 Android 版本**: Android 7.0 (API 24)
- **目标 Android 版本**: Android 10 (API 29)
- **NDK 版本**: 25.1.8937393 或更高

## 🔗 相关链接

- **使用文档**: [HXCPlayer_AAR_Usage_Guide.md](../../HXCPlayer_AAR_Usage_Guide.md)
- **测试应用**: [examples/android-test](../../examples/android-test)
- **核心代码**: [core/](../../core)

## 📝 注意事项

1. **首次构建**：确保先编译第三方库 (`android-third/build_all.sh`)
2. **库更新**：更新 FFmpeg/SoundTouch/mbedTLS 后需要重新复制
3. **架构支持**：默认支持 3 个架构，可在 `build.gradle` 中调整
4. **测试**：建议在真机上测试 Release 版本

## 🐛 故障排查

### 问题：找不到 .so 文件

**解决**：运行 `./build_aar.sh release --copy-libs` 复制第三方库

### 问题：JAVA_HOME 未设置

**解决**：脚本会自动检测 Android Studio 的 JDK，如果失败请手动设置：
```bash
export JAVA_HOME="/Applications/Android Studio.app/Contents/jbr/Contents/Home"
```

### 问题：Gradle 下载慢

**解决**：设置代理或使用国内镜像

## 📄 许可证

本库使用的开源组件：
- FFmpeg: LGPL v2.1+
- SoundTouch: LGPL v2.1
- mbedTLS: Apache License 2.0
