# 播放器 UI 功能说明

## 🎬 新增功能

### iOS 版本 (`examples/ios-test/Sources/ViewController.m`)

#### 1. 播放进度条 (Progress Slider)
- **位置**: 视频下方
- **功能**: 
  - 显示当前播放进度
  - 拖拽快进/快退
  - 实时显示拖拽位置的时间
- **实现**: 
  - 使用 `UISlider`
  - 拖拽时设置 `isSeeking` 标志防止自动更新
  - 松手时调用 `seekToPosition:`

#### 2. 时间显示 (Time Label)
- **位置**: 进度条下方
- **格式**: `当前时间 / 总时长`
- **示例**: `01:23 / 05:47` 或 `01:23:45 / 02:30:00`
- **功能**: 
  - 自动根据时长决定显示格式（小于1小时显示 MM:SS，否则显示 HH:MM:SS）
  - 拖拽进度条时实时更新

#### 3. 播放/暂停按钮
- **动态标题**: 
  - 未播放/暂停时显示 "播放"
  - 播放中显示 "暂停"
- **功能**: 切换播放状态

#### 4. 变速播放按钮
- **支持速度**: 0.5x, 0.75x, 1.0x, 1.25x, 1.5x, 2.0x
- **循环切换**: 点击按钮依次切换速度
- **显示**: 按钮显示当前速度（如 "1.5x"）

#### 5. 视频比例按钮
- **支持模式**:
  - **适应 (Fit)**: 保持比例，完整显示视频
  - **填充 (Fill)**: 保持比例，填满屏幕（可能裁剪）
  - **拉伸 (Stretch)**: 拉伸填满屏幕（可能变形）
- **循环切换**: 点击按钮依次切换模式

#### 6. 音量滑块 (Volume Slider)
- **范围**: 0% - 100%
- **位置**: 控制按钮下方
- **功能**: 
  - 实时调节音量
  - 标签显示当前音量百分比
- **实现**: 使用 `player.volume` 属性（0.0-1.0）

### macOS 版本 (`examples/macos-test/Sources/ViewController.m`)

功能与 iOS 版本完全相同，但使用 macOS 原生控件：
- `NSSlider` 替代 `UISlider`
- `NSButton` 替代 `UIButton`
- `NSTextField` 替代 `UILabel`

## 📐 UI 布局

### iOS 布局（从上到下）
```
┌──────────────────────────────┐
│   HXCPlayer iOS 测试          │  状态标签
├──────────────────────────────┤
│                              │
│      [视频显示区域]           │  250px 高度
│                              │
├──────────────────────────────┤
│  ═══════════●═══════         │  进度条
│     01:23 / 05:47            │  时间显示
├──────────────────────────────┤
│  [播放] [1.0x] [适应]         │  控制按钮
├──────────────────────────────┤
│  音量: ═════●════            │  音量控制
├──────────────────────────────┤
│  ✅ 功能说明...               │
└──────────────────────────────┘
```

### macOS 布局（从上到下）
```
┌────────────────────────────────────┐
│     HXCPlayer macOS 测试            │  状态标签
├────────────────────────────────────┤
│                                    │
│                                    │
│        [视频显示区域]               │  360px 高度
│                                    │
│                                    │
├────────────────────────────────────┤
│  ════════════●═══════════          │  进度条
│        01:23 / 05:47               │  时间显示
├────────────────────────────────────┤
│    [播放]   [1.0x]   [适应]         │  控制按钮
├────────────────────────────────────┤
│  音量: 100%  ═════●════════         │  音量控制
├────────────────────────────────────┤
│        ✅ 功能说明...               │
└────────────────────────────────────┘
```

## 🎯 代理回调 (Delegate)

### 实现的代理方法

```objc
@interface ViewController () <HXCPlayerControlDelegate>
@end

// 1. 状态变化回调
- (void)playerDidChangeState:(HXCPlayerState)state {
    // 更新状态标签和播放/暂停按钮
    // 状态: Idle, Opening, Playing, Paused, Stopped, Error
}

// 2. 进度更新回调
- (void)playerDidUpdatePosition:(double)position duration:(double)duration {
    // 更新进度条和时间显示
    // 如果正在拖拽（isSeeking）则不更新
}

// 3. 错误回调
- (void)playerDidEncounterError:(NSError *)error {
    // 显示错误信息
}
```

## 🔧 关键实现细节

### 1. 进度条拖拽处理

**iOS:**
```objc
// 开始拖拽
- (void)progressSliderTouchDown:(UISlider *)slider {
    self.isSeeking = YES;  // 设置标志，停止自动更新
}

// 拖拽中
- (void)progressSliderValueChanged:(UISlider *)slider {
    // 只更新时间显示，不seek
    if (self.isSeeking && self.player.duration > 0) {
        double position = slider.value * self.player.duration;
        self.timeLabel.text = [self formatTime:position] + " / " + ...;
    }
}

// 结束拖拽
- (void)progressSliderTouchUp:(UISlider *)slider {
    // 真正执行 seek
    double position = slider.value * self.player.duration;
    [self.player seekToPosition:position];
    self.isSeeking = NO;  // 恢复自动更新
}
```

**macOS:**
```objc
// macOS 使用 continuous 模式，直接在 action 中处理
- (void)progressSliderChanged:(NSSlider *)slider {
    if (self.player.duration > 0) {
        double position = slider.doubleValue * self.player.duration;
        [self.player seekToPosition:position];
    }
}
```

### 2. 时间格式化

```objc
- (NSString *)formatTime:(double)seconds {
    int hours = (int)seconds / 3600;
    int minutes = ((int)seconds % 3600) / 60;
    int secs = (int)seconds % 60;
    
    if (hours > 0) {
        return [NSString stringWithFormat:@"%02d:%02d:%02d", hours, minutes, secs];
    } else {
        return [NSString stringWithFormat:@"%02d:%02d", minutes, secs];
    }
}
```

### 3. 循环切换功能

```objc
// 变速播放
- (void)toggleSpeed {
    static NSArray *speeds = @[@0.5, @0.75, @1.0, @1.25, @1.5, @2.0];
    static NSInteger currentIndex = 2; // 默认 1.0x
    
    currentIndex = (currentIndex + 1) % speeds.count;
    double speed = [speeds[currentIndex] doubleValue];
    
    [self.player setPlaybackRate:speed];
    // 更新按钮标题...
}

// 视频比例
- (void)toggleAspectRatio {
    static HXCAspectRatioMode modes[] = {
        HXCAspectRatioModeFit,
        HXCAspectRatioModeFill,
        HXCAspectRatioModeStretch
    };
    static NSString *modeNames[] = {@"适应", @"填充", @"拉伸"};
    static NSInteger currentIndex = 0;
    
    currentIndex = (currentIndex + 1) % 3;
    self.player.aspectRatioMode = modes[currentIndex];
    // 更新按钮标题...
}
```

## ✅ 测试功能

### 1. 编译并运行

**iOS:**
```bash
cd examples/ios-test
xcodebuild -project HXCPlayerIOSTest.xcodeproj \
           -scheme HXCPlayerIOSTest \
           -destination 'platform=iOS Simulator,name=iPhone 17' \
           build
```

**macOS:**
```bash
cd examples/macos-test
xcodebuild -project HXCPlayerMacOSTest.xcodeproj \
           -scheme HXCPlayerMacOSTest \
           build
```

或直接在 Xcode 中打开并运行。

### 2. 测试场景

- ✅ 点击"播放"开始播放，按钮变为"暂停"
- ✅ 观察进度条自动前进
- ✅ 拖拽进度条，观察视频跳转
- ✅ 点击"1.0x"切换播放速度，观察视频加速/减速
- ✅ 点击"适应"切换视频比例，观察视频缩放方式
- ✅ 拖动音量滑块，观察音量变化
- ✅ 观察时间标签实时更新

## 🎨 样式说明

### iOS 按钮样式
- 圆角: 8px (`layer.cornerRadius`)
- 背景色: 系统颜色（蓝色/橙色/绿色）
- 文字: 白色
- 高度: 44px

### macOS 按钮样式
- 系统圆角按钮 (`NSBezelStyleRounded`)
- 使用系统默认样式
- 高度: 36px

### 滑块样式
- iOS: `UISlider` 使用系统默认样式
- macOS: `NSSlider` 使用系统默认样式

## 📝 注意事项

1. **进度更新频率**: 通过代理回调自动更新，无需手动定时器
2. **拖拽优化**: iOS 使用 `isSeeking` 标志避免拖拽时的冲突
3. **内存管理**: `dealloc` 时调用 `[player stop]` 释放资源
4. **线程安全**: UI 更新在代理回调中进行，已在主线程
5. **状态同步**: 通过代理回调保持 UI 与播放器状态同步

## 🚀 扩展建议

如需进一步扩展，可以添加：
- 全屏按钮
- 播放列表
- 字幕选择
- 音轨切换
- 截图功能
- 画中画模式（iOS/macOS）
- 手势控制（iOS: 滑动调节音量/亮度）
