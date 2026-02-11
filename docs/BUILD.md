# YXVodPlayer 构建指南

## 准备工作

### 公共依赖

#### FFmpeg 8.x

**macOS (使用 Homebrew)**
```bash
brew install ffmpeg
```

**Windows (使用 vcpkg)**
```bash
vcpkg install ffmpeg:x64-windows
```

**从源码编译**
```bash
git clone https://github.com/FFmpeg/FFmpeg.git
cd FFmpeg
./configure --prefix=/usr/local --enable-shared
make -j8
sudo make install
```

#### SDL2

**macOS**
```bash
brew install sdl2
```

**Windows**
```bash
vcpkg install sdl2:x64-windows
```

#### CMake

**macOS**
```bash
brew install cmake
```

**Windows**
下载并安装: https://cmake.org/download/

## Desktop 版本构建

### macOS

#### 1. 安装 Qt5

```bash
brew install qt@5
```

#### 2. 配置环境变量

```bash
export Qt5_DIR=/usr/local/opt/qt@5/lib/cmake/Qt5
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
```

#### 3. 构建

```bash
cd /Users/debug/project/YXVodPlayer
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

#### 4. 运行

```bash
./bin/YXVodPlayer
```

### Windows

#### 1. 安装 Qt5

下载并安装 Qt5: https://www.qt.io/download

或使用 vcpkg:
```bash
vcpkg install qt5-base:x64-windows
```

#### 2. 使用 CMake GUI 配置

1. 设置源代码目录
2. 设置构建目录
3. 点击 "Configure"
4. 选择 Visual Studio 生成器
5. 设置 Qt5_DIR 路径
6. 点击 "Generate"

#### 3. 构建

使用 Visual Studio 打开生成的解决方案，或使用命令行：

```bash
cmake --build . --config Release
```

#### 4. 运行

```bash
bin\Release\YXVodPlayer.exe
```

### Linux (Ubuntu/Debian)

#### 1. 安装依赖

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    libavcodec-dev \
    libavformat-dev \
    libavutil-dev \
    libswscale-dev \
    libswresample-dev \
    libsdl2-dev \
    qtbase5-dev \
    qtdeclarative5-dev
```

#### 2. 构建

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

#### 3. 运行

```bash
./bin/YXVodPlayer
```

## Android 版本构建

### 准备工作

#### 1. 安装 Android Studio

下载并安装: https://developer.android.com/studio

#### 2. 安装 NDK

在 Android Studio 中:
- Tools → SDK Manager → SDK Tools
- 勾选 "NDK (Side by side)"
- 点击 "Apply"

#### 3. 编译 FFmpeg for Android

```bash
# 下载 FFmpeg
git clone https://github.com/FFmpeg/FFmpeg.git
cd FFmpeg

# 配置交叉编译
export NDK=/path/to/android-ndk
export TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/darwin-x86_64
export API=21

# 编译 arm64-v8a
./configure \
    --prefix=./android/arm64-v8a \
    --enable-cross-compile \
    --target-os=android \
    --arch=aarch64 \
    --cpu=armv8-a \
    --cross-prefix=$TOOLCHAIN/bin/aarch64-linux-android- \
    --cc=$TOOLCHAIN/bin/aarch64-linux-android${API}-clang \
    --enable-shared \
    --disable-static \
    --disable-doc \
    --disable-programs

make clean
make -j8
make install
```

### 构建步骤

#### 1. 创建 Android 项目

在 `android/` 目录中已包含 Android 项目结构。

#### 2. 配置 FFmpeg 路径

编辑 `android/app/build.gradle`:

```gradle
android {
    ...
    externalNativeBuild {
        cmake {
            path "src/main/cpp/CMakeLists.txt"
            version "3.18.1"
        }
    }
}
```

编辑 `android/app/src/main/cpp/CMakeLists.txt`:

```cmake
set(FFMPEG_DIR /path/to/ffmpeg/android/arm64-v8a)
```

#### 3. 使用 Android Studio 构建

1. 打开 `android/` 目录
2. 等待 Gradle 同步完成
3. Build → Make Project
4. Run → Run 'app'

#### 4. 使用命令行构建

```bash
cd android
./gradlew assembleDebug
# 或发布版本
./gradlew assembleRelease
```

生成的 APK 位于: `android/app/build/outputs/apk/`

## iOS 版本构建

### 准备工作

#### 1. 安装 Xcode

从 App Store 安装 Xcode

#### 2. 编译 FFmpeg for iOS

```bash
# 使用 FFmpeg-iOS-build-script
git clone https://github.com/kewlbear/FFmpeg-iOS-build-script.git
cd FFmpeg-iOS-build-script
./build-ffmpeg.sh

# 或手动编译
git clone https://github.com/FFmpeg/FFmpeg.git
cd FFmpeg

# 编译 arm64 (真机)
./configure \
    --prefix=./ios/arm64 \
    --enable-cross-compile \
    --target-os=darwin \
    --arch=arm64 \
    --cc="xcrun -sdk iphoneos clang" \
    --as="gas-preprocessor.pl -arch arm64 -- xcrun -sdk iphoneos clang" \
    --enable-pic \
    --disable-programs \
    --disable-doc

make clean
make -j8
make install

# 编译 x86_64 (模拟器)
./configure \
    --prefix=./ios/x86_64 \
    --enable-cross-compile \
    --target-os=darwin \
    --arch=x86_64 \
    --cc="xcrun -sdk iphonesimulator clang" \
    --enable-pic \
    --disable-programs \
    --disable-doc

make clean
make -j8
make install

# 创建 universal 库
lipo -create \
    ios/arm64/lib/libavcodec.a \
    ios/x86_64/lib/libavcodec.a \
    -output libavcodec.a
# 对所有 FFmpeg 库重复此操作
```

### 构建步骤

#### 1. 创建 Xcode 项目

在 `ios/` 目录中已包含 Xcode 项目。

#### 2. 配置 FFmpeg 库

1. 打开 `YXVodPlayer.xcodeproj`
2. 选择项目 → Build Settings
3. 在 "Library Search Paths" 添加 FFmpeg 库路径
4. 在 "Header Search Paths" 添加 FFmpeg 头文件路径

#### 3. 添加 FFmpeg 库

将编译好的 FFmpeg 静态库添加到项目:
- libavcodec.a
- libavformat.a
- libavutil.a
- libswscale.a
- libswresample.a

#### 4. 构建

**使用 Xcode**
1. 选择目标设备或模拟器
2. Product → Build (⌘B)
3. Product → Run (⌘R)

**使用命令行**
```bash
cd ios

# 构建模拟器版本
xcodebuild -project YXVodPlayer.xcodeproj \
    -scheme YXVodPlayer \
    -sdk iphonesimulator \
    -configuration Debug \
    build

# 构建真机版本
xcodebuild -project YXVodPlayer.xcodeproj \
    -scheme YXVodPlayer \
    -sdk iphoneos \
    -configuration Release \
    build

# 创建 Archive
xcodebuild -project YXVodPlayer.xcodeproj \
    -scheme YXVodPlayer \
    -sdk iphoneos \
    -configuration Release \
    archive \
    -archivePath ./build/YXVodPlayer.xcarchive

# 导出 IPA
xcodebuild -exportArchive \
    -archivePath ./build/YXVodPlayer.xcarchive \
    -exportPath ./build \
    -exportOptionsPlist exportOptions.plist
```

## 常见问题

### Desktop

**Q: 找不到 Qt5**
```bash
# macOS
export Qt5_DIR=/usr/local/opt/qt@5/lib/cmake/Qt5

# Linux
export Qt5_DIR=/usr/lib/x86_64-linux-gnu/cmake/Qt5
```

**Q: 找不到 FFmpeg**
```bash
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
```

**Q: SDL2 链接错误**
```bash
# 确保安装了 SDL2
brew install sdl2  # macOS
sudo apt install libsdl2-dev  # Linux
```

### Android

**Q: NDK 版本不匹配**

在 `android/app/build.gradle` 中指定 NDK 版本:
```gradle
android {
    ndkVersion "21.4.7075529"
}
```

**Q: FFmpeg 库找不到**

检查 CMakeLists.txt 中的路径:
```cmake
set(FFMPEG_DIR ${CMAKE_SOURCE_DIR}/libs/ffmpeg/${ANDROID_ABI})
```

### iOS

**Q: 架构不匹配**

确保 FFmpeg 库包含所需的架构:
```bash
lipo -info libavcodec.a
```

**Q: Bitcode 错误**

在 Build Settings 中禁用 Bitcode，或重新编译 FFmpeg 时添加 `--enable-bitcode`

## 性能优化

### 编译优化选项

**Release 构建**
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
```

**启用 LTO (Link Time Optimization)**
```bash
cmake .. -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON
```

**特定 CPU 优化**
```bash
# x86_64
cmake .. -DCMAKE_CXX_FLAGS="-march=native"

# ARM
cmake .. -DCMAKE_CXX_FLAGS="-mcpu=native"
```

## 调试构建

### Debug 模式

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
```

### 启用 Sanitizers

```bash
# Address Sanitizer
cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=address"

# Thread Sanitizer
cmake .. -DCMAKE_CXX_FLAGS="-fsanitize=thread"
```

### 生成编译数据库

```bash
cmake .. -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

## 持续集成

### GitHub Actions

创建 `.github/workflows/build.yml`:

```yaml
name: Build

on: [push, pull_request]

jobs:
  build-macos:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v2
      - name: Install dependencies
        run: brew install ffmpeg sdl2 qt@5
      - name: Build
        run: |
          mkdir build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Release
          make -j4
```

## 打包和分发

### macOS DMG

```bash
# 使用 create-dmg
brew install create-dmg
create-dmg \
    --volname "YXVodPlayer" \
    --window-pos 200 120 \
    --window-size 800 400 \
    --icon-size 100 \
    --app-drop-link 600 185 \
    YXVodPlayer.dmg \
    bin/YXVodPlayer.app
```

### Windows Installer

使用 NSIS 或 WiX 创建安装程序。

### Android APK 签名

```bash
jarsigner -verbose \
    -sigalg SHA1withRSA \
    -digestalg SHA1 \
    -keystore my-release-key.keystore \
    app-release-unsigned.apk \
    alias_name

zipalign -v 4 app-release-unsigned.apk YXVodPlayer.apk
```

### iOS IPA

参见上面的 xcodebuild 导出步骤。
