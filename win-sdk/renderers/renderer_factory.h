/**
 * @file renderer_factory.h
 * @brief 渲染器工厂 - 创建和管理不同类型的渲染器
 */

#ifndef RENDERER_FACTORY_H
#define RENDERER_FACTORY_H

#include "video_renderer_interface.h"
#include <memory>

namespace hxcplayer {
namespace windows {

/**
 * @brief 渲染器类型枚举
 */
enum class HXCRendererType {
    Auto,      // 自动选择（优先 D3D11 > OpenGL）
    D3D11,     // Direct3D 11（推荐）
    OpenGL,    // OpenGL 3.3+（兼容性）
    // Vulkan, // 未来可扩展
};

/**
 * @brief 渲染器工厂类
 */
class HXCRendererFactory {
public:
    /**
     * @brief 创建渲染器实例
     * @param type 渲染器类型
     * @param window 窗口句柄 (HWND)
     * @return 渲染器实例，失败返回 nullptr
     */
    static std::unique_ptr<HXCIVideoRenderer> Create(HXCRendererType type, void* window);
    
    /**
     * @brief 检查指定类型的渲染器是否可用
     * @param type 渲染器类型
     * @return true=可用，false=不可用
     */
    static bool IsRendererAvailable(HXCRendererType type);
    
    /**
     * @brief 获取渲染器类型的名称
     */
    static const char* GetRendererTypeName(HXCRendererType type);
};

} // namespace windows
} // namespace hxcplayer

#endif // RENDERER_FACTORY_H
