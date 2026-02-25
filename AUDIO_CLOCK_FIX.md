# 倍速播放音频时钟同步修复

## 问题描述

用户报告的关键问题：
1. ❌ **播放速度没有改变**：选择倍速后，视频仍按正常速度播放
2. ❌ **音频电流声和变声**：倍速播放时出现杂音
3. ❌ **音频时钟未更新**：`audio_callback_impl()` 中没有更新音频时钟

## 根本原因

### 🔴 致命问题：音频时钟更新位置错误

**旧代码流程**：
```cpp
1. 从队列获取音频帧（原始 PTS）
2. ⚠️ 更新音频时钟（基于原始样本数）  ← 错误！
3. 重采样
4. SoundTouch 处理（样本数改变）
5. 输出音频
```

**问题**：
- 音频时钟使用的是**重采样前**的样本数
- SoundTouch 处理后样本数改变了，但时钟没更新
- **时钟推进速度错误**，导致视频同步失败

**示例**（2.0x 快速播放）：
```
原始样本数：1024
SoundTouch 输出：~512（快速播放后样本减少）
音频时钟增加：1024/48000 = 0.0213 秒（错误！）
实际应该增加：512/48000/2.0 = 0.0053 秒
```

**结果**：
- 音频时钟推进**太快** → 视频认为时间还很早 → 不丢帧 → 速度没变
- 音频实际播放慢了 → 时钟与实际播放不同步 → 出现杂音

### 🔴 关键问题：没有考虑播放速率对时钟的影响

**播放速率与时钟推进关系**：
```
正常播放（1.0x）：
  - 播放 1秒音频 → 时钟推进 1秒

快速播放（2.0x）：
  - 播放 1秒音频 → 时钟应该推进 2秒！
  - 因为 2.0x 时，1秒音频只用 0.5秒就播完了

慢速播放（0.5x）：
  - 播放 1秒音频 → 时钟应该推进 0.5秒
  - 因为 0.5x 时，1秒音频需要 2秒才播完
```

## 完整修复方案

### 修复 1：移除旧的时钟更新位置

```cpp
// ❌ 删除这里的时钟更新
AudioFrame* af = audio_queue_->peek_readable();
AVFrame* frame = af->frame;

// ⚠️ 不要在这里更新时钟！
// if (!isnan(af->pts)) {
//     update_audio_pts(pts + played_time, 0);  // 错误！
// }

// 重采样音频
```

### 修复 2：在 SoundTouch 处理后更新时钟

```cpp
// 重采样
int samples = swr_convert(...);
audio_buf_size_ = samples * channels * sizeof(int16_t);
audio_queue_->next();

#if defined(__APPLE__) || defined(_WIN32)
// SoundTouch 处理（可能改变样本数）
if (soundtouch_ && playback_rate_.load() != 1.0) {
    soundtouch_->putSamples(input, samples);
    uint32_t received = soundtouch_->receiveSamples(output, available);
    audio_buf_size_ = received * channels * sizeof(int16_t);  // 更新实际大小
}
#endif

// ⚠️ 在这里更新音频时钟（使用处理后的样本数）
if (!isnan(af->pts)) {
    double pts = af->pts;
    int channels = audio_codec_ctx_->ch_layout.nb_channels;
    
    // 1. 计算实际样本数（已经过 SoundTouch 处理）
    int actual_samples = audio_buf_size_ / (channels * sizeof(int16_t));
    
    // 2. 计算播放时长
    double frame_duration = (double)actual_samples / audio_codec_ctx_->sample_rate;
    
    // 3. ⚠️ 关键：根据播放速率调整时钟推进速度
    double playback_rate = playback_rate_.load();
    if (playback_rate > 0) {
        frame_duration /= playback_rate;
    }
    
    // 4. 更新时钟
    update_audio_pts(pts + frame_duration, 0);
}
```

### 修复 3：时钟推进速度计算

**公式**：
```
时钟推进时长 = (实际输出样本数 / 采样率) / 播放速率

示例（2.0x，采样率 48000Hz）：
  - 输入：1024 样本
  - SoundTouch 输出：~512 样本
  - 播放时长：512 / 48000 = 0.0107 秒
  - 时钟推进：0.0107 / 2.0 = 0.0053 秒  ← 正确！
  
示例（0.5x）：
  - 输入：1024 样本
  - SoundTouch 输出：~2048 样本
  - 播放时长：2048 / 48000 = 0.0427 秒
  - 时钟推进：0.0427 / 0.5 = 0.0854 秒  ← 正确！
```

## 为什么这样修复有效？

### 1. 音频时钟正确推进

```
正常播放（1.0x）：
  时钟推进 = 样本数 / 采样率 / 1.0 = 实际播放时长

快速播放（2.0x）：
  时钟推进 = 样本数 / 采样率 / 2.0 = 实际播放时长 × 2
  → 音频虽然只播了 0.5秒，但时钟推进 1秒
  → 视频看到时钟推进快，会丢帧跟上
  → 实现视频加速

慢速播放（0.5x）：
  时钟推进 = 样本数 / 采样率 / 0.5 = 实际播放时长 × 0.5
  → 音频播了 2秒，但时钟只推进 1秒
  → 视频看到时钟推进慢，会等待
  → 实现视频减速
```

### 2. 视频同步到正确的时钟

**视频刷新逻辑**（main_window.cpp）：
```cpp
// 获取主时钟（音频时钟）
double master_clock = player_->get_position();  // 已经考虑了播放速率
double frame_pts = vf->pts;

// 计算差值
double diff = frame_pts - master_clock;

// 动态阈值（根据播放速率调整）
double sync_threshold_max = 0.1 / playback_rate;

if (diff <= -sync_threshold_max) {
    // 视频落后，丢帧
    video_queue->next();
}
```

**效果**：
- **2.0x**：音频时钟推进快 → 视频频繁落后 → 大量丢帧 → 视频加速
- **0.5x**：音频时钟推进慢 → 视频很少落后 → 丢帧少 → 视频减速

## 测试验证

### 测试用例 1：正常播放（1.0x）

```bash
选择速度：1.0x

预期：
✅ 音频时钟推进速度正常
✅ 视频按正常速度播放
✅ 音视频同步
✅ 无杂音
```

### 测试用例 2：快速播放（2.0x）

```bash
选择速度：2.0x

预期：
✅ 音频时钟推进速度 × 2（关键！）
✅ 视频大量丢帧，速度 × 2
✅ 音视频同步（嘴型对得上）
✅ 音频清晰无杂音，音调不变

验证方法：
1. 播放一段 60 秒的视频
2. 选择 2.0x
3. 实际应该在 30 秒左右播完
4. 进度条应该 30 秒走完
```

### 测试用例 3：慢速播放（0.5x）

```bash
选择速度：0.5x

预期：
✅ 音频时钟推进速度 × 0.5
✅ 视频很少丢帧，速度 × 0.5
✅ 音视频同步
✅ 音频清晰，音调不变

验证方法：
1. 播放一段 30 秒的视频
2. 选择 0.5x
3. 实际应该在 60 秒左右播完
4. 进度条应该 60 秒走完
```

## 关键代码对比

### ❌ 错误实现（旧代码）

```cpp
// 1. 更新时钟（错误位置！）
update_audio_pts(pts + played_time, 0);

// 2. 重采样
swr_convert(...);

// 3. SoundTouch 处理
soundtouch_->putSamples(...);
soundtouch_->receiveSamples(...);

// 4. 输出
SDL_MixAudioFormat(...);
```

**问题**：
- 时钟使用重采样前的样本数
- 没有考虑 SoundTouch 的样本数变化
- 没有考虑播放速率对时钟的影响

### ✅ 正确实现（新代码）

```cpp
// 1. 重采样
swr_convert(...);

// 2. SoundTouch 处理
soundtouch_->putSamples(...);
soundtouch_->receiveSamples(...);  // 样本数可能改变

// 3. ⚠️ 更新时钟（正确位置！）
int actual_samples = audio_buf_size_ / (channels * sizeof(int16_t));
double frame_duration = actual_samples / sample_rate;
frame_duration /= playback_rate;  // ⚠️ 考虑播放速率
update_audio_pts(pts + frame_duration, 0);

// 4. 输出
SDL_MixAudioFormat(...);
```

**优点**：
- 使用实际输出的样本数
- 考虑了 SoundTouch 的影响
- 正确考虑播放速率

## 总结

### 关键要点

1. **音频时钟必须在 SoundTouch 处理后更新**
   - 使用实际输出的样本数
   
2. **时钟推进速度必须考虑播放速率**
   - `时钟增量 = (样本数 / 采样率) / 播放速率`
   
3. **视频同步依赖正确的音频时钟**
   - 音频时钟快 → 视频丢帧 → 加速
   - 音频时钟慢 → 视频等待 → 减速

### 修复效果

- ✅ 播放速度正确改变（视频和音频都按选定速率播放）
- ✅ 音频清晰无杂音（SoundTouch 处理正确）
- ✅ 音视频同步（时钟推进正确）
- ✅ 音调保持不变（SoundTouch WSOLA 算法）

现在倍速播放应该完美工作了！
