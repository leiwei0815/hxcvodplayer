/**
 * @file video_renderer_interface.h
 * @brief Windows 视频渲染器接口定义
 * 
 * 类似于 macOS/iOS 的 AVSampleBufferDisplayLayer，
 * 提供统一的渲染器抽象接口
 */

#ifndef VIDEO_RENDERER_INTERFACE_H
#define VIDEO_RENDERER_INTERFACE_H

#include <cstdint>

namespace hxcplayer {
namespace windows {

/**
 * @brief 宽高比模式
 */
enum class AspectRatioMode {
    Fit = 0,    // 适应：保持比例，完整显示（可能有黑边）
    Fill = 1,   // 填充：保持比例，填满窗口（可能裁剪）
    Stretch = 2 // 拉伸：不保持比例，填满窗口
};

/**
 * @brief 视频渲染器接口
 * 
 * 所有 Windows 平台的渲染器（D3D11/OpenGL/Vulkan）都实现此接口
 */
class HXCIVideoRenderer {
public:
    virtual ~HXCIVideoRenderer() = default;
    
    /**
     * @brief 初始化渲染器
     * @param window 目标窗口句柄 (HWND)
     * @return true=成功，false=失败
     */
    virtual bool Initialize(void* window) = 0;
    
    /**
     * @brief 渲染 YUV420P 视频帧
     * @param y_data Y 平面数据指针
     * @param u_data U 平面数据指针
     * @param v_data V 平面数据指针
     * @param width 视频宽度
     * @param height 视频高度
     * @param y_linesize Y 平面每行字节数
     * @param u_linesize U 平面每行字节数
     * @param v_linesize V 平面每行字节数
     * @return true=渲染成功，false=失败
     */
    virtual bool RenderFrame(
        const uint8_t* y_data, const uint8_t* u_data, const uint8_t* v_data,
        int width, int height,
        int y_linesize, int u_linesize, int v_linesize
    ) = 0;
    
    /**
     * @brief 窗口大小改变回调
     * @param width 新宽度
     * @param height 新高度
     * 
     * 注意：此方法不应重建整个渲染器，只应调整视口/交换链大小
     */
    virtual void OnResize(int width, int height) = 0;
    
    /**
     * @brief 设置宽高比模式
     * @param mode 显示模式
     */
    virtual void SetAspectRatioMode(AspectRatioMode mode) = 0;
    
    /**
     * @brief 清理渲染器资源
     */
    virtual void Cleanup() = 0;
    
    /**
     * @brief 获取渲染器类型名称
     * @return 渲染器类型字符串（如 "D3D11", "OpenGL"）
     */
    virtual const char* GetType() const = 0;
    
    /**
     * @brief 检查渲染器是否已初始化
     */
    virtual bool IsInitialized() const = 0;
};

} // namespace windows
} // namespace hxcplayer

#endif // VIDEO_RENDERER_INTERFACE_H
