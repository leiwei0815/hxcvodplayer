# YXVodPlayer 测试指南

## 编译状态

✅ **编译成功！** (Debug 版本)

构建输出：
- 可执行文件：`build/desktop_debug/bin/YXVodPlayer.app/Contents/MacOS/YXVodPlayer`
- 核心库：`build/desktop_debug/src/core/libyxplayer_core.a`
- 平台库：`build/desktop_debug/src/platform/libyxplayer_platform.a`

## 测试视频

测试视频已生成在 `test_videos/` 目录：

1. **test1_pattern.mp4** (279KB) - 彩色测试图案，10秒，1280x720，30fps
2. **test2_simple.mp4** - 蓝色背景+文字，5秒
3. **test3_video_only.mp4** (46KB) - 仅视频，无音频，5秒，640x480
4. **test4_4k.mp4** (494KB) - 4K分辨率测试，3秒，3840x2160

## 运行播放器

### 方法 1: 直接运行

```bash
# 方式 1: 运行 .app
cd /Users/debug/project/YXVodPlayer
./build/desktop_debug/bin/YXVodPlayer.app/Contents/MacOS/YXVodPlayer

# 方式 2: 通过 open 命令（推荐）
open build/desktop_debug/bin/YXVodPlayer.app

# 方式 3: 使用快速测试脚本
./scripts/quick_test.sh
```

### 方法 2: 命令行打开文件

```bash
./build/desktop_debug/bin/YXVodPlayer.app/Contents/MacOS/YXVodPlayer test_videos/test1_pattern.mp4
```

### 方法 3: 在 Xcode 中运行（调试）

```bash
open build/desktop_debug/YXVodPlayer.xcodeproj
```

## 测试清单

### 基本功能测试

- [ ] **启动测试**
  - [ ] 应用正常启动
  - [ ] 窗口正常显示
  - [ ] 控制条正常显示

- [ ] **文件打开**
  - [ ] 点击"打开"按钮选择文件
  - [ ] 拖拽文件到窗口
  - [ ] 命令行参数打开文件

- [ ] **播放控制**
  - [ ] 播放/暂停按钮工作正常
  - [ ] 停止按钮工作正常
  - [ ] 进度条显示正常
  - [ ] 进度条拖动正常

- [ ] **视频显示**
  - [ ] 视频正常显示
  - [ ] 视频比例正确
  - [ ] 窗口缩放时视频自适应

- [ ] **音频播放**
  - [ ] 音频正常播放
  - [ ] 音频视频同步
  - [ ] 音量控制正常

### 格式兼容性测试

- [ ] **test1_pattern.mp4** - H.264 + AAC
- [ ] **test2_simple.mp4** - H.264 + AAC
- [ ] **test3_video_only.mp4** - 仅视频
- [ ] **test4_4k.mp4** - 4K 分辨率

### 性能测试

- [ ] **CPU 使用率**
  - 播放 720p 视频时 CPU < 20%
  - 播放 4K 视频时 CPU < 50%

- [ ] **内存使用**
  - 启动时内存 < 100MB
  - 播放时内存 < 200MB
  - 无明显内存泄漏

- [ ] **响应性能**
  - UI 操作流畅
  - 播放无卡顿
  - Seek 操作响应快速

## 已知问题和限制

### 当前实现状态

✅ **已完成**:
- 核心播放引擎
- 音频解码和播放
- 视频解码和显示
- 音视频同步（AudioMaster 模式）
- 基本UI控制
- 文件打开
- 播放/暂停/停止
- Seek 功能
- 音量控制
- 进度显示

🚧 **待完善**:
- 硬件加速（可选）
- 字幕支持
- 多音轨/多视频轨支持
- 播放列表
- 截图功能
- 倍速播放
- 全屏模式优化

⚠️ **已知问题**:
1. 部分编码格式可能不支持（取决于 FFmpeg 编译选项）
2. 4K 视频播放可能需要较高 CPU（软解码）
3. 某些格式的音视频同步可能不完美
4. UI 在高分辨率屏幕上可能需要调整

## 日志和调试

### 查看日志

播放器会输出日志到终端：

```bash
# 运行并查看日志
./build/desktop_debug/bin/YXVodPlayer.app/Contents/MacOS/YXVodPlayer 2>&1 | tee player.log
```

### 日志级别

日志输出包括：
- **[INFO]** - 一般信息（文件打开、流信息等）
- **[WARNING]** - 警告信息
- **[ERROR]** - 错误信息
- **[DEBUG]** - 调试信息（需要在代码中启用）

### 调试技巧

1. **查看媒体信息**：播放器会通过 `av_dump_format()` 输出详细的媒体信息

2. **检查音视频同步**：观察日志中的时间戳信息

3. **性能分析**：使用 Instruments 工具
   ```bash
   instruments -t "Time Profiler" build/desktop_debug/bin/YXVodPlayer.app
   ```

## 测试建议

### 基础测试流程

1. **启动测试** (1分钟)
   - 启动应用
   - 检查界面
   - 检查控制条

2. **播放测试** (3分钟)
   - 打开 test1_pattern.mp4
   - 验证视频和音频播放
   - 测试播放/暂停
   - 测试进度条拖动

3. **兼容性测试** (5分钟)
   - 依次播放所有测试视频
   - 验证不同格式的支持

4. **压力测试** (2分钟)
   - 播放 4K 视频
   - 观察性能表现

### 回归测试

每次代码修改后应运行：

```bash
# 完整构建和测试
./build.sh desktop debug
./scripts/quick_test.sh

# 手动测试
open build/desktop_debug/bin/YXVodPlayer.app
```

## 性能优化建议

如果播放性能不佳：

1. **启用硬件加速**（未来实现）
2. **降低视频分辨率**
3. **关闭其他应用**
4. **使用 Release 构建**
   ```bash
   ./build.sh desktop release
   ```

## 贡献测试用例

欢迎添加更多测试视频：

```bash
# 添加你的视频到 test_videos/
cp /path/to/your/video.mp4 test_videos/test_custom.mp4

# 运行测试
./build/desktop_debug/bin/YXVodPlayer.app/Contents/MacOS/YXVodPlayer test_videos/test_custom.mp4
```

## 问题反馈

如果发现问题，请记录：

1. **操作步骤** - 如何重现问题
2. **预期行为** - 应该发生什么
3. **实际行为** - 实际发生了什么
4. **环境信息** - macOS 版本、FFmpeg 版本等
5. **日志输出** - 相关的错误日志

## 下一步

- [ ] 在 Mac 上完成全面测试
- [ ] 修复发现的 bug
- [ ] 性能优化
- [ ] 准备创建 Android 和 iOS 版本
- [ ] 编写自动化测试脚本

---

**测试愉快！** 🎉

如果遇到问题，请查看 `docs/` 目录中的文档或提交 Issue。
