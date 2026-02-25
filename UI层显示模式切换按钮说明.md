# UI 层添加视频显示模式切换按钮说明

## 功能概述

在 iOS 播放器 UI 层（`PlayerViewController`）添加了一个按钮，用于切换视频的显示模式（AspectRatioMode）。

## UI 布局

### 控制栏按钮布局

```
┌──────────────────────────────────────────────────────────┐
│                     播放器视频区域                          │
│                                                            │
└──────────────────────────────────────────────────────────┘
┌──────────────────────────────────────────────────────────┐
│  [播放]                          [1.0x] [适应]            │ ← 第一行按钮
│  ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━                │ ← 进度条
│  00:00 / 05:30            ━━━━━━━━━━━━━                  │ ← 时间 + 音量
└──────────────────────────────────────────────────────────┘
```

### 按钮位置

- **左上角**: 播放/暂停按钮
- **右上角（从右到左）**:
  1. **显示模式按钮** (新增) - "适应" / "填充"
  2. **倍速按钮** - "1.0x" / "1.5x" / "2.0x" / "0.5x"

## 实现细节

### 1. 添加按钮属性

```objc
@interface PlayerViewController () <PlayerCore_iOSDelegate>
// ... 其他属性 ...
@property (nonatomic, strong) UIButton *aspectRatioButton;  // 显示模式按钮
@end
```

### 2. 创建按钮

```objc
- (void)setupUI {
    // ... 其他 UI 创建代码 ...
    
    // 显示模式按钮
    _aspectRatioButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [_aspectRatioButton setTitle:@"适应" forState:UIControlStateNormal];
    _aspectRatioButton.tintColor = [UIColor whiteColor];
    _aspectRatioButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_aspectRatioButton addTarget:self 
                           action:@selector(aspectRatioButtonTapped:) 
                 forControlEvents:UIControlEventTouchUpInside];
    [controlBar addSubview:_aspectRatioButton];
}
```

### 3. 布局约束

```objc
// 显示模式按钮（在右上角）
[_aspectRatioButton.trailingAnchor constraintEqualToAnchor:controlBar.trailingAnchor constant:-16],
[_aspectRatioButton.topAnchor constraintEqualToAnchor:controlBar.topAnchor constant:16],
[_aspectRatioButton.widthAnchor constraintEqualToConstant:60],

// 倍速按钮（在显示模式按钮左边）
[_speedButton.trailingAnchor constraintEqualToAnchor:_aspectRatioButton.leadingAnchor constant:-8],
[_speedButton.topAnchor constraintEqualToAnchor:controlBar.topAnchor constant:16],
[_speedButton.widthAnchor constraintEqualToConstant:60],
```

### 4. 按钮点击事件处理

```objc
- (void)aspectRatioButtonTapped:(UIButton *)sender {
    // 切换显示模式：Fit（适应）<-> Fill（填充）
    if (_player.aspectRatioMode == AspectRatioModeFit) {
        _player.aspectRatioMode = AspectRatioModeFill;
        [sender setTitle:@"填充" forState:UIControlStateNormal];
    } else {
        _player.aspectRatioMode = AspectRatioModeFit;
        [sender setTitle:@"适应" forState:UIControlStateNormal];
    }
}
```

## 功能说明

### 按钮状态切换

| 当前模式 | 按钮显示 | 点击后模式 | 点击后显示 |
|---------|---------|-----------|-----------|
| Fit (适应) | "适应" | Fill (填充) | "填充" |
| Fill (填充) | "填充" | Fit (适应) | "适应" |

### 视觉效果

#### Fit 模式（适应）
- 按钮显示：**"适应"**
- 视频效果：等比缩放，保持完整画面
- 可能有黑边（当视频宽高比与屏幕不同时）
- 适合：观看完整内容，不裁剪

#### Fill 模式（填充）
- 按钮显示：**"填充"**
- 视频效果：等比拉伸填充整个屏幕
- 无黑边，但画面可能被裁剪
- 适合：沉浸式观看，追求全屏效果

## 用户交互流程

```
用户操作               系统响应                   视觉变化
   │                      │                        │
   ├─ 点击 "适应" 按钮    │                        │
   │                      ├─ 切换到 Fill 模式      │
   │                      ├─ 更新按钮文字为 "填充"  │
   │                      │                        ├─ 视频填充屏幕（无黑边）
   │                      │                        │
   ├─ 点击 "填充" 按钮    │                        │
   │                      ├─ 切换到 Fit 模式       │
   │                      ├─ 更新按钮文字为 "适应"  │
   │                      │                        ├─ 视频完整显示（可能有黑边）
```

## 代码示例

### 初始状态
```objc
// 播放器初始化时，默认是 Fit 模式
_player = [[PlayerCore_iOS alloc] init];
// _player.aspectRatioMode 默认为 AspectRatioModeFit

// 按钮初始文字为 "适应"
[_aspectRatioButton setTitle:@"适应" forState:UIControlStateNormal];
```

### 编程方式设置
```objc
// 除了用户点击按钮，也可以通过代码直接设置
_player.aspectRatioMode = AspectRatioModeFill;
[_aspectRatioButton setTitle:@"填充" forState:UIControlStateNormal];
```

## 修改文件

- **`/Users/debug/project/YXVodPlayer/src/ios/PlayerViewController.mm`**
  - 添加 `_aspectRatioButton` 属性
  - 在 `setupUI` 中创建显示模式按钮
  - 更新布局约束，将按钮放在右上角
  - 实现 `aspectRatioButtonTapped:` 方法处理点击事件

## 编译验证

```bash
cd /Users/debug/project/YXVodPlayer/src/ios/build/ios
xcodebuild -project YXVodPlayer-iOS.xcodeproj -scheme YXVodPlayer-iOS -sdk iphonesimulator -configuration Debug build
** BUILD SUCCEEDED **
```

## 测试方法

### 1. 基本功能测试
1. 启动应用，播放一个视频
2. 观察初始状态：按钮显示 "适应"，视频完整显示
3. 点击 "适应" 按钮
4. 验证：按钮变为 "填充"，视频填充整个屏幕
5. 再次点击 "填充" 按钮
6. 验证：按钮变为 "适应"，视频恢复完整显示

### 2. 不同宽高比视频测试

#### 16:9 视频（标准横屏）
- **Fit 模式**: 在竖屏设备上会有上下黑边
- **Fill 模式**: 填充屏幕，左右画面被裁剪

#### 4:3 视频（旧格式）
- **Fit 模式**: 在竖屏设备上会有左右黑边
- **Fill 模式**: 填充屏幕，上下画面被裁剪

#### 9:16 视频（竖屏视频）
- **Fit 模式**: 在竖屏设备上完整显示，可能有左右小黑边
- **Fill 模式**: 完全填充屏幕，几乎无裁剪

### 3. 实时切换测试
- 在播放过程中切换模式，验证是否实时生效
- 切换模式后暂停/播放，验证模式保持不变
- 切换模式后拖动进度条，验证模式保持不变

## UI 设计说明

### 按钮样式
- **颜色**: 白色文字（`tintColor = whiteColor`）
- **字体**: 系统默认字体
- **宽度**: 60pt（与倍速按钮一致）
- **高度**: 自适应内容

### 位置选择原因
1. **右上角**: 与倍速按钮相邻，形成控制按钮组
2. **易于操作**: 位于常用的操作区域
3. **视觉平衡**: 与左边的播放按钮对称

### 文字选择
- **"适应"**: 简洁明了，表示保持完整画面
- **"填充"**: 直观表达填充屏幕的含义
- 备选方案：使用图标（🔲 适应、🔳 填充）

## 优势

1. **操作简单**: 单击即可切换，无需多级菜单
2. **状态清晰**: 按钮文字直接显示当前模式
3. **实时生效**: 切换后立即看到效果
4. **布局合理**: 与其他控制按钮统一风格
5. **易于扩展**: 未来可以添加更多显示模式

## 未来增强

### 1. 添加更多显示模式
```objc
// 可以扩展为多状态切换
- (void)aspectRatioButtonTapped:(UIButton *)sender {
    switch (_player.aspectRatioMode) {
        case AspectRatioModeFit:
            _player.aspectRatioMode = AspectRatioModeFill;
            [sender setTitle:@"填充" forState:UIControlStateNormal];
            break;
        case AspectRatioModeFill:
            _player.aspectRatioMode = AspectRatioModeStretch;  // 如果未来支持
            [sender setTitle:@"拉伸" forState:UIControlStateNormal];
            break;
        case AspectRatioModeStretch:
            _player.aspectRatioMode = AspectRatioModeFit;
            [sender setTitle:@"适应" forState:UIControlStateNormal];
            break;
    }
}
```

### 2. 使用图标替代文字
```objc
// 使用 SF Symbols 图标
[_aspectRatioButton setImage:[UIImage systemImageNamed:@"rectangle"] 
                    forState:UIControlStateNormal];  // Fit
[_aspectRatioButton setImage:[UIImage systemImageNamed:@"rectangle.fill"] 
                    forState:UIControlStateNormal];  // Fill
```

### 3. 添加长按菜单
```objc
// 长按显示所有模式选项
UILongPressGestureRecognizer *longPress = [[UILongPressGestureRecognizer alloc] 
    initWithTarget:self action:@selector(showAspectRatioMenu:)];
[_aspectRatioButton addGestureRecognizer:longPress];
```

## 注意事项

1. **初始状态**: 按钮初始显示 "适应"，与播放器默认的 Fit 模式一致
2. **状态同步**: 按钮文字始终反映当前模式
3. **布局适配**: 在不同屏幕尺寸上都能正常显示
4. **线程安全**: 按钮点击在主线程处理，无需额外同步

## 总结

通过添加这个简单的按钮，用户可以方便地在 "适应" 和 "填充" 两种显示模式之间切换，获得不同的观看体验。实现简洁、易用，符合 iOS 应用的设计规范。
