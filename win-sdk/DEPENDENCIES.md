# SDK 依赖管理说明

## 📦 SDK 依赖

HXCPlayer SDK 依赖以下库：

| 库 | 版本 | 用途 | 必需 |
|----|------|------|------|
| **SDL2** | 2.x | 音频输出 + 视频渲染 | ✅ 是 |
| **FFmpeg** | 6.x/5.x | 视频解码 | ✅ 是 |
| **SoundTouch** | 2.x | 变速播放 | ⚠️ 可选 |

## 🔧 自动依赖处理

### 构建时

运行 `build_sdk.bat` 时，脚本会自动：

1. ✅ 从 vcpkg 复制 **SDL2.dll**
2. ✅ 从 vcpkg 复制 **FFmpeg DLLs**（avcodec, avformat, avutil, swscale, swresample）
3. ✅ 从 `win-third` 复制 **SoundTouch.dll**（如果已编译）

### 打包到 SDK

所有依赖 DLL 会自动复制到：
```
HXCPlayerSDK/
└── bin/
    ├── hxcplayer.dll       ← 主 DLL
    ├── SDL2.dll            ← 音频/视频
    ├── avcodec-61.dll      ← FFmpeg
    ├── avformat-61.dll
    ├── avutil-59.dll
    ├── swscale-8.dll
    ├── swresample-5.dll
    └── SoundTouch.dll      ← 变速播放（可选）
```

## 📋 依赖复制脚本

### 主脚本：`copy_dependencies.bat`

**功能**：
- ✅ 自动检测 FFmpeg 版本（支持 58-61）
- ✅ 从 vcpkg 复制 SDL2 和 FFmpeg
- ✅ 从本地编译复制 SoundTouch
- ✅ Debug/Release 自动选择
- ✅ 友好的错误提示

**调用方式**：
```batch
copy_dependencies.bat <SDK_BIN_DIR> <BUILD_TYPE> <VCPKG_ROOT>

# 示例
copy_dependencies.bat "build\HXCPlayerSDK\bin" Release "C:\vcpkg"
```

**输出示例**：
```
========================================
复制依赖 DLL 到 SDK
========================================
SDK bin 目录: build\HXCPlayerSDK\bin
构建类型: Release
vcpkg 路径: C:\vcpkg

[1/3] 复制 SDL2...
  ✓ SDL2.dll

[2/3] 复制 FFmpeg...
  ✓ avcodec-61.dll
  ✓ avformat-61.dll
  ✓ avutil-59.dll
  ✓ swscale-8.dll
  ✓ swresample-5.dll

[3/3] 复制 SoundTouch (可选)...
  ✓ SoundTouch.dll (本地编译)

========================================
依赖 DLL 复制完成
========================================

已复制的 DLL:
avcodec-61.dll
avformat-61.dll
avutil-59.dll
hxcplayer.dll
SDL2.dll
SoundTouch.dll
swresample-5.dll
swscale-8.dll
```

## 🔍 依赖来源

### 1. SDL2 和 FFmpeg（来自 vcpkg）

**路径**：`C:\vcpkg\installed\x64-windows\bin\`

**安装**：
```bash
# 安装 FFmpeg
vcpkg install ffmpeg:x64-windows

# 安装 SDL2
vcpkg install sdl2:x64-windows
```

**版本检测**：
- FFmpeg：自动检测版本 58-61（支持不同 vcpkg 版本）
- SDL2：版本无关（DLL 名称固定）

### 2. SoundTouch（本地编译或 vcpkg）

**优先级**：
1. **本地编译**（推荐）：`win-third/soundtouch-install/{debug|release}/bin/`
2. **vcpkg**（备选）：`C:\vcpkg\installed\x64-windows\bin\`

**本地编译**：
```bash
cd win-third
.\install_all.bat
```

**vcpkg 安装**（可选）：
```bash
vcpkg install soundtouch:x64-windows
```

## ⚙️ CMake 集成

### package_sdk 目标

```cmake
add_custom_target(package_sdk
    # ... 其他复制操作 ...
    
    # 使用脚本复制依赖
    COMMAND cmd /c "${CMAKE_SOURCE_DIR}/win-sdk/copy_dependencies.bat" 
        "${CMAKE_BINARY_DIR}/HXCPlayerSDK/bin"
        "$<CONFIG>"
        "${VCPKG_ROOT}"
    
    DEPENDS hxcplayer_dll
)
```

**特点**：
- ✅ 自动在 `cmake --build . --target package_sdk` 时执行
- ✅ 根据 Debug/Release 配置选择对应的 DLL
- ✅ 自动检测 vcpkg 路径

## 📝 用户使用

### 开发者（构建 SDK）

运行构建脚本即可，无需手动处理依赖：

```bash
cd win-sdk
.\build_sdk.bat
```

所有依赖 DLL 自动打包到 `HXCPlayerSDK\bin\`。

### 最终用户（使用 SDK）

直接使用 SDK 包，无需安装任何依赖：

```bash
# 解压 SDK
unzip HXCPlayerSDK-v1.0.0.zip

# 所有依赖已包含
HXCPlayerSDK\bin\*.dll  ← 所有 DLL 都在这里
```

## 🐛 故障排查

### 问题 1：SDL2.dll 未复制

**原因**：vcpkg 未安装 SDL2

**解决**：
```bash
vcpkg install sdl2:x64-windows
```

### 问题 2：FFmpeg DLL 未复制

**原因**：vcpkg 未安装 FFmpeg

**解决**：
```bash
vcpkg install ffmpeg:x64-windows
```

### 问题 3：FFmpeg 版本不匹配

**原因**：脚本自动检测版本 58-61

**解决**：
- 检查 `C:\vcpkg\installed\x64-windows\bin\avcodec-*.dll`
- 如果版本号不在 58-61，修改 `copy_dependencies.bat` 第 45 行：
  ```batch
  for %%v in (61 60 59 58 57) do (  ← 添加你的版本号
  ```

### 问题 4：SoundTouch.dll 未复制

**原因**：SoundTouch 是可选依赖

**影响**：
- ⚠️ 无法使用变速播放功能
- ✅ 其他功能正常

**解决**（可选）：
```bash
# 方案 1：本地编译
cd win-third
.\install_all.bat

# 方案 2：vcpkg 安装
vcpkg install soundtouch:x64-windows

# 重新构建 SDK
cd win-sdk
.\build_sdk.bat
```

### 问题 5：vcpkg 路径不正确

**错误**：
```
vcpkg bin 目录不存在: C:\vcpkg\installed\x64-windows\bin
```

**解决**：
修改 `build_sdk.bat` 第 16 行：
```batch
set VCPKG_ROOT=D:\your\vcpkg  ← 修改为实际路径
```

或在命令行设置环境变量：
```bash
set VCPKG_ROOT=D:\your\vcpkg
.\build_sdk.bat
```

## ✅ 验证依赖

### 构建后验证

```bash
# 检查 SDK bin 目录
dir build\win-sdk-release\HXCPlayerSDK\bin

# 应该看到：
#   hxcplayer.dll
#   SDL2.dll
#   avcodec-*.dll
#   avformat-*.dll
#   avutil-*.dll
#   swscale-*.dll
#   swresample-*.dll
#   SoundTouch.dll (可选)
```

### 运行时验证

使用 [Dependency Walker](http://www.dependencywalker.com/) 检查：

```bash
# 下载 Dependency Walker
# 打开 hxcplayer.dll
# 检查所有依赖是否在同目录
```

或使用示例程序测试：

```bash
cd build\win-sdk-release\HXCPlayerSDK\example
mkdir build
cd build
cmake ..
cmake --build . --config Release

# 运行（所有 DLL 会自动复制）
.\Release\simple_player.exe test.mp4
```

## 📊 依赖大小

| DLL | 大小（约） | 说明 |
|-----|-----------|------|
| hxcplayer.dll | 500 KB | 主 DLL |
| SDL2.dll | 1.5 MB | 音视频基础库 |
| avcodec-61.dll | 8 MB | FFmpeg 解码器 |
| avformat-61.dll | 2 MB | FFmpeg 格式处理 |
| avutil-59.dll | 1 MB | FFmpeg 工具 |
| swscale-8.dll | 500 KB | FFmpeg 视频缩放 |
| swresample-5.dll | 200 KB | FFmpeg 音频重采样 |
| SoundTouch.dll | 300 KB | 变速播放 |
| **总计** | **~14 MB** | 完整 SDK |

## 🎯 最佳实践

### 1. 构建前准备

```bash
# 确保 vcpkg 已安装依赖
vcpkg install ffmpeg:x64-windows sdl2:x64-windows

# （可选）编译 SoundTouch
cd win-third
.\install_all.bat
```

### 2. 构建 SDK

```bash
cd win-sdk
.\build_sdk.bat
```

### 3. 验证输出

```bash
# 检查依赖
dir build\win-sdk-release\HXCPlayerSDK\bin\*.dll
```

### 4. 分发 SDK

```bash
# 打包整个目录
cd build\win-sdk-release
powershell Compress-Archive -Path HXCPlayerSDK -DestinationPath HXCPlayerSDK-v1.0.0.zip
```

用户解压后即可使用，无需安装任何依赖！

## 📞 获取帮助

- 构建问题：查看 `BUILD_GUIDE.md`
- 依赖问题：查看本文档
- 使用问题：查看 `SDK_USAGE.md`
