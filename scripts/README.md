# 构建脚本使用指南

本目录包含跨平台库的构建脚本。

## 脚本列表

### ✅ macOS 环境可执行

| 脚本 | 输出 | 说明 |
|------|------|------|
| `build_xcframework.sh` | `HXCPlayer.xcframework` | iOS + macOS 通用框架（推荐） |
| `build_static_lib_macos.sh` | `libhxcplayer_core.a` | macOS 静态库（Qt 项目用） |

### ⚠️ Windows 环境执行

| 脚本 | 输出 | 说明 |
|------|------|------|
| `build_windows_lib.ps1` | `HXCPlayer.dll` + `.lib` | Windows 动态/静态库 |

---

## 快速开始

### 1️⃣ 构建 XCFramework（推荐）

**适用场景**：iOS/macOS 应用开发

```bash
cd /Users/debug/project/YXVodPlayer
./scripts/build_xcframework.sh
```

**输出**：`output/HXCPlayer.xcframework`

**包含架构**：
- iOS 真机（arm64）
- iOS 模拟器（arm64 + x86_64）
- macOS（arm64 + x86_64 Universal）

**使用方法**：
1. 拖拽 `HXCPlayer.xcframework` 到 Xcode 项目
2. General -> Frameworks -> Embed & Sign
3. `import HXCPlayer`

---

### 2️⃣ 构建 macOS 静态库

**适用场景**：Qt 桌面应用、C++ 项目

```bash
cd /Users/debug/project/YXVodPlayer
./scripts/build_static_lib_macos.sh
```

**输出**：`output/macos-static/libhxcplayer_core.a`

**使用方法**（CMake）：
```cmake
include_directories(/path/to/macos-static/include)
target_link_libraries(your_app
    /path/to/macos-static/libhxcplayer_core.a
    avcodec avformat avutil swscale swresample
    "-framework CoreFoundation"
    "-framework CoreAudio"
    "-framework AudioToolbox"
)
```

---

### 3️⃣ 构建 Windows 库

**⚠️ 需要在 Windows 环境执行**

**前置要求**：
```powershell
# 安装依赖（vcpkg）
vcpkg install ffmpeg:x64-windows
vcpkg install sdl2:x64-windows
vcpkg install soundtouch:x64-windows
```

**执行构建**：
```powershell
cd C:\path\to\YXVodPlayer
.\scripts\build_windows_lib.ps1
```

**输出**：
- `output/windows/dll/HXCPlayer.dll` - 动态库
- `output/windows/static/hxcplayer_core.lib` - 静态库

---

## 常见问题

### Q1: XCFramework 构建失败

**错误**：`xcodebuild: error: Unable to find a scheme`

**解决**：确保已安装 Xcode 命令行工具
```bash
xcode-select --install
```

---

### Q2: 找不到 FFmpeg

**错误**：`pkg_check_modules(FFMPEG REQUIRED ...) failed`

**解决**：
```bash
# macOS
brew install ffmpeg soundtouch

# Windows
vcpkg install ffmpeg:x64-windows soundtouch:x64-windows
```

---

### Q3: macOS 静态库架构不匹配

**错误**：`architecture mismatch`

**解决**：检查静态库架构
```bash
lipo -info output/macos-static/libhxcplayer_core.a
# 应输出: arm64 x86_64
```

如果只有单架构，修改 `build_static_lib_macos.sh` 中的 `CMAKE_OSX_ARCHITECTURES`。

---

### Q4: 无法在 macOS 上构建 Windows 库

**回答**：这是正常的。Windows DLL 必须在 Windows 环境编译。

**替代方案**：
1. 使用 GitHub Actions（免费 Windows runner）
2. 使用云 Windows 虚拟机
3. 提供源码让 Windows 用户自行编译

---

## 架构支持

### XCFramework
- ✅ iOS 真机（arm64）
- ✅ iOS 模拟器（arm64 + x86_64）
- ✅ macOS（arm64 + x86_64 Universal）

### macOS 静态库
- ✅ Apple Silicon（arm64）
- ✅ Intel Mac（x86_64）

### Windows 库
- ✅ x64（需在 Windows 上构建）
- ❌ x86（32位，不支持）

---

## 输出文件结构

```
output/
├── HXCPlayer.xcframework/          # iOS + macOS 通用框架
│   ├── ios-arm64/
│   ├── ios-arm64_x86_64-simulator/
│   └── macos-arm64_x86_64/
│
├── macos-static/                   # macOS 静态库
│   ├── libhxcplayer_core.a
│   ├── include/
│   └── README.md
│
└── windows/                        # Windows 库（需在 Windows 构建）
    ├── dll/
    │   ├── HXCPlayer.dll
    │   ├── HXCPlayer.lib
    │   └── include/
    ├── static/
    │   ├── hxcplayer_core.lib
    │   └── include/
    └── README.md
```

---

## 下一步

1. **iOS/macOS 开发者**：运行 `./scripts/build_xcframework.sh`
2. **Qt 开发者（macOS）**：运行 `./scripts/build_static_lib_macos.sh`
3. **Windows 开发者**：在 Windows 上运行 `.\scripts\build_windows_lib.ps1`

更多详细信息请参考：
- [跨平台库完整指南](../CROSS_PLATFORM_LIBRARY_GUIDE.md)
- 各输出目录中的 `README.md`
