# 自动渲染功能使用指南

## 📺 概述

HXCPlayer SDK 现在支持两种渲染模式：

1. **自动渲染模式（Auto Rendering）**：SDK 内部自动渲染到窗口
2. **手动渲染模式（Manual Rendering）**：用户主动获取帧并渲染

## 🎯 自动渲染模式

### 特点

- ✅ **极简易用**：只需设置窗口句柄，无需渲染代码
- ✅ **跨平台**：Windows (HWND/CWnd*/QWidget*), macOS (NSView*), Linux (X11 Window)
- ✅ **高性能**：使用 SDL2 硬件加速渲染
- ✅ **自动适配**：支持 Fit/Fill 两种宽高比模式

### 使用步骤

#### 1. 设置渲染窗口

```c
// Windows Win32/MFC
HWND hwnd = GetDlgItem(hDlg, IDC_VIDEO_WINDOW);
player_core_set_render_window(player, (void*)hwnd);

// Qt
HWND hwnd = (HWND)videoWidget->winId();
player_core_set_render_window(player, (void*)hwnd);

// macOS
NSView* view = (__bridge void*)myView;
player_core_set_render_window(player, view);
```

#### 2. 设置渲染模式（可选，默认已是 AUTO）

```c
player_core_set_render_mode(player, RENDER_MODE_AUTO);
```

#### 3. 创建刷新定时器

```c
// Windows - 使用 SetTimer
SetTimer(hwnd, TIMER_REFRESH, 16, NULL);  // 约 60 FPS

// 在 WM_TIMER 消息中刷新视频
case WM_TIMER:
    if (wParam == TIMER_REFRESH) {
        player_core_refresh_video(player);
    }
    break;
```

```cpp
// Qt - 使用 QTimer
QTimer* timer = new QTimer(this);
connect(timer, &QTimer::timeout, [this]() {
    player_core_refresh_video(player_);
});
timer->start(16);  // 约 60 FPS
```

#### 4. 开始播放

```c
player_core_open(player, "video.mp4");
player_core_play(player);  // 视频自动渲染到窗口！
```

就这么简单！视频会自动渲染到你指定的窗口中。

## 🎨 完整示例

### MFC 示例

```cpp
class CPlayerDlg : public CDialogEx
{
private:
    PlayerCoreHandle* m_player;
    CStatic m_videoWindow;
    
public:
    BOOL OnInitDialog()
    {
        CDialogEx::OnInitDialog();
        
        // 1. 创建播放器
        m_player = player_core_create();
        
        // 2. 设置渲染窗口
        HWND hwnd = m_videoWindow.GetSafeHwnd();
        player_core_set_render_window(m_player, (void*)hwnd);
        
        // 3. 设置为自动渲染（默认已是）
        player_core_set_render_mode(m_player, RENDER_MODE_AUTO);
        
        // 4. 启动刷新定时器
        SetTimer(1, 16, NULL);  // 60 FPS
        
        return TRUE;
    }
    
    void OnTimer(UINT_PTR nIDEvent)
    {
        if (nIDEvent == 1) {
            // 刷新视频
            player_core_refresh_video(m_player);
        }
    }
    
    void OnOpenFile()
    {
        CFileDialog dlg(TRUE, NULL, NULL, OFN_FILEMUSTEXIST,
            _T("视频文件|*.mp4;*.avi;*.mkv||"));
        
        if (dlg.DoModal() == IDOK) {
            CString path = dlg.GetPathName();
            player_core_open(m_player, CT2A(path));
            player_core_play(m_player);  // 视频自动显示！
        }
    }
    
    void OnDestroy()
    {
        KillTimer(1);
        player_core_destroy(m_player);
        CDialogEx::OnDestroy();
    }
};
```

### Qt 示例

```cpp
class VideoPlayer : public QWidget
{
    Q_OBJECT
    
private:
    PlayerCoreHandle* player_;
    QWidget* videoWidget_;
    QTimer* refreshTimer_;
    
public:
    VideoPlayer(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        // 创建视频显示区域
        videoWidget_ = new QWidget(this);
        videoWidget_->setMinimumSize(640, 480);
        videoWidget_->setStyleSheet("background-color: black;");
        
        // 创建播放器
        player_ = player_core_create();
        
        // 设置渲染窗口 (Qt Widget)
#ifdef _WIN32
        HWND hwnd = (HWND)videoWidget_->winId();
        player_core_set_render_window(player_, (void*)hwnd);
#else
        player_core_set_render_window(player_, (void*)videoWidget_->winId());
#endif
        
        // 启动刷新定时器
        refreshTimer_ = new QTimer(this);
        connect(refreshTimer_, &QTimer::timeout, this, &VideoPlayer::refreshVideo);
        refreshTimer_->start(16);  // 60 FPS
    }
    
    ~VideoPlayer()
    {
        player_core_destroy(player_);
    }
    
private slots:
    void refreshVideo()
    {
        player_core_refresh_video(player_);
    }
    
    void openFile()
    {
        QString fileName = QFileDialog::getOpenFileName(
            this, "打开视频", "", "视频文件 (*.mp4 *.avi *.mkv)");
        
        if (!fileName.isEmpty()) {
            std::string path = fileName.toUtf8().constData();
            player_core_open(player_, path.c_str());
            player_core_play(player_);  // 视频自动显示！
        }
    }
};
```

### Win32 示例

```c
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static PlayerCoreHandle* player = NULL;
    static HWND hwndVideo = NULL;
    
    switch (msg)
    {
    case WM_CREATE:
        {
            // 创建子窗口用于视频显示
            hwndVideo = CreateWindow(_T("STATIC"), NULL,
                WS_CHILD | WS_VISIBLE | SS_BLACKFRAME,
                10, 10, 640, 480, hwnd, NULL, NULL, NULL);
            
            // 创建播放器
            player = player_core_create();
            player_core_set_render_window(player, (void*)hwndVideo);
            
            // 启动定时器
            SetTimer(hwnd, 1, 16, NULL);  // 60 FPS
        }
        break;
        
    case WM_TIMER:
        if (wParam == 1) {
            player_core_refresh_video(player);
        }
        break;
        
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_FILE_OPEN) {
            // 打开文件对话框
            OPENFILENAME ofn = {0};
            TCHAR fileName[MAX_PATH] = {0};
            // ... 初始化 ofn ...
            
            if (GetOpenFileName(&ofn)) {
                char path[MAX_PATH];
                WideCharToMultiByte(CP_UTF8, 0, fileName, -1, 
                                    path, MAX_PATH, NULL, NULL);
                player_core_open(player, path);
                player_core_play(player);  // 视频自动显示！
            }
        }
        break;
        
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        player_core_destroy(player);
        PostQuitMessage(0);
        break;
    }
    
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
```

## 🎛️ 高级设置

### 宽高比模式

```c
// Fit 模式：保持宽高比，可能有黑边（默认）
player_core_set_aspect_ratio_mode(player, ASPECT_RATIO_FIT);

// Fill 模式：填充整个窗口，可能裁剪画面
player_core_set_aspect_ratio_mode(player, ASPECT_RATIO_FILL);
```

### 查询渲染模式

```c
RenderModeC mode = player_core_get_render_mode(player);
if (mode == RENDER_MODE_AUTO) {
    printf("自动渲染模式\n");
} else {
    printf("手动渲染模式\n");
}
```

### 刷新返回值

```c
int result = player_core_refresh_video(player);
// 返回值:
//   0: 成功刷新了一帧
//  -1: 无新帧可用（正常，继续轮询）
//  -2: 未设置渲染窗口
```

## 🔄 与手动渲染模式对比

| 特性 | 自动渲染 | 手动渲染 |
|------|---------|---------|
| **代码量** | 10-20 行 | 50-200 行 |
| **易用性** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| **灵活性** | ⭐⭐⭐ | ⭐⭐⭐⭐⭐ |
| **性能** | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ |
| **适用场景** | MFC/Qt 桌面应用 | 游戏引擎/自定义渲染 |
| **后处理** | ❌ | ✅ |
| **硬件加速** | ✅ SDL2 | 自定义 |

## ⚙️ 性能优化建议

### 1. 刷新频率

```c
// 推荐：根据视频帧率调整
double fps = 30.0;  // 假设视频是 30 FPS
int interval = (int)(1000.0 / fps);  // 计算间隔（毫秒）
SetTimer(hwnd, 1, interval, NULL);
```

### 2. 窗口大小变化

```c
// Windows: 处理 WM_SIZE
case WM_SIZE:
    // SDL 会自动适配新窗口大小，无需特殊处理
    break;
```

### 3. 多显示器支持

自动支持，SDL2 会自动适配不同 DPI 和分辨率。

## 🐛 故障排查

### 问题 1：黑屏，无视频显示

**检查**：
```c
// 1. 确认窗口句柄有效
HWND hwnd = m_videoWindow.GetSafeHwnd();
if (!hwnd || !IsWindow(hwnd)) {
    // 窗口无效
}

// 2. 确认定时器运行
// 添加日志确认 refresh_video 被调用

// 3. 检查返回值
int ret = player_core_refresh_video(player);
if (ret == -2) {
    // 未设置窗口
} else if (ret == -1) {
    // 无新帧（可能视频还没解码好）
}
```

### 问题 2：视频卡顿

**原因**：刷新频率太低

**解决**：
```c
// 提高刷新频率到 60 FPS
SetTimer(hwnd, 1, 16, NULL);  // 16ms ≈ 60 FPS
```

### 问题 3：CPU 占用高

**原因**：刷新频率过高

**解决**：
```c
// 降低刷新频率，匹配视频帧率
// 例如 30 FPS 视频：
SetTimer(hwnd, 1, 33, NULL);  // 33ms ≈ 30 FPS
```

### 问题 4：窗口调整大小后显示异常

**解决**：SDL2 应该自动处理，如果有问题：
```c
// 重新初始化渲染窗口
player_core_set_render_window(player, nullptr);
player_core_set_render_window(player, (void*)hwnd);
```

## 📝 最佳实践

### 1. 窗口生命周期

```c
// ✅ 正确：在窗口创建后设置
OnCreate() {
    player = player_core_create();
    player_core_set_render_window(player, hwnd);
}

// ❌ 错误：窗口销毁后仍使用
OnDestroy() {
    DestroyWindow(hwnd);  // ← 先销毁窗口
    player_core_destroy(player);  // ← 再销毁播放器（可能崩溃）
}

// ✅ 正确顺序
OnDestroy() {
    player_core_stop(player);
    player_core_destroy(player);  // ← 先销毁播放器
    // 窗口会自动销毁
}
```

### 2. 线程安全

```c
// 回调函数在播放器线程，不要直接操作 UI
void on_error(int code, const char* msg, void* user_data) {
    // ❌ 错误：直接操作 UI
    // MessageBox(NULL, msg, "Error", MB_OK);
    
    // ✅ 正确：转发到主线程
    PostMessage(hwnd, WM_USER_ERROR, code, (LPARAM)msg);
}
```

### 3. 资源管理

```c
// ✅ 使用 RAII 包装
class PlayerWrapper {
    PlayerCoreHandle* player_;
public:
    PlayerWrapper() {
        player_ = player_core_create();
    }
    ~PlayerWrapper() {
        player_core_destroy(player_);
    }
};
```

## 🎓 完整项目模板

参见：
- `example/mfc_player_example.cpp` - MFC 完整示例
- `example/qt_player_example.cpp` - Qt 完整示例

## 📞 获取帮助

- GitHub Issues: [项目地址]
- 文档: `docs/SDK_USAGE.md`
- 示例: `example/` 目录
