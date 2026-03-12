# YXVodPlayer - Windows 平台构建指南

## 概述

本文档说明如何在 Windows 平台上构建 YXVodPlayer 项目，并生成可用于 Visual Studio 调试的项目文件。

## 系统要求

- **操作系统**: Windows 10 或更高版本
- **Visual Studio**: 2019 或 2022（推荐 2022）
- **CMake**: 3.15 或更高版本
- **vcpkg**: 包管理器（强烈推荐）

## 依赖库

本项目依赖以下库：

1. **Qt5** - GUI 框架
2. **SDL2** - 音视频渲染
3. **FFmpeg** - 音视频解码
4. **SoundTouch** - 倍速播放（可选）

## 安装步骤

### 1. 安装 Visual Studio

从 [Visual Studio 官网](https://visualstudio.microsoft.com/) 下载并安装：

- Visual Studio 2022 Community（免费）或更高版本
- 安装时选择 "C++ 桌面开发" 工作负载

### 2. 安装 CMake

从 [CMake 官网](https://cmake.org/download/) 下载安装，并确保添加到 PATH。

验证安装：
```cmd
cmake --version
```

### 3. 安装 vcpkg（推荐）

vcpkg 是 Microsoft 推荐的 C++ 包管理器，可以极大简化依赖管理。

#### 安装 vcpkg

```cmd
cd C:\
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat
```

#### 设置环境变量

将 `C:\vcpkg` 添加到系统环境变量 `VCPKG_ROOT`:

```cmd
setx VCPKG_ROOT "C:\vcpkg" /M
```

#### 安装依赖库

使用 vcpkg 安装所有必需的依赖：

```cmd
cd C:\vcpkg

# 基础依赖
vcpkg install ffmpeg:x64-windows
vcpkg install sdl2:x64-windows
vcpkg install soundtouch:x64-windows

# Qt5 依赖
vcpkg install qt5-base:x64-windows
vcpkg install qt5-multimedia:x64-windows

# 或者一次性安装所有依赖
vcpkg install ffmpeg:x64-windows sdl2:x64-windows soundtouch:x64-windows qt5-base:x64-windows qt5-multimedia:x64-windows
```

**注意**: Qt5 的安装可能需要较长时间（30分钟到1小时）。

#### vcpkg 集成到 Visual Studio

```cmd
vcpkg integrate install
```

### 4. 手动安装（不使用 vcpkg）

如果不想使用 vcpkg，可以手动安装依赖：

#### Qt5
- 下载: https://www.qt.io/download
- 安装后设置环境变量 `Qt5_DIR` 指向 Qt5 的 CMake 目录
  ```cmd
  setx Qt5_DIR "C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5"
  ```

#### FFmpeg
- 下载预编译版本: https://github.com/BtbN/FFmpeg-Builds/releases
- 解压后设置 `PKG_CONFIG_PATH` 或手动配置 CMake 路径

#### SDL2
- 下载: https://www.libsdl.org/download-2.0.php
- 下载开发库（Development Libraries）

#### SoundTouch
- 下载: https://www.surina.net/soundtouch/download.html
- 编译或使用预编译版本

## 构建项目

### 使用构建脚本（推荐）

本项目提供了 `build_windows.bat` 脚本来简化构建过程。

#### 生成 Visual Studio 2022 项目

```cmd
# Release 版本
build_windows.bat vs2022 release

# Debug 版本
build_windows.bat vs2022 debug
```

#### 生成 Visual Studio 2019 项目

```cmd
build_windows.bat vs2019 release
```

#### 直接构建（不打开 Visual Studio）

```cmd
# Release 版本
build_windows.bat build release

# Debug 版本
build_windows.bat build debug
```

#### 清理构建文件

```cmd
build_windows.bat clean
```

### 手动使用 CMake

如果你想手动控制构建过程：

```cmd
# 创建构建目录
mkdir build\vs2022_release
cd build\vs2022_release

# 使用 vcpkg
cmake ..\.. -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_TOOLCHAIN_FILE=C:\vcpkg\scripts\buildsystems\vcpkg.cmake ^
    -DBUILD_DESKTOP=ON

# 不使用 vcpkg
cmake ..\.. -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_DESKTOP=ON

# 打开生成的解决方案
start YXVodPlayer.sln
```

## Visual Studio 中调试

1. **打开项目**
   ```cmd
   start build\vs2022_release\YXVodPlayer.sln
   ```

2. **设置启动项目**
   - 在解决方案资源管理器中，右键点击 `YXVodPlayer` 项目
   - 选择"设为启动项目"

3. **配置调试选项**
   - 右键点击 `YXVodPlayer` 项目 -> 属性
   - 调试 -> 工作目录：设置为项目根目录
   - 调试 -> 命令参数：可以添加测试视频文件路径

4. **开始调试**
   - 按 F5 开始调试
   - 按 Ctrl+F5 运行（不调试）

## 项目结构

```
hxcvodplayer/
├── core/                   # 核心播放器逻辑
│   ├── include/           # 头文件
│   └── src/               # 源文件
├── desktop/               # Qt 桌面应用
│   ├── main.cpp           # 程序入口
│   ├── main_window.cpp    # 主窗口
│   ├── video_widget.cpp   # 视频显示窗口
│   ├── sdl_renderer.cpp   # SDL 渲染器
│   └── qt_platform_factory.cpp  # Qt 平台工厂
├── CMakeLists.txt         # 主 CMake 配置
├── build_windows.bat      # Windows 构建脚本
└── README_WINDOWS.md      # 本文档
```

## 常见问题

### 0. PowerShell 中文乱码

**现象**: 运行批处理脚本时中文显示为乱码或问号

**解决方法**:

方法 1（临时）: 运行脚本前执行
```powershell
chcp 65001
```

方法 2（推荐）: 使用命令提示符（CMD）代替 PowerShell
```cmd
cmd
cd /d d:\git\hxcvodplayer
quickstart_windows.bat
```

方法 3（推荐）: 使用 Windows Terminal（从 Microsoft Store 安装）

详细解决方案请查看 [ENCODING_FIX.md](ENCODING_FIX.md)

### 1. CMake 找不到 Qt5

**解决方法**:
```cmd
# 设置 Qt5_DIR 环境变量
setx Qt5_DIR "C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5"

# 或者在 CMake 命令中指定
cmake .. -DQt5_DIR="C:\Qt\5.15.2\msvc2019_64\lib\cmake\Qt5"
```

### 2. FFmpeg 库找不到

**解决方法**:
- 使用 vcpkg 安装: `vcpkg install ffmpeg:x64-windows`
- 或设置 `PKG_CONFIG_PATH` 指向 FFmpeg 的 .pc 文件

### 3. 编译时出现链接错误

**检查事项**:
1. 确保所有依赖都是 x64 版本
2. 确保 Debug 配置链接 Debug 库，Release 配置链接 Release 库
3. 检查 vcpkg 是否正确集成: `vcpkg integrate install`

### 4. 运行时缺少 DLL

**解决方法**:
- 将依赖的 DLL 文件复制到可执行文件目录
- 或者将依赖库的 bin 目录添加到系统 PATH
- 使用 vcpkg 时，它会自动处理大部分 DLL 依赖

### 5. SoundTouch 未找到

SoundTouch 是可选依赖，用于倍速播放功能。如果不需要此功能，可以忽略此警告。

**安装方法**:
```cmd
vcpkg install soundtouch:x64-windows
```

## 性能优化建议

### Release 构建
- 使用 Release 配置以获得最佳性能
- 启用编译器优化（CMake 默认已启用）

### Visual Studio 配置
- 配置 -> Release
- 优化 -> 最大优化（速度优先）
- 代码生成 -> 运行时库 -> 多线程 DLL

## 技术栈

- **语言**: C++17
- **GUI 框架**: Qt5
- **视频渲染**: SDL2
- **音视频解码**: FFmpeg (libavcodec, libavformat, libavutil, libswscale, libswresample)
- **倍速播放**: SoundTouch
- **构建系统**: CMake 3.15+

## 开发工具推荐

- **IDE**: Visual Studio 2022
- **调试工具**: Visual Studio Debugger
- **性能分析**: Visual Studio Profiler
- **代码格式化**: ClangFormat
- **版本控制**: Git

## 参考链接

- [Qt 文档](https://doc.qt.io/)
- [SDL2 文档](https://wiki.libsdl.org/)
- [FFmpeg 文档](https://ffmpeg.org/documentation.html)
- [CMake 文档](https://cmake.org/documentation/)
- [vcpkg 文档](https://vcpkg.io/)

## 支持

如有问题，请查看：
- 项目主 README.md
- GitHub Issues
- 或联系开发团队

---

最后更新: 2026-02-26
