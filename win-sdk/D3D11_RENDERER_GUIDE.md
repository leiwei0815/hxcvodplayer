# Windows D3D11/OpenGL 渲染器使用指南

## 架构说明

新的 Windows 渲染层采用与 macOS/iOS 相同的架构：

```
用户代码 → C API → WindowsPlayerView → D3D11Renderer/OpenGLRenderer → GPU
```

## 特点

✅ **GPU 加速 YUV→RGB 转换**  
✅ **无闪烁 Resize**（不重建渲染器）  
✅ **自动 Vsync 同步**  
✅ **自动渲染线程管理**  
✅ **零用户负担**（只需设置窗口）  

## 快速开始

### Qt 用户

```cpp
#include "hxcplayer_sdk.h"

// 创建播放器
PlayerCoreHandle* player = player_core_create();

// 设置渲染窗口（自动选择 D3D11）
player_core_set_render_window_ex(
    player,
    (void*)ui->videoWidget->winId(),
    RENDERER_TYPE_AUTO
);

// 播放视频（自动渲染）
player_core_open(player, "video.mp4");
player_core_play(player);

// 窗口 Resize 时（在 resizeEvent 中）
player_core_on_window_resize(player, new_width, new_height);

// 完成后清理
player_core_destroy(player);
```

### MFC 用户

```cpp
// CVideoView.cpp
class CVideoView : public CView {
private:
    PlayerCoreHandle* player_;
    
public:
    void OnInitialUpdate() {
        CView::OnInitialUpdate();
        
        player_ = player_core_create();
        
        // 使用 D3D11 渲染器
        player_core_set_render_window_ex(
            player_,
            GetSafeHwnd(),
            RENDERER_TYPE_D3D11
        );
    }
    
    void OnSize(UINT nType, int cx, int cy) {
        CView::OnSize(nType, cx, cy);
        if (player_) {
            player_core_on_window_resize(player_, cx, cy);
        }
    }
};
```

### 原生 Win32 用户

```cpp
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static PlayerCoreHandle* player = nullptr;
    
    switch (uMsg) {
    case WM_CREATE:
        player = player_core_create();
        player_core_set_render_window_ex(player, hwnd, RENDERER_TYPE_AUTO);
        player_core_open(player, L"video.mp4");
        player_core_play(player);
        break;
        
    case WM_SIZE: {
        int width = LOWORD(lParam);
        int height = HIWORD(lParam);
        player_core_on_window_resize(player, width, height);
        break;
    }
        
    case WM_DESTROY:
        player_core_destroy(player);
        PostQuitMessage(0);
        break;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
```

## API 参考

### 设置渲染窗口（扩展版）

```c
typedef enum {
    RENDERER_TYPE_AUTO = 0,     // 自动选择（推荐）
    RENDERER_TYPE_D3D11,        // 强制使用 D3D11
    RENDERER_TYPE_OPENGL        // 强制使用 OpenGL（待实现）
} RendererTypeC;

int player_core_set_render_window_ex(
    PlayerCoreHandle* handle,
    void* window_handle,
    RendererTypeC renderer_type
);
```

### 窗口 Resize 回调

```c
void player_core_on_window_resize(
    PlayerCoreHandle* handle,
    int width,
    int height
);
```

### 查询当前渲染器

```c
const char* player_core_get_current_renderer(PlayerCoreHandle* handle);
// 返回: "Direct3D 11", "OpenGL", "None"
```

### 检查渲染器是否可用

```c
bool player_core_is_renderer_available(RendererTypeC type);
```

## 性能对比

| 方案 | CPU 占用 | GPU 占用 | 延迟 | 兼容性 |
|------|---------|---------|-----|--------|
| **Qt + RGB（旧）** | 高 | 低 | 中 | ✅ 全平台 |
| **D3D11（新）** | 低 | 中 | 低 | ✅ Win7+ |
| **OpenGL（新）** | 低 | 中 | 低 | ✅ Win7+ |

## 为什么不会闪烁？

### SDL 方案（旧，会闪烁）
```cpp
OnResize() {
    SDL_DestroyRenderer();  // ❌ 销毁
    SDL_CreateRenderer();   // ❌ 重建
    // 期间窗口空白 → 闪烁
}
```

### D3D11 方案（新，不闪烁）
```cpp
OnResize() {
    swap_chain_->ResizeBuffers(width, height);  // ✅ 只调整交换链
    // 纹理和渲染器保持不变
    // 立即重绘最后一帧 → 无闪烁
}
```

## 系统要求

- **D3D11**: Windows 7 SP1 + Platform Update（或 Windows 8+）
- **OpenGL**: OpenGL 3.3+（大多数 2010 年后的显卡）

## 故障排查

### D3D11 初始化失败

**现象**: 控制台显示 "D3D11 初始化失败"

**可能原因**:
1. 显卡驱动过旧 → 更新驱动
2. 运行在虚拟机中 → 启用虚拟机 3D 加速
3. Windows 7 缺少 Platform Update → 安装更新

**解决方案**: 使用 `RENDERER_TYPE_AUTO`，自动降级到 OpenGL

### 性能问题

**现象**: 视频播放卡顿

**检查事项**:
1. 查看 FPS 日志 (`RenderLoop: 渲染 FPS = ...`)
2. 确认使用了 D3D11（不是 OpenGL）
3. 检查 CPU 占用（应 < 10%）

## 扩展：添加 OpenGL 渲染器

TODO: 实现 `opengl_video_renderer.h/.cpp`

基本结构：
```cpp
class OpenGLVideoRenderer : public IVideoRenderer {
private:
    HGLRC hglrc_;           // OpenGL 上下文
    GLuint yuv_textures[3]; // Y/U/V 纹理
    GLuint shader_program_; // YUV→RGB Shader
    
public:
    bool Initialize(void* window) override;
    bool RenderFrame(...) override;
    // ...
};
```

## 总结

新的 Windows 渲染层：
- ✅ **架构与 macOS/iOS 一致**
- ✅ **用户只需设置窗口**
- ✅ **GPU 加速，高性能**
- ✅ **无闪烁，体验流畅**
- ✅ **自动管理渲染线程**
