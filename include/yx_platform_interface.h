/**
 * @file platform_interface.h
 * @brief 平台抽象接口
 */

#ifndef YXVODPLAYER_PLATFORM_INTERFACE_H
#define YXVODPLAYER_PLATFORM_INTERFACE_H

#include "yx_player_types.h"
#include <string>

namespace yxplayer {

/**
 * @brief 视频渲染接口
 */
class IVideoRenderer {
public:
    virtual ~IVideoRenderer() = default;
    
    // 初始化渲染器
    virtual bool init(int width, int height, PixelFormat format) = 0;
    
    // 渲染一帧
    virtual bool render_frame(const VideoFrame* frame) = 0;
    
    // 更新窗口大小
    virtual void resize(int width, int height) = 0;
    
    // 清空
    virtual void clear() = 0;
    
    // 释放资源
    virtual void destroy() = 0;
};

/**
 * @brief 音频渲染接口
 */
class IAudioRenderer {
public:
    virtual ~IAudioRenderer() = default;
    
    // 初始化音频设备
    virtual bool init(int sample_rate, int channels, int samples) = 0;
    
    // 播放音频
    virtual bool play_audio(const uint8_t* data, int len) = 0;
    
    // 暂停/恢复
    virtual void pause(bool pause) = 0;
    
    // 设置音量 (0-100)
    virtual void set_volume(int volume) = 0;
    
    // 释放资源
    virtual void destroy() = 0;
};

/**
 * @brief UI 接口
 */
class IPlayerUI {
public:
    virtual ~IPlayerUI() = default;
    
    // 显示/隐藏控制条
    virtual void show_controls(bool show) = 0;
    
    // 更新播放进度
    virtual void update_progress(double position, double duration) = 0;
    
    // 更新播放状态
    virtual void update_state(PlayerState state) = 0;
    
    // 显示错误信息
    virtual void show_error(const std::string& error) = 0;
    
    // 显示缓冲状态
    virtual void show_buffering(bool buffering) = 0;
    
    // 更新音量显示
    virtual void update_volume(int volume) = 0;
};

/**
 * @brief 平台工厂接口
 */
class IPlatformFactory {
public:
    virtual ~IPlatformFactory() = default;
    
    // 创建视频渲染器
    virtual IVideoRenderer* create_video_renderer() = 0;
    
    // 创建音频渲染器
    virtual IAudioRenderer* create_audio_renderer() = 0;
    
    // 创建 UI
    virtual IPlayerUI* create_player_ui() = 0;
    
    // 获取平台名称
    virtual std::string get_platform_name() const = 0;
};

} // namespace yxplayer

#endif // YXVODPLAYER_PLATFORM_INTERFACE_H
