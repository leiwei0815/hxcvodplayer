/**
 * @file d3d11_video_renderer.cpp
 * @brief Direct3D 11 视频渲染器实现
 */

#include "d3d11_video_renderer.h"
#include "hxc_logger.h"
#include <d3dcompiler.h>
#include <iostream>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace hxcplayer {
namespace windows {

// 顶点结构
struct Vertex {
    float pos[3];    // 位置 (x, y, z)
    float tex[2];    // 纹理坐标 (u, v)
};

HXCD3D11VideoRenderer::HXCD3D11VideoRenderer()
    : window_(nullptr)
    , window_width_(0)
    , window_height_(0)
    , video_width_(0)
    , video_height_(0)
    , initialized_(false)
    , aspect_ratio_mode_(AspectRatioMode::Fit) {  // 默认适应模式
}

HXCD3D11VideoRenderer::~HXCD3D11VideoRenderer() {
    Cleanup();
}

bool HXCD3D11VideoRenderer::Initialize(void* window) {
    if (initialized_) {
        return true;
    }
    
    window_ = static_cast<HWND>(window);
    if (!window_ || !IsWindow(window_)) {
        std::cerr << "[D3D11] 无效的窗口句柄" << std::endl;
        return false;
    }
    
    // 获取窗口大小
    RECT rect;
    GetClientRect(window_, &rect);
    window_width_ = rect.right - rect.left;
    window_height_ = rect.bottom - rect.top;
    
    // 创建 D3D11 设备
    if (!CreateDevice()) {
        std::cerr << "[D3D11] 创建设备失败" << std::endl;
        return false;
    }
    
    // 创建交换链
    if (!CreateSwapChain()) {
        std::cerr << "[D3D11] 创建交换链失败" << std::endl;
        return false;
    }
    
    // 创建渲染目标视图
    if (!CreateRenderTarget()) {
        std::cerr << "[D3D11] 创建渲染目标失败" << std::endl;
        return false;
    }
    
    // 创建 Shader
    if (!CreateShaders()) {
        std::cerr << "[D3D11] 创建 Shader 失败" << std::endl;
        return false;
    }
    
    // 创建顶点缓冲区
    if (!CreateVertexBuffer()) {
        std::cerr << "[D3D11] 创建顶点缓冲区失败" << std::endl;
        return false;
    }
    
    // 创建采样器
    if (!CreateSamplerState()) {
        std::cerr << "[D3D11] 创建采样器失败" << std::endl;
        return false;
    }
    
    initialized_ = true;
    std::cout << "[D3D11] 渲染器初始化成功 (" << window_width_ << "x" << window_height_ << ")" << std::endl;
    return true;
}

bool HXCD3D11VideoRenderer::CreateDevice() {
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    
    D3D_FEATURE_LEVEL feature_level;
    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // 使用默认适配器
        D3D_DRIVER_TYPE_HARDWARE,   // 硬件加速
        nullptr,
        flags,
        feature_levels,
        ARRAYSIZE(feature_levels),
        D3D11_SDK_VERSION,
        &device_,
        &feature_level,
        &context_
    );
    
    if (FAILED(hr)) {
        std::cerr << "[D3D11] D3D11CreateDevice 失败: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    std::cout << "[D3D11] 设备创建成功，Feature Level: " << std::hex << feature_level << std::endl;
    return true;
}

bool HXCD3D11VideoRenderer::CreateSwapChain() {
    // 获取 DXGI 工厂
    ComPtr<IDXGIDevice> dxgi_device;
    HRESULT hr = device_.As(&dxgi_device);
    if (FAILED(hr)) return false;
    
    ComPtr<IDXGIAdapter> adapter;
    hr = dxgi_device->GetAdapter(&adapter);
    if (FAILED(hr)) return false;
    
    ComPtr<IDXGIFactory> factory;
    hr = adapter->GetParent(__uuidof(IDXGIFactory), &factory);
    if (FAILED(hr)) return false;
    
    // 配置交换链描述
    DXGI_SWAP_CHAIN_DESC swap_chain_desc = {};
    swap_chain_desc.BufferCount = 2;  // 双缓冲
    swap_chain_desc.BufferDesc.Width = window_width_;
    swap_chain_desc.BufferDesc.Height = window_height_;
    swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_chain_desc.BufferDesc.RefreshRate.Numerator = 60;
    swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
    swap_chain_desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;  // 不缩放
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.OutputWindow = window_;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.SampleDesc.Quality = 0;
    swap_chain_desc.Windowed = TRUE;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // 现代交换效果
    swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;  // 允许 tearing（减少延迟）
    
    hr = factory->CreateSwapChain(device_.Get(), &swap_chain_desc, &swap_chain_);
    if (FAILED(hr)) {
        std::cerr << "[D3D11] CreateSwapChain 失败: 0x" << std::hex << hr << std::endl;
        return false;
    }
    
    // 禁用 Alt+Enter 全屏切换
    factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);
    
    return true;
}

bool HXCD3D11VideoRenderer::CreateRenderTarget() {
    // 获取后缓冲区
    ComPtr<ID3D11Texture2D> back_buffer;
    HRESULT hr = swap_chain_->GetBuffer(0, __uuidof(ID3D11Texture2D), &back_buffer);
    if (FAILED(hr)) {
        std::cerr << "[D3D11] GetBuffer 失败" << std::endl;
        return false;
    }
    
    // 创建渲染目标视图
    hr = device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &render_target_view_);
    if (FAILED(hr)) {
        std::cerr << "[D3D11] CreateRenderTargetView 失败" << std::endl;
        return false;
    }
    
    // 设置渲染目标
    context_->OMSetRenderTargets(1, render_target_view_.GetAddressOf(), nullptr);
    
    // 更新视口（根据宽高比模式）
    UpdateViewport();
    
    return true;
}

void HXCD3D11VideoRenderer::UpdateViewport() {
    // Viewport 始终填满整个窗口（不用于缩放）
    D3D11_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(window_width_);
    viewport.Height = static_cast<float>(window_height_);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    context_->RSSetViewports(1, &viewport);
    
    // 宽高比通过调整顶点坐标实现（在 RenderFrame 中更新）
}

// YUV→RGB Pixel Shader（BT.709 色彩空间）
const char* HXCD3D11VideoRenderer::GetPixelShaderSource() {
    return R"(
Texture2D yTexture : register(t0);
Texture2D uTexture : register(t1);
Texture2D vTexture : register(t2);
SamplerState samplerState : register(s0);

struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float y = yTexture.Sample(samplerState, input.tex).r;
    float u = uTexture.Sample(samplerState, input.tex).r - 0.5;
    float v = vTexture.Sample(samplerState, input.tex).r - 0.5;
    
    // BT.709 YUV → RGB 转换矩阵
    float r = y + 1.5748 * v;
    float g = y - 0.1873 * u - 0.4681 * v;
    float b = y + 1.8556 * u;
    
    return float4(r, g, b, 1.0);
}
)";
}

// Vertex Shader
const char* HXCD3D11VideoRenderer::GetVertexShaderSource() {
    return R"(
struct VS_INPUT {
    float3 pos : POSITION;
    float2 tex : TEXCOORD0;
};

struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.pos = float4(input.pos, 1.0);
    output.tex = input.tex;
    return output;
}
)";
}

bool HXCD3D11VideoRenderer::CreateShaders() {
    HRESULT hr;
    
    // 编译 Vertex Shader
    ComPtr<ID3DBlob> vs_blob;
    ComPtr<ID3DBlob> error_blob;
    
    hr = D3DCompile(
        GetVertexShaderSource(),
        strlen(GetVertexShaderSource()),
        nullptr, nullptr, nullptr,
        "main", "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS, 0,
        &vs_blob, &error_blob
    );
    
    if (FAILED(hr)) {
        if (error_blob) {
            std::cerr << "[D3D11] Vertex Shader 编译失败: " 
                     << (char*)error_blob->GetBufferPointer() << std::endl;
        }
        return false;
    }
    
    hr = device_->CreateVertexShader(
        vs_blob->GetBufferPointer(),
        vs_blob->GetBufferSize(),
        nullptr,
        &vertex_shader_
    );
    if (FAILED(hr)) return false;
    
    // 编译 Pixel Shader
    ComPtr<ID3DBlob> ps_blob;
    hr = D3DCompile(
        GetPixelShaderSource(),
        strlen(GetPixelShaderSource()),
        nullptr, nullptr, nullptr,
        "main", "ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS, 0,
        &ps_blob, &error_blob
    );
    
    if (FAILED(hr)) {
        if (error_blob) {
            std::cerr << "[D3D11] Pixel Shader 编译失败: " 
                     << (char*)error_blob->GetBufferPointer() << std::endl;
        }
        return false;
    }
    
    hr = device_->CreatePixelShader(
        ps_blob->GetBufferPointer(),
        ps_blob->GetBufferSize(),
        nullptr,
        &pixel_shader_
    );
    if (FAILED(hr)) return false;
    
    // 创建输入布局
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    
    hr = device_->CreateInputLayout(
        layout,
        ARRAYSIZE(layout),
        vs_blob->GetBufferPointer(),
        vs_blob->GetBufferSize(),
        &input_layout_
    );
    
    return SUCCEEDED(hr);
}

bool HXCD3D11VideoRenderer::CreateVertexBuffer() {
    // 创建动态顶点缓冲区（用于更新宽高比）
    D3D11_BUFFER_DESC buffer_desc = {};
    buffer_desc.Usage = D3D11_USAGE_DYNAMIC;  // 动态更新
    buffer_desc.ByteWidth = sizeof(Vertex) * 4;
    buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    buffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;  // CPU 可写
    
    HRESULT hr = device_->CreateBuffer(&buffer_desc, nullptr, &vertex_buffer_);
    return SUCCEEDED(hr);
}

bool HXCD3D11VideoRenderer::CreateSamplerState() {
    D3D11_SAMPLER_DESC sampler_desc = {};
    // 使用简单的双线性过滤（最兼容）
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampler_desc.MinLOD = 0;
    sampler_desc.MaxLOD = 0;
    
    HRESULT hr = device_->CreateSamplerState(&sampler_desc, &sampler_state_);
    return SUCCEEDED(hr);
}

bool HXCD3D11VideoRenderer::CreateYUVTextures(int width, int height) {
    HRESULT hr;
    
    // 创建 Y 纹理 (width x height)
    D3D11_TEXTURE2D_DESC y_desc = {};
    y_desc.Width = width;
    y_desc.Height = height;
    y_desc.MipLevels = 1;
    y_desc.ArraySize = 1;
    y_desc.Format = DXGI_FORMAT_R8_UNORM;
    y_desc.SampleDesc.Count = 1;
    y_desc.Usage = D3D11_USAGE_DYNAMIC;
    y_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    y_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    
    hr = device_->CreateTexture2D(&y_desc, nullptr, &y_texture_);
    if (FAILED(hr)) return false;
    
    hr = device_->CreateShaderResourceView(y_texture_.Get(), nullptr, &y_srv_);
    if (FAILED(hr)) return false;
    
    // 创建 U 纹理 (width/2 x height/2)
    D3D11_TEXTURE2D_DESC uv_desc = y_desc;
    uv_desc.Width = width / 2;
    uv_desc.Height = height / 2;
    
    hr = device_->CreateTexture2D(&uv_desc, nullptr, &u_texture_);
    if (FAILED(hr)) return false;
    
    hr = device_->CreateShaderResourceView(u_texture_.Get(), nullptr, &u_srv_);
    if (FAILED(hr)) return false;
    
    // 创建 V 纹理 (width/2 x height/2)
    hr = device_->CreateTexture2D(&uv_desc, nullptr, &v_texture_);
    if (FAILED(hr)) return false;
    
    hr = device_->CreateShaderResourceView(v_texture_.Get(), nullptr, &v_srv_);
    if (FAILED(hr)) return false;
    
    video_width_ = width;
    video_height_ = height;
    
    std::cout << "[D3D11] YUV 纹理已创建: " << width << "x" << height << std::endl;
    return true;
}

bool HXCD3D11VideoRenderer::UpdateYUVTextures(
    const uint8_t* y_data, const uint8_t* u_data, const uint8_t* v_data,
    int width, int height,
    int y_linesize, int u_linesize, int v_linesize
) {
    // 如果尺寸改变，重建纹理
    if (width != video_width_ || height != video_height_) {
        y_texture_.Reset();
        u_texture_.Reset();
        v_texture_.Reset();
        y_srv_.Reset();
        u_srv_.Reset();
        v_srv_.Reset();
        
        if (!CreateYUVTextures(width, height)) {
            return false;
        }
        
        // 视频尺寸变化，更新视口
        UpdateViewport();
    }
    
    HRESULT hr;
    D3D11_MAPPED_SUBRESOURCE mapped;
    
    // 更新 Y 纹理
    hr = context_->Map(y_texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
        for (int y = 0; y < height; ++y) {
            memcpy(dst + y * mapped.RowPitch, y_data + y * y_linesize, width);
        }
        context_->Unmap(y_texture_.Get(), 0);
    }
    
    // 更新 U 纹理
    hr = context_->Map(u_texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
        int uv_width = width / 2;
        int uv_height = height / 2;
        for (int y = 0; y < uv_height; ++y) {
            memcpy(dst + y * mapped.RowPitch, u_data + y * u_linesize, uv_width);
        }
        context_->Unmap(u_texture_.Get(), 0);
    }
    
    // 更新 V 纹理
    hr = context_->Map(v_texture_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        uint8_t* dst = static_cast<uint8_t*>(mapped.pData);
        int uv_width = width / 2;
        int uv_height = height / 2;
        for (int y = 0; y < uv_height; ++y) {
            memcpy(dst + y * mapped.RowPitch, v_data + y * v_linesize, uv_width);
        }
        context_->Unmap(v_texture_.Get(), 0);
    }
    
    return true;
}

bool HXCD3D11VideoRenderer::RenderFrame(
    const uint8_t* y_data, const uint8_t* u_data, const uint8_t* v_data,
    int width, int height,
    int y_linesize, int u_linesize, int v_linesize
) {
    if (!initialized_) {
        return false;
    }
    
    // 更新 YUV 纹理数据
    if (!UpdateYUVTextures(y_data, u_data, v_data, width, height, 
                          y_linesize, u_linesize, v_linesize)) {
        return false;
    }
    
    // ⚠️ 重新绑定渲染目标（FLIP 模式的 Present 会解绑）
    context_->OMSetRenderTargets(1, render_target_view_.GetAddressOf(), nullptr);
    
    // 清除渲染目标（黑色背景）
    float clear_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    context_->ClearRenderTargetView(render_target_view_.Get(), clear_color);
    
    // 计算顶点坐标（根据宽高比模式）
    float left = -1.0f, right = 1.0f, top = 1.0f, bottom = -1.0f;
    
    if (video_width_ > 0 && video_height_ > 0 && window_width_ > 0 && window_height_ > 0) {
        float video_aspect = static_cast<float>(video_width_) / video_height_;
        float window_aspect = static_cast<float>(window_width_) / window_height_;
        
        static bool first_log = true;
        if (first_log) {
            LOG_INFO("[D3D11] 视频尺寸: ", video_width_, "x", video_height_, ", 宽高比: ", video_aspect);
            LOG_INFO("[D3D11] 窗口尺寸: ", window_width_, "x", window_height_, ", 宽高比: ", window_aspect);
            LOG_INFO("[D3D11] 当前模式: ", 
                     (aspect_ratio_mode_ == AspectRatioMode::Fit ? "Fit" : 
                      aspect_ratio_mode_ == AspectRatioMode::Fill ? "Fill" : "Stretch"));
            first_log = false;
        }
        
        switch (aspect_ratio_mode_) {
            case AspectRatioMode::Fit: {
                // 适应模式：保持比例，完整显示
                if (video_aspect > window_aspect) {
                    // 视频更宽，上下留黑边
                    float scale = window_aspect / video_aspect;
                    top = scale;
                    bottom = -scale;
                    LOG_INFO("[D3D11 Fit] 视频更宽，上下黑边，scale=", scale);
                } else {
                    // 视频更高，左右留黑边
                    float scale = video_aspect / window_aspect;
                    left = -scale;
                    right = scale;
                    LOG_INFO("[D3D11 Fit] 视频更高，左右黑边，scale=", scale);
                }
                break;
            }
            
            case AspectRatioMode::Fill: {
                // 填充模式：保持比例，填满窗口（可能裁剪）
                if (video_aspect > window_aspect) {
                    // 视频更宽，左右裁剪
                    float scale = video_aspect / window_aspect;
                    left = -scale;
                    right = scale;
                    LOG_INFO("[D3D11 Fill] 视频更宽，左右裁剪，scale=", scale);
                } else {
                    // 视频更高，上下裁剪
                    float scale = window_aspect / video_aspect;
                    top = scale;
                    bottom = -scale;
                    LOG_INFO("[D3D11 Fill] 视频更高，上下裁剪，scale=", scale);
                }
                break;
            }
            
            case AspectRatioMode::Stretch:
            default: {
                // 拉伸模式：填满整个窗口（不保持比例）
                // 使用默认值即可
                break;
            }
        }
    } else {
        LOG_WARNING("[D3D11] 尺寸无效 - video:", video_width_, "x", video_height_, 
                   ", window:", window_width_, "x", window_height_);
    }
    
    // 更新顶点缓冲区
    Vertex vertices[] = {
        { { left,  top,    0.0f }, { 0.0f, 0.0f } },  // 左上
        { { right, top,    0.0f }, { 1.0f, 0.0f } },  // 右上
        { { left,  bottom, 0.0f }, { 0.0f, 1.0f } },  // 左下
        { { right, bottom, 0.0f }, { 1.0f, 1.0f } },  // 右下
    };
    
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = context_->Map(vertex_buffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, vertices, sizeof(vertices));
        context_->Unmap(vertex_buffer_.Get(), 0);
    } else {
        static bool logged_error = false;
        if (!logged_error) {
            LOG_ERROR("[D3D11] 更新顶点缓冲区失败: 0x", std::hex, hr);
            logged_error = true;
        }
    }
    
    // 设置 Shader
    context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    
    // 设置纹理
    ID3D11ShaderResourceView* srvs[] = { y_srv_.Get(), u_srv_.Get(), v_srv_.Get() };
    context_->PSSetShaderResources(0, 3, srvs);
    context_->PSSetSamplers(0, 1, sampler_state_.GetAddressOf());
    
    // 设置顶点缓冲区
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    context_->IASetVertexBuffers(0, 1, vertex_buffer_.GetAddressOf(), &stride, &offset);
    context_->IASetInputLayout(input_layout_.Get());
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    
    // 绘制
    context_->Draw(4, 0);
    
    // Present（Vsync）
    swap_chain_->Present(1, 0);
    
    return true;
}

void HXCD3D11VideoRenderer::SetAspectRatioMode(AspectRatioMode mode) {
    if (aspect_ratio_mode_ != mode) {
        aspect_ratio_mode_ = mode;
        LOG_INFO("[D3D11] 宽高比模式已更改为: ", 
                (mode == AspectRatioMode::Fit ? "Fit(适应)" : 
                 mode == AspectRatioMode::Fill ? "Fill(填充)" : "Stretch(拉伸)"));
        
        if (initialized_) {
            UpdateViewport();
            
            // 输出当前状态用于调试
            if (video_width_ > 0 && video_height_ > 0) {
                float video_aspect = static_cast<float>(video_width_) / video_height_;
                float window_aspect = static_cast<float>(window_width_) / window_height_;
                LOG_INFO("[D3D11] 当前状态 - 视频:", video_width_, "x", video_height_, 
                        " (", video_aspect, "), 窗口:", window_width_, "x", window_height_, 
                        " (", window_aspect, ")");
            }
        }
    }
}

void HXCD3D11VideoRenderer::OnResize(int width, int height) {
    if (!initialized_) {
        return;
    }
    
    if (width <= 0 || height <= 0) {
        return;
    }
    
    window_width_ = width;
    window_height_ = height;
    
    // 释放旧的渲染目标
    render_target_view_.Reset();
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    
    // Resize 交换链缓冲区（flags 必须与创建时一致）
    HRESULT hr = swap_chain_->ResizeBuffers(
        0,  // 保持当前缓冲区数量
        width, height,
        DXGI_FORMAT_UNKNOWN,  // 保持当前格式
        DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING  // 必须与 CreateSwapChain 一致
    );
    
    if (FAILED(hr)) {
        LOG_ERROR("[D3D11] ResizeBuffers 失败: 0x", std::hex, hr);
        return;
    }
    
    // 重新创建渲染目标
    if (!CreateRenderTarget()) {
        LOG_ERROR("[D3D11] ResizeBuffers 成功但 CreateRenderTarget 失败");
        return;
    }
}

void HXCD3D11VideoRenderer::Cleanup() {
    if (!initialized_) {
        return;
    }
    
    // 释放所有 COM 对象（ComPtr 自动处理）
    y_srv_.Reset();
    u_srv_.Reset();
    v_srv_.Reset();
    y_texture_.Reset();
    u_texture_.Reset();
    v_texture_.Reset();
    
    sampler_state_.Reset();
    vertex_buffer_.Reset();
    input_layout_.Reset();
    pixel_shader_.Reset();
    vertex_shader_.Reset();
    
    render_target_view_.Reset();
    swap_chain_.Reset();
    context_.Reset();
    device_.Reset();
    
    initialized_ = false;
    std::cout << "[D3D11] 渲染器已清理" << std::endl;
}

} // namespace windows
} // namespace hxcplayer
