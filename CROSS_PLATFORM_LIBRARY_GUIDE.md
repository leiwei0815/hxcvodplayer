# 跨平台通用库构建指南

## 概述

本指南说明如何构建适用于 **macOS**、**Windows** 和 **iOS** 的跨平台播放器库。

## 架构设计

### 1. **核心层（跨平台）**
```
core/
├── include/          # 公共头文件
│   ├── hxc_player_core.h
│   ├── hxc_player_core_c_bridge.h  # C API（跨语言调用）
│   └── ...
└── src/              # 核心实现（纯 C++）
    ├── hxc_player_core.cpp
    └── ...
```

**特点**：
- 纯 C++ 实现
- 不依赖平台特定 API
- 通过 C 接口对外暴露（`hxc_player_core_c_bridge.h`）

### 2. **平台层**
```
apple/                # macOS/iOS（Objective-C++ 封装）
├── HXCPlayer.framework    # iOS/macOS Framework
└── HXCPlayer.xcframework  # 通用 XCFramework

windows/              # Windows（C++/CLI 或纯 C 接口）
└── HXCPlayer.dll     # 动态链接库

desktop/              # Qt 桌面应用（macOS + Windows）
└── 使用 Qt5 + hxcplayer_core 静态库
```

---

## 方案选择

### **方案 1：针对不同平台构建不同格式的库**（推荐）

#### macOS
- **输出**：`HXCPlayer.xcframework`（包含 macOS + iOS）
- **构建环境**：macOS（你当前的环境）
- **特点**：
  - 可在 Swift/Objective-C 项目中使用
  - 支持 iOS 模拟器 + 真机 + macOS
  - 单一文件包含所有架构

#### Windows
- **输出**：`HXCPlayer.dll` + `HXCPlayer.lib` + 头文件
- **构建环境**：Windows（需要 Windows 机器或虚拟机）
- **特点**：
  - 标准 Windows DLL
  - 可在 C++/C#/Python 等语言中调用
  - 需要在 Windows 上编译

#### 跨平台通用（Qt）
- **输出**：`libhxcplayer_core.a`（静态库）+ C 头文件
- **构建环境**：macOS / Windows
- **特点**：
  - 纯 C++ 核心库
  - Qt 应用直接链接
  - 可跨平台编译

---

### **方案 2：在 macOS 上构建所有库**（部分可行）

#### ✅ 可以在 macOS 上构建：
1. **macOS Framework**：`HXCPlayer.framework`
2. **iOS Framework**：`HXCPlayer.framework`（iOS Simulator + Device）
3. **XCFramework**：`HXCPlayer.xcframework`（合并 macOS + iOS）
4. **macOS 静态库**：`libhxcplayer_core.a`（用于 Qt）

#### ❌ 不能在 macOS 上构建：
- **Windows DLL**：需要 Windows 环境和 MSVC/MinGW 编译器

---

## 实施步骤

### 阶段 1：构建 XCFramework（macOS + iOS）

#### 1.1 创建构建脚本

```bash
#!/bin/bash
# scripts/build_xcframework.sh

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-xcframework"
OUTPUT_DIR="${PROJECT_ROOT}/output"

echo "========================================="
echo "构建 HXCPlayer XCFramework"
echo "========================================="

# 清理旧构建
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}"

# ========== 构建 iOS 真机版本 ==========
echo ""
echo "📱 构建 iOS 真机版本（arm64）..."
cmake -G Xcode \
    -DIOS=ON \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/ios-device" \
    -S "${PROJECT_ROOT}/apple/framework" \
    -B "${BUILD_DIR}/ios-device-build"

xcodebuild -project "${BUILD_DIR}/ios-device-build/HXCPlayerFramework.xcodeproj" \
    -scheme HXCPlayer \
    -configuration Release \
    -sdk iphoneos \
    -arch arm64 \
    BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
    SKIP_INSTALL=NO \
    -derivedDataPath "${BUILD_DIR}/ios-device-build"

# ========== 构建 iOS 模拟器版本 ==========
echo ""
echo "🖥️  构建 iOS 模拟器版本（arm64 + x86_64）..."
cmake -G Xcode \
    -DIOS=ON \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/ios-simulator" \
    -S "${PROJECT_ROOT}/apple/framework" \
    -B "${BUILD_DIR}/ios-simulator-build"

xcodebuild -project "${BUILD_DIR}/ios-simulator-build/HXCPlayerFramework.xcodeproj" \
    -scheme HXCPlayer \
    -configuration Release \
    -sdk iphonesimulator \
    -arch "arm64 x86_64" \
    BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
    SKIP_INSTALL=NO \
    -derivedDataPath "${BUILD_DIR}/ios-simulator-build"

# ========== 构建 macOS 版本 ==========
echo ""
echo "💻 构建 macOS 版本（arm64 + x86_64）..."
cmake -G Xcode \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}/macos" \
    -S "${PROJECT_ROOT}/apple/framework" \
    -B "${BUILD_DIR}/macos-build"

xcodebuild -project "${BUILD_DIR}/macos-build/HXCPlayerFramework.xcodeproj" \
    -scheme HXCPlayer \
    -configuration Release \
    -sdk macosx \
    -arch "arm64 x86_64" \
    BUILD_LIBRARY_FOR_DISTRIBUTION=YES \
    SKIP_INSTALL=NO \
    -derivedDataPath "${BUILD_DIR}/macos-build"

# ========== 创建 XCFramework ==========
echo ""
echo "📦 创建 XCFramework..."

# 查找生成的 framework 路径
IOS_DEVICE_FRAMEWORK=$(find "${BUILD_DIR}/ios-device-build" -name "HXCPlayer.framework" | head -n 1)
IOS_SIMULATOR_FRAMEWORK=$(find "${BUILD_DIR}/ios-simulator-build" -name "HXCPlayer.framework" | head -n 1)
MACOS_FRAMEWORK=$(find "${BUILD_DIR}/macos-build" -name "HXCPlayer.framework" | head -n 1)

echo "iOS Device Framework: ${IOS_DEVICE_FRAMEWORK}"
echo "iOS Simulator Framework: ${IOS_SIMULATOR_FRAMEWORK}"
echo "macOS Framework: ${MACOS_FRAMEWORK}"

xcodebuild -create-xcframework \
    -framework "${IOS_DEVICE_FRAMEWORK}" \
    -framework "${IOS_SIMULATOR_FRAMEWORK}" \
    -framework "${MACOS_FRAMEWORK}" \
    -output "${OUTPUT_DIR}/HXCPlayer.xcframework"

echo ""
echo "✅ XCFramework 构建成功！"
echo "输出路径: ${OUTPUT_DIR}/HXCPlayer.xcframework"
echo ""
echo "使用方法："
echo "1. 拖拽 HXCPlayer.xcframework 到 Xcode 项目"
echo "2. Embed & Sign"
echo "3. import HXCPlayer"
echo ""
```

#### 1.2 执行构建

```bash
cd /Users/debug/project/YXVodPlayer
chmod +x scripts/build_xcframework.sh
./scripts/build_xcframework.sh
```

---

### 阶段 2：构建跨平台静态库（用于 Qt 桌面应用）

#### 2.1 macOS 静态库

```bash
#!/bin/bash
# scripts/build_static_lib_macos.sh

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build-static-macos"
OUTPUT_DIR="${PROJECT_ROOT}/output/macos-static"

mkdir -p "${BUILD_DIR}"
mkdir -p "${OUTPUT_DIR}"

echo "构建 macOS 静态库..."

cmake -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 \
    -DBUILD_DESKTOP=OFF \
    -S "${PROJECT_ROOT}" \
    -B "${BUILD_DIR}"

cmake --build "${BUILD_DIR}" --config Release

# 复制输出
cp "${BUILD_DIR}/core/src/libhxcplayer_core.a" "${OUTPUT_DIR}/"
cp -r "${PROJECT_ROOT}/core/include" "${OUTPUT_DIR}/"

echo "✅ macOS 静态库构建完成！"
echo "输出: ${OUTPUT_DIR}/libhxcplayer_core.a"
```

#### 2.2 Windows 静态库（需在 Windows 上执行）

```powershell
# scripts/build_static_lib_windows.ps1

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = "$ProjectRoot\build-static-windows"
$OutputDir = "$ProjectRoot\output\windows-static"

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Write-Host "构建 Windows 静态库..."

# 使用 MSVC 编译
cmake -G "Visual Studio 17 2022" `
    -A x64 `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_DESKTOP=OFF `
    -S "$ProjectRoot" `
    -B "$BuildDir"

cmake --build "$BuildDir" --config Release

# 复制输出
Copy-Item "$BuildDir\core\src\Release\hxcplayer_core.lib" "$OutputDir\"
Copy-Item -Recurse "$ProjectRoot\core\include" "$OutputDir\"

Write-Host "✅ Windows 静态库构建完成！"
Write-Host "输出: $OutputDir\hxcplayer_core.lib"
```

---

### 阶段 3：构建 Windows DLL（需在 Windows 上执行）

#### 3.1 修改 CMakeLists.txt 支持 DLL 导出

创建 `windows/CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.15)
project(HXCPlayerDLL VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# 核心源文件
set(CORE_SOURCES
    ../core/src/hxc_player_core.cpp
    ../core/src/hxc_decoder.cpp
    ../core/src/hxc_player_types.cpp
    ../core/src/hxc_packet_queue.cpp
    ../core/src/hxc_player_core_c_bridge.cpp
)

# 创建 DLL
add_library(HXCPlayer SHARED ${CORE_SOURCES})

target_include_directories(HXCPlayer PUBLIC
    ../core/include
)

# 链接 FFmpeg、SoundTouch 等依赖
# （需要在 Windows 上安装，可通过 vcpkg）
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavcodec libavformat libavutil libswscale libswresample
)

target_link_libraries(HXCPlayer PUBLIC
    ${FFMPEG_LIBRARIES}
)

# 安装配置
install(TARGETS HXCPlayer
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY ../core/include/
    DESTINATION include
)
```

#### 3.2 Windows 构建脚本

```powershell
# scripts/build_dll_windows.ps1

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = "$ProjectRoot\build-dll-windows"
$OutputDir = "$ProjectRoot\output\windows-dll"

cmake -G "Visual Studio 17 2022" `
    -A x64 `
    -S "$ProjectRoot\windows" `
    -B "$BuildDir"

cmake --build "$BuildDir" --config Release

# 复制 DLL 和 LIB
Copy-Item "$BuildDir\Release\HXCPlayer.dll" "$OutputDir\"
Copy-Item "$BuildDir\Release\HXCPlayer.lib" "$OutputDir\"
Copy-Item -Recurse "$ProjectRoot\core\include" "$OutputDir\"

Write-Host "✅ Windows DLL 构建完成！"
```

---

## 总结

### ✅ 在你的 macOS 上可以完成：

1. **macOS Framework**（`.framework`）
2. **iOS Framework**（`.framework`）
3. **XCFramework**（`.xcframework`）- 包含 macOS + iOS 全架构
4. **macOS 静态库**（`.a`）- 用于 Qt 等 C++ 项目

### ❌ 需要 Windows 环境才能完成：

5. **Windows DLL**（`.dll` + `.lib`）
6. **Windows 静态库**（`.lib`）

### 推荐工作流：

```
macOS 环境：
├─ 构建 XCFramework（iOS + macOS）     ✅
├─ 构建 macOS 静态库（Qt 用）          ✅
└─ 提供给 iOS/macOS 开发者使用

Windows 环境（远程或虚拟机）：
├─ 构建 Windows DLL                    ⚠️ 需要 Windows
├─ 构建 Windows 静态库（Qt 用）        ⚠️ 需要 Windows
└─ 提供给 Windows 开发者使用
```

### 替代方案（无 Windows 环境）：

如果没有 Windows 环境，可以考虑：
1. **使用 CI/CD**：GitHub Actions 提供免费的 Windows runner
2. **交叉编译**：使用 MinGW-w64（有限支持）
3. **云虚拟机**：AWS/Azure Windows 虚拟机
4. **提供源码**：让 Windows 用户自行编译

---

## 下一步

我可以立即为您创建 **XCFramework 构建脚本**，这样您就可以在当前 macOS 环境构建出通用的 iOS/macOS 库。是否需要我创建这些脚本？
