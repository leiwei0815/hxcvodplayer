# SoundTouch MSVC 编译错误修复

## 问题现象

编译时出现错误：
```
STTypes.h(164,44): error C1017: 无效的整数常量表达式
STTypes.h(164,77): error C1017: 无效的整数常量表达式
```

## 根本原因

SoundTouch 的头文件 `STTypes.h` 在 `#if` 预处理指令中直接使用宏名称，而没有先检查宏是否已定义。

### 问题代码（第 164-165 行）

```cpp
#if ((SOUNDTOUCH_ALLOW_SSE) || (__SSE__) || (SOUNDTOUCH_USE_NEON))
    #if SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION
        #define ST_SIMD_AVOID_UNALIGNED
    #endif
#endif
```

### 为什么会报错？

在 C/C++ 预处理器中：
- GCC/Clang：未定义的宏在 `#if` 中被视为 `0`
- **MSVC**：未定义的宏在 `#if` 中报错 `C1017: 无效的整数常量表达式`

当这些宏未定义时：
- `SOUNDTOUCH_ALLOW_SSE` → MSVC 报错（第 44 列）
- `SOUNDTOUCH_USE_NEON` → MSVC 报错（第 77 列）
- `SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION` → MSVC 报错

## 解决方案

### 修复后的代码

```cpp
#if ((defined(SOUNDTOUCH_ALLOW_SSE) && SOUNDTOUCH_ALLOW_SSE) || (__SSE__) || (defined(SOUNDTOUCH_USE_NEON) && SOUNDTOUCH_USE_NEON))
    #if defined(SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION) && SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION
        #define ST_SIMD_AVOID_UNALIGNED
    #endif
#endif
```

**关键改动**：
1. `SOUNDTOUCH_ALLOW_SSE` → `defined(SOUNDTOUCH_ALLOW_SSE) && SOUNDTOUCH_ALLOW_SSE`
2. `SOUNDTOUCH_USE_NEON` → `defined(SOUNDTOUCH_USE_NEON) && SOUNDTOUCH_USE_NEON`
3. `#if SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION` → `#if defined(...) && ...`

这样即使宏未定义，`defined()` 也会返回 `false`，不会触发 MSVC 错误。

## 自动修复方法

### 方法 1：使用修复脚本（推荐）

```cmd
cd win-third
.\fix_soundtouch_headers_multi.bat
```

这会同时修复 Debug 和 Release 版本的头文件。

### 方法 2：重新编译并配置

```cmd
cd win-third
.\build_soundtouch_multi.bat    # 编译时会自动调用修复脚本
.\configure_project_multi.bat   # 配置时也会自动调用修复脚本
```

### 方法 3：一键安装

```cmd
cd win-third
.\install_all.bat   # 全自动，包含修复
```

## 手动修复方法

如果脚本失败，可以手动编辑：

### Debug 版本
```
win-third\soundtouch-install\debug\include\soundtouch\STTypes.h
```

### Release 版本
```
win-third\soundtouch-install\release\include\soundtouch\STTypes.h
```

**定位到第 164-168 行**，将：
```cpp
#if ((SOUNDTOUCH_ALLOW_SSE) || (__SSE__) || (SOUNDTOUCH_USE_NEON))
    #if SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION
        #define ST_SIMD_AVOID_UNALIGNED
    #endif
#endif
```

**替换为**：
```cpp
#if ((defined(SOUNDTOUCH_ALLOW_SSE) && SOUNDTOUCH_ALLOW_SSE) || (__SSE__) || (defined(SOUNDTOUCH_USE_NEON) && SOUNDTOUCH_USE_NEON))
    #if defined(SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION) && SOUNDTOUCH_ALLOW_NONEXACT_SIMD_OPTIMIZATION
        #define ST_SIMD_AVOID_UNALIGNED
    #endif
#endif
```

## 验证修复

修复后，在 Visual Studio 中：

1. **清理解决方案**
   - 菜单：生成 → 清理解决方案

2. **重新生成解决方案**
   - 按 `Ctrl + Shift + B`
   - 或菜单：生成 → 重新生成解决方案

3. **检查输出**
   - 应该没有 `error C1017` 错误
   - 可能还有其他警告（如 `-g` 选项被忽略），这些是正常的

## 技术细节

### 为什么 `__SSE__` 不需要修复？

注意修复后的代码中，`__SSE__` 仍然是：
```cpp
|| (__SSE__)
```

而不是：
```cpp
|| (defined(__SSE__) && __SSE__)
```

**原因**：
- `__SSE__` 是编译器内置宏（built-in macro）
- MSVC 在 x64 模式下会自动定义 `__SSE__` 为 `1`
- 即使未定义，MSVC 也不会对内置宏报错

### Debug 和 Release 都需要修复吗？

**是的！**

Debug 和 Release 是两个完全独立编译的库，使用不同的头文件副本：
- `soundtouch-install\debug\include\soundtouch\STTypes.h`
- `soundtouch-install\release\include\soundtouch\STTypes.h`

必须分别修复，否则会出现：
- Debug 项目编译失败
- Release 项目编译失败

## 常见问题

### Q: 为什么脚本修复后还是报错？

A: 可能的原因：
1. Visual Studio 缓存了旧的头文件 → 清理解决方案
2. 修复的是 Release 版本，但编译的是 Debug 项目 → 检查文件路径
3. 头文件权限问题 → 以管理员身份运行

### Q: 每次重新编译 SoundTouch 都要修复吗？

A: 是的。每次运行 `build_soundtouch_multi.bat` 都会重新生成头文件，需要再次修复。但脚本已经集成了自动修复，无需手动操作。

### Q: 其他平台需要这个修复吗？

A: **不需要**。这是 MSVC 特有的问题：
- macOS/Linux（GCC/Clang）：未定义宏自动视为 `0`，不需要修复
- MinGW（Windows 上的 GCC）：同样不需要

---

**注意**：此问题已在 `build_soundtouch_multi.bat` 和 `configure_project_multi.bat` 中自动处理，通常无需手动修复。
