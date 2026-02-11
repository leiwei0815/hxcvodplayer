# ✅ 编译成功！在 Xcode 中运行播放器

## 🎉 状态

**BUILD SUCCEEDED** - Xcode 项目编译成功！

可执行文件位置：
```
build/xcode/bin/Debug/YXVodPlayer.app
```

## 🚀 立即运行（3 步）

### 步骤 1：打开 Xcode 项目

```bash
cd /Users/debug/project/YXVodPlayer
open build/xcode/YXVodPlayer.xcodeproj
```

### 步骤 2：配置 Scheme

在 Xcode 中：
1. 点击顶部的 Scheme 选择器（显示 "YXVodPlayer"）
2. 确保选中 **YXVodPlayer**
3. 目标选择 **My Mac**

### 步骤 3：运行

按 **⌘R** 或点击播放按钮 ▶️

播放器窗口应该会启动！

## 🎬 测试播放视频

### 方式 1：UI 中打开

1. 点击播放器中的 "打开" 按钮
2. 选择 `test_videos/test1_pattern.mp4`
3. 视频开始播放

### 方式 2：配置启动参数

让播放器启动时自动打开视频：

1. Product → Scheme → **Edit Scheme...** (⌘<)
2. 左侧选择 **Run**
3. 切换到 **Arguments** 标签
4. 在 **Arguments Passed On Launch** 点击 **+** 添加：
   ```
   $(SRCROOT)/../../test_videos/test1_pattern.mp4
   ```
5. 点击 **Close**
6. 按 **⌘R** 重新运行

现在播放器会自动打开测试视频！

### 方式 3：拖拽文件

1. 启动播放器（⌘R）
2. 直接拖拽视频文件到播放器窗口

## 🔍 调试功能

### 查看日志

按 **⌘⇧C** 打开 Console，你会看到详细的日志：

```
[INFO] 初始化 PlayerCore...
[INFO] SDL 初始化成功
[INFO] PlayerCore 初始化完成
[INFO] 正在打开文件: test_videos/test1_pattern.mp4
[INFO] 文件打开成功

========== 媒体信息 ==========
文件: test_videos/test1_pattern.mp4
格式: QuickTime / MOV
时长: 00:10.000
比特率: 274.27 kbps
流数量: 2

--- 流 #0 ---
类型: 视频
编码: H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10
分辨率: 1280x720
像素格式: yuv420p
帧率: 30.00 fps

--- 流 #1 ---
类型: 音频
编码: AAC (Advanced Audio Coding)
采样率: 44100 Hz
声道: 1
采样格式: fltp
================================
```

### 设置断点

推荐在以下位置设置断点：

1. **PlayerCore::open** (文件打开)
   - 文件: `src/core/player_core.cpp`
   - 行号: ~60
   - 操作：在行号左侧点击设置蓝色断点

2. **PlayerCore::video_thread** (视频解码)
   - 文件: `src/core/player_core.cpp`
   - 行号: ~440

3. **PlayerCore::audio_callback_impl** (音频回调)
   - 文件: `src/core/player_core.cpp`
   - 行号: ~530

### 单步调试

当程序在断点处暂停：
- **F6**: 单步跳过（执行当前行，不进入函数）
- **F7**: 单步进入（进入函数内部）
- **F8**: 继续执行（运行到下一个断点）

### 查看变量

在左下角的 **Variables View** 可以看到：
- `state_` - 播放器状态
- `format_ctx_` - 格式上下文
- `video_stream_` - 视频流索引
- `audio_stream_` - 音频流索引

## 🎯 测试清单

启动播放器后测试：

- [ ] 窗口正常显示
- [ ] 点击"打开"按钮，选择测试视频
- [ ] 视频正常播放
- [ ] 音频正常播放
- [ ] 进度条正常显示
- [ ] 拖动进度条可以跳转
- [ ] 音量滑块工作正常
- [ ] 播放/暂停按钮正常
- [ ] 停止按钮正常

## ⌨️ Xcode 快捷键

| 操作 | 快捷键 |
|------|--------|
| 运行 | ⌘R |
| 停止 | ⌘. |
| 构建 | ⌘B |
| 清理构建 | ⌘⇧K |
| 单步跳过 | F6 |
| 单步进入 | F7 |
| 继续执行 | F8 |
| 设置断点 | ⌘\ |
| 显示 Console | ⌘⇧C |
| 显示调试区域 | ⌘⇧Y |
| Profile | ⌘I |

## 🔧 高级调试

### CPU 性能分析

1. 按 **⌘I** 或 Product → Profile
2. 选择 **Time Profiler**
3. 点击红色录制按钮
4. 播放视频
5. 停止录制
6. 查看哪些函数耗时最多

### 内存泄漏检测

1. 按 **⌘I** 或 Product → Profile
2. 选择 **Leaks**
3. 录制一段时间
4. 查看是否有内存泄漏

### 静态分析

按 **⌘⇧B** 或 Product → Analyze

Xcode 会自动检测：
- 内存泄漏
- 空指针
- 未初始化变量
- 逻辑错误

## 🐛 常见问题

### Q: 运行后窗口不显示

A: 检查 Console 日志（⌘⇧C），查看是否有错误信息

### Q: 找不到测试视频

A: 生成测试视频：
```bash
./scripts/generate_test_video.sh
```

### Q: 视频无法播放

A: 
1. 查看 Console 日志
2. 检查视频文件路径是否正确
3. 尝试其他测试视频

### Q: 断点不生效

A: 
1. 确保使用 Debug 配置（不是 Release）
2. 检查断点是否启用（蓝色，不是灰色）
3. 按 ⌘Y 切换断点启用/禁用

## 📁 测试视频位置

```
test_videos/
├── test1_pattern.mp4    (推荐) - 彩色测试图案，10秒
├── test2_simple.mp4     - 蓝色背景，5秒
├── test3_video_only.mp4 - 仅视频，无音频
└── test4_4k.mp4         - 4K分辨率测试
```

## 🎊 成功标志

如果一切正常，你应该看到：

✅ 播放器窗口启动
✅ Console 显示日志输出
✅ 可以打开视频文件
✅ 视频和音频同步播放
✅ 控制按钮响应正常

## 📚 更多帮助

- **详细调试指南**: [HOW_TO_DEBUG_IN_XCODE.md](HOW_TO_DEBUG_IN_XCODE.md)
- **Xcode 完整指南**: [XCODE_GUIDE.md](XCODE_GUIDE.md)
- **用户手册**: [USER_GUIDE.md](USER_GUIDE.md)

---

## 🎉 现在就开始！

```bash
open build/xcode/YXVodPlayer.xcodeproj
```

然后在 Xcode 中按 **⌘R**

**祝你调试愉快！** 🚀
