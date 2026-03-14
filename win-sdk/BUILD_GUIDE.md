# Windows SDK 构建指南

本文档说明如何构建 HXCPlayer Windows SDK (DLL)。

## 📋 前置要求

### 必需工具

1. **Visual Studio 2019/2022**
   - 包含 C++ 桌面开发工作负载
   - 包含 CMake 工具

2. **CMake 3.15+**
   ```bash
   cmake --version
   ```

3. **vcpkg** (包管理器)
   ```bash
   # 克隆 vcpkg
   git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
   cd C:\vcpkg
   .\bootstrap-vcpkg.bat
   
   # 设置环境变量
   setx VCPKG_ROOT "C:\vcpkg"
   ```

4. **Qt 5.15.2** (可选，仅桌面版需要)
   - 下载：https://www.qt.io/download
   - 安装路径：`C:\Qt\5.15.2\msvc2019_64`

### 依赖库

使用 vcpkg 安装：

```bash
cd C:\vcpkg

# 安装 FFmpeg
.\vcpkg install ffmpeg:x64-windows

# 安装 SDL2
.\vcpkg install sdl2:x64-windows

# (可选) 安装 SoundTouch
.\vcpkg install soundtouch:x64-windows
```

或者使用项目自带的脚本编译 SoundTouch：
```bash
cd win-third
.\install_all.bat
```

**依赖说明**：
- **FFmpeg**：必需，用于视频解码
- **SDL2**：必需，用于音频输出和视频渲染
- **SoundTouch**：可选，用于变速播放（不影响其他功能）

**自动处理**：构建脚本会自动将所有依赖 DLL 复制到 SDK 包中，详见 [DEPENDENCIES.md](DEPENDENCIES.md)

## 🔨 构建步骤

### 方法 1：使用构建脚本（推荐）

#### Release 版本
```bash
cd win-sdk
.\build_sdk.bat
```

#### Debug 版本
```bash
cd win-sdk
.\build_sdk.bat debug
```

### 方法 2：手动构建

#### 1. 创建构建目录
```bash
mkdir build\win-sdk-release
cd build\win-sdk-release
```

#### 2. 配置 CMake
```bash
cmake ..\.. ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_PREFIX_PATH="C:\vcpkg\installed\x64-windows" ^
    -DBUILD_SHARED_LIBS=ON ^
    -DBUILD_DESKTOP=OFF
```

**重要参数说明**：
- `-DBUILD_SHARED_LIBS=ON` - 构建 DLL（不是静态库）
- `-DBUILD_DESKTOP=OFF` - 不构建桌面应用（仅 SDK）

#### 3. 编译
```bash
cmake --build . --config Release --parallel
```

#### 4. 打包 SDK
```bash
cmake --build . --config Release --target package_sdk
```

## 📦 输出结果

成功构建后，SDK 位于：
```
build\win-sdk-release\HXCPlayerSDK\
├── include\                 # 头文件
│   ├── hxc_player_core_c_bridge.h
│   └── hxcplayer_sdk.h
├── lib\                     # 导入库
│   └── hxcplayer.lib
├── bin\                     # 运行时 DLL
│   ├── hxcplayer.dll
│   └── (其他依赖 DLL)
├── example\                 # 示例代码
│   ├── simple_player.c
│   └── CMakeLists.txt
├── docs\                    # 文档
│   └── SDK_USAGE.md
└── README.md
```

## 🧪 测试 SDK

### 编译示例程序

```bash
cd build\win-sdk-release\HXCPlayerSDK\example
mkdir build
cd build

cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

# 运行
.\Release\simple_player.exe test.mp4
```

## 📤 分发 SDK

将整个 `HXCPlayerSDK` 文件夹打包即可：

```bash
# 创建 ZIP 压缩包
cd build\win-sdk-release
powershell Compress-Archive -Path HXCPlayerSDK -DestinationPath HXCPlayerSDK-v1.0.0-win64.zip
```

用户只需解压即可使用！

## 🔍 故障排查

### 问题 1：找不到 vcpkg

**错误**：
```
CMake Error: Could not find CMAKE_PREFIX_PATH
```

**解决**：
```bash
# 检查 vcpkg 安装
dir C:\vcpkg\installed\x64-windows

# 重新设置环境变量
set VCPKG_ROOT=C:\vcpkg
```

### 问题 2：FFmpeg 找不到

**错误**：
```
CMake Error: Could not find FFmpeg
```

**解决**：
```bash
# 重新安装 FFmpeg
cd C:\vcpkg
.\vcpkg remove ffmpeg:x64-windows
.\vcpkg install ffmpeg:x64-windows
```

### 问题 3：编译时缺少头文件

**错误**：
```
fatal error C1083: Cannot open include file: 'soundtouch/SoundTouch.h'
```

**解决**：
SoundTouch 是可选的，可以：
1. 安装 SoundTouch：`.\vcpkg install soundtouch:x64-windows`
2. 或使用项目脚本：`cd win-third && .\install_all.bat`

### 问题 4：DLL 依赖缺失

**错误**：运行时提示找不到 DLL

**解决**：
确保 `bin/` 目录包含所有依赖 DLL：
```bash
# 手动复制依赖
copy C:\vcpkg\installed\x64-windows\bin\*.dll build\win-sdk-release\HXCPlayerSDK\bin\
```

## 🔄 持续集成

可以在 CI/CD 中自动构建：

```yaml
# GitHub Actions 示例
- name: Build Windows SDK
  run: |
    cd win-sdk
    .\build_sdk.bat
    
- name: Upload SDK
  uses: actions/upload-artifact@v3
  with:
    name: HXCPlayerSDK-Windows
    path: build/win-sdk-release/HXCPlayerSDK/
```

## 📝 版本管理

修改版本号：

1. 编辑 `CMakeLists.txt`：
   ```cmake
   project(HXCVodPlayer VERSION 1.0.0)
   ```

2. 编辑 `win-sdk/hxcplayer_sdk.h`：
   ```c
   #define HXCPLAYER_SDK_VERSION_MAJOR 1
   #define HXCPLAYER_SDK_VERSION_MINOR 0
   #define HXCPLAYER_SDK_VERSION_PATCH 0
   ```

3. 重新构建

## 🎯 构建选项

可以通过 CMake 选项自定义构建：

```bash
cmake .. \
    -DBUILD_SHARED_LIBS=ON \          # ON=DLL, OFF=静态库
    -DCMAKE_BUILD_TYPE=Release \      # Release/Debug
    -DBUILD_DESKTOP=OFF \             # 是否构建桌面版
    -DVCPKG_ROOT="C:\vcpkg" \         # vcpkg 路径
    -DQt5_DIR="C:\Qt\5.15.2\..."      # Qt 路径（如果需要）
```

## 🌟 最佳实践

1. **使用 Release 构建**：
   - 性能更好
   - 文件更小
   - 用户不需要 Debug 运行时

2. **包含所有依赖**：
   - 将所有 DLL 打包到 `bin/`
   - 用户无需单独安装依赖

3. **提供示例代码**：
   - 让用户快速上手
   - 演示最佳实践

4. **版本化分发**：
   - 文件名包含版本号
   - 如：`HXCPlayerSDK-v1.0.0-win64.zip`

## 📞 获取帮助

- Issues: [GitHub Issues]
- Email: [支持邮箱]
- 文档: `docs/SDK_USAGE.md`
