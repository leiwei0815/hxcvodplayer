# Xcode 调试指南

## 打开项目

```bash
cd /Users/debug/project/YXVodPlayer
open build/xcode/YXVodPlayer.xcodeproj
```

## Xcode 配置

### 1. Scheme 设置

1. 在 Xcode 中，点击顶部的 Scheme 选择器
2. 选择 "YXVodPlayer" scheme
3. 点击 "Edit Scheme..."

### 2. 运行配置

**Run（调试运行）**:
- Build Configuration: Debug
- Executable: YXVodPlayer.app

**Arguments（启动参数）**:
可以添加测试视频路径：
```
Arguments Passed On Launch:
  $(SRCROOT)/../../test_videos/test1_pattern.mp4
```

**Environment Variables（环境变量）**:
```
DYLD_LIBRARY_PATH = /opt/homebrew/lib:/usr/local/lib
```

### 3. 调试配置

**Breakpoints（断点）**:

推荐设置的断点位置：

1. **PlayerCore::open()** - 文件打开
   - 文件: `src/core/player_core.cpp`
   - 行号: ~50

2. **PlayerCore::audio_callback_impl()** - 音频回调
   - 文件: `src/core/player_core.cpp`
   - 行号: ~525

3. **PlayerCore::video_thread()** - 视频线程
   - 文件: `src/core/player_core.cpp`
   - 行号: ~430

4. **MainWindow::openFile()** - UI 文件打开
   - 文件: `src/desktop/main_window.cpp`
   - 行号: ~160

**Symbolic Breakpoints（符号断点）**:
```
av_log
SDL_Log
```

### 4. LLDB 调试命令

在调试时可用的有用命令：

```lldb
# 查看变量
po player_
p state_
p video_stream_
p audio_stream_

# 查看 AVFrame
p *frame
p frame->width
p frame->height
p frame->pts

# 查看队列状态
p video_queue_->size()
p audio_queue_->size()

# 查看时钟
p audio_clock_.get_clock()
p video_clock_.get_clock()

# 继续执行
c

# 单步执行
n  # 下一行
s  # 进入函数
```

## 性能分析

### 1. Instruments 集成

**CPU Profiler**:
```bash
# 从 Xcode 启动
Product → Profile (⌘I)
选择 "Time Profiler"
```

**Memory Leaks**:
```bash
Product → Profile (⌘I)
选择 "Leaks"
```

### 2. 日志查看

**Console（控制台）**:
- View → Debug Area → Activate Console (⌘⇧C)
- 所有 LOG_INFO、LOG_ERROR 都会显示在这里

**实时日志过滤**:
```
在 Console 搜索框输入：
- "ERROR" - 只看错误
- "INFO" - 只看信息
- "PlayerCore" - 只看播放器相关
```

## 常见问题

### Q1: Xcode 找不到头文件

**解决**:
1. Project Navigator → YXVodPlayer → Build Settings
2. 搜索 "Header Search Paths"
3. 添加：
   ```
   /opt/homebrew/include
   $(SRCROOT)/../../include
   ```

### Q2: 链接错误

**解决**:
1. Build Settings → Library Search Paths
2. 添加：
   ```
   /opt/homebrew/lib
   ```

### Q3: Qt 相关错误

**解决**:
```bash
export Qt5_DIR=/opt/homebrew/opt/qt@5/lib/cmake/Qt5
```

重新生成 Xcode 项目：
```bash
cd build/xcode
rm -rf *
cmake ../.. -G Xcode -DBUILD_DESKTOP=ON
```

## 调试技巧

### 1. 条件断点

右键点击断点 → Edit Breakpoint → Condition:
```cpp
state_ == PlayerState::Error
pts > 10.0
video_stream_ >= 0
```

### 2. 日志断点

不暂停程序，只输出日志：
1. 右键断点 → Edit Breakpoint
2. 取消勾选 "Automatically continue after evaluating actions"
3. 添加 Action → Log Message:
   ```
   Video PTS: @pts@, Clock: @audio_clock_.get_clock()@
   ```

### 3. 符号断点

调试 FFmpeg 调用：
```
Debug → Breakpoints → Create Symbolic Breakpoint
Symbol: av_read_frame
Module: (留空或填写 libavformat)
```

### 4. 异常断点

捕获所有异常：
```
Debug → Breakpoints → Create Exception Breakpoint
Exception: All
Break: On Throw
```

## 快捷键

| 快捷键 | 功能 |
|--------|------|
| ⌘R | 运行 |
| ⌘. | 停止 |
| ⌘I | Profile |
| ⌘B | 构建 |
| ⌘K | 清理 |
| F6 | 单步跳过 |
| F7 | 单步进入 |
| F8 | 继续执行 |
| ⌘Y | 启用/禁用断点 |
| ⌘⇧C | 显示/隐藏控制台 |
| ⌘⇧Y | 显示/隐藏调试区域 |

## 优化建议

### 1. 编译优化

**Debug 配置**:
- Optimization Level: None [-O0]
- Debug Information Format: DWARF with dSYM File

**Release 配置**:
- Optimization Level: Fastest, Smallest [-Os]
- Strip Debug Symbols: Yes

### 2. 静态分析

```
Product → Analyze (⌘⇧B)
```

自动检测：
- 内存泄漏
- 空指针解引用
- 未初始化变量
- 死代码

### 3. 代码覆盖率

```
Scheme → Edit Scheme → Test
勾选 "Gather coverage for some targets"
```

## 测试工作流

### 1. 快速测试

```bash
# 在 Xcode 中按 ⌘R，或
# 在终端运行：
cd /Users/debug/project/YXVodPlayer
./scripts/quick_test.sh
```

### 2. 单元测试（将来添加）

```
Product → Test (⌘U)
```

### 3. UI 测试（将来添加）

在 Xcode 中录制 UI 测试

## 项目结构（Xcode 视图）

```
YXVodPlayer
├── Products
│   └── YXVodPlayer.app
├── src
│   ├── core
│   │   ├── player_core.cpp
│   │   ├── decoder.cpp
│   │   └── ...
│   ├── desktop
│   │   ├── main.cpp
│   │   ├── main_window.cpp
│   │   └── ...
│   └── platform
├── include
│   ├── player_core.h
│   └── ...
└── Resources
    └── test_videos
```

## 推荐插件

虽然 Xcode 不支持插件，但可以使用：

1. **GitHub Copilot** - AI 代码补全（需要 GitHub 帐户）
2. **Alcatraz**（已废弃，仅作参考）
3. 使用外部工具如 **clang-format** 格式化代码

## 代码格式化

在项目根目录创建 `.clang-format`:
```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 100
```

使用：
```bash
clang-format -i src/**/*.cpp src/**/*.h
```

## Git 集成

Xcode 自带 Git 支持：

- Source Control → Commit (⌥⌘C)
- Source Control → Push
- Source Control → Pull
- View → Navigators → Show Source Control Navigator

## 性能基准

在 Debug 模式下的预期性能：

| 操作 | 预期时间 |
|------|---------|
| 冷启动 | < 2s |
| 打开文件 | < 1s |
| 开始播放 | < 200ms |
| Seek 跳转 | < 500ms |

如果超出这些时间，可能需要优化。

## 调试输出示例

正常启动的日志应该类似：
```
[INFO] 正在打开文件: test_videos/test1_pattern.mp4
[INFO] 文件打开成功
[INFO] 打开视频流...
[INFO] 视频流打开成功, 分辨率: 1280x720
[INFO] 打开音频流...
[INFO] 音频流打开成功, 采样率: 44100 Hz
```

## 更多资源

- [Xcode 官方文档](https://developer.apple.com/documentation/xcode)
- [LLDB 调试指南](https://lldb.llvm.org/)
- [Instruments 用户指南](https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/InstrumentsUserGuide/)

---

**开始调试**: `open build/xcode/YXVodPlayer.xcodeproj`
