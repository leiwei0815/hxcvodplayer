# SoundTouch 在 Qt/MSVC 环境中的使用方案

## 问题分析

你的开发环境：
- Qt Creator / Visual Studio
- Qt 5.15.2 (msvc2019_64 版本) ← 使用 MSVC 编译
- MSVC v143 (Visual Studio 2022)
- FFmpeg、SDL2 等库都是 MSVC 编译的

## ❌ 为什么不能用 MinGW？

### MinGW 与 MSVC 的不兼容性

| 特性 | MinGW (GCC) | MSVC |
|------|-------------|------|
| 编译器 | GCC | Microsoft C++ |
| C++ 名称修饰 | GCC 规则 | MSVC 规则 |
| 异常处理 | Dwarf/SEH | SEH |
| 标准库 | libstdc++ | MSVCRT |
| ABI | GCC ABI | MSVC ABI |

**结论**: MinGW 编译的库**无法**链接到 MSVC 项目中！

### 你的 Qt 版本

你安装的是 `C:\Qt\5.15.2\msvc2019_64`，这意味着：
- Qt 本身是用 MSVC 2019 编译的
- 你的项目也必须用 MSVC 编译
- 所有依赖库也必须是 MSVC 版本

如果要用 MinGW，需要：
1. 重新安装 Qt 的 MinGW 版本
2. 重新编译所有依赖（FFmpeg、SDL2 等）
3. 使用 MinGW 编译整个项目

**这不现实！**

---

## ✅ 推荐方案

### 方案 1: 修复当前的 MSVC 编译（推荐）

当前错误的具体信息是什么？请提供完整的错误信息，我来帮你解决。

常见问题：
1. 头文件宏定义问题 → 已修复
2. 链接错误 → 需要调整 CMake 配置
3. 其他编译错误 → 需要看具体信息

### 方案 2: 使用 vcpkg 的 SoundTouch（最简单）

回到 vcpkg 方案，但正确安装 ATL/MFC：

```cmd
# 1. 打开 Visual Studio Installer
# 2. 修改 VS 2022
# 3. 单个组件 → 搜索 "ATL"
# 4. 勾选:
#    ✓ C++ ATL for latest v143 build tools (x86 & x64)
#    ✓ C++ MFC for latest v143 build tools (x86 & x64)
# 5. 点击修改

# 安装完成后
cd C:\vcpkg
vcpkg install soundtouch:x64-windows

# 重新生成项目
cd d:\git\hxcvodplayer
rmdir /s /q build\vs2022_release
.\build_windows.bat vs2022 release
```

### 方案 3: 暂时不使用 SoundTouch

项目可以在没有 SoundTouch 的情况下完美运行，只是没有变速播放功能：

```cmd
# 重新生成项目（不使用 SoundTouch）
cd d:\git\hxcvodplayer
rmdir /s /q build\vs2022_release

# 不指定 SoundTouch 路径
.\build_windows.bat vs2022 release
```

功能对比：
- ✅ 视频播放
- ✅ 音频播放
- ✅ 所有控制功能
- ❌ 变速播放（0.5x-2.0x）

---

## 🔍 当前错误诊断

请提供完整的错误信息，包括：
1. 错误代码（如 C2001, LNK2019 等）
2. 具体的错误描述
3. 哪个文件报错

我可以针对性地解决！

---

## 💡 Qt 开发建议

### 如果主要用 Qt Creator 开发

你可以：
1. 继续用 MSVC 工具链（推荐）
   - 兼容性最好
   - 性能优秀
   - 调试工具完善

2. 或者完全切换到 MinGW（不推荐）
   - 需要重新安装所有内容
   - 工作量巨大
   - 调试体验较差

### MSVC + Qt Creator

Qt Creator 完全支持 MSVC 工具链：
1. Tools → Options → Kits
2. 选择 MSVC 编译器
3. 选择 Qt msvc2019_64 版本
4. 配置完成后可以正常开发

---

## 📝 下一步

请告诉我：
1. 当前完整的错误信息
2. 你倾向于哪个方案
3. 是否需要变速播放功能

我会帮你解决！
