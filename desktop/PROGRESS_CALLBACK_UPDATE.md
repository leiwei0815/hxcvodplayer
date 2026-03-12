# 播放进度回调更新说明

## 📝 更新内容

### 1. 移除了定时器更新机制
- **移除**：`update_timer_`（原本用于每 100ms 更新进度条）
- **原因**：使用播放器核心的回调机制更精确、更高效

### 2. 添加播放进度回调
```cpp
player_->set_position_changed_callback([this](double position) {
    QMetaObject::invokeMethod(this, [this, position]() {
        updateProgress(position);
    });
});
```

**特点**：
- 由播放器核心直接触发，时间更精确
- 根据实际播放位置更新，不会有延迟
- 使用 `QMetaObject::invokeMethod` 确保线程安全

### 3. 添加播放完成回调
```cpp
player_->set_playback_completed_callback([this]() {
    QMetaObject::invokeMethod(this, [this]() {
        onPlaybackCompleted();
    });
});
```

**功能**：
- 播放结束时自动触发
- 停止视频刷新定时器
- 重置播放按钮状态为"播放"

## 🔧 实现细节

### `updateProgress(double position)` 方法
```cpp
void MainWindow::updateProgress(double position) {
    if (!player_) return;
    
    // 拖动时跳过更新，避免干扰
    if (is_seeking_) {
        return;
    }
    
    double duration = player_->get_duration();
    
    if (duration > 0) {
        // 更新进度条
        int value = static_cast<int>((position / duration) * 1000);
        int current_value = ui->seekSlider->value();
        
        // 只有值变化超过阈值时才更新，避免小幅抖动
        const int threshold = 2;  // 允许 0.2% 的误差
        if (std::abs(value - current_value) > threshold) {
            ui->seekSlider->blockSignals(true);
            ui->seekSlider->setValue(value);
            ui->seekSlider->blockSignals(false);
        }
        
        // 更新时间标签
        ui->timeLabel->setText(formatTime(position) + " / " + formatTime(duration));
    }
}
```

**优化点**：
- ✅ 保留了 `is_seeking_` 检查，避免拖动时冲突
- ✅ 保留了阈值检查，避免微小抖动
- ✅ 使用 `blockSignals` 避免触发不必要的信号

### `onPlaybackCompleted()` 方法
```cpp
void MainWindow::onPlaybackCompleted() {
    qDebug() << "播放完成";
    
    // 停止视频刷新
    if (refresh_timer_) {
        refresh_timer_->stop();
    }
    
    // 重置播放按钮为"播放"状态
    ui->playPauseButton->setText("播放");
    
    // 可选：将进度条移到开头或结尾
    // ui->seekSlider->setValue(0);  // 移到开头
    // ui->seekSlider->setValue(1000);  // 停在结尾
}
```

## 📊 架构改进

### 之前（定时器驱动）
```
┌─────────────┐
│ QTimer      │ 100ms 轮询
│ update_timer│────────────┐
└─────────────┘            │
                           ▼
                    ┌──────────────┐
                    │ updateUI()   │
                    │ get_position │ ← 主动查询
                    └──────────────┘
```

### 现在（事件驱动）
```
┌──────────────┐
│ PlayerCore   │
│ 播放线程      │
└──────┬───────┘
       │ position 变化时触发
       ▼
┌──────────────────────┐
│ position_changed_    │ 回调
│ callback_            │
└──────┬───────────────┘
       │ 线程安全调用
       ▼
┌──────────────────────┐
│ QMetaObject::        │
│ invokeMethod         │
└──────┬───────────────┘
       │ 在主线程执行
       ▼
┌──────────────────────┐
│ updateProgress()     │
│ 更新 UI              │
└──────────────────────┘
```

## ✅ 优势

1. **更精确**：直接使用播放器的实际位置，无需轮询
2. **更高效**：只在位置变化时更新，减少不必要的 UI 刷新
3. **更简洁**：移除了 `update_timer_`，代码更清晰
4. **更及时**：播放完成立即响应，不需要等待定时器周期

## 🔍 保留的部分

- **`refresh_timer_`**：仍然保留，用于视频帧刷新（这是高频操作，需要定时器）
- **`updateUI()`**：标记为已弃用但保留，以防有其他地方引用

## 🚀 使用建议

1. **播放进度更新**：完全由 `set_position_changed_callback` 驱动
2. **播放完成处理**：在 `onPlaybackCompleted()` 中添加自定义逻辑（如循环播放、列表播放等）
3. **拖动进度条**：现有的 `is_seeking_` 机制仍然有效，不会与回调冲突

## 📌 注意事项

- 所有回调都使用 `QMetaObject::invokeMethod` 确保线程安全
- 回调函数在播放器线程触发，UI 更新在主线程执行
- `is_seeking_` 标志保护拖动操作不受回调干扰
