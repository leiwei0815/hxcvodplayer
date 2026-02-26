# macOS 窗口和 Auto Layout 优化

## 🎯 优化内容

### 1. 窗口大小调整

**修改前**: 600x560  
**修改后**: **1280x720** (标准 720p 分辨率)

**修改文件**: `examples/macos-test/Sources/Main.storyboard`

```xml
<!-- 第 46 行 -->
<rect key="contentRect" x="196" y="240" width="1280" height="720"/>
```

### 2. 使用 Auto Layout 约束布局

**优势**:
- ✅ 窗口拉伸时界面自适应
- ✅ 视频区域自动调整大小
- ✅ 控件位置相对固定，保持美观
- ✅ 支持不同屏幕尺寸

## 📐 布局结构（1280x720）

```
┌─────────────────────────────────────────────────────────────┐
│                    HXCPlayer macOS 测试                      │ ← 顶部 20px
│                                                             │
│  ┌───────────────────────────────────────────────────────┐  │
│  │                                                       │  │
│  │                                                       │  │
│  │                  [视频显示区域]                        │  │ ← 距顶 60px
│  │                 (自动拉伸)                             │  │   距底 240px
│  │                                                       │  │   左右边距 20px
│  │                                                       │  │
│  │                                                       │  │
│  └───────────────────────────────────────────────────────┘  │
│                                                             │
│                    00:00 / 00:00                            │ ← 时间标签
│                 ═════════●══════════                        │ ← 进度条
│                                                             │
│            [播放]  [1.0x]  [适应]                           │ ← 控制按钮（居中）
│                                                             │
│  ✅ 支持播放进度拖拽  ✅ 支持变速播放...                       │ ← 功能说明
│                                                             │
│  音量: 100%  ═════════●═════════                            │ ← 音量控制
│                                                             │ ← 底部 80px
└─────────────────────────────────────────────────────────────┘
```

## 🔧 约束布局详解

### 1. 视频视图约束 (Video View)

```objc
videoView.translatesAutoresizingMaskIntoConstraints = NO;

[NSLayoutConstraint activateConstraints:@[
    [videoView.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:60],
    [videoView.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:20],
    [videoView.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-20],
    [videoView.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-240]
]];
```

**效果**:
- 距顶部 60px（为状态标签留空间）
- 左右边距各 20px
- 距底部 240px（为控件留空间）
- **自动拉伸**: 窗口变大时，视频区域自动增大

### 2. 状态标签约束 (Status Label)

```objc
statusLabel.translatesAutoresizingMaskIntoConstraints = NO;

[NSLayoutConstraint activateConstraints:@[
    [statusLabel.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:20],
    [statusLabel.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
    [statusLabel.widthAnchor constraintGreaterThanOrEqualToConstant:300],
    [statusLabel.heightAnchor constraintEqualToConstant:30]
]];
```

**效果**:
- 距顶部 20px
- **水平居中**: 窗口变宽时始终居中
- 最小宽度 300px
- 固定高度 30px

### 3. 进度条约束 (Progress Slider)

```objc
[NSLayoutConstraint activateConstraints:@[
    [progressSlider.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-200],
    [progressSlider.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:40],
    [progressSlider.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-40],
    [progressSlider.heightAnchor constraintEqualToConstant:25]
]];
```

**效果**:
- 距底部 200px（固定位置）
- 左右边距各 40px
- **自动拉伸**: 窗口变宽时，进度条自动变长
- 固定高度 25px

### 4. 时间标签约束 (Time Label)

```objc
[NSLayoutConstraint activateConstraints:@[
    [timeLabel.bottomAnchor constraintEqualToAnchor:progressSlider.topAnchor constant:-5],
    [timeLabel.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
    [timeLabel.heightAnchor constraintEqualToConstant:20]
]];
```

**效果**:
- **相对定位**: 位于进度条上方 5px
- **水平居中**: 始终在窗口中心
- 固定高度 20px

### 5. 按钮组约束 (Button Container)

```objc
// 创建按钮容器
NSView *buttonContainer = [[NSView alloc] init];
buttonContainer.translatesAutoresizingMaskIntoConstraints = NO;

[NSLayoutConstraint activateConstraints:@[
    [buttonContainer.bottomAnchor constraintEqualToAnchor:progressSlider.topAnchor constant:-50],
    [buttonContainer.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor],
    [buttonContainer.heightAnchor constraintEqualToConstant:40]
]];

// 按钮约束（相对于容器）
[NSLayoutConstraint activateConstraints:@[
    // 播放按钮
    [playPauseButton.leadingAnchor constraintEqualToAnchor:buttonContainer.leadingAnchor],
    [playPauseButton.widthAnchor constraintEqualToConstant:120],
    
    // 变速按钮
    [speedButton.leadingAnchor constraintEqualToAnchor:playPauseButton.trailingAnchor constant:20],
    [speedButton.widthAnchor constraintEqualToConstant:120],
    
    // 比例按钮
    [aspectRatioButton.leadingAnchor constraintEqualToAnchor:speedButton.trailingAnchor constant:20],
    [aspectRatioButton.widthAnchor constraintEqualToConstant:120]
]];
```

**效果**:
- **整组居中**: 三个按钮作为一个整体水平居中
- 按钮间距 20px
- 每个按钮固定宽度 120px
- **位置固定**: 窗口变宽时，按钮组始终居中

### 6. 音量控制约束

```objc
// 音量标签
[NSLayoutConstraint activateConstraints:@[
    [volumeLabel.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor constant:-80],
    [volumeLabel.leadingAnchor constraintEqualToAnchor:self.view.leadingAnchor constant:40],
    [volumeLabel.widthAnchor constraintEqualToConstant:100]
]];

// 音量滑块
[NSLayoutConstraint activateConstraints:@[
    [volumeSlider.centerYAnchor constraintEqualToAnchor:volumeLabel.centerYAnchor],
    [volumeSlider.leadingAnchor constraintEqualToAnchor:volumeLabel.trailingAnchor constant:10],
    [volumeSlider.trailingAnchor constraintEqualToAnchor:self.view.trailingAnchor constant:-40]
]];
```

**效果**:
- 标签固定在左侧，宽度 100px
- 滑块距标签 10px
- **滑块自动拉伸**: 窗口变宽时，滑块自动变长
- 垂直对齐标签

## 🎯 关键技术点

### 1. 禁用 AutoresizingMask

```objc
view.translatesAutoresizingMaskIntoConstraints = NO;
```

**必须**为所有使用约束的视图设置此属性，否则约束不生效。

### 2. 激活约束

```objc
[NSLayoutConstraint activateConstraints:@[
    // 约束数组
]];
```

使用 `activateConstraints:` 批量激活约束，性能更好。

### 3. 约束类型

| 约束类型 | 用途 | 示例 |
|---------|------|------|
| `topAnchor` | 顶部对齐 | 状态标签距顶部 20px |
| `bottomAnchor` | 底部对齐 | 进度条距底部 200px |
| `leadingAnchor` | 左边对齐 | 视频视图左边距 20px |
| `trailingAnchor` | 右边对齐 | 视频视图右边距 20px |
| `centerXAnchor` | 水平居中 | 状态标签居中 |
| `centerYAnchor` | 垂直居中 | 音量滑块与标签对齐 |
| `widthAnchor` | 宽度约束 | 按钮宽度 120px |
| `heightAnchor` | 高度约束 | 按钮高度 40px |

### 4. 相对约束 vs 绝对约束

**绝对约束** (距离边界):
```objc
[view.topAnchor constraintEqualToAnchor:self.view.topAnchor constant:20]
```

**相对约束** (相对其他视图):
```objc
[timeLabel.bottomAnchor constraintEqualToAnchor:progressSlider.topAnchor constant:-5]
```

**最佳实践**: 使用相对约束使布局更灵活。

### 5. 容器视图技巧

对于需要**整组居中**的控件，使用容器视图：

```objc
// 1. 创建容器
NSView *container = [[NSView alloc] init];

// 2. 容器居中
[container.centerXAnchor constraintEqualToAnchor:self.view.centerXAnchor]

// 3. 子视图相对容器布局
[button1.leadingAnchor constraintEqualToAnchor:container.leadingAnchor]
[button2.leadingAnchor constraintEqualToAnchor:button1.trailingAnchor constant:20]
```

## 📊 布局效果对比

### 窗口拉伸效果

| 窗口宽度 | 视频宽度 | 进度条宽度 | 按钮位置 |
|---------|---------|-----------|---------|
| 1280px | 1240px (1280-40) | 1200px (1280-80) | 居中 |
| 1600px | 1560px (1600-40) | 1520px (1600-80) | 居中 |
| 2000px | 1960px (2000-40) | 1920px (2000-80) | 居中 |

**结论**: 
- ✅ 视频区域和进度条**自动拉伸**
- ✅ 按钮组**始终居中**
- ✅ 边距保持一致

### 窗口高度变化

| 窗口高度 | 视频高度 | 控制区域 |
|---------|---------|---------|
| 720px | 420px (720-60-240) | 240px（底部固定） |
| 900px | 600px (900-60-240) | 240px（底部固定） |
| 1080px | 780px (1080-60-240) | 240px（底部固定） |

**结论**:
- ✅ 视频高度**自动调整**
- ✅ 控制区域高度**保持固定**（240px）
- ✅ 所有控件位置**相对稳定**

## ✅ 测试验证

### 1. 编译运行

```bash
cd examples/macos-test
xcodebuild -project HXCPlayerMacOSTest.xcodeproj \
           -scheme HXCPlayerMacOSTest \
           build
```

或在 Xcode 中打开并运行。

### 2. 测试窗口拉伸

- ✅ 启动应用，初始窗口为 1280x720
- ✅ 拖动窗口边缘放大窗口
  - 观察视频区域自动增大
  - 观察进度条自动拉长
  - 观察按钮组保持居中
  - 观察音量滑块自动拉长
- ✅ 缩小窗口
  - 所有控件按比例缩小
  - 布局保持美观

### 3. 最小窗口尺寸

虽然没有设置最小尺寸约束，但建议窗口至少为 **800x600** 以保证良好的用户体验。

可以在 Storyboard 中添加最小尺寸限制：
```xml
<window ... minSize="800,600">
```

## 🎨 视觉改进

### 窗口大小对比

**修改前** (600x560):
- ❌ 视频区域小（约 400x300）
- ❌ 控件拥挤
- ❌ 不适合观看视频

**修改后** (1280x720):
- ✅ 标准 720p 分辨率
- ✅ 视频区域大（约 1240x420）
- ✅ 控件间距合理
- ✅ 观看体验优秀

### 布局优势

**使用 Auto Layout 前**:
- ❌ 固定位置和大小
- ❌ 窗口拉伸后变形
- ❌ 控件位置错乱

**使用 Auto Layout 后**:
- ✅ 自适应布局
- ✅ 窗口拉伸时美观
- ✅ 控件位置始终合理
- ✅ 支持不同屏幕尺寸

## 📝 最佳实践总结

1. **视频区域**: 使用四边约束，自动填充可用空间
2. **标题/标签**: 使用 centerX 约束，保持居中
3. **进度条**: 左右约束到边界，自动拉伸
4. **按钮组**: 使用容器视图，整组居中
5. **音量控制**: 标签固定宽度，滑块自动拉伸
6. **底部控件**: 使用 bottomAnchor，固定距底部距离

## 🚀 进一步优化建议

1. **添加最小窗口尺寸**: 防止窗口过小导致控件重叠
2. **响应式布局**: 窗口过小时自动调整控件大小或隐藏部分控件
3. **保存窗口尺寸**: 使用 `NSUserDefaults` 记住用户的窗口大小偏好
4. **全屏支持**: 添加全屏按钮和全屏模式适配

## 📚 相关资源

- [Apple Auto Layout Guide](https://developer.apple.com/library/archive/documentation/UserExperience/Conceptual/AutolayoutPG/)
- [NSLayoutConstraint 文档](https://developer.apple.com/documentation/appkit/nslayoutconstraint)
- [NSLayoutAnchor 文档](https://developer.apple.com/documentation/appkit/nslayoutanchor)
