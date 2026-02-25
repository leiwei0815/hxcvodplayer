# macOS 项目移除 SDL 依赖总结

## 问题发现

用户发现 macOS 原生项目运行时日志显示：
```
[2026-02-25 16:04:32] [INFO] SDL 初始化成功
```

这说明虽然 macOS 项目使用了 AVFoundation + AudioQueue 进行原生渲染，但底层核心代码仍然初始化了 SDL。

## 问题分析

### 代码检查

在 `src/core/hxc_player_core.cpp` 中：

```cpp
#ifndef NO_SDL
    // 初始化 SDL（仅桌面平台）
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        LOG_ERROR("SDL初始化失败: ", SDL_GetError());
        std::cerr << "SDL初始化失败: " << SDL_GetError() << std::endl;
    } else {
        LOG_INFO("SDL 初始化成功");  // ← 这个日志出现了！
    }
#else
    LOG_INFO("iOS 平台，跳过 SDL 初始化");
#endif
```

### 原因

- iOS 项目在 Xcode 项目设置中定义了 `NO_SDL=1` 宏
- macOS 纯 Cocoa 项目的 `CMakeLists.txt` 中**没有**定义这个宏
- 因此 macOS 项目仍然会初始化 SDL，尽管实际上不使用

## 解决方案

### 修改 `src/macos/CMakeLists.txt`

#### 1. 添加 NO_SDL 宏定义

```cmake
# 编译选项
target_compile_options(HXCPlayer-macOS PRIVATE
    -Wno-deprecated-declarations
)

# 定义 NO_SDL 宏（macOS 原生项目不使用 SDL）
target_compile_definitions(HXCPlayer-macOS PRIVATE
    NO_SDL=1
)
```

#### 2. 移除 SDL2 相关配置

**移除 SDL2 路径定义：**
```cmake
# 删除这些行：
# set(SDL2_INCLUDE_DIR "${FFMPEG_ROOT}/include/SDL2")
# set(SDL2_LIB_DIR "${FFMPEG_ROOT}/lib")
```

**移除 SDL2 包含目录：**
```cmake
# 包含目录
include_directories(
    ${PROJECT_ROOT}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIR}
    ${SOUNDTOUCH_INCLUDE_DIR}
    # ${SDL2_INCLUDE_DIR}  # ← 删除
)
```

**移除 SDL2 链接目录：**
```cmake
# 链接目录
link_directories(
    ${FFMPEG_LIB_DIR}
    ${SOUNDTOUCH_LIB_DIR}
    # ${SDL2_LIB_DIR}  # ← 删除
)
```

**移除 SDL2 库链接：**
```cmake
# 链接 FFmpeg 库
target_link_libraries(HXCPlayer-macOS
    # FFmpeg
    avcodec
    avformat
    avutil
    swscale
    swresample
    
    # SoundTouch
    SoundTouch
    
    # SDL2  # ← 删除这一行
    
    # macOS 系统框架
    "-framework Foundation"
    # ...
)
```

## 验证

### 编译验证

```bash
cd src/macos
rm -rf build
mkdir -p build
cd build
cmake -G Xcode ..
xcodebuild -project HXCPlayer-macOS.xcodeproj -scheme HXCPlayer-macOS
```

✅ **编译成功**

### 链接验证

查看链接器输出，确认**没有** `-lSDL2`：

```bash
# 链接命令中只有：
-lavcodec -lavformat -lavutil -lswscale -lswresample -lSoundTouch
-framework Foundation -framework Cocoa -framework AVFoundation
# ...
# 没有 -lSDL2 ✅
```

### 运行验证

运行应用后，日志应该显示：

```
[2026-02-25] [INFO] iOS 平台，跳过 SDL 初始化
```

或者完全没有 SDL 相关日志。

## 对比：iOS vs macOS

| 平台 | NO_SDL 定义位置 | 结果 |
|-----|---------------|------|
| **iOS** | Xcode 项目设置 | ✅ 跳过 SDL 初始化 |
| **macOS (Qt)** | 未定义 | ❌ 初始化 SDL（正常，因为使用 SDL 渲染） |
| **macOS (Cocoa)** | CMakeLists.txt | ✅ 跳过 SDL 初始化（本次修复） |

## 技术要点

### 条件编译宏

在 C/C++ 代码中：
```cpp
#ifndef NO_SDL
    // 使用 SDL 的代码
#else
    // 不使用 SDL 的代码
#endif
```

### CMake 定义宏

```cmake
target_compile_definitions(target_name PRIVATE
    MACRO_NAME=value
)
```

这等同于编译时添加 `-DNO_SDL=1` 参数。

### 为什么要移除 SDL？

1. **减少依赖** - macOS 原生项目不需要 SDL
2. **更清晰** - 代码和配置更明确
3. **减小体积** - 不链接不必要的库
4. **避免混淆** - 不会在日志中看到 SDL 初始化信息

## 项目依赖对比

### macOS 纯 Cocoa 项目依赖

**需要的库：**
- FFmpeg (avcodec, avformat, avutil, swscale, swresample)
- SoundTouch
- macOS 系统框架 (AVFoundation, AudioToolbox, etc.)

**不需要的库：**
- ❌ SDL2（已移除）

### Qt 桌面项目依赖

**需要的库：**
- FFmpeg
- SoundTouch
- SDL2 ✅（用于音视频渲染）
- Qt5

### iOS 项目依赖

**需要的库：**
- FFmpeg
- SoundTouch
- iOS 系统框架

**不需要的库：**
- ❌ SDL2

## 修改文件

### 修改的文件

1. ✅ `src/macos/CMakeLists.txt`
   - 添加 `NO_SDL=1` 宏定义
   - 移除 SDL2 路径配置
   - 移除 SDL2 包含目录
   - 移除 SDL2 链接目录
   - 移除 SDL2 库链接

### 未修改的文件

- ❌ `src/core/hxc_player_core.cpp` - **保持原样**
  - 已经有 `#ifndef NO_SDL` 条件编译
  - 只需定义宏即可

## 最佳实践

### 平台特定宏定义

不同平台应根据实际使用的渲染方式定义宏：

```cmake
# iOS - Xcode 项目设置
Preprocessor Macros: NO_SDL=1

# macOS 原生 - CMakeLists.txt
target_compile_definitions(HXCPlayer-macOS PRIVATE NO_SDL=1)

# macOS Qt - 不定义（使用 SDL）
# 不添加 NO_SDL 宏定义
```

### 条件编译原则

> **使用宏控制平台特定功能，而不是修改共享代码**

这样可以：
- 保持核心代码统一
- 通过编译配置控制行为
- 避免平台差异导致的问题

## 总结

通过在 macOS 纯 Cocoa 项目中：
1. ✅ 定义 `NO_SDL=1` 宏
2. ✅ 移除 SDL2 相关配置和链接

实现了：
- ✅ 跳过 SDL 初始化
- ✅ 减少不必要的依赖
- ✅ 代码行为与 iOS 项目一致
- ✅ 日志输出更清晰

**核心原则：**
> macOS 原生项目使用 AVFoundation + AudioQueue 渲染，不需要 SDL

---

**修复日期**: 2026-02-25  
**状态**: ✅ 已完成并编译通过  
**修改文件**: 1 个（CMakeLists.txt）
