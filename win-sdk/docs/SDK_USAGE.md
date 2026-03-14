# HXCPlayer SDK 详细使用文档

## 目录

1. [SDK 架构](#sdk-架构)
2. [API 参考](#api-参考)
3. [高级用法](#高级用法)
4. [平台差异](#平台差异)
5. [性能优化](#性能优化)
6. [常见问题](#常见问题)

## SDK 架构

### 整体架构

```
┌─────────────────────────────────────┐
│      用户应用程序 (C/C++)            │
├─────────────────────────────────────┤
│   HXCPlayer SDK (hxcplayer.dll)     │
│   ├── C API 接口层                   │
│   ├── PlayerCore (C++)               │
│   ├── 解码器 (FFmpeg)                │
│   ├── 音频输出 (SDL2)                │
│   └── 视频渲染接口                   │
├─────────────────────────────────────┤
│      系统依赖 DLL                    │
│   ├── SDL2.dll                       │
│   ├── FFmpeg DLLs (avcodec等)       │
│   └── MSVC Runtime                   │
└─────────────────────────────────────┘
```

### 线程模型

SDK 使用多线程架构：

- **主线程**：用户应用程序的线程
- **解码线程**：视频/音频解码
- **音频线程**：SDL2 音频回调
- **回调线程**：状态/进度回调（在播放器线程）

⚠️ **重要**：所有回调函数都在播放器线程中执行，如果需要更新 UI，必须转发到主线程！

## API 参考

### 1. 初始化与清理

#### `hxcplayer_init()`

```c
int hxcplayer_init(void);
```

**说明**：初始化 SDK（可选，DLL 加载时自动初始化）

**返回值**：
- `0` - 成功
- `非0` - 失败

**示例**：
```c
if (hxcplayer_init() != 0) {
    fprintf(stderr, "SDK 初始化失败\n");
    return 1;
}
```

#### `hxcplayer_cleanup()`

```c
void hxcplayer_cleanup(void);
```

**说明**：清理 SDK 资源（可选，DLL 卸载时自动清理）

### 2. 播放器生命周期

#### `player_core_create()`

```c
PlayerCoreHandle* player_core_create(void);
```

**说明**：创建播放器实例

**返回值**：
- 成功：播放器句柄指针
- 失败：`NULL`

**注意**：
- 一个进程可以创建多个播放器实例
- 每个实例是独立的，互不干扰

**示例**：
```c
PlayerCoreHandle* player = player_core_create();
if (!player) {
    fprintf(stderr, "创建播放器失败\n");
    return 1;
}
```

#### `player_core_destroy()`

```c
void player_core_destroy(PlayerCoreHandle* handle);
```

**说明**：销毁播放器实例并释放所有资源

**参数**：
- `handle` - 播放器句柄

**注意**：
- 调用后 `handle` 变为无效，不能再使用
- 自动停止播放并清理所有资源

**示例**：
```c
player_core_destroy(player);
player = NULL;  // 避免悬空指针
```

### 3. 播放控制

#### `player_core_open()`

```c
int player_core_open(PlayerCoreHandle* handle, const char* url);
```

**说明**：打开视频文件或网络流

**参数**：
- `handle` - 播放器句柄
- `url` - 视频路径或 URL

**返回值**：
- `0` - 成功
- `非0` - 失败（错误码）

**支持的 URL 格式**：
- 本地文件：`C:\videos\test.mp4` 或 `test.mp4`
- HTTP/HTTPS：`http://example.com/video.mp4`
- RTMP：`rtmp://live.example.com/stream`
- RTSP：`rtsp://camera.example.com/live`
- HLS：`http://example.com/playlist.m3u8`

**示例**：
```c
// 本地文件
int ret = player_core_open(player, "D:\\videos\\movie.mp4");

// 网络流
int ret = player_core_open(player, "http://example.com/live.m3u8");

if (ret != 0) {
    fprintf(stderr, "打开失败，错误码: %d\n", ret);
}
```

#### `player_core_open_with_start_position()`

```c
int player_core_open_with_start_position(
    PlayerCoreHandle* handle,
    const char* url,
    double start_pos
);
```

**说明**：打开视频并从指定位置开始播放

**参数**：
- `handle` - 播放器句柄
- `url` - 视频路径或 URL
- `start_pos` - 起始位置（秒）

**示例**：
```c
// 从 1 分 30 秒开始播放
player_core_open_with_start_position(player, "video.mp4", 90.0);
```

#### `player_core_play()`

```c
void player_core_play(PlayerCoreHandle* handle);
```

**说明**：开始播放或从暂停恢复

**示例**：
```c
player_core_play(player);
```

#### `player_core_pause()`

```c
void player_core_pause(PlayerCoreHandle* handle);
```

**说明**：暂停播放（保持当前位置）

**示例**：
```c
player_core_pause(player);
```

#### `player_core_stop()`

```c
void player_core_stop(PlayerCoreHandle* handle);
```

**说明**：停止播放并重置状态

**注意**：停止后需要重新 `open()` 才能播放

**示例**：
```c
player_core_stop(player);
```

### 4. 状态查询

#### `player_core_get_state()`

```c
PlayerStateC player_core_get_state(PlayerCoreHandle* handle);
```

**返回值**：播放器状态

```c
typedef enum {
    PLAYER_STATE_IDLE = 0,      // 空闲（未打开）
    PLAYER_STATE_OPENING = 1,   // 正在打开
    PLAYER_STATE_PLAYING = 2,   // 播放中
    PLAYER_STATE_PAUSED = 3,    // 已暂停
    PLAYER_STATE_STOPPED = 4,   // 已停止
    PLAYER_STATE_ERROR = -1     // 错误
} PlayerStateC;
```

**示例**：
```c
PlayerStateC state = player_core_get_state(player);
if (state == PLAYER_STATE_PLAYING) {
    printf("正在播放\n");
}
```

#### `player_core_get_duration()`

```c
double player_core_get_duration(PlayerCoreHandle* handle);
```

**返回值**：视频总时长（秒），如果未知返回 `0.0`

**示例**：
```c
double duration = player_core_get_duration(player);
printf("视频时长: %.2f 秒\n", duration);
```

#### `player_core_get_position()`

```c
double player_core_get_position(PlayerCoreHandle* handle);
```

**返回值**：当前播放位置（秒）

**示例**：
```c
double pos = player_core_get_position(player);
double duration = player_core_get_duration(player);
printf("进度: %.1f / %.1f (%.1f%%)\n", 
       pos, duration, (pos / duration) * 100.0);
```

### 5. 播放控制

#### `player_core_seek()`

```c
void player_core_seek(PlayerCoreHandle* handle, double pos);
```

**说明**：跳转到指定位置

**参数**：
- `handle` - 播放器句柄
- `pos` - 目标位置（秒）

**注意**：
- Seek 操作可能不精确（取决于视频的关键帧位置）
- Seek 到接近但不完全等于目标的关键帧

**示例**：
```c
// 跳转到 1 分 30 秒
player_core_seek(player, 90.0);

// 跳到 25%
double duration = player_core_get_duration(player);
player_core_seek(player, duration * 0.25);
```

#### `player_core_set_volume()`

```c
void player_core_set_volume(PlayerCoreHandle* handle, float volume);
```

**说明**：设置音量

**参数**：
- `handle` - 播放器句柄
- `volume` - 音量值（0-100）

**示例**：
```c
player_core_set_volume(player, 80);  // 80%
player_core_set_volume(player, 0);   // 静音
```

#### `player_core_set_playback_rate()`

```c
void player_core_set_playback_rate(PlayerCoreHandle* handle, float rate);
```

**说明**：设置播放速度

**参数**：
- `handle` - 播放器句柄
- `rate` - 播放速率（0.5 - 2.0）
  - `0.5` = 0.5x 慢速
  - `1.0` = 正常速度
  - `1.5` = 1.5x 快速
  - `2.0` = 2.0x 快速

**注意**：使用 SoundTouch 库保持音调不变

**示例**：
```c
player_core_set_playback_rate(player, 1.5);  // 1.5 倍速
```

### 6. 回调函数

#### 状态变化回调

```c
typedef void (*StateChangedCallbackC)(PlayerStateC state, void* user_data);

void player_core_set_state_changed_callback(
    PlayerCoreHandle* handle,
    StateChangedCallbackC callback,
    void* user_data
);
```

**说明**：当播放器状态变化时调用

**示例**：
```c
void on_state_changed(PlayerStateC state, void* user_data) {
    switch (state) {
        case PLAYER_STATE_IDLE:
            printf("状态: 空闲\n");
            break;
        case PLAYER_STATE_PLAYING:
            printf("状态: 播放中\n");
            break;
        case PLAYER_STATE_PAUSED:
            printf("状态: 已暂停\n");
            break;
        case PLAYER_STATE_ERROR:
            printf("状态: 错误\n");
            break;
    }
}

player_core_set_state_changed_callback(player, on_state_changed, NULL);
```

#### 错误回调

```c
typedef void (*ErrorCallbackC)(int error_code, const char* error_msg, void* user_data);

void player_core_set_error_callback(
    PlayerCoreHandle* handle,
    ErrorCallbackC callback,
    void* user_data
);
```

**说明**：发生错误时调用

**示例**：
```c
void on_error(int error_code, const char* error_msg, void* user_data) {
    fprintf(stderr, "播放器错误 [%d]: %s\n", error_code, error_msg);
}

player_core_set_error_callback(player, on_error, NULL);
```

#### 播放进度回调

```c
typedef void (*PositionChangedCallbackC)(double position, void* user_data);

void player_core_set_position_changed_callback(
    PlayerCoreHandle* handle,
    PositionChangedCallbackC callback,
    void* user_data
);
```

**说明**：播放位置变化时调用（高频）

**示例**：
```c
void on_position_changed(double position, void* user_data) {
    printf("\r进度: %.1f 秒", position);
    fflush(stdout);
}

player_core_set_position_changed_callback(player, on_position_changed, NULL);
```

#### 播放完成回调

```c
typedef void (*PlaybackCompletedCallbackC)(void* user_data);

void player_core_set_playback_completed_callback(
    PlayerCoreHandle* handle,
    PlaybackCompletedCallbackC callback,
    void* user_data
);
```

**说明**：视频播放完成时调用

**示例**：
```c
void on_completed(void* user_data) {
    printf("播放完成！\n");
    // 可以在这里实现循环播放、播放下一个等
}

player_core_set_playback_completed_callback(player, on_completed, NULL);
```

## 高级用法

### 1. 多实例播放

```c
// 创建多个播放器
PlayerCoreHandle* player1 = player_core_create();
PlayerCoreHandle* player2 = player_core_create();

// 播放不同的视频
player_core_open(player1, "video1.mp4");
player_core_open(player2, "video2.mp4");

player_core_play(player1);
player_core_play(player2);  // 同时播放

// 清理
player_core_destroy(player1);
player_core_destroy(player2);
```

### 2. 线程安全的 UI 更新（Windows）

```c
#include <windows.h>

HWND g_hwnd;  // 主窗口句柄

void on_position_changed(double position, void* user_data) {
    // 不能直接更新 UI！必须使用 PostMessage
    PostMessage(g_hwnd, WM_USER + 1, 0, (LPARAM)(position * 1000));
}

// 在窗口过程中处理
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_USER + 1:
            {
                double position = lParam / 1000.0;
                // 现在可以安全地更新 UI
                UpdateProgressBar(position);
            }
            break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
```

### 3. 日志配置

```c
// 设置日志级别为 INFO
player_core_set_log_level(1);

// 启用文件日志
player_core_enable_file_logging("C:\\logs", "myplayer");

// 播放器日志将写入: C:\logs\myplayer_YYYYMMDD.log

// 设置最大日志文件大小（10MB）
player_core_set_max_log_file_size(10 * 1024 * 1024);

// 设置日志保留天数
player_core_set_log_retention_days(7);

// 手动清理旧日志
int deleted = player_core_cleanup_old_logs();
printf("删除了 %d 个旧日志文件\n", deleted);
```

## 平台差异

### Windows 特有注意事项

1. **路径分隔符**：
   - 使用 `\` 或 `/` 都可以
   - 推荐使用 `/` 或转义的 `\\`

2. **文件路径编码**：
   - SDK 内部使用 UTF-8
   - Windows API 使用 UTF-16
   - 建议使用 `MultiByteToWideChar` 转换

3. **DLL 依赖**：
   - 必须部署所有依赖 DLL
   - 可以使用 `LoadLibrary` 动态加载

### 与 macOS Framework 的对应

| Windows SDK | macOS Framework |
|-------------|-----------------|
| `hxcplayer.dll` | `HXCPlayer.framework` |
| `hxcplayer.lib` | 自动链接 |
| Include path | Framework Headers |
| Runtime DLLs | 嵌入 Framework |

API 完全一致，代码可直接移植！

## 性能优化

### 1. 减少回调开销

```c
// 不要在回调中做耗时操作
void on_position_changed(double position, void* user_data) {
    // ❌ 错误：耗时操作
    // SaveToDatabase(position);
    
    // ✅ 正确：快速处理
    ((AppData*)user_data)->last_position = position;
}
```

### 2. 内存管理

```c
// 不要频繁创建/销毁播放器
// ❌ 错误
for (int i = 0; i < 100; i++) {
    PlayerCoreHandle* player = player_core_create();
    player_core_open(player, videos[i]);
    player_core_play(player);
    player_core_destroy(player);
}

// ✅ 正确：复用播放器
PlayerCoreHandle* player = player_core_create();
for (int i = 0; i < 100; i++) {
    player_core_stop(player);
    player_core_open(player, videos[i]);
    player_core_play(player);
}
player_core_destroy(player);
```

## 常见问题

### Q1: 回调函数中可以调用 SDK API 吗？

**A**: 可以，但要注意：
- ✅ 可以：`player_core_get_*()` 查询函数
- ✅ 可以：`player_core_seek/pause/play()` 控制函数
- ❌ 不可以：`player_core_destroy()` (会死锁)

### Q2: 如何实现循环播放？

```c
void on_completed(void* user_data) {
    PlayerCoreHandle* player = (PlayerCoreHandle*)user_data;
    player_core_seek(player, 0.0);  // 回到开头
    player_core_play(player);       // 继续播放
}

player_core_set_playback_completed_callback(player, on_completed, player);
```

### Q3: 如何获取视频分辨率？

```c
int width = player_core_get_video_width(player);
int height = player_core_get_video_height(player);
printf("分辨率: %dx%d\n", width, height);
```

### Q4: 支持硬件加速吗？

**A**: FFmpeg 会自动使用可用的硬件解码器（如 DXVA2, D3D11VA）。

### Q5: 如何处理网络超时？

```c
void on_error(int error_code, const char* error_msg, void* user_data) {
    if (strstr(error_msg, "timeout") || strstr(error_msg, "Connection timed out")) {
        // 网络超时，可以重试
        printf("网络超时，正在重试...\n");
        // ... 重新 open
    }
}
```

## 更多信息

- GitHub: [项目地址]
- 示例代码: `example/simple_player.c`
- API 头文件: `include/hxc_player_core_c_bridge.h`
