# Windows 第三方库编译

本目录用于编译 Windows 平台的第三方依赖库。

## ⚠️ 重要提示：Debug 和 Release 版本

**必须分别编译 Debug 和 Release 版本的 SoundTouch！**

原因：
- Debug 版本使用 `/MDd` 运行时库（`_ITERATOR_DEBUG_LEVEL=2`）
- Release 版本使用 `/MD` 运行时库（`_ITERATOR_DEBUG_LEVEL=0`）
- 混用会导致链接错误：`LNK2038: 检测到"RuntimeLibrary"的不匹配项`

## 目录结构

```
win-third/
├── build_soundtouch_multi.bat      # SoundTouch Debug+Release 编译脚本（推荐）
├── configure_project_multi.bat     # 配置 Debug+Release 项目（推荐）
├── install_all.bat                 # 一键完成所有步骤（推荐）
├── soundtouch-src/                 # SoundTouch 源码（自动下载）
├── soundtouch-build/               # 构建目录（临时）
│   ├── debug/                      # Debug 构建
│   └── release/                    # Release 构建
└── soundtouch-install/             # 安装目录（最终输出）
    ├── debug/                      # Debug 版本
    │   ├── include/                # 头文件
    │   └── lib/                    # SoundTouch.lib (Debug)
    └── release/                    # Release 版本
        ├── include/                # 头文件
        └── lib/                    # SoundTouch.lib (Release)
```

## 快速开始（推荐）

### 一键安装

```cmd
cd win-third
.\install_all.bat
```

这会自动：
1. 下载 SoundTouch 源码
2. 编译 Debug 和 Release 版本
3. 配置两个独立的 Visual Studio 项目
4. 询问打开哪个项目

**预计时间**：10-15 分钟

## 手动分步操作

如果你想手动控制每个步骤：

### 步骤 1: 下载源码

```cmd
.\download_soundtouch.bat
```

### 步骤 2: 编译 Debug + Release

```cmd
.\build_soundtouch_multi.bat
```

脚本会：
1. 配置并编译 Debug 版本
2. 配置并编译 Release 版本
3. 分别安装到 `soundtouch-install/debug` 和 `soundtouch-install/release`

### 步骤 3: 配置项目

```cmd
.\configure_project_multi.bat
```

这会生成两个独立的 Visual Studio 项目：
- `build\vs2022_debug\HXCVodPlayer.sln` - 使用 Debug 版 SoundTouch
- `build\vs2022_release\HXCVodPlayer.sln` - 使用 Release 版 SoundTouch

### 步骤 4: 编译运行

在 Visual Studio 中：
- Debug 项目使用 Debug 配置编译
- Release 项目使用 Release 配置编译

## 验证安装

编译成功后，检查文件：

### Debug 版本
```cmd
dir soundtouch-install\debug\include\soundtouch\
dir soundtouch-install\debug\lib\
```

### Release 版本
```cmd
dir soundtouch-install\release\include\soundtouch\
dir soundtouch-install\release\lib\
```

都应该看到：
- 头文件: `SoundTouch.h`, `STTypes.h` 等
- 库文件: `SoundTouch.lib`

## 手动编译（高级）

如果脚本失败，可以手动操作：

### 使用 CMake

```cmd
# 1. 下载源码
git clone https://github.com/soundtouch/soundtouch.git soundtouch-src

# 2. 创建构建目录
mkdir soundtouch-build
cd soundtouch-build

# 3. 配置
cmake ..\soundtouch-src ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX=..\soundtouch-install ^
    -DBUILD_SHARED_LIBS=OFF

# 4. 编译
cmake --build . --config Release --parallel

# 5. 安装
cmake --install . --config Release
```

### 使用 Visual Studio 项目文件

SoundTouch 源码中包含 Visual Studio 解决方案：

```
soundtouch-src/SoundTouch.sln
```

可以直接在 Visual Studio 中打开并编译。

## 故障排除

### 问题 1: Debug 模式链接错误

**错误信息**:
```
error LNK2038: 检测到"_ITERATOR_DEBUG_LEVEL"的不匹配项: 值"0"不匹配值"2"
error LNK2038: 检测到"RuntimeLibrary"的不匹配项: 值"MD_DynamicRelease"不匹配值"MDd_DynamicDebug"
```

**原因**: 使用了 Release 版本的 SoundTouch 链接 Debug 项目

**解决**: 使用 `build_soundtouch_multi.bat` 和 `configure_project_multi.bat` 分别编译和配置 Debug/Release

### 问题 2: Git 克隆失败

**原因**: 网络问题或代理设置

**解决**: 运行 `download_soundtouch.bat`，它提供多种下载方式

### 问题 3: CMake 配置失败

**检查**:
- CMake 版本是否 >= 3.15
- Visual Studio 2022 是否正确安装
- 是否有足够的磁盘空间

### 问题 4: 编译错误

**常见原因**:
- 源码不完整 → 重新下载
- 编译器版本不匹配 → 使用 VS 2022
- 头文件错误 → `fix_soundtouch_headers_multi.bat` 会自动修复

### 问题 5: 项目配置后仍找不到 SoundTouch

**检查 CMake 输出**:
```
-- ✓ SoundTouch found!
--   Include: D:/git/hxcvodplayer/win-third/soundtouch-install/debug/include
--   Library: D:/git/hxcvodplayer/win-third/soundtouch-install/debug/lib/SoundTouch.lib
```

如果未找到，确保已运行 `build_soundtouch_multi.bat`

## 技术说明

### 为什么需要 Debug 和 Release 分开编译？

**MSVC 运行时库差异**：
- Debug 使用 `/MDd`（Multi-threaded Debug DLL）
- Release 使用 `/MD`（Multi-threaded DLL）

这两个版本的 STL 容器（如 `vector`, `string`）和迭代器实现不同：
- Debug 版本有额外的调试检查（`_ITERATOR_DEBUG_LEVEL=2`）
- Release 版本优化性能（`_ITERATOR_DEBUG_LEVEL=0`）

**混用后果**：
```
error LNK2038: 检测到"RuntimeLibrary"的不匹配项
LINK : warning LNK4098: 默认库"MSVCRT"与其他库的使用冲突
```

### 为什么不需要 ATL/MFC？

SoundTouch 核心库是纯 C++，不依赖 Windows 特定的 API。
vcpkg 版本需要 ATL/MFC 是因为它的构建脚本有这个依赖，
但直接从源码编译不需要。

### 静态链接 vs 动态链接

脚本默认编译**静态库**（.lib），这样：
- ✅ 部署简单，无需额外 DLL
- ✅ 避免版本冲突
- ❌ 可执行文件稍大

如果需要动态库，修改 CMake 参数：
```cmd
-DBUILD_SHARED_LIBS=ON
```

### SoundTouch 版本

脚本下载最新的 master 分支。
如需特定版本，在源码目录：
```cmd
cd soundtouch-src
git checkout 2.3.1  # 指定版本
```

## 相关文档

- SoundTouch 官网: https://www.surina.net/soundtouch/
- GitHub 仓库: https://github.com/soundtouch/soundtouch
- 项目主文档: ../README_WINDOWS.md

---

最后更新: 2026-02-26
