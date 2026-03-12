/**
 * @file player_core_c_bridge.h
 * @brief C 接口桥接层（隔离 C++ 和 Objective-C++，避免 FFmpeg/AVFoundation 冲突）
 */

#ifndef YXVODPLAYER_PLAYER_CORE_C_BRIDGE_H
#define YXVODPLAYER_PLAYER_CORE_C_BRIDGE_H

#include <stddef.h>  // for size_t

#ifdef __cplusplus
extern "C" {
#endif

// 不透明指针类型（隐藏 C++ 实现细节）
typedef struct PlayerCoreHandle PlayerCoreHandle;

// 播放器状态枚举
typedef enum {
    PLAYER_STATE_IDLE = 0,
    PLAYER_STATE_OPENING = 1,
    PLAYER_STATE_PLAYING = 2,
    PLAYER_STATE_PAUSED = 3,
    PLAYER_STATE_STOPPED = 4,
    PLAYER_STATE_ERROR = -1
} PlayerStateC;

// 视频显示模式（宽高比模式）
typedef enum {
    ASPECT_RATIO_FIT = 0,   // 适应模式：等比缩放，保持完整画面，可能有黑边（默认）
    ASPECT_RATIO_FILL = 1   // 填充模式：等比拉伸填充，无黑边，画面会被裁剪
} AspectRatioModeC;

// ⚠️ 播放器错误码定义
typedef enum {
    // 自定义错误码 (1-999)
    PLAYER_ERROR_NONE = 0,                          // 无错误
    PLAYER_ERROR_INVALID_URL = 1,                   // 无效的 URL
    PLAYER_ERROR_OPEN_INPUT_FAILED = 2,             // 打开输入失败
    PLAYER_ERROR_FIND_STREAM_INFO_FAILED = 3,       // 查找流信息失败
    PLAYER_ERROR_NO_VIDEO_STREAM = 4,               // 没有视频流
    PLAYER_ERROR_NO_AUDIO_STREAM = 5,               // 没有音频流
    PLAYER_ERROR_CODEC_NOT_FOUND = 6,               // 找不到解码器
    PLAYER_ERROR_CODEC_OPEN_FAILED = 7,             // 打开解码器失败
    PLAYER_ERROR_ALLOC_CONTEXT_FAILED = 8,          // 分配上下文失败
    PLAYER_ERROR_SDL_INIT_FAILED = 9,               // SDL 初始化失败
    PLAYER_ERROR_AUDIO_DEVICE_OPEN_FAILED = 10,     // 音频设备打开失败
    PLAYER_ERROR_SEEK_FAILED = 11,                  // Seek 操作失败
    PLAYER_ERROR_READ_FRAME_FAILED = 12,            // 读取帧失败
    PLAYER_ERROR_DECODE_FAILED = 13,                // 解码失败
    PLAYER_ERROR_OUT_OF_MEMORY = 14,                // 内存不足
    PLAYER_ERROR_UNKNOWN = 999,                     // 未知错误
    
    // FFmpeg 错误码范围 (负数)
    // 使用 FFmpeg 原始错误码，例如：
    // AVERROR_EOF = -541478725 (0xDFFFFFE3)
    // AVERROR(ENOMEM) = -12
    // AVERROR(EINVAL) = -22
    // 等等...
    // 可以使用 av_strerror() 将负数错误码转换为错误信息
} PlayerErrorCodeC;

// 创建/销毁播放器
PlayerCoreHandle* player_core_create(void);
void player_core_destroy(PlayerCoreHandle* handle);

// 播放控制
int player_core_open(PlayerCoreHandle* handle, const char* url);
int player_core_open_with_start_position(PlayerCoreHandle* handle, const char* url, double start_pos);
void player_core_play(PlayerCoreHandle* handle);
void player_core_pause(PlayerCoreHandle* handle);
void player_core_stop(PlayerCoreHandle* handle);

// 状态查询
PlayerStateC player_core_get_state(PlayerCoreHandle* handle);
double player_core_get_duration(PlayerCoreHandle* handle);
double player_core_get_position(PlayerCoreHandle* handle);

// 控制
void player_core_seek(PlayerCoreHandle* handle, double pos);
void player_core_set_volume(PlayerCoreHandle* handle, float volume);
void player_core_set_playback_rate(PlayerCoreHandle* handle, float rate);
float player_core_get_playback_rate(PlayerCoreHandle* handle);

// 视频显示模式
void player_core_set_aspect_ratio_mode(PlayerCoreHandle* handle, AspectRatioModeC mode);
AspectRatioModeC player_core_get_aspect_ratio_mode(PlayerCoreHandle* handle);

// 视频帧获取（用于 iOS 渲染）
typedef struct {
    void* y_data;
    void* u_data;
    void* v_data;
    int y_linesize;
    int u_linesize;
    int v_linesize;
    int width;
    int height;
    double pts;
} VideoFrameDataC;

int player_core_get_video_frame(PlayerCoreHandle* handle, VideoFrameDataC* frame_data);
void player_core_consume_video_frame(PlayerCoreHandle* handle);

// 音频数据获取（用于 iOS AudioQueue）
int player_core_get_audio_data(PlayerCoreHandle* handle, unsigned char* buffer, int buffer_size);

// 媒体信息
int player_core_get_audio_sample_rate(PlayerCoreHandle* handle);
int player_core_get_audio_channels(PlayerCoreHandle* handle);
int player_core_get_video_width(PlayerCoreHandle* handle);
int player_core_get_video_height(PlayerCoreHandle* handle);

// 回调函数类型定义
typedef void (*StateChangedCallbackC)(PlayerStateC state, void* user_data);
typedef void (*ErrorCallbackC)(int error_code, const char* error_msg, void* user_data);  // 添加错误码参数
typedef void (*PositionChangedCallbackC)(double position, void* user_data);  // 真实播放位置
typedef void (*BufferProgressCallbackC)(double position, void* user_data);   // 缓冲进度（解码位置）
typedef void (*PlaybackCompletedCallbackC)(void* user_data);                 // 播放完成
typedef void (*LoadingCallbackC)(bool is_loading, void* user_data);          // 网络加载状态

// 设置回调函数
void player_core_set_state_changed_callback(PlayerCoreHandle* handle, StateChangedCallbackC callback, void* user_data);
void player_core_set_error_callback(PlayerCoreHandle* handle, ErrorCallbackC callback, void* user_data);
void player_core_set_position_changed_callback(PlayerCoreHandle* handle, PositionChangedCallbackC callback, void* user_data);
void player_core_set_buffer_progress_callback(PlayerCoreHandle* handle, BufferProgressCallbackC callback, void* user_data);
void player_core_set_playback_completed_callback(PlayerCoreHandle* handle, PlaybackCompletedCallbackC callback, void* user_data);
void player_core_set_loading_callback(PlayerCoreHandle* handle, LoadingCallbackC callback, void* user_data);

// ========== 日志配置 ==========

// 设置日志级别（0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR）
void player_core_set_log_level(int level);

// 启用文件日志
// log_dir: 日志文件目录（如 "/tmp" 或 Android 的 getExternalFilesDir().getAbsolutePath()）
// prefix: 日志文件前缀（默认 "hxcplayer"）
void player_core_enable_file_logging(const char* log_dir, const char* prefix);

// 禁用文件日志
void player_core_disable_file_logging(void);

// 设置最大日志文件大小（字节），超过后自动轮转
void player_core_set_max_log_file_size(size_t max_size);

// 设置日志保留天数（默认7天）
void player_core_set_log_retention_days(int days);

// 手动清理旧日志文件（返回删除的文件数量）
int player_core_cleanup_old_logs(void);

// 获取当前日志文件路径
const char* player_core_get_current_log_file(void);

#ifdef __cplusplus
}
#endif

#endif // YXVODPLAYER_PLAYER_CORE_C_BRIDGE_H
