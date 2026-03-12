/**
 * @file player_core.h
 * @brief 播放器核心类（参照 ffplay 架构）
 */

#ifndef YXVODPLAYER_PLAYER_CORE_H
#define YXVODPLAYER_PLAYER_CORE_H

#include "hxc_player_types.h"
#include "hxc_packet_queue.h"
#include "hxc_frame_queue.h"
#include "hxc_decoder.h"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>

extern "C" {
#include <libavformat/avformat.h>
#ifndef NO_SDL
#include <SDL2/SDL.h>
#endif
}

// ⚠️ SoundTouch 库（用于倍速播放，保持音调）
#if defined(__APPLE__) || defined(_WIN32) || defined(__ANDROID__)
#include <soundtouch/SoundTouch.h>
#endif

namespace hxcplayer {

// ⚠️ 播放器错误码定义
enum PlayerErrorCode {
    // 自定义错误码 (1-999)
    ERROR_NONE = 0,                      // 无错误
    ERROR_INVALID_URL = 1,               // 无效的 URL
    ERROR_OPEN_INPUT_FAILED = 2,         // 打开输入失败
    ERROR_FIND_STREAM_INFO_FAILED = 3,   // 查找流信息失败
    ERROR_NO_VIDEO_STREAM = 4,           // 没有视频流
    ERROR_NO_AUDIO_STREAM = 5,           // 没有音频流
    ERROR_CODEC_NOT_FOUND = 6,           // 找不到解码器
    ERROR_CODEC_OPEN_FAILED = 7,         // 打开解码器失败
    ERROR_ALLOC_CONTEXT_FAILED = 8,      // 分配上下文失败
    ERROR_SDL_INIT_FAILED = 9,           // SDL 初始化失败
    ERROR_AUDIO_DEVICE_OPEN_FAILED = 10, // 音频设备打开失败
    ERROR_SEEK_FAILED = 11,              // Seek 操作失败
    ERROR_READ_FRAME_FAILED = 12,        // 读取帧失败
    ERROR_DECODE_FAILED = 13,            // 解码失败
    ERROR_OUT_OF_MEMORY = 14,            // 内存不足
    ERROR_UNKNOWN = 999,                 // 未知错误
    
    // FFmpeg 错误码范围 (负数)
    // 使用 FFmpeg 原始错误码，例如：
    // AVERROR_EOF, AVERROR(ENOMEM), AVERROR(EINVAL) 等
    // 这些值保持为 FFmpeg 的负数错误码
};

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
    
    // ⚠️ 播放速率控制（倍速播放）
    void set_playback_rate(double rate);  // 0.5 ~ 2.0
    double get_playback_rate() const { return playback_rate_; }
    
    // ⚠️ 音频时钟更新（供 iOS/macOS/Android 平台使用）
    void update_audio_pts(double pts, int serial);
    
    // 事件回调
    using StateChangedCallback = std::function<void(PlayerState)>;
    using ErrorCallback = std::function<void(int, const std::string&)>;  // 错误码 + 错误信息
    using PositionChangedCallback = std::function<void(double)>;  // 真实播放位置
    using BufferProgressCallback = std::function<void(double)>;   // 缓冲进度（解码位置）
    using PlaybackCompletedCallback = std::function<void()>;      // 播放完成
    using LoadingCallback = std::function<void(bool)>;            // 网络加载状态（true=加载中，false=加载完成）
    
    void set_state_changed_callback(StateChangedCallback callback) {
        state_changed_callback_ = callback;
    }
    
    void set_error_callback(ErrorCallback callback) {
        error_callback_ = callback;
    }
    
    void set_position_changed_callback(PositionChangedCallback callback) {
        position_changed_callback_ = callback;
    }
    
    void set_buffer_progress_callback(BufferProgressCallback callback) {
        buffer_progress_callback_ = callback;
    }
    
    void set_playback_completed_callback(PlaybackCompletedCallback callback) {
        playback_completed_callback_ = callback;
    }
    
    void set_loading_callback(LoadingCallback callback) {
        loading_callback_ = callback;
    }

private:
    // 读取线程（解复用）
    void read_thread();
    
    // 解码线程
    void video_thread();
    void audio_thread();  // ⚠️ 新增：音频解码线程
    void progress_timer_thread();  // ⚠️ 播放进度回调定时器线程
    
#ifndef NO_SDL
    // SDL 音频回调（只负责从队列取数据）- 仅桌面平台
    static void audio_callback(void* userdata, uint8_t* stream, int len);
    void audio_callback_impl(uint8_t* stream, int len);
#endif
    
    // 同步控制
    double get_master_clock() const;
    void update_video_pts(double pts, int serial);
    
    // 流打开
    int stream_component_open(int stream_index);
    void stream_component_close(int stream_index);
    
    // 状态更新
    void set_state(PlayerState state);
    void emit_error(int error_code, const std::string& error_msg);

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
    std::thread progress_timer_thread_;  // ⚠️ 播放进度回调定时器线程
    
    // 控制标志
    std::atomic<bool> abort_request_;
    std::atomic<bool> pause_request_;
    std::atomic<bool> seek_request_;
    std::atomic<double> seek_pos_;
    std::atomic<bool> seeking_;  // ⚠️ 标识正在 seek，暂停进度回调
    std::atomic<double> seek_target_pos_;  // ⚠️ seek 的目标位置，在 seek 期间返回此值
    std::atomic<bool> decode_finished_;  // ⚠️ 标识视频解码已结束（用于判断播放完成）
    bool playback_completed_notified_;  // ⚠️ 是否已通知播放完成（避免重复通知）
    
#ifndef NO_SDL
    // SDL 音频（仅桌面平台）
    SDL_AudioDeviceID audio_dev_;
#endif
    int volume_;
    
    // 重采样上下文
    SwrContext* swr_ctx_;
    uint8_t* audio_buf_;
    unsigned int audio_buf_size_;
    unsigned int audio_buf_index_;
    
    // ⚠️ 音频时钟跟踪变量
    double audio_current_pts_;          // 当前音频帧的 PTS
    double audio_current_pts_drift_;    // PTS 漂移
    
    // ⚠️ 倍速播放支持（SoundTouch）
#if defined(__APPLE__) || defined(_WIN32) || defined(__ANDROID__)
    soundtouch::SoundTouch* soundtouch_;
    std::vector<float> soundtouch_buffer_;  // SoundTouch 输出缓冲区
    size_t soundtouch_buffer_index_;        // 当前读取位置
#endif
    std::atomic<double> playback_rate_;     // 当前播放速率
    
    // 回调
    StateChangedCallback state_changed_callback_;
    ErrorCallback error_callback_;
    PositionChangedCallback position_changed_callback_;  // 真实播放位置回调
    BufferProgressCallback buffer_progress_callback_;    // 缓冲进度回调
    PlaybackCompletedCallback playback_completed_callback_;  // 播放完成回调
    LoadingCallback loading_callback_;                   // 网络加载回调
};

} // namespace hxcplayer

#endif // YXVODPLAYER_PLAYER_CORE_H
