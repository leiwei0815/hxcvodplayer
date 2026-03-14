/**
 * @file d3d11_video_renderer.h
 * @brief Direct3D 11 视频渲染器
 * 
 * 使用 D3D11 实现高性能 YUV→RGB 渲染
 * - GPU 加速转换
 * - 无闪烁 resize
 * - 自动 Vsync
 */

#ifndef D3D11_VIDEO_RENDERER_H
#define D3D11_VIDEO_RENDERER_H

#include "video_renderer_interface.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

namespace hxcplayer {
namespace windows {

class HXCD3D11VideoRenderer : public HXCIVideoRenderer {
public:
    HXCD3D11VideoRenderer();
    ~HXCD3D11VideoRenderer() override;
    
    // HXCIVideoRenderer 接口实现
    bool Initialize(void* window) override;
    bool RenderFrame(
        const uint8_t* y_data, const uint8_t* u_data, const uint8_t* v_data,
        int width, int height,
        int y_linesize, int u_linesize, int v_linesize
    ) override;
    void OnResize(int width, int height) override;
    void SetAspectRatioMode(AspectRatioMode mode) override;
    void Cleanup() override;
    const char* GetType() const override { return "Direct3D 11"; }
    bool IsInitialized() const override { return initialized_; }

private:
    // D3D11 核心对象
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain> swap_chain_;
    ComPtr<ID3D11RenderTargetView> render_target_view_;
    
    // YUV 纹理
    ComPtr<ID3D11Texture2D> y_texture_;
    ComPtr<ID3D11Texture2D> u_texture_;
    ComPtr<ID3D11Texture2D> v_texture_;
    ComPtr<ID3D11ShaderResourceView> y_srv_;
    ComPtr<ID3D11ShaderResourceView> u_srv_;
    ComPtr<ID3D11ShaderResourceView> v_srv_;
    
    // Shader 和渲染状态
    ComPtr<ID3D11VertexShader> vertex_shader_;
    ComPtr<ID3D11PixelShader> pixel_shader_;
    ComPtr<ID3D11InputLayout> input_layout_;
    ComPtr<ID3D11Buffer> vertex_buffer_;
    ComPtr<ID3D11SamplerState> sampler_state_;
    
    // 窗口和状态
    HWND window_;
    int window_width_;
    int window_height_;
    int video_width_;
    int video_height_;
    bool initialized_;
    AspectRatioMode aspect_ratio_mode_;  // 宽高比模式
    
    // 初始化辅助函数
    bool CreateDevice();
    bool CreateSwapChain();
    bool CreateRenderTarget();
    bool CreateShaders();
    bool CreateVertexBuffer();
    bool CreateSamplerState();
    bool CreateYUVTextures(int width, int height);
    bool UpdateYUVTextures(
        const uint8_t* y_data, const uint8_t* u_data, const uint8_t* v_data,
        int width, int height,
        int y_linesize, int u_linesize, int v_linesize
    );
    
    // 视口计算
    void UpdateViewport();
    
    // Shader 源码
    static const char* GetVertexShaderSource();
    static const char* GetPixelShaderSource();
};

} // namespace windows
} // namespace hxcplayer

#endif // D3D11_VIDEO_RENDERER_H
