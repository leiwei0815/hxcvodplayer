# 🚀 Windows 本地编译快速开始

## ⚡ 5 分钟上手

### 第 1 步：安装必需工具

1. **CMake** (3.15+)
   - 下载: https://cmake.org/download/
   - 安装时选择 "Add CMake to system PATH"

2. **Visual Studio 2022**
   - 下载: https://visualstudio.microsoft.com/
   - 安装组件: "使用 C++ 的桌面开发"

3. **MSYS2** (仅 FFmpeg 需要)
   - 下载: https://www.msys2.org/
   - 安装后在 MSYS2 终端执行:
     ```bash
     pacman -S base-devel mingw-w64-x86_64-toolchain
     pacman -S mingw-w64-x86_64-nasm yasm
     ```
   - 添加到 PATH: `C:\msys64\mingw64\bin`

### 第 2 步：一键编译所有库

打开 PowerShell 或 CMD，执行：

```bash
cd d:\git\hxcvodplayer\win-third
build_all.bat
```

按 `Y` 确认，然后等待编译完成（约 30-60 分钟）。

### 第 3 步：构建项目

编译完成后，打开生成的 Visual Studio 解决方案：

```bash
cd ..\build\vs2022_release
start HXCVodPlayer.sln
```

或者使用命令行编译：

```bash
cd ..\build\vs2022_release
msbuild HXCVodPlayer.sln /p:Configuration=Release
```

### 第 4 步：运行程序

```bash
cd bin\Release
.\HXCVodPlayer.exe
```

## 🎯 仅编译 SDK

如果只需要构建 Windows SDK：

```bash
cd d:\git\hxcvodplayer\win-third
build_all.bat  # 编译所有依赖库

cd ..\win-sdk
build_sdk.bat   # 构建 SDK
```

SDK 输出位置: `build\win-sdk-Release\HXCPlayerSDK\`

## 📦 目录说明

编译后的目录结构：

```
hxcvodplayer/
├── win-third/
│   ├── sdl2-install/        ← SDL2 库
│   ├── soundtouch-install/  ← SoundTouch 库
│   ├── ffmpeg-install/      ← FFmpeg 库
│   ├── build_all.bat        ← 一键编译脚本
│   └── LOCAL_BUILD_GUIDE.md ← 详细文档
│
├── build/
│   ├── vs2022_debug/        ← Debug 构建
│   ├── vs2022_release/      ← Release 构建
│   └── win-sdk-Release/     ← SDK 构建
│       └── HXCPlayerSDK/    ← SDK 输出目录
│
└── win-sdk/
    └── build_sdk.bat        ← SDK 构建脚本
```

## ⚠️ 常见问题速查

### FFmpeg 编译失败

**错误**: `找不到 bash`

**解决**: 安装 MSYS2 并添加到 PATH

```bash
# 检查
where bash

# 应输出: C:\msys64\mingw64\bin\bash.exe
```

### CMake 找不到库

**错误**: `SDL2 not found` 或 `FFmpeg not found`

**解决**: 确保已运行 `build_all.bat` 并成功编译

```bash
# 检查目录是否存在
dir win-third\sdl2-install
dir win-third\ffmpeg-install
```

### 编译 SDK 时 DLL 未复制

**原因**: `copy_dependencies.bat` 优先使用本地编译版本

**检查**:
```bash
dir win-third\sdl2-install\release\bin\SDL2.dll
dir win-third\ffmpeg-install\bin\av*.dll
```

如果文件不存在，重新运行 `build_all.bat`。

## 🔄 更新库版本

如需更新到新版本：

1. 编辑对应的 `build_xxx.bat` 脚本
2. 修改版本号变量（如 `FFMPEG_VERSION`）
3. 清理旧版本: `rmdir /s /q xxx-*`
4. 重新运行编译脚本

## 📚 详细文档

- **[LOCAL_BUILD_GUIDE.md](LOCAL_BUILD_GUIDE.md)** - 完整编译指南
- **[MSVC_FIX.md](MSVC_FIX.md)** - SoundTouch MSVC 兼容性修复
- **[../win-sdk/README.md](../win-sdk/README.md)** - SDK 使用文档

## ✅ 验证安装

运行以下命令验证所有库都已正确编译：

```bash
cd win-third

# 检查 SDL2
dir sdl2-install\release\bin\SDL2.dll

# 检查 SoundTouch
dir soundtouch-install\release\lib\SoundTouch.lib

# 检查 FFmpeg
dir ffmpeg-install\bin\avcodec-*.dll
dir ffmpeg-install\bin\avformat-*.dll
dir ffmpeg-install\bin\avutil-*.dll
```

所有文件都应该存在。

## 🎉 完成！

现在你已经有了完全本地化的 Windows 构建环境，无需依赖 vcpkg 或其他外部包管理器。

开始开发吧！🚀
