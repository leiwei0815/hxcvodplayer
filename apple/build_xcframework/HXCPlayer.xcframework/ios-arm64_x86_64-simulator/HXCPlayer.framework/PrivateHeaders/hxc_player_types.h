/**
 * @file player_types.h
 * @brief 播放器核心类型定义
 */

#ifndef YXVODPLAYER_TYPES_H
#define YXVODPLAYER_TYPES_H

#include <cstdint>
#include <string>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

namespace hxcplayer {

// 播放器状态
enum class PlayerState {
    Idle,
    Opening,
    Playing,
    Paused,
    Stopped,
    Error
};

// 媒体类型
enum class MediaType {
    Unknown,
    Video,
    Audio,
    Subtitle
};

// 同步模式
enum class SyncMode {
    AudioMaster,    // 以音频为基准
    VideoMaster,    // 以视频为基准
    ExternalClock   // 外部时钟
};

/**
 * @brief 视频显示模式（宽高比模式）
 * 用于控制视频在窗口中的显示方式
 */
enum class AspectRatioMode {
    Fit,   // 适应模式：等比缩放，保持完整画面，可能有黑边（默认）
    Fill   // 填充模式：等比拉伸填充，无黑边，画面会被裁剪
};

// 像素格式
enum class PixelFormat {
    YUV420P,
    RGB24,
    RGBA,
    NV12,
    NV21
};

// 播放器配置
struct PlayerConfig {
    SyncMode sync_mode = SyncMode::AudioMaster;
    int audio_buffer_size = 1024;
    int video_queue_size = 3;
    int audio_queue_size = 9;
    int subtitle_queue_size = 16;
    bool enable_audio = true;
    bool enable_video = true;
    bool enable_subtitle = true;
    int max_fps = 60;
    
    // ⚠️ 开始播放时间（秒），类似 ffplay -ss 参数
    // 如果 > 0，在打开文件后会自动 seek 到该位置
    double start_time = 0.0;
    
    // ⚠️ 播放速率（倍速播放）
    // 0.5 = 0.5x慢速, 1.0 = 正常速度, 2.0 = 2x快速
    // 支持范围：0.5 ~ 2.0
    double playback_rate = 1.0;
};

// 媒体信息
struct MediaInfo {
    std::string filename;
    int64_t duration = 0;           // 微秒
    int64_t bitrate = 0;
    
    // 视频信息
    int video_width = 0;
    int video_height = 0;
    double video_fps = 0.0;
    AVCodecID video_codec = AV_CODEC_ID_NONE;
    
    // 音频信息
    int audio_sample_rate = 0;
    int audio_channels = 0;
    AVCodecID audio_codec = AV_CODEC_ID_NONE;
    
    bool has_video() const { return video_codec != AV_CODEC_ID_NONE; }
    bool has_audio() const { return audio_codec != AV_CODEC_ID_NONE; }
};

// 视频帧
struct VideoFrame {
    AVFrame* frame = nullptr;
    double pts = 0.0;               // 显示时间戳（秒）
    double duration = 0.0;
    int width = 0;
    int height = 0;
    
    VideoFrame() = default;
    ~VideoFrame() {
        if (frame) {
            av_frame_free(&frame);
        }
    }
    
    // 禁止拷贝
    VideoFrame(const VideoFrame&) = delete;
    VideoFrame& operator=(const VideoFrame&) = delete;
    
    // 允许移动
    VideoFrame(VideoFrame&& other) noexcept {
        frame = other.frame;
        pts = other.pts;
        duration = other.duration;
        width = other.width;
        height = other.height;
        other.frame = nullptr;
    }
    
    VideoFrame& operator=(VideoFrame&& other) noexcept {
        if (this != &other) {
            if (frame) av_frame_free(&frame);
            frame = other.frame;
            pts = other.pts;
            duration = other.duration;
            width = other.width;
            height = other.height;
            other.frame = nullptr;
        }
        return *this;
    }
};

// 音频帧
struct AudioFrame {
    AVFrame* frame = nullptr;
    double pts = 0.0;
    int nb_samples = 0;
    
    AudioFrame() = default;
    ~AudioFrame() {
        if (frame) {
            av_frame_free(&frame);
        }
    }
    
    AudioFrame(const AudioFrame&) = delete;
    AudioFrame& operator=(const AudioFrame&) = delete;
    
    AudioFrame(AudioFrame&& other) noexcept {
        frame = other.frame;
        pts = other.pts;
        nb_samples = other.nb_samples;
        other.frame = nullptr;
    }
    
    AudioFrame& operator=(AudioFrame&& other) noexcept {
        if (this != &other) {
            if (frame) av_frame_free(&frame);
            frame = other.frame;
            pts = other.pts;
            nb_samples = other.nb_samples;
            other.frame = nullptr;
        }
        return *this;
    }
};

// 时钟信息
struct Clock {
    double pts = 0.0;               // 当前时间戳
    double pts_drift = 0.0;         // 时钟漂移
    double last_updated = 0.0;      // 上次更新时间
    int serial = 0;                 // 序列号
    bool paused = false;
    
    double get_clock() const;
    void set_clock(double pts, int serial);
    void set_clock_at(double pts, int serial, double time);
    void sync_clock_to_slave(Clock* slave);
};

} // namespace hxcplayer

#endif // YXVODPLAYER_TYPES_H
