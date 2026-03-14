# HXCPlayer Windows SDK

> 跨平台视频播放器 SDK - Windows DLL 版本

## 🚀 快速开始

### 1. 下载 SDK

从 [Releases](releases) 下载最新的 `HXCPlayerSDK-vX.X.X-win64.zip`

### 2. 解压并查看内容

```
HXCPlayerSDK/
├── include/       ← 头文件
├── lib/           ← 导入库 (.lib)
├── bin/           ← DLL 文件
├── example/       ← 示例代码
└── README.md      ← 详细文档
```

### 3. 5 分钟示例

#### 自动渲染模式（推荐用于桌面应用）

```c
#define HXCPLAYER_DLL_IMPORTS
#include "hxcplayer_sdk.h"

// Windows/MFC
void InitPlayer(HWND hwndVideo) {
    PlayerCoreHandle* player = player_core_create();
    
    // 设置渲染窗口
    player_core_set_render_window(player, (void*)hwndVideo);
    
    // 开始播放
    player_core_open(player, "video.mp4");
    player_core_play(player);
    
    // 在定时器中刷新视频 (60 FPS)
    SetTimer(hwnd, 1, 16, NULL);
}

void OnTimer(UINT_PTR id) {
    if (id == 1) {
        player_core_refresh_video(player);  // 自动渲染！
    }
}
```

#### 手动渲染模式（用于游戏引擎/自定义渲染）

```c
#define HXCPLAYER_DLL_IMPORTS
#include "hxcplayer_sdk.h"

int main() {
    PlayerCoreHandle* player = player_core_create();
    
    // 设置为手动渲染
    player_core_set_render_mode(player, RENDER_MODE_MANUAL);
    
    // 播放
    player_core_open(player, "video.mp4");
    player_core_play(player);
    
    // 游戏主循环
    while (running) {
        VideoFrameDataC frame;
        if (player_core_get_video_frame(player, &frame) == 0) {
            // 渲染到你的纹理/屏幕
            RenderYUVFrame(&frame);
            player_core_consume_video_frame(player);
        }
    }
    
    player_core_destroy(player);
    return 0;
}
```

**编译**：
```bash
cl /I"SDK\include" main.c /link SDK\lib\hxcplayer.lib
```

**运行**（确保 DLL 在同目录）：
```bash
copy SDK\bin\*.dll .
main.exe
```

## 📖 文档

- [SDK_README.md](SDK_README.md) - 完整使用指南
- [AUTO_RENDERING_GUIDE.md](AUTO_RENDERING_GUIDE.md) - **自动渲染详细教程** ⭐
- [BUILD_GUIDE.md](BUILD_GUIDE.md) - 构建 SDK（开发者）
- [docs/SDK_USAGE.md](docs/SDK_USAGE.md) - 详细 API 文档
- [RENDERING_MODES.md](RENDERING_MODES.md) - 渲染模式对比
- [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) - 实现总结

## ✨ 特性

- ✅ 纯 C API（兼容 C/C++/C#/Python 等）
- ✅ **自动渲染模式**：极简易用，适合 MFC/Qt 桌面应用
- ✅ **手动渲染模式**：完全控制，适合游戏引擎/自定义渲染
- ✅ 支持多种格式（MP4, AVI, MKV, FLV, MOV...）
- ✅ 支持网络流（HTTP, RTMP, RTSP, HLS...）
- ✅ 硬件加速解码
- ✅ 音量/速度控制
- ✅ Seek 支持
- ✅ 异步回调
- ✅ 详细日志

## 🔧 构建 SDK（开发者）

### 前置要求

- Visual Studio 2019/2022
- CMake 3.15+
- vcpkg
- Qt 5.15.2（可选）

### 构建命令

```bash
cd win-sdk
.\build_sdk.bat
```

详见 [BUILD_GUIDE.md](BUILD_GUIDE.md)

## 📦 集成到项目

### CMake

```cmake
set(HXCPLAYER_SDK_DIR "path/to/SDK")
include_directories("${HXCPLAYER_SDK_DIR}/include")
link_directories("${HXCPLAYER_SDK_DIR}/lib")
target_link_libraries(myapp hxcplayer)
```

### Visual Studio

1. **属性 → C/C++ → 常规 → 附加包含目录**  
   添加：`SDK\include`

2. **属性 → 链接器 → 常规 → 附加库目录**  
   添加：`SDK\lib`

3. **属性 → 链接器 → 输入 → 附加依赖项**  
   添加：`hxcplayer.lib`

4. **复制 DLL**  
   将 `SDK\bin\*.dll` 复制到 exe 目录

## 🎯 示例

### C 语言

```c
#define HXCPLAYER_DLL_IMPORTS
#include "hxcplayer_sdk.h"

void on_state_changed(PlayerStateC state, void* user_data) {
    printf("状态: %d\n", state);
}

int main() {
    PlayerCoreHandle* player = player_core_create();
    
    player_core_set_state_changed_callback(player, on_state_changed, NULL);
    player_core_open(player, "video.mp4");
    player_core_play(player);
    
    // ...
    
    player_core_destroy(player);
}
```

### C++

```cpp
#define HXCPLAYER_DLL_IMPORTS
#include "hxcplayer_sdk.h"

int main() {
    auto* player = player_core_create();
    
    player_core_set_state_changed_callback(
        player,
        [](PlayerStateC state, void*) {
            std::cout << "状态: " << state << std::endl;
        },
        nullptr
    );
    
    player_core_open(player, "video.mp4");
    player_core_play(player);
    
    // ...
    
    player_core_destroy(player);
}
```

更多示例见 `example/simple_player.c`

## 🌐 跨平台

相同的 API 适用于：

- ✅ Windows (DLL)
- ✅ macOS (Framework)
- ✅ Linux (.so)
- ✅ iOS (Framework)
- ✅ Android (.so)

代码可直接移植！

## 📄 许可证

[LICENSE](../LICENSE)

## 🤝 贡献

欢迎提交 Issues 和 Pull Requests！

## 📧 联系

- Issues: [GitHub Issues](issues)
- Email: [support@example.com]

---

**Happy Coding! 🎬**
