# 如何在 Xcode 中调试 YXVodPlayer

## 🚀 立即开始（30 秒）

```bash
cd /Users/debug/project/YXVodPlayer
./scripts/open_xcode.sh
```

然后在 Xcode 中：
1. 选择顶部的 Scheme: **YXVodPlayer**
2. 按 **⌘R** 运行
3. 开始调试！

## 📋 完整步骤

### 第一步：打开项目

**方式 1 - 使用脚本（推荐）**:
```bash
./scripts/open_xcode.sh
```

**方式 2 - 手动打开**:
```bash
open build/xcode/YXVodPlayer.xcodeproj
```

### 第二步：配置运行参数（可选）

如果想启动时自动打开测试视频：

1. 点击顶部工具栏的 **YXVodPlayer** scheme
2. 选择 **Edit Scheme...**
3. 左侧选择 **Run**
4. 切换到 **Arguments** 标签
5. 在 **Arguments Passed On Launch** 添加：
   ```
   $(SRCROOT)/../../test_videos/test1_pattern.mp4
   ```
6. 点击 **Close**

### 第三步：设置断点

推荐的调试断点：

#### 1. 文件打开
```
文件: src/core/player_core.cpp
函数: PlayerCore::open
行号: ~57 (avformat_open_input 之后)
```

#### 2. 视频解码
```
文件: src/core/player_core.cpp
函数: PlayerCore::video_thread  
行号: ~435 (decode_frame 调用处)
```

#### 3. 音频回调
```
文件: src/core/player_core.cpp
函数: PlayerCore::audio_callback_impl
行号: ~530 (音频处理逻辑)
```

**设置方法**:
1. 在左侧 Project Navigator 找到文件
2. 打开文件
3. 在行号左侧点击设置断点（蓝色箭头）

### 第四步：运行

按 **⌘R** 或点击顶部的播放按钮

### 第五步：调试

当程序在断点处暂停时：

**查看变量**:
- 左下角 Variables View 显示所有局部变量
- 鼠标悬停在代码上查看变量值

**LLDB 命令**（在底部 Console 输入）:
```lldb
# 打印变量
po player_
p state_
p video_stream_
p audio_stream_

# 打印 AVFrame 信息
p *frame
p frame->width
p frame->height

# 查看队列状态
p video_queue_->size()
p audio_queue_->size()

# 继续执行
c

# 单步执行
n   # 下一行
s   # 进入函数
```

**控制执行**:
- **F6**: 单步跳过（Step Over）
- **F7**: 单步进入（Step Into）
- **F8**: 继续执行（Continue）
- **⌘\**: 在当前行设置/取消断点

## 📊 查看日志

### Console 输出

按 **⌘⇧C** 打开 Console 面板，你会看到：

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
...
```

### 过滤日志

在 Console 右上角的搜索框输入：
- `INFO` - 只看信息日志
- `ERROR` - 只看错误
- `PlayerCore` - 只看播放器相关

## 🔍 性能分析

### CPU Profiler

1. 按 **⌘I** 或 Product → Profile
2. 选择 **Time Profiler**
3. 点击红色录制按钮
4. 使用播放器播放视频
5. 点击停止
6. 查看 **Heaviest Stack Trace**
7. 找到耗时最多的函数

### 内存泄漏检测

1. 按 **⌘I** 或 Product → Profile
2. 选择 **Leaks**
3. 录制一段时间
4. 查看是否有红色的 Leaks
5. 双击 Leak 查看调用栈

## 🐛 常见调试场景

### 场景1：播放器无法打开文件

**设置断点**: 
```
src/core/player_core.cpp:59  
(avformat_open_input 之后)
```

**检查**:
```lldb
p filename.c_str()  // 查看文件路径是否正确
p format_ctx_       // 查看是否为 NULL
```

### 场景2：视频显示异常

**设置断点**:
```
src/core/player_core.cpp:435
(video_thread 中)
```

**检查**:
```lldb
p frame->width
p frame->height
p frame->format
p pts  // 时间戳是否正常
```

### 场景3：音频卡顿

**设置断点**:
```
src/core/player_core.cpp:530
(audio_callback_impl)
```

**检查**:
```lldb
p len  // 请求的音频数据长度
p audio_buf_index_  // 当前缓冲区位置
p audio_buf_size_   // 缓冲区大小
```

## 💡 调试技巧

### 1. 条件断点

右键点击断点 → **Edit Breakpoint** → **Condition**:

```cpp
// 只在错误状态时暂停
state_ == PlayerState::Error

// 只在 PTS 大于 10 秒时暂停
pts > 10.0

// 只在视频流有效时暂停
video_stream_ >= 0
```

### 2. 日志断点

不暂停程序，只打印日志：

1. 右键断点 → **Edit Breakpoint**
2. 勾选 **Automatically continue after evaluating actions**
3. 添加 Action → **Log Message**:
   ```
   Video PTS: @pts@, Width: @frame->width@
   ```

### 3. 符号断点

捕获所有对某个函数的调用：

1. Debug → Breakpoints → **Create Symbolic Breakpoint**
2. Symbol: `av_read_frame`
3. Module: 留空

### 4. 异常断点

捕获所有异常：

1. Debug → Breakpoints → **Create Exception Breakpoint**
2. Exception: **All**
3. Break: **On Throw**

## ⌨️ 快捷键速查

| 功能 | 快捷键 |
|------|--------|
| 运行 | ⌘R |
| 停止 | ⌘. |
| 构建 | ⌘B |
| 清理构建 | ⌘⇧K |
| Profile | ⌘I |
| 静态分析 | ⌘⇧B |
| 单步跳过 | F6 |
| 单步进入 | F7 |
| 继续执行 | F8 |
| 设置断点 | ⌘\ |
| 启用/禁用断点 | ⌘Y |
| 显示 Console | ⌘⇧C |
| 显示调试区域 | ⌘⇧Y |
| 快速打开 | ⌘⇧O |
| 查找 | ⌘F |

## 📚 更多资源

- **详细调试指南**: [XCODE_GUIDE.md](XCODE_GUIDE.md)
- **优化总结**: [XCODE_OPTIMIZATION_SUMMARY.md](XCODE_OPTIMIZATION_SUMMARY.md)
- **测试指南**: [TEST_GUIDE.md](TEST_GUIDE.md)
- **用户手册**: [USER_GUIDE.md](USER_GUIDE.md)

## ❓ 常见问题

### Q: Xcode 项目不存在怎么办？

A: 运行生成脚本：
```bash
./scripts/open_xcode.sh
# 或
./build.sh xcode
```

### Q: 编译失败，找不到头文件

A: 
1. Project → Build Settings
2. 搜索 "Header Search Paths"
3. 添加: `/opt/homebrew/include`

### Q: 链接失败

A:
1. Project → Build Settings  
2. 搜索 "Library Search Paths"
3. 添加: `/opt/homebrew/lib`

### Q: 无法查看某些变量

A: 可能是编译优化导致，使用 Debug 配置：
```bash
cd build/xcode
cmake ../.. -G Xcode -DCMAKE_BUILD_TYPE=Debug
```

## ✅ 检查清单

开始调试前：

- [ ] Xcode 项目已生成
- [ ] 测试视频已生成
- [ ] Scheme 选择正确
- [ ] 断点已设置
- [ ] Console 已打开（⌘⇧C）

调试时：

- [ ] 日志正常输出
- [ ] 断点正常触发
- [ ] 变量值正确显示
- [ ] 单步执行流畅

## 🎯 开始调试！

```bash
# 一键启动
./scripts/open_xcode.sh

# 在 Xcode 中按 ⌘R
# 开始你的调试之旅！
```

---

**祝调试顺利！** 🐛➡️✨
