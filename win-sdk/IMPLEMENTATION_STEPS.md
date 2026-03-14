# Windows D3D11 渲染器 - 剩余实施步骤

## ✅ 已完成

1. **渲染器框架**
   - ✅ `HXCIVideoRenderer` 接口
   - ✅ `HXCD3D11VideoRenderer` 完整实现
   - ✅ `HXCRendererFactory` 工厂类
   - ✅ `HXCWindowsPlayerView` 管理类
   - ✅ 所有类已添加 `HXC` 前缀

2. **CMake 配置**
   - ✅ 创建 `win-sdk/CMakeLists.txt`

## 🔄 待完成步骤

### 步骤 1：集成到 SDK DLL 编译

需要修改 SDK 的主 CMakeLists.txt（如果存在），或在构建脚本中添加渲染器源文件。

**文件**: `win-sdk/hxcplayer_dll` 或类似的 CMake 配置

添加：
```cmake
# 包含渲染器子目录
add_subdirectory(renderers)

# 添加渲染器源文件到 DLL
target_sources(hxcplayer_dll PRIVATE
    renderers/d3d11_video_renderer.cpp
    renderers/renderer_factory.cpp
    windows_player_view.cpp
)

# 链接 D3D11 库
target_link_libraries(hxcplayer_dll PRIVATE
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
)

# 包含目录
target_include_directories(hxcplayer_dll PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/renderers
)
```

### 步骤 2：扩展 C API

**文件**: `core/include/hxc_player_core_c_bridge.h`

添加新 API：
```c
// ========== Windows D3D11/OpenGL 渲染器 API ==========

typedef enum {
    HXC_RENDERER_TYPE_AUTO = 0,     // 自动选择
    HXC_RENDERER_TYPE_D3D11,        // Direct3D 11
    HXC_RENDERER_TYPE_OPENGL        // OpenGL 3.3+
} HXCRendererTypeC;

/**
 * @brief 设置渲染窗口（扩展版，支持选择渲染器类型）
 * @param handle 播放器句柄
 * @param window_handle 窗口句柄 (HWND on Windows)
 * @param renderer_type 渲染器类型
 * @return 0=成功，-1=失败
 */
int player_core_set_render_window_ex(
    PlayerCoreHandle* handle,
    void* window_handle,
    HXCRendererTypeC renderer_type
);

/**
 * @brief 窗口大小改变回调
 * @param handle 播放器句柄
 * @param width 新宽度
 * @param height 新高度
 */
void player_core_on_window_resize(
    PlayerCoreHandle* handle,
    int width,
    int height
);

/**
 * @brief 获取当前使用的渲染器类型
 * @return 渲染器类型名称（"Direct3D 11", "OpenGL", "None"）
 */
const char* player_core_get_current_renderer(PlayerCoreHandle* handle);

/**
 * @brief 检查指定渲染器是否可用
 */
int player_core_is_renderer_available(HXCRendererTypeC type);
```

### 步骤 3：实现 C API 桥接

**文件**: `core/src/hxc_player_core_c_bridge.cpp`

添加实现：
```cpp
#ifdef _WIN32
#include "../win-sdk/windows_player_view.h"
#endif

struct PlayerCoreHandle {
    hxcplayer::PlayerCore* core;
    
    // ... existing fields ...
    
#ifdef _WIN32
    // Windows 渲染器管理
    hxcplayer::windows::HXCWindowsPlayerView* windows_view;
#endif
};

int player_core_set_render_window_ex(
    PlayerCoreHandle* handle,
    void* window_handle,
    HXCRendererTypeC renderer_type
) {
    if (!handle || !handle->core || !window_handle) {
        return -1;
    }
    
#ifdef _WIN32
    // 转换渲染器类型
    hxcplayer::windows::HXCRendererType type;
    switch (renderer_type) {
        case HXC_RENDERER_TYPE_D3D11:
            type = hxcplayer::windows::HXCRendererType::D3D11;
            break;
        case HXC_RENDERER_TYPE_OPENGL:
            type = hxcplayer::windows::HXCRendererType::OpenGL;
            break;
        default:
            type = hxcplayer::windows::HXCRendererType::Auto;
    }
    
    // 创建或更新 WindowsPlayerView
    if (!handle->windows_view) {
        handle->windows_view = new hxcplayer::windows::HXCWindowsPlayerView(handle->core);
    }
    
    return handle->windows_view->SetWindow(window_handle, type) ? 0 : -1;
#else
    return -1;  // 非 Windows 平台
#endif
}

void player_core_on_window_resize(PlayerCoreHandle* handle, int width, int height) {
    if (!handle) return;
    
#ifdef _WIN32
    if (handle->windows_view) {
        handle->windows_view->OnWindowResize(width, height);
    }
#endif
}

const char* player_core_get_current_renderer(PlayerCoreHandle* handle) {
    if (!handle) return "None";
    
#ifdef _WIN32
    if (handle->windows_view) {
        return handle->windows_view->GetCurrentRendererType();
    }
#endif
    return "None";
}

int player_core_is_renderer_available(HXCRendererTypeC type) {
#ifdef _WIN32
    hxcplayer::windows::HXCRendererType hxc_type;
    switch (type) {
        case HXC_RENDERER_TYPE_D3D11:
            hxc_type = hxcplayer::windows::HXCRendererType::D3D11;
            break;
        case HXC_RENDERER_TYPE_OPENGL:
            hxc_type = hxcplayer::windows::HXCRendererType::OpenGL;
            break;
        default:
            hxc_type = hxcplayer::windows::HXCRendererType::Auto;
    }
    return hxcplayer::windows::HXCRendererFactory::IsRendererAvailable(hxc_type) ? 1 : 0;
#else
    return 0;
#endif
}
```

### 步骤 4：更新 DEF 文件

**文件**: `win-sdk/hxcplayer_dll.def`

添加导出：
```def
    ; Windows D3D11/OpenGL 渲染器 API
    player_core_set_render_window_ex
    player_core_on_window_resize
    player_core_get_current_renderer
    player_core_is_renderer_available
```

### 步骤 5：更新示例代码

**文件**: `win-sdk-example/player_window.cpp`

修改为使用 D3D11 渲染器：
```cpp
// 移除手动 RGB 转换代码
// 移除 eventFilter

PlayerWindow::PlayerWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::PlayerWindow)
    , player_(nullptr)
    , is_seeking_(false)
    , duration_(0.0) {
    
    ui->setupUi(this);
    
    // 配置视频容器为原生窗口
    ui->videoContainer->setAttribute(Qt::WA_NativeWindow);
    
    // 创建播放器
    player_ = player_core_create();
    
    // 使用 D3D11 自动渲染（一行搞定！）
    player_core_set_render_window_ex(
        player_,
        (void*)ui->videoContainer->winId(),
        HXC_RENDERER_TYPE_AUTO
    );
    
    // 设置回调...
    // （保持原有的播放控制回调）
}

// 添加 resize 事件处理
void PlayerWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (player_) {
        player_core_on_window_resize(
            player_,
            ui->videoContainer->width(),
            ui->videoContainer->height()
        );
    }
}
```

### 步骤 6：测试

1. **重新构建 SDK**
   ```bash
   cd win-sdk
   .\build_sdk.bat
   ```

2. **重新构建示例**
   ```bash
   cd win-sdk-example
   .\build.bat
   .\run.bat
   ```

3. **验证功能**
   - ✅ 视频正常播放
   - ✅ 无闪烁
   - ✅ Resize 流畅
   - ✅ 性能良好（低 CPU）

## 📝 注意事项

1. **PlayerCoreHandle 结构体**需要在析构时清理 `windows_view`:
   ```cpp
   // 在 player_core_destroy 中
   #ifdef _WIN32
   if (handle->windows_view) {
       delete handle->windows_view;
       handle->windows_view = nullptr;
   }
   #endif
   ```

2. **条件编译**确保非 Windows 平台不受影响：
   ```cpp
   #ifdef _WIN32
   // Windows 特定代码
   #endif
   ```

3. **依赖库**确保 CMake 正确链接：
   - `d3d11.lib`
   - `dxgi.lib`
   - `d3dcompiler.lib`

## 🎯 最终效果

用户代码极简：
```cpp
player_ = player_core_create();
player_core_set_render_window_ex(player_, hwnd, HXC_RENDERER_TYPE_AUTO);
player_core_open(player_, "video.mp4");
player_core_play(player_);
// 完全自动渲染，无需任何额外代码！
```

## 📊 性能预期

- **CPU 占用**: < 5%（vs 之前 15-20%）
- **GPU 占用**: 10-15%（YUV→RGB 转换）
- **帧率**: 稳定 60 FPS
- **延迟**: < 16ms（Vsync）
- **内存**: +10MB（D3D11 资源）

是否需要我继续实现剩余步骤？
