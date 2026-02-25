/**
 * @file player_core_c_bridge.h
 * @brief C 接口桥接层（隔离 C++ 和 Objective-C++，避免 FFmpeg/AVFoundation 冲突）
 */

#ifndef YXVODPLAYER_PLAYER_CORE_C_BRIDGE_H
#define YXVODPLAYER_PLAYER_CORE_C_BRIDGE_H

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

#ifdef __cplusplus
}
#endif

#endif // YXVODPLAYER_PLAYER_CORE_C_BRIDGE_H
