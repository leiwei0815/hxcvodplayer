# HXCPlayer SDK 测试项目

这是一个独立的测试项目，用于验证 HXCPlayer SDK 的功能。

## 📋 项目说明

这个项目完全独立于主项目，模拟外部开发者如何使用 SDK。

## 🚀 快速开始

### 1. 确保 SDK 已构建

```bash
cd ../win-sdk
build_sdk.bat
```

SDK 会被构建到 `../build/win-sdk-Release/HXCPlayerSDK/`

### 2. 构建测试项目

```bash
build.bat
```

### 3. 运行测试

```bash
run.bat
```

或者直接运行：
```bash
build\bin\Release\SDKTestPlayer.exe
```

## 🛠️ 开发

### 在 Visual Studio 中调试

```bash
# 1. 配置项目
build.bat

# 2. 打开 VS 项目
start build\SDKTestPlayer.sln
```

## 📦 项目结构

```
win-sdk-example/
├── CMakeLists.txt          # CMake 配置
├── main.cpp                # 程序入口
├── player_window.h         # 播放器窗口头文件
├── player_window.cpp       # 播放器窗口实现
├── player_window.ui        # Qt UI 界面设计
├── build.bat               # 构建脚本
├── run.bat                 # 运行脚本
└── README.md               # 本文件
```

## ✅ 测试功能

- [x] 打开本地视频文件
- [x] 打开网络流（HTTP/HTTPS/RTSP）
- [x] 播放/暂停/停止
- [x] 进度条拖动
- [x] 音量调节
- [x] 时间显示
- [x] 状态显示
- [x] 错误处理
- [x] 播放完成通知
- [x] 使用回调机制更新 UI（无定时器）

## 📝 SDK 使用示例

### 创建播放器并设置渲染窗口

```cpp
// 1. 创建播放器
PlayerCoreHandle* player = player_core_create();

// 2. 设置渲染窗口（Qt Widget）
player_core_set_render_window(player, (void*)ui->videoContainer->winId());

// 3. 设置渲染模式为自动渲染
player_core_set_render_mode(player, RENDER_MODE_AUTO);
```

### 设置回调（推荐方式，替代定时器）

```cpp
// 播放进度回调
player_core_set_position_changed_callback(player, [](double position, void* user_data) {
    PlayerWindow* self = static_cast<PlayerWindow*>(user_data);
    // 使用 Qt 的线程安全机制更新 UI
    QMetaObject::invokeMethod(self, "onProgressChanged", Qt::QueuedConnection);
}, this);

// 状态变化回调
player_core_set_state_changed_callback(player, [](PlayerStateC state, void* user_data) {
    PlayerWindow* self = static_cast<PlayerWindow*>(user_data);
    QMetaObject::invokeMethod(self, "onStateChanged", Qt::QueuedConnection);
}, this);

// 播放完成回调
player_core_set_playback_finished_callback(player, [](void* user_data) {
    PlayerWindow* self = static_cast<PlayerWindow*>(user_data);
    QMetaObject::invokeMethod(self, "onPlaybackFinished", Qt::QueuedConnection);
}, this);

// 错误回调
player_core_set_error_callback(player, [](int error_code, const char* error, void* user_data) {
    PlayerWindow* self = static_cast<PlayerWindow*>(user_data);
    QString errorMsg = QString::fromUtf8(error);
    QMetaObject::invokeMethod(self, [self, errorMsg]() {
        QMessageBox::critical(self, "播放错误", errorMsg);
    }, Qt::QueuedConnection);
}, this);
```

**重要说明**：
- 使用回调机制比定时器更高效，减少 CPU 占用
- 回调在解码线程中调用，需要使用 `QMetaObject::invokeMethod` 保证线程安全
- `Qt::QueuedConnection` 确保 UI 更新在主线程执行

### 设置回调

```cpp
player_core_set_state_changed_callback(player, [](PlayerState state, void* user_data) {
    qDebug() << "状态变化:" << (int)state;
}, this);

player_core_set_error_callback(player, [](int error_code, const char* error, void* user_data) {
    qDebug() << "错误:" << error;
}, this);
```

### 打开并播放媒体

```cpp
// 支持本地文件和网络流
const char* media_path = "video.mp4";  // 或 "http://example.com/stream.m3u8"
player_core_open(player, media_path);
player_core_play(player);
```

**支持的媒体类型**：
- 本地文件：MP4, MKV, AVI, FLV, MOV, TS 等
- HTTP/HTTPS 流：MP4 文件、HLS (m3u8)
- RTSP 流：实时视频流

### 控制播放

```cpp
player_core_pause(player);
player_core_seek(player, 10.0);  // 跳转到 10 秒
player_core_set_volume(player, 0.8);  // 设置音量 80%
```

### 清理资源

```cpp
player_core_stop(player);
player_core_destroy(player);
```

## 🎬 视频渲染机制

### 自动渲染模式（推荐）

SDK 默认使用 **自动渲染模式**，优势：
- ✅ **零代码渲染**：无需编写任何渲染逻辑
- ✅ **性能优化**：SDK 内部使用 SDL2 硬件加速
- ✅ **跨平台**：同一套代码支持 Windows、macOS、Linux
- ✅ **自动缩放**：支持窗口大小变化，自动适配
- ✅ **宽高比**：自动保持视频比例或填充模式

**使用步骤**：
```cpp
// 1. 设置渲染窗口（只需调用一次）
player_core_set_render_window(player, (void*)window_handle);

// 2. 播放视频
player_core_open(player, "video.mp4");
player_core_play(player);

// SDK 会自动在后台渲染视频帧到窗口，无需其他操作
```

### 手动渲染模式（高级）

如果需要自定义渲染（例如添加特效、水印等）：
```cpp
// 1. 设置为手动模式
player_core_set_render_mode(player, RENDER_MODE_MANUAL);

// 2. 在渲染循环中获取视频帧
VideoFrameDataC frame;
if (player_core_get_video_frame(player, &frame) == 0) {
    // 使用 frame.y_data, frame.u_data, frame.v_data 渲染
    // frame.width, frame.height 是帧尺寸
    
    // 渲染完成后必须调用
    player_core_consume_video_frame(player);
}
```

## 🔍 注意事项

1. **SDK 依赖**：确保 SDK 的所有 DLL（hxcplayer.dll、FFmpeg DLL、SDL2.dll）都在输出目录中
2. **Qt 版本**：需要 Qt 5.15.2 或兼容版本
3. **编译器**：使用 MSVC 2019/2022 (x64)
4. **运行时**：确保安装了 Visual C++ Redistributable

## 🐛 故障排除

### DLL 缺失

如果运行时提示缺少 DLL，检查 `build/bin/Release/` 目录是否包含：
- hxcplayer.dll
- SDL2.dll
- avcodec-*.dll
- avformat-*.dll
- avutil-*.dll
- swscale-*.dll
- swresample-*.dll
- Qt5Core.dll
- Qt5Gui.dll
- Qt5Widgets.dll

### 无法打开视频

检查：
1. 视频格式是否支持（MP4、FLV、MKV 等）
2. 视频编码是否在支持列表中（H.264、HEVC、AAC、MP3）
3. 文件路径是否正确

## 📚 更多信息

查看完整 SDK 文档：
- [SDK 使用指南](../win-sdk/docs/SDK_USAGE.md)
- [自动渲染指南](../win-sdk/AUTO_RENDERING_GUIDE.md)
- [渲染模式说明](../win-sdk/RENDERING_MODES.md)
