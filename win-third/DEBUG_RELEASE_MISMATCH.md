# Debug/Release 库混用问题说明

## 问题现象

在 Visual Studio 中编译 Debug 版本时出现大量链接错误：

```
error LNK2038: 检测到"_ITERATOR_DEBUG_LEVEL"的不匹配项: 值"0"不匹配值"2"
error LNK2038: 检测到"RuntimeLibrary"的不匹配项: 值"MD_DynamicRelease"不匹配值"MDd_DynamicDebug"
LINK : warning LNK4098: 默认库"MSVCRT"与其他库的使用冲突；请使用 /NODEFAULTLIB:library
```

## 根本原因

这是 **MSVC 运行时库不匹配** 导致的典型问题。

### MSVC 运行时库类型

| 配置    | 运行时库标志 | 库文件       | `_ITERATOR_DEBUG_LEVEL` |
|---------|-------------|-------------|-------------------------|
| Debug   | `/MDd`      | msvcrtd.lib | 2                       |
| Release | `/MD`       | msvcrt.lib  | 0                       |

### 核心问题

当你：
1. 用 **Release** 模式编译了 SoundTouch（使用 `/MD`）
2. 然后用 **Debug** 模式编译你的项目（使用 `/MDd`）

链接器发现：
- SoundTouch 的 `.lib` 期待链接 `msvcrt.lib`（Release 运行时）
- 你的项目期待链接 `msvcrtd.lib`（Debug 运行时）
- 这两个运行时库 **互不兼容**

### 为什么不兼容？

Debug 和 Release 运行时库的内部实现不同：

**Debug 版本（`/MDd`）**：
- STL 容器有额外的边界检查
- 迭代器有调试信息追踪
- 内存分配使用调试堆
- 性能较慢但能检测更多错误

**Release 版本（`/MD`）**：
- 没有额外的调试检查
- 优化的内存布局
- 更快的性能
- 无法检测某些内存问题

这些差异导致：
```cpp
// SoundTouch (Release) 编译的代码
std::vector<float> buffer;  // 使用 Release 版 STL

// 你的项目 (Debug) 期待
std::vector<float> buffer;  // 使用 Debug 版 STL

// 内存布局、函数调用约定完全不同！
```

## 解决方案

### ✅ 方案 1：分别编译 Debug 和 Release（推荐）

使用提供的脚本：

```cmd
cd win-third
.\install_all.bat
```

这会：
1. 编译 SoundTouch Debug 版本（`/MDd`）
2. 编译 SoundTouch Release 版本（`/MD`）
3. 为你的项目生成两个独立的 VS 解决方案：
   - `build\vs2022_debug\HXCVodPlayer.sln` - 链接 Debug 版 SoundTouch
   - `build\vs2022_release\HXCVodPlayer.sln` - 链接 Release 版 SoundTouch

**使用时**：
- 开发调试 → 打开 `vs2022_debug` 项目，使用 Debug 配置
- 发布版本 → 打开 `vs2022_release` 项目，使用 Release 配置

### ❌ 错误方案：使用 `/NODEFAULTLIB`

有些人会尝试：
```
/NODEFAULTLIB:msvcrt.lib
```

**不要这样做**！这会：
- 隐藏链接错误，但运行时会崩溃
- 可能导致随机的内存损坏
- 难以调试的行为异常

## 手动编译步骤

如果你想手动操作，需要编译两次 SoundTouch：

### 编译 Debug 版本

```cmd
cd win-third\soundtouch-src
mkdir build-debug
cd build-debug

cmake .. ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Debug ^
    -DCMAKE_INSTALL_PREFIX=..\..\soundtouch-install\debug ^
    -DBUILD_SHARED_LIBS=OFF

cmake --build . --config Debug
cmake --install . --config Debug
```

### 编译 Release 版本

```cmd
cd win-third\soundtouch-src
mkdir build-release
cd build-release

cmake .. ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_INSTALL_PREFIX=..\..\soundtouch-install\release ^
    -DBUILD_SHARED_LIBS=OFF

cmake --build . --config Release
cmake --install . --config Release
```

### 配置项目

然后分别配置两个项目，使用不同的 SoundTouch 路径：

**Debug 项目**：
```cmd
cmake ../.. ^
    -DSOUNDTOUCH_INCLUDE_DIR="D:\git\hxcvodplayer\win-third\soundtouch-install\debug\include" ^
    -DSOUNDTOUCH_LIBRARY="D:\git\hxcvodplayer\win-third\soundtouch-install\debug\lib\SoundTouch.lib" ^
    ...
```

**Release 项目**：
```cmd
cmake ../.. ^
    -DSOUNDTOUCH_INCLUDE_DIR="D:\git\hxcvodplayer\win-third\soundtouch-install\release\include" ^
    -DSOUNDTOUCH_LIBRARY="D:\git\hxcvodplayer\win-third\soundtouch-install\release\lib\SoundTouch.lib" ^
    ...
```

## 其他平台

**这个问题只存在于 Windows + MSVC**。

- **macOS/Linux**: 使用动态运行时（`libc++`, `libstdc++`），没有 Debug/Release 区分
- **MinGW**: 使用 GCC 工具链，运行时不分 Debug/Release

## 参考

- [Microsoft Docs: C 运行时库](https://docs.microsoft.com/cpp/c-runtime-library/)
- [LNK2038 错误说明](https://docs.microsoft.com/cpp/error-messages/tool-errors/linker-tools-error-lnk2038)
- `win-third/README.md` - 自动化脚本使用指南

---

**总结**：永远不要混用 Debug 和 Release 编译的库，这是 Windows C++ 开发的基本规则。
