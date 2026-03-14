# Windows 第三方库本地编译指南

## 📋 概述

本目录包含在 Windows 平台上编译所有第三方依赖库的脚本，**无需 vcpkg**，完全本地化编译。

### ✨ 优势

- ✅ **完全自主可控**：不依赖 vcpkg，可完全控制编译选项
- ✅ **版本固定**：锁定特定版本，避免兼容性问题
- ✅ **简化部署**：新开发者只需运行脚本即可
- ✅ **统一管理**：所有第三方库集中在 `win-third` 目录
- ✅ **Debug/Release 分离**：支持编译调试和发布版本

### 📦 包含的库

| 库名 | 版本 | 用途 | 编译时间 |
|------|------|------|---------|
| **SDL2** | 2.30.9 | 音视频渲染 | ~2 分钟 |
| **SoundTouch** | 2.3.x | 变速播放 | ~1 分钟 |
| **FFmpeg** | 8.0.1 | 音视频解码 | ~30 分钟 |

## 🚀 快速开始

### 一键编译所有库（推荐）

```bash
cd win-third
build_all.bat
```

这将依次编译：
1. SDL2（Debug + Release）
2. SoundTouch（Debug + Release）
3. FFmpeg（共享库）
4. 配置 CMake 项目

预计时间：**30-60 分钟**（取决于 CPU 性能）

### 分别编译

如果只需要编译特定库：

```bash
# 编译 SDL2
build_sdl2.bat

# 编译 SoundTouch
download_soundtouch.bat          # 下载源码
build_soundtouch_multi.bat       # 编译
fix_soundtouch_headers_multi.bat # 修复头文件

# 编译 FFmpeg
build_ffmpeg.bat
```

## 📋 前置要求

### 1. 必需工具

- **CMake 3.15+**
  - 下载: https://cmake.org/download/
  - 用途: 配置 SDL2、SoundTouch

- **Visual Studio 2019/2022**
  - 组件: C++ 桌面开发
  - 用途: 编译 C++ 代码

### 2. FFmpeg 编译额外要求

- **MSYS2**
  - 下载: https://www.msys2.org/
  - 安装后执行：
    ```bash
    pacman -S base-devel mingw-w64-x86_64-toolchain
    pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-nasm yasm
    ```
  - 添加到 PATH: `C:\msys64\mingw64\bin`

> **注意**: SDL2 和 SoundTouch 只需要 CMake + Visual Studio，无需 MSYS2

## 📁 目录结构

编译完成后的目录结构：

```
win-third/
├── sdl2-src/              # SDL2 源码
├── sdl2-build/            # SDL2 构建目录
├── sdl2-install/          # SDL2 安装目录
│   ├── debug/             # Debug 版本
│   │   ├── include/
│   │   ├── lib/
│   │   └── bin/           # SDL2.dll (Debug)
│   └── release/           # Release 版本
│       ├── include/
│       ├── lib/
│       └── bin/           # SDL2.dll (Release)
│
├── soundtouch-src/        # SoundTouch 源码
├── soundtouch-build/      # SoundTouch 构建目录
├── soundtouch-install/    # SoundTouch 安装目录
│   ├── debug/
│   │   ├── include/
│   │   └── lib/           # SoundTouch.lib (静态库)
│   └── release/
│       ├── include/
│       └── lib/           # SoundTouch.lib (静态库)
│
├── ffmpeg-src/            # FFmpeg 源码
├── ffmpeg-build/          # FFmpeg 构建目录
└── ffmpeg-install/        # FFmpeg 安装目录
    ├── include/           # FFmpeg 头文件
    ├── lib/               # .lib 导入库
    └── bin/               # .dll 动态库
        ├── avcodec-62.dll
        ├── avformat-62.dll
        ├── avutil-60.dll
        ├── swscale-8.dll
        └── swresample-5.dll
```

## 🔧 各库详细说明

### SDL2

**脚本**: `build_sdl2.bat`

**功能**:
- 自动下载 SDL2 2.30.9 源码
- 使用 CMake + MSVC 编译
- 生成 Debug 和 Release 两个版本
- 输出动态库 (DLL)

**输出**:
- `sdl2-install/debug/bin/SDL2.dll`
- `sdl2-install/release/bin/SDL2.dll`

### SoundTouch

**脚本**: 
- `download_soundtouch.bat` - 下载源码
- `build_soundtouch_multi.bat` - 编译
- `fix_soundtouch_headers_multi.bat` - 修复 MSVC 兼容性问题

**功能**:
- 从 GitLab 克隆源码
- 编译为静态库 (`.lib`)
- 修复头文件以兼容 MSVC
- 生成 Debug 和 Release 版本

**输出**:
- `soundtouch-install/debug/lib/SoundTouch.lib`
- `soundtouch-install/release/lib/SoundTouch.lib`

**注意**: SoundTouch 是静态库，会被链接到 `hxcplayer.dll` 中，不需要单独分发。

### FFmpeg

**脚本**: `build_ffmpeg.bat`

**功能**:
- 自动下载 FFmpeg 8.0.1 源码
- 使用 MSYS2/MinGW64 编译
- 生成共享库 (DLL) + 导入库 (LIB)
- 精简配置，只包含必要组件

**配置说明**:
- ✅ 启用: H.264/HEVC 解码、AAC/MP3 音频、网络协议
- ❌ 禁用: ffmpeg/ffplay/ffprobe 工具、文档、静态库

**输出**:
- `ffmpeg-install/bin/*.dll` - 动态库
- `ffmpeg-install/lib/*.lib` - MSVC 导入库
- `ffmpeg-install/include/` - 头文件

**编译时间**: 约 30-40 分钟（取决于 CPU）

## 🔄 CMake 集成

项目的 `CMakeLists.txt` 已配置为优先使用本地编译的库：

```cmake
# 查找顺序：
# 1. win-third/xxx-install  ← 优先（本地编译）
# 2. CMAKE_PREFIX_PATH      ← 其次
# 3. vcpkg                  ← 最后（兼容性）
# 4. 系统路径               ← 默认
```

### 配置项目

编译完库后，配置 CMake 项目：

```bash
cd win-third
configure_project_multi.bat
```

或手动配置：

```bash
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DBUILD_DESKTOP=ON
```

CMake 会自动检测并使用本地编译的库。

## 🎯 SDK 构建

构建 Windows SDK 时，会自动复制依赖 DLL：

```bash
cd win-sdk
build_sdk.bat
```

`copy_dependencies.bat` 会按以下优先级查找 DLL：
1. **本地编译版本** (`win-third/xxx-install`)
2. **vcpkg 版本** (`C:\vcpkg\installed\x64-windows\bin`)

## 🐛 常见问题

### Q1: FFmpeg 编译失败，提示找不到 bash

**A**: 需要安装 MSYS2 并添加到 PATH。

```bash
# 检查 bash 是否可用
where bash

# 应该输出类似：C:\msys64\mingw64\bin\bash.exe
```

如果没有输出，安装 MSYS2：
1. 下载: https://www.msys2.org/
2. 安装
3. 在 MSYS2 终端执行：
   ```bash
   pacman -S base-devel mingw-w64-x86_64-toolchain
   pacman -S mingw-w64-x86_64-nasm yasm
   ```
4. 添加到 PATH: `C:\msys64\mingw64\bin`

### Q2: SDL2 或 SoundTouch 编译失败

**A**: 检查 CMake 和 Visual Studio 是否正确安装。

```bash
# 检查 CMake
cmake --version

# 检查 MSVC（在 VS 开发者命令提示符中）
cl
```

### Q3: CMake 找不到本地编译的库

**A**: 确保库已成功编译并安装到 `xxx-install` 目录。

检查目录是否存在：
```bash
dir win-third\sdl2-install\release\bin
dir win-third\soundtouch-install\release\lib
dir win-third\ffmpeg-install\bin
```

### Q4: SoundTouch 头文件报错 C1017

**A**: 运行头文件修复脚本：

```bash
fix_soundtouch_headers_multi.bat
```

### Q5: 编译的 FFmpeg DLL 能在 MSVC 项目中使用吗？

**A**: 可以！虽然 FFmpeg 是用 MinGW 编译的，但生成的 DLL 可以被 MSVC 链接。脚本会自动生成 MSVC 兼容的 `.lib` 导入库。

如果需要重新生成 `.lib`：
1. 在 VS 开发者命令提示符中
2. 运行 `build_ffmpeg.bat`
3. 或手动使用 `lib /def:xxx.def` 生成

### Q6: 如何清理并重新编译？

**A**: 删除对应的目录即可：

```bash
# 清理 SDL2
rmdir /s /q sdl2-build sdl2-install

# 清理 SoundTouch
rmdir /s /q soundtouch-build soundtouch-install

# 清理 FFmpeg
rmdir /s /q ffmpeg-build ffmpeg-install

# 清理所有（保留源码）
rmdir /s /q sdl2-build sdl2-install soundtouch-build soundtouch-install ffmpeg-build ffmpeg-install

# 完全清理（包括源码）
rmdir /s /q sdl2-* soundtouch-* ffmpeg-*
```

## 📚 相关文档

- [README.md](README.md) - SoundTouch 旧版说明（已废弃）
- [MSVC_FIX.md](MSVC_FIX.md) - SoundTouch MSVC 兼容性修复
- [DEBUG_RELEASE_MISMATCH.md](DEBUG_RELEASE_MISMATCH.md) - Debug/Release 混用问题

## ✅ 验证编译结果

编译完成后，可以通过以下方式验证：

### 1. 检查目录结构

```bash
tree /F sdl2-install
tree /F soundtouch-install
tree /F ffmpeg-install
```

### 2. 检查 DLL 依赖

```bash
# 使用 dumpbin 查看 DLL 导出
dumpbin /exports ffmpeg-install\bin\avcodec-62.dll
```

### 3. 配置并编译项目

```bash
cd win-third
configure_project_multi.bat
cd ..\build\vs2022_release
msbuild HXCVodPlayer.sln /p:Configuration=Release
```

### 4. 运行程序

```bash
cd bin\Release
HXCVodPlayer.exe
```

## 🎉 总结

使用本地编译方案后：

✅ **不再需要 vcpkg**  
✅ **版本完全可控**  
✅ **新开发者上手简单**  
✅ **构建过程透明**  
✅ **支持离线环境**

如有问题，请参考各个 `.bat` 脚本中的注释说明。
