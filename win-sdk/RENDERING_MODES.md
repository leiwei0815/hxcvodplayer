# Windows SDK 渲染模式说明

## 🎨 音视频渲染方式

### ✅ 音频渲染（自动）

**实现方式**：使用 SDL2 自动输出音频

**特点**：
- ✅ **完全自动**：无需用户代码
- ✅ 跨平台一致（Windows/macOS/Linux）
- ✅ 支持音量控制
- ✅ 支持倍速播放（SoundTouch）

**用户代码**：
```c
// 音频自动播放，无需任何额外代码
player_core_play(player);
```

---

### 🎬 视频渲染（两种模式）

当前 SDK 提供了两种视频渲染模式：

## 模式 1：自动渲染模式（推荐用于桌面应用）

**适用场景**：
- Windows 桌面应用
- 简单的视频播放需求
- 不需要自定义渲染

**实现方式**：
- 核心库内部使用 SDL2 渲染器
- 自动在窗口中显示视频
- 用户只需提供窗口句柄

**当前状态**：❌ 尚未在 SDK 中暴露

**建议实现**：添加以下 API

```c
// 设置渲染窗口（Windows HWND）
void player_core_set_window(PlayerCoreHandle* handle, void* window_handle);

// 启用自动渲染（默认）
void player_core_enable_auto_rendering(PlayerCoreHandle* handle, bool enable);
```

**用户代码示例**：
```c
// 创建播放器
PlayerCoreHandle* player = player_core_create();

// 设置渲染窗口（Windows HWND）
HWND hwnd = GetDlgItem(hDlg, IDC_VIDEO_WINDOW);
player_core_set_window(player, (void*)hwnd);

// 启用自动渲染（默认已启用）
player_core_enable_auto_rendering(player, true);

// 播放 - 视频自动渲染到窗口
player_core_open(player, "video.mp4");
player_core_play(player);

// 用户无需任何渲染代码！
```

---

## 模式 2：手动渲染模式（推荐用于游戏引擎/自定义渲染）

**适用场景**：
- 游戏引擎（Unity, UE4）
- 自定义渲染管线（DirectX, OpenGL, Vulkan）
- 需要后处理效果
- WebView 内嵌播放器

**实现方式**：
- 用户主动拉取视频帧（YUV420P 格式）
- 用户自己渲染到纹理/屏幕

**当前状态**：✅ 已实现

**API**：
```c
// 获取视频帧（YUV420P）
int player_core_get_video_frame(PlayerCoreHandle* handle, VideoFrameDataC* frame_data);

// 消费视频帧（标记为已使用）
void player_core_consume_video_frame(PlayerCoreHandle* handle);
```

**用户代码示例**：

### Windows DirectX 11 渲染

```c
#include <d3d11.h>
#include "hxcplayer_sdk.h"

PlayerCoreHandle* player;
ID3D11Device* device;
ID3D11DeviceContext* context;
ID3D11Texture2D* yuv_textures[3];  // Y, U, V

void init_player() {
    player = player_core_create();
    
    // 禁用自动渲染（使用手动模式）
    player_core_enable_auto_rendering(player, false);
    
    player_core_open(player, "video.mp4");
    player_core_play(player);
    
    // 创建 YUV 纹理
    create_yuv_textures();
}

void render_loop() {
    VideoFrameDataC frame;
    
    // 尝试获取新帧
    if (player_core_get_video_frame(player, &frame) == 0) {
        // 上传 YUV 数据到 GPU
        update_texture(yuv_textures[0], frame.y_data, frame.y_linesize, frame.width, frame.height);
        update_texture(yuv_textures[1], frame.u_data, frame.u_linesize, frame.width/2, frame.height/2);
        update_texture(yuv_textures[2], frame.v_data, frame.v_linesize, frame.width/2, frame.height/2);
        
        // 渲染 YUV 到 RGB（使用 Shader）
        render_yuv_to_rgb(yuv_textures);
        
        // 标记帧已消费
        player_core_consume_video_frame(player);
    }
    
    // 呈现
    swapChain->Present(1, 0);
}
```

### Unity C# 渲染

```csharp
using UnityEngine;
using System.Runtime.InteropServices;

public class VideoPlayer : MonoBehaviour
{
    [DllImport("hxcplayer")]
    private static extern IntPtr player_core_create();
    
    [DllImport("hxcplayer")]
    private static extern int player_core_get_video_frame(IntPtr handle, ref VideoFrameDataC frame);
    
    [DllImport("hxcplayer")]
    private static extern void player_core_consume_video_frame(IntPtr handle);
    
    private IntPtr player;
    private Texture2D videoTexture;
    
    void Start()
    {
        player = player_core_create();
        player_core_open(player, "video.mp4");
        player_core_play(player);
    }
    
    void Update()
    {
        VideoFrameDataC frame = new VideoFrameDataC();
        
        if (player_core_get_video_frame(player, ref frame) == 0)
        {
            // 转换 YUV 到 RGB 并更新纹理
            UpdateVideoTexture(frame);
            
            player_core_consume_video_frame(player);
        }
    }
}
```

### OpenGL 渲染

```c
#include <GL/gl.h>
#include "hxcplayer_sdk.h"

GLuint yuv_textures[3];
GLuint shader_program;

void init_opengl_player() {
    // 创建 YUV 纹理
    glGenTextures(3, yuv_textures);
    for (int i = 0; i < 3; i++) {
        glBindTexture(GL_TEXTURE_2D, yuv_textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    
    // 加载 YUV to RGB Shader
    shader_program = load_yuv_shader();
}

void render_video_frame() {
    VideoFrameDataC frame;
    
    if (player_core_get_video_frame(player, &frame) == 0) {
        // 更新 Y 平面
        glBindTexture(GL_TEXTURE_2D, yuv_textures[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 
                     frame.width, frame.height, 0, 
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, frame.y_data);
        
        // 更新 U 平面
        glBindTexture(GL_TEXTURE_2D, yuv_textures[1]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 
                     frame.width/2, frame.height/2, 0, 
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, frame.u_data);
        
        // 更新 V 平面
        glBindTexture(GL_TEXTURE_2D, yuv_textures[2]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 
                     frame.width/2, frame.height/2, 0, 
                     GL_LUMINANCE, GL_UNSIGNED_BYTE, frame.v_data);
        
        // 使用 Shader 渲染
        render_with_yuv_shader(yuv_textures, shader_program);
        
        player_core_consume_video_frame(player);
    }
}
```

---

## 📊 两种模式对比

| 特性 | 自动渲染模式 | 手动渲染模式 |
|------|-------------|-------------|
| **易用性** | ⭐⭐⭐⭐⭐ 极简 | ⭐⭐⭐ 需要渲染代码 |
| **灵活性** | ⭐⭐ 有限 | ⭐⭐⭐⭐⭐ 完全控制 |
| **性能** | ⭐⭐⭐⭐ 好 | ⭐⭐⭐⭐⭐ 可优化 |
| **适用场景** | 桌面应用 | 游戏引擎/自定义渲染 |
| **后处理** | ❌ 不支持 | ✅ 完全支持 |
| **代码量** | 3-5 行 | 50-200 行 |

---

## 🎯 推荐实现计划

### 短期（立即）

✅ **手动渲染模式**：已实现
- `player_core_get_video_frame()`
- `player_core_consume_video_frame()`

### 中期（建议实现）

⏳ **自动渲染模式增强**：

1. 添加窗口设置 API
```c
void player_core_set_window(PlayerCoreHandle* handle, void* window_handle);
```

2. 添加渲染模式切换
```c
void player_core_enable_auto_rendering(PlayerCoreHandle* handle, bool enable);
```

3. 添加渲染回调（可选）
```c
typedef void (*VideoFrameReadyCallbackC)(VideoFrameDataC* frame, void* user_data);
void player_core_set_video_frame_callback(
    PlayerCoreHandle* handle,
    VideoFrameReadyCallbackC callback,
    void* user_data
);
```

### 长期（高级功能）

⏳ **多渲染后端支持**：
```c
typedef enum {
    RENDER_BACKEND_AUTO,      // 自动选择
    RENDER_BACKEND_SDL2,      // SDL2（默认）
    RENDER_BACKEND_D3D11,     // DirectX 11
    RENDER_BACKEND_OPENGL,    // OpenGL
    RENDER_BACKEND_VULKAN,    // Vulkan
    RENDER_BACKEND_MANUAL     // 手动渲染
} RenderBackendC;

void player_core_set_render_backend(PlayerCoreHandle* handle, RenderBackendC backend);
```

---

## 💡 使用建议

### 场景 1：简单视频播放器
**推荐**：自动渲染模式
```c
player_core_set_window(player, hwnd);
player_core_play(player);
```

### 场景 2：Unity 游戏
**推荐**：手动渲染模式
```c
player_core_enable_auto_rendering(player, false);
// 在 Update() 中拉取帧并渲染到 Texture2D
```

### 场景 3：WPF 应用
**推荐**：手动渲染模式 + WriteableBitmap
```csharp
// 拉取 YUV 帧
player_core_get_video_frame(player, ref frame);
// 转换 YUV 到 RGB
ConvertYUVtoRGB(frame, writeableBitmap);
// 显示在 Image 控件
```

### 场景 4：Direct2D 应用
**推荐**：手动渲染模式 + D2D Bitmap
```c
player_core_get_video_frame(player, &frame);
// 转换 YUV 到 RGB
// 创建 ID2D1Bitmap 并渲染
```

---

## 🔧 当前 SDK 状态总结

| 功能 | 状态 | 说明 |
|------|------|------|
| 音频自动播放 | ✅ 完全支持 | SDL2 自动输出 |
| 视频手动渲染 | ✅ 完全支持 | 提供 YUV 帧接口 |
| 视频自动渲染 | ⚠️ 部分支持 | Desktop 版有，SDK 未暴露 |
| 窗口管理 | ❌ 未实现 | 需要添加 API |
| 多渲染后端 | ❌ 未实现 | 未来功能 |

---

## 📞 问题反馈

如果你需要：
1. **简单桌面应用** → 我可以为你添加自动渲染 API
2. **游戏引擎集成** → 当前手动模式已满足需求
3. **特定渲染后端** → 告诉我具体需求

请告诉我你的具体使用场景，我可以为你优化 SDK！
