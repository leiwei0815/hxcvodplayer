# Xcode 快速开始

## 一键启动

```bash
./scripts/open_xcode.sh
```

这个脚本会：
1. ✅ 检查并生成 Xcode 项目
2. ✅ 生成测试视频（如果需要）
3. ✅ 自动打开 Xcode

## 手动步骤

### 1. 生成 Xcode 项目

```bash
mkdir -p build/xcode
cd build/xcode
cmake ../.. -G Xcode -DCMAKE_BUILD_TYPE=Debug -DBUILD_DESKTOP=ON
```

### 2. 打开项目

```bash
open build/xcode/YXVodPlayer.xcodeproj
```

### 3. 在 Xcode 中配置

1. **选择 Scheme**: YXVodPlayer
2. **选择目标**: My Mac
3. **点击运行**: ⌘R

## 添加启动参数（可选）

打开测试视频：

1. Product → Scheme → Edit Scheme (⌘<)
2. Run → Arguments
3. Arguments Passed On Launch 添加：
   ```
   $(SRCROOT)/../../test_videos/test1_pattern.mp4
   ```

## 调试快捷键

| 快捷键 | 功能 |
|--------|------|
| ⌘R | 运行 |
| ⌘. | 停止 |
| F6 | 单步跳过 |
| F7 | 单步进入 |
| F8 | 继续 |
| ⌘\ | 设置断点 |
| ⌘Y | 启用/禁用所有断点 |

## 推荐断点位置

设置这些断点以调试关键功能：

### 文件打开
```
File: src/core/player_core.cpp
Function: PlayerCore::open
Line: ~50
```

### 视频解码
```
File: src/core/player_core.cpp  
Function: PlayerCore::video_thread
Line: ~430
```

### 音频回调
```
File: src/core/player_core.cpp
Function: PlayerCore::audio_callback_impl  
Line: ~525
```

## 查看日志

所有日志会显示在 Xcode 的 Console 中：

```
[INFO] 初始化 PlayerCore...
[INFO] SDL 初始化成功
[INFO] PlayerCore 初始化完成
[INFO] 正在打开文件: test_videos/test1_pattern.mp4
[INFO] 文件打开成功
...
```

## 性能分析

### CPU Profiler

```
Product → Profile (⌘I)
选择 "Time Profiler"
点击红色录制按钮
使用播放器
点击停止
查看调用栈和热点
```

### 内存泄漏检测

```
Product → Profile (⌘I)
选择 "Leaks"
点击红色录制按钮
使用播放器一段时间
查看是否有内存泄漏
```

## 常见问题

### Q: 编译失败，找不到头文件

A: 检查 Header Search Paths：
```
Project → Build Settings → Search Paths → Header Search Paths
添加: /opt/homebrew/include
```

### Q: 链接失败

A: 检查 Library Search Paths：
```
Project → Build Settings → Search Paths → Library Search Paths  
添加: /opt/homebrew/lib
```

### Q: 运行时找不到动态库

A: 设置环境变量：
```
Edit Scheme → Run → Environment Variables
DYLD_LIBRARY_PATH = /opt/homebrew/lib:/usr/local/lib
```

## 代码格式化

项目包含 `.clang-format` 文件。

使用：
```bash
# 格式化单个文件
clang-format -i src/core/player_core.cpp

# 格式化所有文件
find src include -name "*.cpp" -o -name "*.h" | xargs clang-format -i
```

## 静态分析

```
Product → Analyze (⌘⇧B)
```

Xcode 会检测：
- 内存泄漏
- 空指针解引用
- 未初始化变量
- 逻辑错误

## Git 集成

Xcode 内置 Git 支持：

```
Source Control → Commit (⌥⌘C)
Source Control → Push
Source Control → Pull
View → Navigators → Source Control Navigator
```

## 更多信息

- 详细调试指南: [XCODE_GUIDE.md](XCODE_GUIDE.md)
- 测试指南: [TEST_GUIDE.md](TEST_GUIDE.md)  
- 用户手册: [USER_GUIDE.md](USER_GUIDE.md)

---

**开始调试**: `./scripts/open_xcode.sh`
