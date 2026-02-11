# 开始播放时间功能说明

## 功能概述

类似 ffplay 的 `-ss` 参数，支持在打开文件后自动跳转到指定时间开始播放。

## 使用方法

### 方式 1：命令行参数

```bash
# 从头开始播放
./YXVodPlayer /path/to/video.mp4

# 从第 60 秒开始播放
./YXVodPlayer /path/to/video.mp4 60

# 从第 2 分 30 秒（150 秒）开始播放
./YXVodPlayer /path/to/video.mp4 150
```

### 方式 2：代码调用

```cpp
MainWindow window;

// 设置从第 120 秒开始播放
window.setStartTime(120.0);

// 打开文件（会自动 seek 到 120 秒）
window.openFile("/path/to/video.mp4");
```

### 方式 3：通过 PlayerCore 配置

```cpp
yxplayer::PlayerCore player;

// 设置配置
yxplayer::PlayerConfig config;
config.start_time = 180.0;  // 从 3 分钟开始
player.set_config(config);

// 打开文件
player.open("/path/to/video.mp4");
```

## 实现细节

### 工作流程（优化版，参考 ffplay）

```
1. 用户设置 start_time = 60.0 秒
   ↓
2. 调用 player->open(filename)
   ↓
3. 打开文件、查找流、创建队列
   ↓
4. ⚠️ 检查 config.start_time > 0
   ↓
5. ⚠️ 在启动线程前调用 avformat_seek_file(60秒)
   ↓
6. ⚠️ 清空解复用器缓冲区 avformat_flush()
   ↓
7. 启动 read_thread（从60秒位置开始读取）
   ↓
8. 启动解码线程（从60秒位置开始解码）
   ↓
9. 从 60 秒位置开始播放（无需再 seek，无浪费数据）
```

**⚠️ 与旧实现的区别：**
- **旧实现（❌）**：启动线程 → 解码 0-60s 数据 → seek → 清空队列（浪费！）
- **新实现（✅）**：seek 到 60s → 启动线程 → 直接从 60s 读取（高效！）

### 参考 ffplay

```bash
# ffplay 用法
ffplay -ss 60 video.mp4

# YXVodPlayer 等价用法
./YXVodPlayer video.mp4 60
```

## 注意事项

1. **有效性检查**：
   - 如果 `start_time < 0`，忽略（从头播放）
   - 如果 `start_time >= duration`，忽略（从头播放）

2. **Seek 精度**：
   - 使用 `AVSEEK_FLAG_BACKWARD`，会 seek 到最近的关键帧
   - 实际开始位置可能略早于指定时间

3. **性能影响**：
   - 启动时会有 100ms 延迟等待 seek 完成
   - 对用户体验影响微小

## 测试示例

```bash
# 测试 1：正常 seek
./YXVodPlayer test.mp4 30

# 测试 2：超出范围（应忽略）
./YXVodPlayer test.mp4 9999

# 测试 3：负数（应忽略）
./YXVodPlayer test.mp4 -10
```

## 日志输出

成功时（新的高效实现）：
```
[INFO] 配置了开始播放时间: 60.000000 秒，在启动线程前先 seek...
[INFO] 初始 seek 成功，将从 60.000000 秒开始播放
[INFO] 打开视频流...
[INFO] 视频流打开成功, 分辨率: 1920x1080
[INFO] 打开音频流...
[INFO] 音频流打开成功, 采样率: 48000 Hz
```

失败时：
```
[WARN] 开始播放时间 9999.000000 无效或超过视频时长，忽略
```

或

```
[WARN] 初始 seek 失败，将从头开始播放
```

## 性能对比

**旧实现（在启动线程后 seek）：**
- 需要读取和解码 0 ~ start_time 的所有数据包
- 然后清空队列（浪费 CPU 和 IO）
- 再重新 seek 和读取
- 启动速度慢，资源浪费

**新实现（参考 ffplay，在启动线程前 seek）：**
- 直接 seek 到目标位置
- read_thread 从一开始就读取正确的数据
- 无需解码无用数据
- 启动速度快，资源利用率高

**与 ffplay 对比：**

```bash
# ffplay 的 -ss 参数
ffplay -ss 60 video.mp4

# YXVodPlayer 的等价实现
./YXVodPlayer video.mp4 60
```

两者都会在启动线程前就 seek 到指定位置，性能和行为完全一致。

## 未来扩展

可以添加更多配置选项：
- `end_time`：结束播放时间（播放片段）
- `loop_start_time` / `loop_end_time`：AB 循环播放
- `seek_by_bytes`：按字节 seek（流媒体场景）
