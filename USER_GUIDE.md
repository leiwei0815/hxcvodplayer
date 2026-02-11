# YXVodPlayer 用户指南

## 快速开始

### 第一次使用

1. **构建播放器**

```bash
cd /Users/debug/project/YXVodPlayer

# 使用构建脚本（推荐）
./build.sh desktop release

# 或手动构建
mkdir -p build/desktop_release
cd build/desktop_release
cmake ../.. -DCMAKE_BUILD_TYPE=Release -DBUILD_DESKTOP=ON
make -j4
```

2. **生成测试视频**

```bash
./scripts/generate_test_video.sh
```

3. **运行播放器**

```bash
# Mac
open build/desktop_release/bin/YXVodPlayer.app

# 或直接运行可执行文件
./build/desktop_release/bin/YXVodPlayer.app/Contents/MacOS/YXVodPlayer
```

## 界面说明

### 主窗口

```
┌─────────────────────────────────────────┐
│  文件  帮助                              │ 菜单栏
├─────────────────────────────────────────┤
│                                         │
│                                         │
│          视频显示区域                    │ 
│                                         │
│                                         │
├─────────────────────────────────────────┤
│ ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ │ 进度条
├─────────────────────────────────────────┤
│ [打开] [播放] [停止] 00:00 / 00:00      │ 控制条
│                       音量: [━━━━━━━] │
└─────────────────────────────────────────┘
```

### 控制按钮

- **打开**: 打开视频文件
- **播放/暂停**: 切换播放状态
- **停止**: 停止播放并重置
- **进度条**: 拖动跳转到指定位置
- **时间显示**: 当前时间 / 总时长
- **音量滑块**: 调节音量 (0-100)

## 基本操作

### 打开文件

**方式 1: 使用菜单**
1. 点击 "文件" → "打开文件..."
2. 选择视频文件
3. 点击"打开"

**方式 2: 使用按钮**
1. 点击工具栏的"打开"按钮
2. 选择视频文件

**方式 3: 拖拽文件**
1. 直接拖拽视频文件到窗口

**方式 4: 命令行**
```bash
./YXVodPlayer /path/to/video.mp4
```

### 播放控制

**播放/暂停**
- 点击"播放"按钮
- 或按空格键

**停止**
- 点击"停止"按钮
- 或按 Ctrl+S

**跳转**
1. 拖动进度条到目标位置
2. 释放鼠标，自动跳转

### 音量控制

**调节音量**
- 拖动音量滑块
- 或使用音量键（如果支持）

**静音**
- 将音量调到 0

## 支持的格式

### 视频编码

- ✅ H.264 / AVC
- ✅ H.265 / HEVC
- ✅ VP8 / VP9
- ✅ MPEG-4
- ✅ MPEG-2
- ✅ 其他 FFmpeg 支持的格式

### 音频编码

- ✅ AAC
- ✅ MP3
- ✅ Opus
- ✅ Vorbis
- ✅ AC3
- ✅ 其他 FFmpeg 支持的格式

### 容器格式

- ✅ MP4 / M4V
- ✅ MKV
- ✅ AVI
- ✅ FLV
- ✅ MOV
- ✅ WebM
- ✅ TS / M2TS
- ✅ 其他 FFmpeg 支持的格式

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| Ctrl+O | 打开文件 |
| Space | 播放/暂停 |
| Ctrl+Q | 退出 |
| ← | 后退 5 秒（计划中） |
| → | 前进 5 秒（计划中） |
| ↑ | 增加音量（计划中） |
| ↓ | 减少音量（计划中） |
| F | 全屏（计划中） |

## 高级功能

### 命令行参数

```bash
# 打开指定文件
./YXVodPlayer video.mp4

# 将来支持更多参数
./YXVodPlayer --fullscreen video.mp4
./YXVodPlayer --volume 50 video.mp4
```

### 配置文件

将来会支持配置文件（计划中）：
- 保存播放历史
- 保存音量设置
- 保存窗口大小和位置

## 性能优化

### 播放高分辨率视频

如果播放 4K 视频卡顿：

1. **使用 Release 版本**
   ```bash
   ./build.sh desktop release
   ```

2. **关闭其他应用**减少系统负载

3. **降低分辨率**（将来支持）

### 减少内存占用

- 不要同时打开多个视频
- 定期重启播放器

## 常见问题

### Q: 为什么某些视频无法播放？

A: 可能的原因：
1. 视频编码格式不支持
2. 文件已损坏
3. 缺少必要的解码器

解决方法：
- 检查视频文件是否完整
- 使用 FFmpeg 转换格式：
  ```bash
  ffmpeg -i input.mkv -c:v libx264 -c:a aac output.mp4
  ```

### Q: 音视频不同步怎么办？

A: 尝试：
1. 重新打开文件
2. 检查是否是视频文件本身的问题
3. 更换其他播放器测试

### Q: 为什么播放很卡？

A: 可能的原因：
1. 视频分辨率太高
2. 使用的是 Debug 版本
3. CPU 性能不足

解决方法：
- 使用 Release 版本
- 降低视频分辨率
- 启用硬件加速（将来支持）

### Q: 如何查看详细的播放信息？

A: 在终端运行播放器可以看到详细日志：
```bash
./build/desktop_release/bin/YXVodPlayer.app/Contents/MacOS/YXVodPlayer video.mp4
```

日志包括：
- 视频信息（分辨率、编码、帧率）
- 音频信息（采样率、声道、编码）
- 播放状态变化
- 错误信息

### Q: 支持字幕吗？

A: 当前版本不支持字幕，计划在将来版本添加。

### Q: 可以截图吗？

A: 当前版本不支持截图，计划在将来版本添加。

## 系统要求

### Mac

- **操作系统**: macOS 10.13 或更高
- **处理器**: Intel 或 Apple Silicon
- **内存**: 最低 4GB，推荐 8GB
- **依赖**:
  - FFmpeg 8.x
  - SDL2
  - Qt5

### Windows

- **操作系统**: Windows 10 或更高
- **处理器**: x64
- **内存**: 最低 4GB，推荐 8GB
- **依赖**:
  - FFmpeg 8.x
  - SDL2
  - Qt5
  - Visual C++ Redistributable

### Linux

- **操作系统**: Ubuntu 20.04+ / Fedora 35+ / Arch
- **处理器**: x64
- **内存**: 最低 4GB，推荐 8GB
- **依赖**:
  - FFmpeg 8.x
  - SDL2
  - Qt5

## 故障排除

### 编译问题

**问题**: CMake 找不到 Qt5

**解决**:
```bash
export Qt5_DIR=/path/to/qt5/lib/cmake/Qt5
```

**问题**: 找不到 FFmpeg

**解决**:
```bash
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
```

### 运行问题

**问题**: 动态库加载失败

**解决**:
```bash
# Mac
export DYLD_LIBRARY_PATH=/usr/local/lib:$DYLD_LIBRARY_PATH

# Linux
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

**问题**: SDL 初始化失败

**解决**:
- 确保 SDL2 正确安装
- 检查音频设备是否可用

## 更新日志

### v0.9.0 (当前)

**新功能**:
- ✅ 基本播放功能
- ✅ 音视频同步
- ✅ 进度控制
- ✅ 音量控制

**已知问题**:
- ⚠️ 不支持字幕
- ⚠️ 不支持截图
- ⚠️ 缺少全屏模式

### v1.0.0 (计划中)

**计划功能**:
- [ ] 完善全屏模式
- [ ] 添加快捷键支持
- [ ] 播放列表
- [ ] Android 版本
- [ ] iOS 版本

## 反馈和支持

### 报告问题

如果遇到问题，请提供：

1. **系统信息**
   - 操作系统版本
   - 播放器版本
   - FFmpeg 版本

2. **问题描述**
   - 详细的操作步骤
   - 预期行为
   - 实际行为

3. **附加信息**
   - 视频文件信息
   - 错误日志
   - 截图（如果适用）

### 获取帮助

- 📖 查看文档：`docs/` 目录
- 🐛 提交 Issue
- 💬 加入讨论

## 贡献

欢迎贡献代码！请查看 `CONTRIBUTING.md`（将来添加）。

## 许可证

MIT License - 详见 LICENSE 文件

---

**祝您使用愉快！** 🎬

如有问题，请参考：
- [快速入门](docs/QUICKSTART.md)
- [架构设计](docs/ARCHITECTURE.md)
- [测试指南](TEST_GUIDE.md)
