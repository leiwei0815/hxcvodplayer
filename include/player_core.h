/**
 * @file player_core.h
 * @brief 播放器核心类（参照 ffplay 架构）
 */

#ifndef YXVODPLAYER_PLAYER_CORE_H
#define YXVODPLAYER_PLAYER_CORE_H

#include "player_types.h"
#include "packet_queue.h"
#include "frame_queue.h"
#include "decoder.h"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>

extern "C" {
#include <libavformat/avformat.h>
#include <SDL2/SDL.h>
}

namespace yxplayer {

/**
 * @brief 播放器核心类
 * 负责媒体文件的解复用、解码、同步和播放控制
 */
class PlayerCore {
public:
    PlayerCore();
    ~PlayerCore();
    
    // 设置配置
    void set_config(const PlayerConfig& config) { config_ = config; }
    const PlayerConfig& get_config() const { return config_; }
    
    // 打开媒体文件
    int open(const std::string& filename);
    
    // 关闭
    void close();
    
    // 播放控制
    void play();
    void pause();
    void stop();
    void seek(double pos);  // 秒
    
    // 获取状态
    PlayerState get_state() const { return state_; }
    const MediaInfo& get_media_info() const { return media_info_; }
    double get_position() const;    // 当前播放位置（秒）
    double get_duration() const;    // 总时长（秒）
    
    // 获取帧队列（用于渲染）
    FrameQueue<VideoFrame>* get_video_queue() { return video_queue_.get(); }
    FrameQueue<AudioFrame>* get_audio_queue() { return audio_queue_.get(); }
    
    // 音量控制
    void set_volume(int volume);  // 0-100
    int get_volume() const { return volume_; }
    
    // ⚠️ 视频显示模式管理
    void set_aspect_ratio_mode(AspectRatioMode mode) { aspect_ratio_mode_ = mode; }
    AspectRatioMode get_aspect_ratio_mode() const { return aspect_ratio_mode_; }
    
    // 事件回调
    using StateChangedCallback = std::function<void(PlayerState)>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using PositionChangedCallback = std::function<void(double)>;
    
    void set_state_changed_callback(StateChangedCallback callback) {
        state_changed_callback_ = callback;
    }
    
    void set_error_callback(ErrorCallback callback) {
        error_callback_ = callback;
    }
    
    void set_position_changed_callback(PositionChangedCallback callback) {
        position_changed_callback_ = callback;
    }

private:
    // 读取线程（解复用）
    void read_thread();
    
    // 解码线程
    void video_thread();
    void audio_thread();  // ⚠️ 新增：音频解码线程
    
    // SDL 音频回调（只负责从队列取数据）
    static void audio_callback(void* userdata, uint8_t* stream, int len);
    void audio_callback_impl(uint8_t* stream, int len);
    
    // 同步控制
    double get_master_clock() const;
    void update_video_pts(double pts, int serial);
    void update_audio_pts(double pts, int serial);
    
    // 流打开
    int stream_component_open(int stream_index);
    void stream_component_close(int stream_index);
    
    // 状态更新
    void set_state(PlayerState state);
    void emit_error(const std::string& error);

private:
    PlayerConfig config_;
    PlayerState state_;
    MediaInfo media_info_;
    
    // ⚠️ 视频显示模式（核心层管理，UI 层读取）
    AspectRatioMode aspect_ratio_mode_;
    
    // FFmpeg 对象
    AVFormatContext* format_ctx_;
    int video_stream_;
    int audio_stream_;
    int subtitle_stream_;
    
    AVCodecContext* video_codec_ctx_;
    AVCodecContext* audio_codec_ctx_;
    
    // 数据包队列
    std::unique_ptr<PacketQueue> video_packet_queue_;
    std::unique_ptr<PacketQueue> audio_packet_queue_;
    std::unique_ptr<PacketQueue> subtitle_packet_queue_;
    
    // 帧队列
    std::unique_ptr<FrameQueue<VideoFrame>> video_queue_;
    std::unique_ptr<FrameQueue<AudioFrame>> audio_queue_;
    
    // 解码器
    std::unique_ptr<VideoDecoder> video_decoder_;
    std::unique_ptr<AudioDecoder> audio_decoder_;
    
    // 时钟
    Clock video_clock_;
    Clock audio_clock_;
    Clock external_clock_;
    
    // 线程
    std::thread read_thread_;
    std::thread video_thread_;
    std::thread audio_thread_;  // ⚠️ 新增：音频解码线程
    
    // 控制标志
    std::atomic<bool> abort_request_;
    std::atomic<bool> pause_request_;
    std::atomic<bool> seek_request_;
    std::atomic<double> seek_pos_;
    
    // SDL 音频
    SDL_AudioDeviceID audio_dev_;
    int volume_;
    
    // 重采样上下文
    SwrContext* swr_ctx_;
    uint8_t* audio_buf_;
    unsigned int audio_buf_size_;
    unsigned int audio_buf_index_;
    
    // 回调
    StateChangedCallback state_changed_callback_;
    ErrorCallback error_callback_;
    PositionChangedCallback position_changed_callback_;
};

} // namespace yxplayer

#endif // YXVODPLAYER_PLAYER_CORE_H
