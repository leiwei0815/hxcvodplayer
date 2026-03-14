/**
 * @file windows_player_view.h
 * @brief Windows 播放器视图管理器
 * 
 * 类似于 macOS/iOS 的 HXCPlayerView，
 * 管理渲染器和渲染线程，提供简单的 API
 */

#ifndef WINDOWS_PLAYER_VIEW_H
#define WINDOWS_PLAYER_VIEW_H

#include "renderers/renderer_factory.h"
#include "../core/include/hxc_player_core.h"
#include <thread>
#include <atomic>
#include <memory>

namespace hxcplayer {
namespace windows {

/**
 * @brief Windows 播放器视图管理器
 * 
 * 封装渲染器和渲染线程，用户只需：
 * 1. SetWindow(hwnd) - 设置窗口
 * 2. player->Play() - 播放（自动渲染）
 */
class HXCWindowsPlayerView {
public:
    explicit HXCWindowsPlayerView(PlayerCore* player);
    ~HXCWindowsPlayerView();
    
    /**
     * @brief 设置渲染窗口和渲染器类型
     * @param window 窗口句柄 (HWND)
     * @param renderer_type 渲染器类型（默认 Auto）
     * @return true=成功，false=失败
     */
    bool SetWindow(void* window, HXCRendererType renderer_type = HXCRendererType::Auto);
    
    /**
     * @brief 启动渲染线程（自动渲染模式）
     */
    void StartRendering();
    
    /**
     * @brief 停止渲染线程
     */
    void StopRendering();
    
    /**
     * @brief 窗口大小改变回调（外部调用）
     * @param width 新宽度
     * @param height 新高度
     */
    void OnWindowResize(int width, int height);
    
    /**
     * @brief 设置宽高比模式
     * @param mode 显示模式
     */
    void SetAspectRatioMode(AspectRatioMode mode);
    
    /**
     * @brief 获取当前渲染器类型
     */
    const char* GetCurrentRendererType() const;
    
    /**
     * @brief 检查渲染器是否已初始化
     */
    bool IsRendererReady() const;

private:
    PlayerCore* player_;                            // 播放器核心（不拥有）
    std::unique_ptr<HXCIVideoRenderer> renderer_;      // 渲染器实例
    std::thread render_thread_;                     // 渲染线程
    std::atomic<bool> running_;                     // 渲染线程运行标志
    
    void RenderLoop();  // 渲染线程主循环
};

} // namespace windows
} // namespace hxcplayer

#endif // WINDOWS_PLAYER_VIEW_H
