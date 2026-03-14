# HXCPlayer Windows SDK 使用指南

## 📦 SDK 内容

HXCPlayerSDK 包含以下内容：

```
HXCPlayerSDK/
├── include/                      # 头文件
│   ├── hxc_player_core_c_bridge.h   # 核心 C API
│   └── hxcplayer_sdk.h              # SDK 主头文件
├── lib/                          # 导入库
│   └── hxcplayer.lib                # 链接用的导入库
├── bin/                          # 运行时 DLL
│   ├── hxcplayer.dll                # 主 DLL
│   ├── SDL2.dll                     # SDL2 依赖
│   ├── avcodec-*.dll                # FFmpeg 依赖
│   ├── avformat-*.dll
│   ├── avutil-*.dll
│   ├── swscale-*.dll
│   └── swresample-*.dll
├── example/                      # 示例代码
│   ├── simple_player.c              # C 语言示例
│   └── CMakeLists.txt               # 示例构建脚本
├── docs/                         # 文档
│   └── SDK_USAGE.md                 # 详细使用文档
└── README.md                     # 快速开始
```

## 🚀 快速开始

### 1. 集成到你的项目

#### 方法 A：使用 CMake

在你的 `CMakeLists.txt` 中：

```cmake
cmake_minimum_required(VERSION 3.15)
project(MyPlayer)

# 设置 SDK 路径
set(HXCPLAYER_SDK_DIR "path/to/HXCPlayerSDK")

# 添加头文件目录
include_directories("${HXCPLAYER_SDK_DIR}/include")

# 添加库目录
link_directories("${HXCPLAYER_SDK_DIR}/lib")

# 创建你的可执行文件
add_executable(myplayer main.c)

# 链接 HXCPlayer
target_link_libraries(myplayer hxcplayer)

# Windows: 复制 DLL 到输出目录
if(WIN32)
    file(GLOB DLLS "${HXCPLAYER_SDK_DIR}/bin/*.dll")
    foreach(dll ${DLLS})
        add_custom_command(TARGET myplayer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${dll}"
                "$<TARGET_FILE_DIR:myplayer>"
        )
    endforeach()
endif()
```

#### 方法 B：使用 Visual Studio

1. **添加头文件目录**：
   - 项目属性 → C/C++ → 常规 → 附加包含目录
   - 添加：`HXCPlayerSDK\include`

2. **添加库目录**：
   - 项目属性 → 链接器 → 常规 → 附加库目录
   - 添加：`HXCPlayerSDK\lib`

3. **添加依赖库**：
   - 项目属性 → 链接器 → 输入 → 附加依赖项
   - 添加：`hxcplayer.lib`

4. **复制 DLL**：
   - 将 `HXCPlayerSDK\bin\*.dll` 复制到你的 exe 输出目录

### 2. 编写代码

#### C 语言示例

```c
#include <stdio.h>

// 定义 DLL 导入
#define HXCPLAYER_DLL_IMPORTS
#include "hxcplayer_sdk.h"

// 状态回调
void on_state_changed(PlayerStateC state, void* user_data) {
    printf("状态变化: %d\n", state);
}

// 错误回调
void on_error(int error_code, const char* error_msg, void* user_data) {
    printf("错误: [%d] %s\n", error_code, error_msg);
}

int main() {
    // 打印 SDK 版本
    printf("HXCPlayer SDK %s\n", hxcplayer_get_version());
    
    // 创建播放器
    PlayerCoreHandle* player = player_core_create();
    
    // 设置回调
    player_core_set_state_changed_callback(player, on_state_changed, NULL);
    player_core_set_error_callback(player, on_error, NULL);
    
    // 打开视频
    player_core_open(player, "video.mp4");
    
    // 播放
    player_core_play(player);
    
    // 等待用户输入（实际应用中应该有 UI 事件循环）
    getchar();
    
    // 清理
    player_core_stop(player);
    player_core_destroy(player);
    
    return 0;
}
```

#### C++ 语言示例

```cpp
#include <iostream>
#include <thread>
#include <chrono>

#define HXCPLAYER_DLL_IMPORTS
#include "hxcplayer_sdk.h"

int main() {
    // 创建播放器
    auto* player = player_core_create();
    
    // 设置回调（使用 lambda）
    player_core_set_state_changed_callback(
        player,
        [](PlayerStateC state, void* user_data) {
            std::cout << "状态: " << state << std::endl;
        },
        nullptr
    );
    
    // 打开并播放
    player_core_open(player, "video.mp4");
    player_core_play(player);
    
    // 播放 10 秒
    std::this_thread::sleep_for(std::chrono::seconds(10));
    
    // 清理
    player_core_stop(player);
    player_core_destroy(player);
    
    return 0;
}
```

### 3. 编译运行

#### 使用 CMake

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

#### 使用 Visual Studio

直接在 Visual Studio 中打开项目并编译。

**重要**：确保 `HXCPlayerSDK\bin\` 下的所有 DLL 都在你的 exe 同目录下！

## 📖 API 文档

### 核心 API

#### 创建/销毁

```c
PlayerCoreHandle* player_core_create(void);
void player_core_destroy(PlayerCoreHandle* handle);
```

#### 播放控制

```c
// 打开视频（支持本地文件和网络 URL）
int player_core_open(PlayerCoreHandle* handle, const char* url);

// 打开视频并指定起始位置
int player_core_open_with_start_position(PlayerCoreHandle* handle, 
                                         const char* url, 
                                         double start_pos);

// 播放/暂停/停止
void player_core_play(PlayerCoreHandle* handle);
void player_core_pause(PlayerCoreHandle* handle);
void player_core_stop(PlayerCoreHandle* handle);
```

#### 状态查询

```c
// 获取播放状态
PlayerStateC player_core_get_state(PlayerCoreHandle* handle);

// 获取时长和当前位置（秒）
double player_core_get_duration(PlayerCoreHandle* handle);
double player_core_get_position(PlayerCoreHandle* handle);
```

#### 控制

```c
// 跳转到指定位置（秒）
void player_core_seek(PlayerCoreHandle* handle, double pos);

// 设置音量（0-100）
void player_core_set_volume(PlayerCoreHandle* handle, float volume);

// 设置播放速度（0.5x - 2.0x）
void player_core_set_playback_rate(PlayerCoreHandle* handle, float rate);
float player_core_get_playback_rate(PlayerCoreHandle* handle);
```

#### 回调设置

```c
// 状态变化回调
void player_core_set_state_changed_callback(
    PlayerCoreHandle* handle,
    StateChangedCallbackC callback,
    void* user_data
);

// 错误回调
void player_core_set_error_callback(
    PlayerCoreHandle* handle,
    ErrorCallbackC callback,
    void* user_data
);

// 播放进度回调
void player_core_set_position_changed_callback(
    PlayerCoreHandle* handle,
    PositionChangedCallbackC callback,
    void* user_data
);

// 播放完成回调
void player_core_set_playback_completed_callback(
    PlayerCoreHandle* handle,
    PlaybackCompletedCallbackC callback,
    void* user_data
);
```

### 日志配置

```c
// 设置日志级别（0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR）
void player_core_set_log_level(int level);

// 启用文件日志
void player_core_enable_file_logging(const char* log_dir, const char* prefix);

// 禁用文件日志
void player_core_disable_file_logging(void);
```

## ⚠️ 注意事项

1. **DLL 依赖**：
   - 必须将 `bin/` 下所有 DLL 复制到 exe 目录
   - 或将 `bin/` 目录添加到系统 PATH

2. **线程安全**：
   - 回调函数在播放器线程中调用
   - UI 更新需要转发到主线程

3. **内存管理**：
   - `player_core_create()` 创建的对象必须调用 `player_core_destroy()` 销毁
   - SDK 自动管理内部资源

4. **支持的格式**：
   - 视频：MP4, AVI, MKV, FLV, MOV 等
   - 音频：MP3, AAC, WAV, FLAC 等
   - 网络流：HTTP, HTTPS, RTMP, RTSP, HLS 等

## 🐛 故障排查

### DLL 找不到

**错误**：`找不到 hxcplayer.dll` 或 `找不到 SDL2.dll`

**解决**：
1. 确保所有 DLL 都在 exe 同目录
2. 使用 [Dependency Walker](http://www.dependencywalker.com/) 检查依赖

### 运行时崩溃

**原因**：可能是 Debug/Release 版本混用

**解决**：
- 使用 Release SDK 的项目必须用 Release 模式编译
- 使用 Debug SDK 的项目必须用 Debug 模式编译

### 视频打不开

**检查**：
1. 文件路径是否正确（使用绝对路径或正确的相对路径）
2. 视频格式是否支持
3. 网络 URL 是否可访问
4. 查看错误回调的错误信息

## 📞 技术支持

- GitHub Issues: [项目地址]
- Email: [联系邮箱]
- 文档: 查看 `docs/SDK_USAGE.md` 获取更多详细信息

## 📄 许可证

本 SDK 遵循 [许可证类型] 许可证。
