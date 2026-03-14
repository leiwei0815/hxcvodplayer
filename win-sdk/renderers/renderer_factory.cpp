#include "renderer_factory.h"
#include "d3d11_video_renderer.h"
// #include "opengl_video_renderer.h"  // TODO: 实现 OpenGL 渲染器
#include <iostream>

namespace hxcplayer {
namespace windows {

std::unique_ptr<HXCIVideoRenderer> HXCRendererFactory::Create(HXCRendererType type, void* window) {
    if (!window) {
        std::cerr << "[RendererFactory] 无效的窗口句柄" << std::endl;
        return nullptr;
    }
    
    // Auto: 尝试 D3D11 -> OpenGL
    if (type == HXCRendererType::Auto) {
        std::cout << "[RendererFactory] 自动选择渲染器..." << std::endl;
        
        // 优先尝试 D3D11
        auto d3d11_renderer = std::make_unique<HXCD3D11VideoRenderer>();
        if (d3d11_renderer->Initialize(window)) {
            std::cout << "[RendererFactory] ✓ 使用 D3D11 渲染器" << std::endl;
            return d3d11_renderer;
        }
        
        std::cerr << "[RendererFactory] D3D11 初始化失败，尝试 OpenGL..." << std::endl;
        
        // 降级到 OpenGL
        // TODO: OpenGL 实现
        // auto opengl_renderer = std::make_unique<HXCOpenGLVideoRenderer>();
        // if (opengl_renderer->Initialize(window)) {
        //     std::cout << "[RendererFactory] ✓ 使用 OpenGL 渲染器" << std::endl;
        //     return opengl_renderer;
        // }
        
        std::cerr << "[RendererFactory] ✗ 所有渲染器都不可用" << std::endl;
        return nullptr;
    }
    
    // 明确指定 D3D11
    if (type == HXCRendererType::D3D11) {
        auto renderer = std::make_unique<HXCD3D11VideoRenderer>();
        if (renderer->Initialize(window)) {
            std::cout << "[RendererFactory] ✓ D3D11 渲染器已创建" << std::endl;
            return renderer;
        }
        std::cerr << "[RendererFactory] ✗ D3D11 渲染器初始化失败" << std::endl;
        return nullptr;
    }
    
    // 明确指定 OpenGL
    if (type == HXCRendererType::OpenGL) {
        // TODO: OpenGL 实现
        // auto renderer = std::make_unique<HXCOpenGLVideoRenderer>();
        // if (renderer->Initialize(window)) {
        //     std::cout << "[RendererFactory] ✓ OpenGL 渲染器已创建" << std::endl;
        //     return renderer;
        // }
        std::cerr << "[RendererFactory] ✗ OpenGL 渲染器尚未实现" << std::endl;
        return nullptr;
    }
    
    return nullptr;
}

bool HXCRendererFactory::IsRendererAvailable(HXCRendererType type) {
    if (type == HXCRendererType::D3D11) {
        // 检查 D3D11 是否可用
        // 简单检查：尝试创建设备
        ID3D11Device* test_device = nullptr;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION,
            &test_device, nullptr, nullptr
        );
        if (SUCCEEDED(hr) && test_device) {
            test_device->Release();
            return true;
        }
        return false;
    }
    
    if (type == HXCRendererType::OpenGL) {
        // TODO: 检查 OpenGL 是否可用
        return false;  // 暂未实现
    }
    
    if (type == HXCRendererType::Auto) {
        return IsRendererAvailable(HXCRendererType::D3D11) || 
               IsRendererAvailable(HXCRendererType::OpenGL);
    }
    
    return false;
}

const char* HXCRendererFactory::GetRendererTypeName(HXCRendererType type) {
    switch (type) {
        case HXCRendererType::Auto: return "Auto";
        case HXCRendererType::D3D11: return "Direct3D 11";
        case HXCRendererType::OpenGL: return "OpenGL";
        default: return "Unknown";
    }
}

} // namespace windows
} // namespace hxcplayer
