/**
 * @file windows_player_view.cpp
 * @brief Windows 播放器视图管理器实现
 */

#include "windows_player_view.h"
#include "hxc_logger.h"
#include <chrono>
#include <thread>

namespace hxcplayer {
namespace windows {

HXCWindowsPlayerView::HXCWindowsPlayerView(PlayerCore* player)
    : player_(player)
    , running_(false) {
    LOG_INFO("HXCWindowsPlayerView 已创建");
}

HXCWindowsPlayerView::~HXCWindowsPlayerView() {
    StopRendering();
    renderer_.reset();
    LOG_INFO("HXCWindowsPlayerView 已销毁");
}

bool HXCWindowsPlayerView::SetWindow(void* window, HXCRendererType renderer_type) {
    if (!window) {
        LOG_ERROR("SetWindow: 无效的窗口句柄");
        return false;
    }
    
    // 停止旧的渲染线程
    StopRendering();
    
    // 创建新渲染器
    renderer_ = HXCRendererFactory::Create(renderer_type, window);
    if (!renderer_) {
        LOG_ERROR("SetWindow: 创建渲染器失败");
        return false;
    }
    
    LOG_INFO("SetWindow: 渲染器已设置 (", renderer_->GetType(), ")");
    
    // 自动启动渲染线程
    StartRendering();
    
    return true;
}

void HXCWindowsPlayerView::StartRendering() {
    if (running_) {
        LOG_WARNING("StartRendering: 渲染线程已在运行");
        return;
    }
    
    if (!renderer_ || !renderer_->IsInitialized()) {
        LOG_ERROR("StartRendering: 渲染器未初始化");
        return;
    }
    
    running_ = true;
    render_thread_ = std::thread(&HXCWindowsPlayerView::RenderLoop, this);
    LOG_INFO("StartRendering: 渲染线程已启动");
}

void HXCWindowsPlayerView::StopRendering() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    if (render_thread_.joinable()) {
        render_thread_.join();
    }
    LOG_INFO("StopRendering: 渲染线程已停止");
}

void HXCWindowsPlayerView::RenderLoop() {
    LOG_INFO("RenderLoop: 进入渲染循环");
    
    int frame_count = 0;
    auto start_time = std::chrono::steady_clock::now();
    bool logged_queue_warning = false;  // 只记录一次警告
    
    while (running_) {
        if (!player_ || !renderer_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        // 获取视频队列
        auto* video_queue = player_->get_video_queue();
        if (!video_queue) {
            if (!logged_queue_warning) {
                LOG_WARNING("RenderLoop: video_queue 为空（等待视频打开...）");
                logged_queue_warning = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // 队列存在后重置警告标志
        if (logged_queue_warning) {
            LOG_INFO("RenderLoop: video_queue 已创建");
            logged_queue_warning = false;
        }
        
        if (video_queue->size() <= 0) {
            // 队列为空，等待新帧
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        
        // 查看当前帧
        auto* vf = video_queue->peek_readable();
        if (!vf || !vf->frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        
        AVFrame* frame = vf->frame;
        
        // 首帧调试
        if (frame_count == 0) {
            LOG_INFO("RenderLoop: 开始渲染第一帧 (", frame->width, "x", frame->height, ")");
        }
        
        // 渲染帧
        bool success = renderer_->RenderFrame(
            frame->data[0], frame->data[1], frame->data[2],
            frame->width, frame->height,
            frame->linesize[0], frame->linesize[1], frame->linesize[2]
        );
        
        if (success) {
            frame_count++;
            
            // 每秒输出一次 FPS
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            if (elapsed >= 5) {
                double fps = frame_count / static_cast<double>(elapsed);
                LOG_INFO("RenderLoop: 渲染 FPS = ", fps);
                frame_count = 0;
                start_time = now;
            }
        } else {
            LOG_ERROR("RenderLoop: 渲染帧失败");
        }
        
        // 消费帧
        video_queue->next();
        
        // 短暂等待（避免 CPU 占用过高）
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    LOG_INFO("RenderLoop: 退出渲染循环，共渲染 ", frame_count, " 帧");
}

void HXCWindowsPlayerView::OnWindowResize(int width, int height) {
    if (renderer_ && renderer_->IsInitialized()) {
        renderer_->OnResize(width, height);
    }
}

void HXCWindowsPlayerView::SetAspectRatioMode(AspectRatioMode mode) {
    if (renderer_ && renderer_->IsInitialized()) {
        renderer_->SetAspectRatioMode(mode);
        LOG_INFO("SetAspectRatioMode: ", 
                 mode == AspectRatioMode::Fit ? "Fit" : 
                 mode == AspectRatioMode::Fill ? "Fill" : "Stretch");
    }
}

const char* HXCWindowsPlayerView::GetCurrentRendererType() const {
    if (renderer_) {
        return renderer_->GetType();
    }
    return "None";
}

bool HXCWindowsPlayerView::IsRendererReady() const {
    return renderer_ && renderer_->IsInitialized();
}

} // namespace windows
} // namespace hxcplayer
