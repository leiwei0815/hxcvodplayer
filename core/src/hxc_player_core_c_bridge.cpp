/**
 * @file player_core_c_bridge.cpp
 * @brief C 接口桥接层实现
 */

#include "hxc_player_core_c_bridge.h"
#include "hxc_player_core.h"
#include "hxc_audio_resampler.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <chrono>
#include "hxc_logger.h"

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
}

namespace {
// C bridge 侧 seek 音频追帧窗口：跳过明显早于目标位置的旧音频帧，
// 让输出层更快收敛到目标点，减少 seek 后短时间错位。
static constexpr double kBridgeSeekAudioBackwardToleranceSec = 0.35;
static constexpr int64_t kBridgePostAnchorAudioGuardMs = 2500;

static int64_t bridge_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

static int bridge_output_channels_for_source(int source_channels) {
    if (source_channels <= 0) return 0;
    // Android/iOS app playback path is device-output oriented: keep mono as mono,
    // downmix all multi-channel layouts (3.0, 5.1, 7.1, etc.) to stereo.
    return source_channels == 1 ? 1 : 2;
}
}

// PlayerCoreHandle 结构，包含音频处理所需的状态
struct PlayerCoreHandle {
    hxcplayer::PlayerCore* core;
    
    // 音频缓冲（用于 SoundTouch 处理后的数据）
    uint8_t* audio_buf;
    unsigned int audio_buf_size;
    unsigned int audio_buf_index;
    
    // ⚠️ 音频时钟跟踪（用于 iOS/macOS/Android 平台）
    double audio_current_pts;           // 当前音频时钟（用于诊断）
    double audio_buf_pts_base;          // 当前桥接缓冲起点 PTS（媒体时间）
    int audio_current_sample_rate;      // 当前音频采样率
    int audio_current_channels;         // 当前音频通道数
    int audio_logged_source_channels;    // 上次记录的源声道数
    int audio_logged_output_channels;    // 上次记录的输出声道数
    std::atomic<uint64_t> audio_seek_serial; // seek 代数，用于丢弃 seek 前旧回调
    int audio_buf_serial;               // 当前桥接音频缓冲对应的 serial
    double post_anchor_audio_guard_pts; // seek 锚定后短时拒绝旧音频 PTS 回拉时钟
    int64_t post_anchor_audio_guard_until_ms;
    int post_anchor_audio_guard_drop_count;
    uint64_t last_core_audio_reset_serial;
    std::mutex audio_data_mutex;        // 保护音频缓冲/重采样/SoundTouch并发访问

    // 音频重采样器
    hxcplayer::AudioResampler* resampler;

#ifdef HAS_SOUNDTOUCH
    soundtouch::SoundTouch* soundtouch;
    bool soundtouch_initialized;  // 标记 SoundTouch 是否已设置采样率和通道数
#endif
    
    // 视频显示模式
    AspectRatioModeC aspect_ratio_mode;
    
    // 回调函数及用户数据
    StateChangedCallbackC state_changed_callback;
    void* state_changed_user_data;
    
    ErrorCallbackC error_callback;
    void* error_user_data;
    
    PositionChangedCallbackC position_changed_callback;
    void* position_user_data;
    
    BufferProgressCallbackC buffer_progress_callback;
    void* buffer_progress_user_data;
    
    PlaybackCompletedCallbackC playback_completed_callback;
    void* playback_completed_user_data;
    
    LoadingCallbackC loading_callback;
    void* loading_user_data;

    PipelineStateChangedCallbackC pipeline_state_changed_callback;
    void* pipeline_state_user_data;

    PlayingChangedCallbackC playing_changed_callback;
    void* playing_changed_user_data;
    std::mutex callback_mutex;
    
    PlayerCoreHandle() 
        : core(nullptr)
        , audio_buf(nullptr)
        , audio_buf_size(0)
        , audio_buf_index(0)
        , audio_current_pts(0.0)
        , audio_buf_pts_base(0.0)
        , audio_current_sample_rate(0)
        , audio_current_channels(0)
        , audio_logged_source_channels(0)
        , audio_logged_output_channels(0)
        , audio_seek_serial(0)
        , audio_buf_serial(0)
        , post_anchor_audio_guard_pts(-1.0)
        , post_anchor_audio_guard_until_ms(0)
        , post_anchor_audio_guard_drop_count(0)
        , last_core_audio_reset_serial(0)
#ifdef HAS_SOUNDTOUCH
        , soundtouch(nullptr)
        , soundtouch_initialized(false)
#endif
        , aspect_ratio_mode(ASPECT_RATIO_FIT)  // 默认 Fit 模式
        , state_changed_callback(nullptr)
        , state_changed_user_data(nullptr)
        , error_callback(nullptr)
        , error_user_data(nullptr)
        , position_changed_callback(nullptr)
        , position_user_data(nullptr)
        , buffer_progress_callback(nullptr)
        , buffer_progress_user_data(nullptr)
        , playback_completed_callback(nullptr)
        , playback_completed_user_data(nullptr)
        , loading_callback(nullptr)
        , loading_user_data(nullptr)
        , pipeline_state_changed_callback(nullptr)
        , pipeline_state_user_data(nullptr)
        , playing_changed_callback(nullptr)
        , playing_changed_user_data(nullptr)
    {}
    
    ~PlayerCoreHandle() {
        if (audio_buf) {
            free(audio_buf);
            audio_buf = nullptr;
        }
#ifdef HAS_SOUNDTOUCH
        if (soundtouch) {
            delete soundtouch;
            soundtouch = nullptr;
        }
#endif
    }
};

static void reset_bridge_audio_output_locked(PlayerCoreHandle* handle,
                                             double anchor_pts,
                                             const char* reason,
                                             bool bump_serial) {
    if (!handle) return;
    if (bump_serial) {
        handle->audio_seek_serial.fetch_add(1, std::memory_order_acq_rel);
    }
    handle->audio_buf_index = 0;
    handle->audio_buf_size = 0;
    handle->audio_current_pts = anchor_pts;
    handle->audio_buf_pts_base = anchor_pts;
    handle->audio_buf_serial = 0;
    handle->post_anchor_audio_guard_pts = anchor_pts;
    handle->post_anchor_audio_guard_until_ms = bridge_now_ms() + kBridgePostAnchorAudioGuardMs;
    handle->post_anchor_audio_guard_drop_count = 0;
#ifdef HAS_SOUNDTOUCH
    if (handle->soundtouch) {
        handle->soundtouch->clear();
    }
#endif
    LOG_INFO("bridge_audio_output_reset reason=", (reason ? reason : "unknown"),
             " anchor=", anchor_pts,
             " serial=", handle->audio_seek_serial.load(std::memory_order_acquire));
}

static PlayerStateC hxc_to_c_player_state(hxcplayer::PlayerState state) {
    switch (state) {
        case hxcplayer::PlayerState::Idle: return PLAYER_STATE_IDLE;
        case hxcplayer::PlayerState::Opening: return PLAYER_STATE_OPENING;
        case hxcplayer::PlayerState::Playing: return PLAYER_STATE_PLAYING;
        case hxcplayer::PlayerState::Paused: return PLAYER_STATE_PAUSED;
        case hxcplayer::PlayerState::Stopped: return PLAYER_STATE_STOPPED;
        case hxcplayer::PlayerState::Error:
        default:
            return PLAYER_STATE_ERROR;
    }
}

static PlayerPipelineStateC hxc_to_c_pipeline_state(hxcplayer::PipelineState state) {
    switch (state) {
        case hxcplayer::PipelineState::Idle: return PLAYER_PIPELINE_STATE_IDLE;
        case hxcplayer::PipelineState::Preparing: return PLAYER_PIPELINE_STATE_PREPARING;
        case hxcplayer::PipelineState::Buffering: return PLAYER_PIPELINE_STATE_BUFFERING;
        case hxcplayer::PipelineState::Ready: return PLAYER_PIPELINE_STATE_READY;
        case hxcplayer::PipelineState::Ended: return PLAYER_PIPELINE_STATE_ENDED;
        case hxcplayer::PipelineState::Error:
        default:
            return PLAYER_PIPELINE_STATE_ERROR;
    }
}

PlayerCoreHandle* player_core_create(void) {
    PlayerCoreHandle* handle = new PlayerCoreHandle();
    handle->core = new hxcplayer::PlayerCore();
    handle->resampler = new hxcplayer::AudioResampler();

    // 配置播放器（iOS 不使用 SDL 音频）
    hxcplayer::PlayerConfig config;
    config.enable_audio = true;
    config.enable_video = true;
    config.sync_mode = hxcplayer::SyncMode::AudioMaster;
    handle->core->set_config(config);
    
#ifdef HAS_SOUNDTOUCH
    // 初始化 SoundTouch（用于倍速播放）
    // 注意：采样率和通道数会在第一次获取音频数据时设置
    handle->soundtouch = new soundtouch::SoundTouch();
    handle->soundtouch->setTempo(1.0);  // 默认 1.0x 速度
    
    // 优化设置
    handle->soundtouch->setSetting(SETTING_USE_QUICKSEEK, 0);
    handle->soundtouch->setSetting(SETTING_USE_AA_FILTER, 1);
    handle->soundtouch->setSetting(SETTING_SEQUENCE_MS, 40);
    handle->soundtouch->setSetting(SETTING_SEEKWINDOW_MS, 15);
    handle->soundtouch->setSetting(SETTING_OVERLAP_MS, 8);
#endif
    
    return handle;
}

void player_core_destroy(PlayerCoreHandle* handle) {
    if (handle) {
        if (handle->core) {
            // 先解绑所有回调，避免销毁期间后台线程回调到悬空的 bridge handle。
            handle->core->set_state_changed_callback(nullptr);
            handle->core->set_error_callback(nullptr);
            handle->core->set_position_changed_callback(nullptr);
            handle->core->set_buffer_progress_callback(nullptr);
            handle->core->set_playback_completed_callback(nullptr);
            handle->core->set_loading_callback(nullptr);
            handle->core->set_pipeline_state_changed_callback(nullptr);
            handle->core->set_playing_changed_callback(nullptr);
        }
        delete handle->core;
        delete handle->resampler;
        delete handle;
    }
}

int player_core_open(PlayerCoreHandle* handle, const char* url) {
    if (!handle || !handle->core || !url || url[0] == '\0') return -1;
    return handle->core->open(url);  // 直接返回，0=成功，-1=失败
}

int player_core_open_with_start_position(PlayerCoreHandle* handle, const char* url, double start_pos) {
    if (!handle || !handle->core || !url || url[0] == '\0') return -1;
    
    // 无论是否 > 0，都写入 start_time，确保 start_pos=0 时能清除上次残留的值。
    auto config = handle->core->get_config();
    config.start_time = (start_pos > 0.0) ? start_pos : 0.0;
    handle->core->set_config(config);
    
    // 打开文件（内部会检查 config.start_time 并在解码前 seek）
    return handle->core->open(url);
}

int player_core_open_with_mode(PlayerCoreHandle* handle, const PlayerDataSourceC* data_source, const PlayerDataSourceConfigC* config) {
    if (!handle || !handle->core || !data_source || !data_source->url || data_source->url[0] == '\0') {
        return -1;
    }

    // 无论是否 > 0，都写入 start_time，确保 start_position=0 时能清除上次残留的值。
    {
        auto playerConfig = handle->core->get_config();
        playerConfig.start_time = (data_source->start_position > 0.0) ? data_source->start_position : 0.0;
        handle->core->set_config(playerConfig);
    }

    // 转换 C 枚举到 C++ 枚举
    hxcplayer::DataSourceMode cppMode;
    switch (data_source->mode) {
        case PLAYER_DATA_SOURCE_MODE_DEFAULT:
            cppMode = hxcplayer::DataSourceMode::Default;
            break;
        case PLAYER_DATA_SOURCE_MODE_CUSTOM_HTTP:
            cppMode = hxcplayer::DataSourceMode::CustomHTTP;
            break;
        case PLAYER_DATA_SOURCE_MODE_CUSTOM_FILE:
            cppMode = hxcplayer::DataSourceMode::CustomFile;
            break;
        case PLAYER_DATA_SOURCE_MODE_SECURE_HLS:
            cppMode = hxcplayer::DataSourceMode::SecureHLS;
            break;
        default:
            return -1;  // 不支持的模式
    }
    
    // 转换配置参数（如果提供了）
    hxcplayer::CustomDataSourceConfig cppConfig;
    if (config) {
        cppConfig.timeout_ms = config->timeout_ms;
        cppConfig.max_retries = config->max_retries;
        cppConfig.cache_size = config->cache_size;
        cppConfig.avio_buffer_size = config->avio_buffer_size;
    }
    // 播放源上的属性
    cppConfig.encrypted_file = (data_source->encrypted_file != 0);
    cppConfig.secure_headers = data_source->secure_headers;
    // 否则使用默认配置
    
    return handle->core->open_with_mode(data_source->url, cppMode, cppConfig);
}

void player_core_play(PlayerCoreHandle* handle) {
    if (handle && handle->core) {
        handle->core->play();
    }
}

void player_core_pause(PlayerCoreHandle* handle) {
    if (handle && handle->core) {
        handle->core->pause();
    }
}

void player_core_stop(PlayerCoreHandle* handle) {
    if (handle && handle->core) {
        handle->core->stop();
    }
}

PlayerStateC player_core_get_state(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return PLAYER_STATE_ERROR;
    return hxc_to_c_player_state(handle->core->get_state());
}

PlayerPipelineStateC player_core_get_pipeline_state(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return PLAYER_PIPELINE_STATE_ERROR;
    return hxc_to_c_pipeline_state(handle->core->get_pipeline_state());
}

int player_core_get_play_when_ready(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0;
    return handle->core->get_play_when_ready() ? 1 : 0;
}

int player_core_is_playing(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0;
    return handle->core->is_playing() ? 1 : 0;
}

double player_core_get_duration(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0.0;
    return handle->core->get_duration();
}

double player_core_get_position(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0.0;
    return handle->core->get_position();
}

int player_core_is_video_hardware_decoding(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0;
    return handle->core->is_video_hardware_decoding() ? 1 : 0;
}

// Stream-open status is consumed by AndroidPlayer loading/audio gating.
int player_core_is_video_stream_opened(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0;
    return handle->core->is_video_stream_opened() ? 1 : 0;
}

int player_core_is_audio_stream_opened(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0;
    return handle->core->is_audio_stream_opened() ? 1 : 0;
}

const char* player_core_get_video_decode_diagnostic(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return "";
    static thread_local std::string decode_diag;
    decode_diag = handle->core->get_video_decode_diagnostic();
    return decode_diag.c_str();
}

void player_core_seek(PlayerCoreHandle* handle, double pos) {
    if (handle && handle->core) {
        std::lock_guard<std::mutex> audio_lock(handle->audio_data_mutex);
        // iOS/macOS/Android 通过 C bridge 拉取音频数据时，先清空桥接层音频残留，
        // 避免 seek 后短暂输出旧缓冲导致主时钟回跳。
        reset_bridge_audio_output_locked(handle, pos, "seek", true);
        handle->core->seek(pos);
    }
}

void player_core_set_volume(PlayerCoreHandle* handle, float volume) {
    if (handle && handle->core) {
        // 转换 float[0.0, 1.0] 到 int[0, 100]
        int volume_int = static_cast<int>(volume * 100.0f);
        handle->core->set_volume(volume_int);
        LOG_INFO("设置音量: ", volume_int, "%");
    }
}

void player_core_set_playback_rate(PlayerCoreHandle* handle, float rate) {
    if (handle && handle->core) {
        std::lock_guard<std::mutex> audio_lock(handle->audio_data_mutex);
        handle->core->set_playback_rate(rate);
        
#ifdef HAS_SOUNDTOUCH
        // 同时更新桥接层的 SoundTouch
        if (handle->soundtouch) {
            handle->soundtouch->clear();  // 清空缓冲
            handle->soundtouch->setTempo(rate);
        }
#endif
    }
}

float player_core_get_playback_rate(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 1.0f;
    return handle->core->get_playback_rate();
}

void player_core_set_play_when_ready(PlayerCoreHandle* handle, int play_when_ready) {
    if (!handle || !handle->core) return;
    if (play_when_ready != 0) {
        handle->core->play();
    } else {
        handle->core->pause();
    }
}

void player_core_set_decode_mode(PlayerCoreHandle* handle, PlayerDecodeModeC mode) {
    if (!handle || !handle->core) return;
    switch (mode) {
        case PLAYER_DECODE_MODE_HARDWARE:
            handle->core->set_decode_mode(hxcplayer::DecodeMode::Hardware);
            break;
        case PLAYER_DECODE_MODE_SOFTWARE:
        default:
            handle->core->set_decode_mode(hxcplayer::DecodeMode::Software);
            break;
    }
}

PlayerDecodeModeC player_core_get_decode_mode(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return PLAYER_DECODE_MODE_SOFTWARE;
    return handle->core->get_decode_mode() == hxcplayer::DecodeMode::Hardware
               ? PLAYER_DECODE_MODE_HARDWARE
               : PLAYER_DECODE_MODE_SOFTWARE;
}

void player_core_apply_secure_playback_profile(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return;
    auto config = handle->core->get_config();
    config.video_queue_size = 15;
    config.audio_queue_size = 24;
    handle->core->set_config(config);
}

int player_core_get_video_frame(PlayerCoreHandle* handle, VideoFrameDataC* frame_data) {
    if (!handle || !handle->core || !frame_data) {
        return -1;
    }
    
    auto* videoQueue = handle->core->get_video_queue();
    if (!videoQueue || videoQueue->size() <= 0) {
        return -1;  // 没有可用帧
    }

    // seek 后 packet_serial 会递增，需要跳过旧 serial 的帧。
    // 此处全程使用非阻塞取帧，避免渲染线程在 seek 后长时间阻塞导致画面冻住。
    int latest_video_serial = handle->core->get_video_packet_serial();

    auto* vf = videoQueue->peek_last_nonblocking();
    if (!vf || !vf->frame) {
        return -1;
    }

    // 丢弃所有 serial 过期的帧（非阻塞）
    while (vf->serial != latest_video_serial) {
        videoQueue->next();
        vf = videoQueue->peek_last_nonblocking();
        if (!vf || !vf->frame) {
            return -1;  // 队列空了，新帧还在解码中，本轮跳过
        }
    }

    if (!vf->frame) {
        return -1;
    }
    
    AVFrame* frame = vf->frame;
    
    // 填充帧数据（YUV420P 格式）
    frame_data->y_data = frame->data[0];
    frame_data->u_data = frame->data[1];
    frame_data->v_data = frame->data[2];
    frame_data->y_linesize = frame->linesize[0];
    frame_data->u_linesize = frame->linesize[1];
    frame_data->v_linesize = frame->linesize[2];
    frame_data->width = frame->width;
    frame_data->height = frame->height;
    frame_data->pts = vf->pts;
    
    return 0;  // 成功
}

// 消费当前视频帧（渲染完成后调用）
void player_core_consume_video_frame(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return;
    
    auto* videoQueue = handle->core->get_video_queue();
    if (videoQueue) {
        videoQueue->next();
    }
}

// YUV → RGB 转换便利函数
int player_core_get_video_frame_rgb(
    PlayerCoreHandle* handle,
    unsigned char* rgb_buffer,
    int buffer_size,
    int* width,
    int* height,
    int* linesize
) {
    if (!handle || !handle->core || !rgb_buffer || !width || !height || !linesize) {
        return -1;
    }
    
    auto* videoQueue = handle->core->get_video_queue();
    if (!videoQueue || videoQueue->size() <= 0) {
        return -1;  // 没有可用帧
    }

    // 使用非阻塞取帧，避免在 Qt 主线程中阻塞
    auto* vf = videoQueue->peek_last_nonblocking();
    if (!vf || !vf->frame) {
        LOG_DEBUG("[RGB] peek_last_nonblocking 返回空帧，queue.size=", videoQueue->size());
        return -1;
    }
    
    AVFrame* frame = vf->frame;
    int frame_width = frame->width;
    int frame_height = frame->height;
    int required_size = frame_width * frame_height * 3;  // RGB24

    static int rgb_call_count = 0;
    if (++rgb_call_count <= 3) {
        LOG_INFO("[RGB] 首次取帧 #", rgb_call_count,
                 " 分辨率=", frame_width, "x", frame_height,
                 " format=", frame->format,
                 " buffer_size=", buffer_size, " required=", required_size);
    }
    
    // 检查缓冲区大小，不足时输出实际尺寸供调用方扩容后重试
    if (buffer_size < required_size) {
        *width = frame_width;
        *height = frame_height;
        *linesize = frame_width * 3;
        LOG_WARNING("[RGB] 缓冲区不足: buffer_size=", buffer_size, " required=", required_size,
                    " (", frame_width, "x", frame_height, ")，返回 -2 请调用方扩容");
        return -2;  // 缓冲区太小，调用方需要扩容
    }
    
    // 创建 RGB 帧
    AVFrame* rgb_frame = av_frame_alloc();
    if (!rgb_frame) {
        LOG_ERROR("[RGB] av_frame_alloc 失败");
        return -1;
    }
    
    rgb_frame->format = AV_PIX_FMT_RGB24;
    rgb_frame->width = frame_width;
    rgb_frame->height = frame_height;
    
    // 将用户提供的缓冲区关联到 RGB 帧
    av_image_fill_arrays(
        rgb_frame->data, rgb_frame->linesize,
        rgb_buffer, AV_PIX_FMT_RGB24,
        frame_width, frame_height, 1
    );
    
    // 创建转换上下文
    SwsContext* sws_ctx = sws_getContext(
        frame_width, frame_height, (AVPixelFormat)frame->format,
        frame_width, frame_height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );
    
    if (!sws_ctx) {
        LOG_ERROR("[RGB] sws_getContext 失败，format=", frame->format);
        av_frame_free(&rgb_frame);
        return -1;
    }
    
    // 执行 YUV → RGB 转换
    sws_scale(
        sws_ctx,
        (const uint8_t* const*)frame->data, frame->linesize,
        0, frame_height,
        rgb_frame->data, rgb_frame->linesize
    );
    
    // 清理
    sws_freeContext(sws_ctx);
    av_frame_free(&rgb_frame);  // 只释放 AVFrame 结构，不释放 rgb_buffer
    
    // 设置输出参数
    *width = frame_width;
    *height = frame_height;
    *linesize = frame_width * 3;

    static int rgb_success_count = 0;
    if (++rgb_success_count <= 5 || rgb_success_count % 300 == 0) {
        LOG_INFO("[RGB] 帧转换成功 #", rgb_success_count, " pts=", vf->pts,
                 " size=", frame_width, "x", frame_height);
    }
    
    // 自动消费帧
    videoQueue->next();
    
    return 0;  // 成功
}


double player_core_get_seek_target(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return -1.0;
    double t = handle->core->get_seek_target_pos();
    return (t > 0.0) ? t : -1.0;
}

int player_core_get_post_seek_warmup(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0;
    return handle->core->get_post_seek_warmup_frames();
}

void player_core_anchor_clock(PlayerCoreHandle* handle, double pts) {
    if (handle && handle->core) {
        std::lock_guard<std::mutex> audio_lock(handle->audio_data_mutex);
        reset_bridge_audio_output_locked(handle, pts, "anchor", false);
        handle->core->anchor_clock(pts);
    }
}

int player_core_get_audio_data(PlayerCoreHandle* handle, unsigned char* buffer, int buffer_size) {
    if (!handle || !handle->core || !buffer || buffer_size <= 0) {
        return 0;
    }
    std::lock_guard<std::mutex> audio_lock(handle->audio_data_mutex);

    uint64_t core_audio_reset_serial = handle->core->get_audio_output_reset_serial();
    if (core_audio_reset_serial != handle->last_core_audio_reset_serial) {
        handle->last_core_audio_reset_serial = core_audio_reset_serial;
        double anchor = handle->core->get_position();
        if (!std::isfinite(anchor) || anchor < 0.0) {
            anchor = 0.0;
        }
        reset_bridge_audio_output_locked(handle, anchor, "core_audio_reset", true);
    }

    uint64_t call_seek_serial = handle->audio_seek_serial.load(std::memory_order_acquire);
    
    // 从缓冲区复制数据（如果有的话）
    while (buffer_size > 0) {
        int latest_audio_serial = handle->core->get_audio_packet_serial();
        if (call_seek_serial != handle->audio_seek_serial.load(std::memory_order_acquire)) {
            handle->audio_buf_index = 0;
            handle->audio_buf_size = 0;
            return 0;
        }

        if (handle->audio_buf_serial != latest_audio_serial) {
            handle->audio_buf_index = 0;
            handle->audio_buf_size = 0;
        }

        // 如果缓冲区有数据，直接复制
        if (handle->audio_buf_index < handle->audio_buf_size) {
            int len1 = handle->audio_buf_size - handle->audio_buf_index;
            if (len1 > buffer_size) {
                len1 = buffer_size;
            }
            
            memcpy(buffer, handle->audio_buf + handle->audio_buf_index, len1);
            handle->audio_buf_index += len1;
            
            // 在数据被复制给平台层后，基于“当前缓冲起点 + 已输出偏移”更新音频时钟。
            if (handle->audio_current_sample_rate > 0 && handle->audio_current_channels > 0) {
                if (call_seek_serial != handle->audio_seek_serial.load(std::memory_order_acquire)) {
                    return 0;
                }
                int bytes_per_sample_all_channels = handle->audio_current_channels * (int)sizeof(int16_t);
                if (bytes_per_sample_all_channels <= 0) {
                    return len1;
                }

                // 注意：这里必须按“当前缓冲累计已输出字节”计算，而不是只按本次 len1 增量累加，
                // 避免浮点累加误差与分段回调引入的时钟漂移。
                int emitted_samples_total = (int)(handle->audio_buf_index / bytes_per_sample_all_channels);
                double consumed_duration = (double)emitted_samples_total / handle->audio_current_sample_rate;

                // SoundTouch 倍速播放：输出样本对应 media time 需要乘当前速率。
                double current_rate = handle->core->get_playback_rate();
                if (current_rate != 1.0) {
                    consumed_duration *= current_rate;
                }

                double pts_for_clock = handle->audio_buf_pts_base + consumed_duration;
                handle->audio_current_pts = pts_for_clock;
                handle->core->update_audio_pts(pts_for_clock, handle->audio_buf_serial);
            }
            
            return len1;  // 返回复制的字节数
        }
        
        // 缓冲区为空，需要从队列获取新帧
        auto* audioQueue = handle->core->get_audio_queue();
        if (!audioQueue || audioQueue->size() <= 0) {
            return 0;  // 没有可用音频帧
        }
        
        auto* af = audioQueue->peek_readable();
        if (!af || !af->frame) {
            return 0;
        }

        while (af && af->frame && af->serial != latest_audio_serial) {
            audioQueue->next();
            if (audioQueue->size() <= 0) {
                return 0;
            }
            af = audioQueue->peek_readable();
        }
        if (!af || !af->frame) {
            return 0;
        }
        
        AVFrame* frame = af->frame;
        int channels = frame->ch_layout.nb_channels;
        int samples = frame->nb_samples;
        int sample_rate = frame->sample_rate;
        AVSampleFormat sample_fmt = static_cast<AVSampleFormat>(frame->format);

        // 防御性检查：部分解码器在 flush/边界时可能给出 nb_samples>0 但 data[0] 为空指针或缓冲区大小为 0 的帧；
        // 此时直接丢弃，避免后续 memcpy / 重采样访问非法地址导致崩溃。
        int expected_src_size = av_samples_get_buffer_size(
            nullptr,
            channels,
            samples,
            sample_fmt,
            0);
        if (channels <= 0 || channels > 8 ||
            sample_rate <= 0 || sample_rate > 384000 ||
            samples <= 0 || samples > 32768 ||
            av_get_bytes_per_sample(sample_fmt) <= 0 ||
            !frame->data[0] || expected_src_size <= 0) {
            audioQueue->next();
            return 0;
        }
        uint8_t** frame_planes = frame->extended_data ? frame->extended_data : frame->data;
        int frame_plane_count = av_sample_fmt_is_planar(sample_fmt) ? channels : 1;
        if (!frame_planes || frame_plane_count <= 0 || frame_plane_count > 8) {
            audioQueue->next();
            return 0;
        }
        bool invalid_audio_plane = false;
        for (int i = 0; i < frame_plane_count; ++i) {
            if (!frame_planes[i]) {
                invalid_audio_plane = true;
                break;
            }
        }
        if (invalid_audio_plane) {
            audioQueue->next();
            return 0;
        }
        double pts = af->pts;  // ⚠️ 保存 PTS，用于后续时钟更新

        if (std::isfinite(pts) && pts >= 0.0 &&
            handle->post_anchor_audio_guard_until_ms > 0) {
            int64_t now_ms = bridge_now_ms();
            if (now_ms <= handle->post_anchor_audio_guard_until_ms &&
                handle->post_anchor_audio_guard_pts >= 0.0 &&
                pts < (handle->post_anchor_audio_guard_pts - kBridgeSeekAudioBackwardToleranceSec)) {
                int drop_count = ++handle->post_anchor_audio_guard_drop_count;
                if (drop_count == 1 || drop_count % 20 == 0) {
                    LOG_WARNING("bridge_audio_post_anchor_drop pts=", pts,
                                " anchor=", handle->post_anchor_audio_guard_pts,
                                " guard_left_ms=", handle->post_anchor_audio_guard_until_ms - now_ms,
                                " count=", drop_count);
                }
                audioQueue->next();
                continue;
            }
            if (now_ms > handle->post_anchor_audio_guard_until_ms ||
                pts >= (handle->post_anchor_audio_guard_pts - kBridgeSeekAudioBackwardToleranceSec)) {
                handle->post_anchor_audio_guard_until_ms = 0;
                handle->post_anchor_audio_guard_pts = -1.0;
                handle->post_anchor_audio_guard_drop_count = 0;
            }
        }

        // Seek 后桥接层继续做一次目标前音频过滤（与 core 内部策略互补）：
        // 若当前音频帧明显早于目标位置，则直接丢弃并继续取下一帧。
        // 这样可减少 seek 后先播一段旧音频导致的视频看起来“快进/前跳”。
        if (std::isfinite(pts) && pts >= 0.0) {
            double seek_target = handle->core->get_seek_target_pos();
            if (seek_target > 0.0 && pts < (seek_target - kBridgeSeekAudioBackwardToleranceSec)) {
                audioQueue->next();
                continue;
            }
        }

        // 保存当前帧的时钟基准（注意：以输出缓冲起点作为媒体时间基准）。
        handle->audio_current_pts = pts;
        handle->audio_buf_pts_base = pts;
        handle->audio_current_sample_rate = sample_rate;
        handle->audio_buf_serial = af->serial;

        // 按当前帧的格式配置重采样器（每个新流/格式变化时重新配置）。
        // Android OpenSL ES 输出侧只承诺 mono/stereo，3.0/5.1/7.1 等源流在这里 downmix。
        AVChannelLayout src_layout{};
        if (frame->ch_layout.nb_channels == channels &&
            av_channel_layout_check(&frame->ch_layout)) {
            int copy_ret = av_channel_layout_copy(&src_layout, &frame->ch_layout);
            if (copy_ret < 0) {
                av_channel_layout_default(&src_layout, channels);
                if (channels != handle->audio_logged_source_channels) {
                    LOG_WARNING("bridge_audio_src_layout_copy_fallback channels=", channels,
                                " ret=", copy_ret);
                }
            }
        }
        if (src_layout.nb_channels <= 0) {
            av_channel_layout_default(&src_layout, channels);
            if (channels != handle->audio_logged_source_channels) {
                LOG_WARNING("bridge_audio_src_layout_fallback channels=", channels);
            }
        }
        AVChannelLayout dst_layout = AV_CHANNEL_LAYOUT_STEREO;
        if (channels == 1) {
            dst_layout = AV_CHANNEL_LAYOUT_MONO;
        }
        int previous_output_channels = handle->audio_current_channels;
        int output_channels = bridge_output_channels_for_source(channels);
        if (output_channels <= 0 || output_channels != dst_layout.nb_channels) {
            audioQueue->next();
            return 0;
        }
        handle->audio_current_channels = output_channels;
        if (channels != handle->audio_logged_source_channels ||
            output_channels != handle->audio_logged_output_channels) {
            LOG_INFO("bridge_audio_output_format source_channels=", channels,
                     " output_channels=", output_channels,
                     " downmix=", (channels > output_channels ? 1 : 0),
                     " sample_rate=", sample_rate);
            handle->audio_logged_source_channels = channels;
            handle->audio_logged_output_channels = output_channels;
        }
#ifdef HAS_SOUNDTOUCH
        if (previous_output_channels > 0 && previous_output_channels != output_channels) {
            handle->soundtouch_initialized = false;
        }
#endif
        int cfg_ret = handle->resampler->configure(&src_layout,
                                                   sample_fmt,
                                                   sample_rate,
                                                   &dst_layout,
                                                   AV_SAMPLE_FMT_S16,
                                                   sample_rate);
        av_channel_layout_uninit(&src_layout);
        if (cfg_ret < 0) {
            audioQueue->next();
            return 0;
        }

        uint8_t* audio_data = nullptr;
        int audio_samples = samples;

        // 重采样（如果需要）
        if (handle->resampler->is_needed()) {
            int ret = handle->resampler->resample(frame_planes, samples, &audio_data, &audio_samples);
            if (ret < 0) {
                audioQueue->next();
                return 0;
            }
        } else {
            audio_data = frame->data[0];
        }

        int64_t raw_output_size64 = (int64_t)audio_samples * output_channels * (int)sizeof(int16_t);
        if (audio_samples <= 0 || !audio_data || raw_output_size64 <= 0 || raw_output_size64 > (8 * 1024 * 1024)) {
            audioQueue->next();
            return 0;
        }
        int raw_output_size = (int)raw_output_size64;
        
        // 确保临时缓冲区足够大
        if (!handle->audio_buf || handle->audio_buf_size < (unsigned int)raw_output_size * 2) {
            if (handle->audio_buf) {
                free(handle->audio_buf);
            }
            handle->audio_buf = (uint8_t*)malloc(raw_output_size * 2);
            if (!handle->audio_buf) {
                audioQueue->next();
                return 0;
            }
        }

        // 复制重采样后的数据
        memcpy(handle->audio_buf, audio_data, raw_output_size);

        // 消费音频帧
        audioQueue->next();
        
#ifdef HAS_SOUNDTOUCH
        // 使用 SoundTouch 处理倍速播放
        double current_rate = handle->core->get_playback_rate();
        
        if (handle->soundtouch && current_rate != 1.0) {
            // 首次使用时设置采样率和通道数
            if (!handle->soundtouch_initialized) {
                handle->soundtouch->setSampleRate(sample_rate);
                handle->soundtouch->setChannels(output_channels);
                handle->soundtouch_initialized = true;
            }
            
            // 转换 S16 为 float
            int16_t* s16_data = (int16_t*)handle->audio_buf;
            std::vector<float> float_input(audio_samples * output_channels);
            for (int i = 0; i < audio_samples * output_channels; i++) {
                float_input[i] = (float)s16_data[i] / 32768.0f;
            }

            // 送入样本到 SoundTouch
            handle->soundtouch->putSamples(float_input.data(), audio_samples);
            
            // 获取可用样本数
            uint32_t available = handle->soundtouch->numSamples();
            
            // 计算期望输出样本数
            uint32_t expected_output = (uint32_t)(audio_samples / current_rate);
            
            // 只有当积累的样本 >= 期望输出的 80% 时才取出
            if (available >= expected_output * 0.8) {
                uint32_t samples_to_get = available > expected_output ? available : expected_output;
                std::vector<float> output_buffer(samples_to_get * output_channels);
                uint32_t received = handle->soundtouch->receiveSamples(output_buffer.data(), samples_to_get);
                
                if (received > 0) {
                    // 转换回 S16
                    int out_size = received * output_channels * sizeof(int16_t);
                    
                    // 确保缓冲区足够大
                    if (handle->audio_buf_size < (unsigned int)out_size * 2) {
                        free(handle->audio_buf);
                        handle->audio_buf = (uint8_t*)malloc(out_size * 2);
                        if (!handle->audio_buf) {
                            return 0;
                        }
                    }
                    
                    int16_t* s16_dst = (int16_t*)handle->audio_buf;
                    for (size_t i = 0; i < received * output_channels; i++) {
                        float sample = output_buffer[i];
                        if (sample > 1.0f) sample = 1.0f;
                        if (sample < -1.0f) sample = -1.0f;
                        s16_dst[i] = (int16_t)(sample * 32767.0f);
                    }
                    
                    handle->audio_buf_size = out_size;
                    handle->audio_buf_index = 0;
                } else {
                    // SoundTouch 没有输出，设置为空缓冲
                    handle->audio_buf_size = 0;
                    handle->audio_buf_index = 0;
                }
            } else {
                // SoundTouch 积累中，设置为空缓冲
                handle->audio_buf_size = 0;
                handle->audio_buf_index = 0;
            }
        } else {
            // 1.0x 速度或 SoundTouch 未启用，使用原始音频
            handle->audio_buf_size = raw_output_size;
            handle->audio_buf_index = 0;
        }
#else
        // 非 macOS/Windows 平台，直接使用原始音频
        handle->audio_buf_size = raw_output_size;
        handle->audio_buf_index = 0;
#endif
        
        // 继续循环，从缓冲区复制数据
    }
    
    return 0;
}
int player_core_get_audio_sample_rate(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0;
    const auto& info = handle->core->get_media_info();
    return info.audio_sample_rate;
}

int player_core_get_audio_channels(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0;
    const auto& info = handle->core->get_media_info();
    return info.audio_channels;
}

int player_core_get_video_width(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0;
    const auto& info = handle->core->get_media_info();
    return info.video_width;
}

int player_core_get_video_height(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0;
    const auto& info = handle->core->get_media_info();
    return info.video_height;
}

// 视频显示模式设置
void player_core_set_aspect_ratio_mode(PlayerCoreHandle* handle, AspectRatioModeC mode) {
    if (handle) {
        handle->aspect_ratio_mode = mode;
    }
}

AspectRatioModeC player_core_get_aspect_ratio_mode(PlayerCoreHandle* handle) {
    if (!handle) return ASPECT_RATIO_FIT;
    return handle->aspect_ratio_mode;
}

// ========== 回调函数实现 ==========

// 设置状态变化回调
void player_core_set_state_changed_callback(PlayerCoreHandle* handle, StateChangedCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;
    {
        std::lock_guard<std::mutex> lock(handle->callback_mutex);
        handle->state_changed_callback = callback;
        handle->state_changed_user_data = user_data;
    }
    
    // 设置 C++ 层回调
    if (callback) {
        handle->core->set_state_changed_callback([handle](hxcplayer::PlayerState state) {
            StateChangedCallbackC cb = nullptr;
            void* ud = nullptr;
            {
                std::lock_guard<std::mutex> lock(handle->callback_mutex);
                cb = handle->state_changed_callback;
                ud = handle->state_changed_user_data;
            }
            if (cb) {
                PlayerStateC c_state = hxc_to_c_player_state(state);
                cb(c_state, ud);
            }
        });
    } else {
        handle->core->set_state_changed_callback(nullptr);
    }
}

// 设置错误回调
void player_core_set_error_callback(PlayerCoreHandle* handle, ErrorCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;
    {
        std::lock_guard<std::mutex> lock(handle->callback_mutex);
        handle->error_callback = callback;
        handle->error_user_data = user_data;
    }
    
    // 设置 C++ 层回调
    if (callback) {
        handle->core->set_error_callback([handle](int error_code, const std::string& error_msg) {
            ErrorCallbackC cb = nullptr;
            void* ud = nullptr;
            {
                std::lock_guard<std::mutex> lock(handle->callback_mutex);
                cb = handle->error_callback;
                ud = handle->error_user_data;
            }
            if (cb) {
                cb(error_code, error_msg.c_str(), ud);
            }
        });
    } else {
        handle->core->set_error_callback(nullptr);
    }
}

// 设置播放进度回调
void player_core_set_position_changed_callback(PlayerCoreHandle* handle, PositionChangedCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;
    {
        std::lock_guard<std::mutex> lock(handle->callback_mutex);
        handle->position_changed_callback = callback;
        handle->position_user_data = user_data;
    }
    
    // 设置 C++ 层回调
    if (callback) {
        handle->core->set_position_changed_callback([handle](double position) {
            PositionChangedCallbackC cb = nullptr;
            void* ud = nullptr;
            {
                std::lock_guard<std::mutex> lock(handle->callback_mutex);
                cb = handle->position_changed_callback;
                ud = handle->position_user_data;
            }
            if (cb) {
                cb(position, ud);
            }
        });
    } else {
        handle->core->set_position_changed_callback(nullptr);
    }
}

void player_core_set_buffer_progress_callback(PlayerCoreHandle* handle, BufferProgressCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;
    {
        std::lock_guard<std::mutex> lock(handle->callback_mutex);
        handle->buffer_progress_callback = callback;
        handle->buffer_progress_user_data = user_data;
    }
    
    // 设置 C++ 层回调
    if (callback) {
        handle->core->set_buffer_progress_callback([handle](double position) {
            BufferProgressCallbackC cb = nullptr;
            void* ud = nullptr;
            {
                std::lock_guard<std::mutex> lock(handle->callback_mutex);
                cb = handle->buffer_progress_callback;
                ud = handle->buffer_progress_user_data;
            }
            if (cb) {
                cb(position, ud);
            }
        });
    } else {
        handle->core->set_buffer_progress_callback(nullptr);
    }
}

void player_core_set_playback_completed_callback(PlayerCoreHandle* handle, PlaybackCompletedCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;
    {
        std::lock_guard<std::mutex> lock(handle->callback_mutex);
        handle->playback_completed_callback = callback;
        handle->playback_completed_user_data = user_data;
    }
    
    // 设置 C++ 层回调
    if (callback) {
        handle->core->set_playback_completed_callback([handle]() {
            PlaybackCompletedCallbackC cb = nullptr;
            void* ud = nullptr;
            {
                std::lock_guard<std::mutex> lock(handle->callback_mutex);
                cb = handle->playback_completed_callback;
                ud = handle->playback_completed_user_data;
            }
            if (cb) {
                cb(ud);
            }
        });
    } else {
        handle->core->set_playback_completed_callback(nullptr);
    }
}

void player_core_set_loading_callback(PlayerCoreHandle* handle, LoadingCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;
    {
        std::lock_guard<std::mutex> lock(handle->callback_mutex);
        handle->loading_callback = callback;
        handle->loading_user_data = user_data;
    }
    
    // 设置 C++ 层回调
    if (callback) {
        handle->core->set_loading_callback([handle](bool is_loading) {
            LoadingCallbackC cb = nullptr;
            void* ud = nullptr;
            {
                std::lock_guard<std::mutex> lock(handle->callback_mutex);
                cb = handle->loading_callback;
                ud = handle->loading_user_data;
            }
            if (cb) {
                cb(is_loading, ud);
            }
        });
    } else {
        handle->core->set_loading_callback(nullptr);
    }
}

void player_core_set_pipeline_state_changed_callback(PlayerCoreHandle* handle, PipelineStateChangedCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;

    {
        std::lock_guard<std::mutex> lock(handle->callback_mutex);
        handle->pipeline_state_changed_callback = callback;
        handle->pipeline_state_user_data = user_data;
    }

    if (callback) {
        handle->core->set_pipeline_state_changed_callback([handle](hxcplayer::PipelineState state) {
            PipelineStateChangedCallbackC cb = nullptr;
            void* ud = nullptr;
            {
                std::lock_guard<std::mutex> lock(handle->callback_mutex);
                cb = handle->pipeline_state_changed_callback;
                ud = handle->pipeline_state_user_data;
            }
            if (cb) {
                cb(hxc_to_c_pipeline_state(state), ud);
            }
        });
    } else {
        handle->core->set_pipeline_state_changed_callback(nullptr);
    }
}

void player_core_set_playing_changed_callback(PlayerCoreHandle* handle, PlayingChangedCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;

    {
        std::lock_guard<std::mutex> lock(handle->callback_mutex);
        handle->playing_changed_callback = callback;
        handle->playing_changed_user_data = user_data;
    }

    if (callback) {
        handle->core->set_playing_changed_callback([handle](bool is_playing) {
            PlayingChangedCallbackC cb = nullptr;
            void* ud = nullptr;
            {
                std::lock_guard<std::mutex> lock(handle->callback_mutex);
                cb = handle->playing_changed_callback;
                ud = handle->playing_changed_user_data;
            }
            if (cb) {
                cb(is_playing ? 1 : 0, ud);
            }
        });
    } else {
        handle->core->set_playing_changed_callback(nullptr);
    }
}

// ========== 日志配置实现 ==========

// 静态变量存储日志文件路径 / 目录（供 C 接口返回稳定指针）
static std::string g_current_log_file;
static std::string g_log_directory;

void player_core_set_log_level(int level) {
    hxcplayer::LogLevel log_level;
    switch (level) {
        case 0: log_level = hxcplayer::LogLevel::DEBUG; break;
        case 1: log_level = hxcplayer::LogLevel::INFO; break;
        case 2: log_level = hxcplayer::LogLevel::WARNING; break;
        case 3: log_level = hxcplayer::LogLevel::ERROR_LEVEL; break;
        default: log_level = hxcplayer::LogLevel::INFO; break;
    }
    hxcplayer::Logger::instance().set_level(log_level);
}

int player_core_get_log_level(void) {
    switch (hxcplayer::Logger::instance().get_level()) {
        case hxcplayer::LogLevel::DEBUG: return 0;
        case hxcplayer::LogLevel::INFO: return 1;
        case hxcplayer::LogLevel::WARNING: return 2;
        case hxcplayer::LogLevel::ERROR_LEVEL: return 3;
        default: return 1;
    }
}

void player_core_enable_file_logging(const char* log_dir, const char* prefix) {
    if (!log_dir) return;
    
    std::string dir(log_dir);
    std::string pfx = prefix ? std::string(prefix) : "hxcplayer";
    
    hxcplayer::Logger::instance().enable_file_logging(dir, pfx);
    g_current_log_file = hxcplayer::Logger::instance().get_current_log_file();
    
    LOG_INFO("========================================");
    LOG_INFO("HXCPlayer 文件日志已启用");
    LOG_INFO("日志目录: ", log_dir);
    LOG_INFO("日志前缀: ", pfx);
    LOG_INFO("日志文件: ", g_current_log_file);
    LOG_INFO("========================================");
    
    // 注意：cleanup_old_logs() 已在 enable_file_logging() 中自动调用
}

void player_core_disable_file_logging(void) {
    LOG_INFO("HXCPlayer 文件日志已禁用");
    hxcplayer::Logger::instance().disable_file_logging();
}

void player_core_set_max_log_file_size(size_t max_size) {
    hxcplayer::Logger::instance().set_max_file_size(max_size);
    LOG_INFO("日志文件最大大小设置为: ", max_size, " 字节");
}

void player_core_set_log_retention_days(int days) {
    if (days < 1) days = 1;  // 至少保留 1 天
    hxcplayer::Logger::instance().set_log_retention_days(days);
    LOG_INFO("日志保留天数设置为: ", days, " 天");
}

int player_core_cleanup_old_logs(void) {
    int deleted_count = hxcplayer::Logger::instance().cleanup_old_logs();
    LOG_INFO("清理了 ", deleted_count, " 个过期日志文件");
    return deleted_count;
}

const char* player_core_get_current_log_file(void) {
    g_current_log_file = hxcplayer::Logger::instance().get_current_log_file();
    return g_current_log_file.c_str();
}

const char* player_core_get_log_directory(void) {
    g_log_directory = hxcplayer::Logger::instance().get_log_dir();
    return g_log_directory.c_str();
}

static const char *hxc_c_log_basename(const char *path) {
    if (!path) {
        return "";
    }
    const char *p = strrchr(path, '/');
    if (!p) {
        p = strrchr(path, '\\');
    }
    return p ? (p + 1) : path;
}

void player_core_log_line(int level, const char *file, int line, const char *func, const char *utf8_message) {
    const char *f = file ? file : "";
    const char *fn = func ? func : "";
    const char *m = utf8_message ? utf8_message : "";
    using hxcplayer::Logger;
    switch (level) {
        case 0:
            Logger::instance().debug_with_location(f, line, fn, m);
            break;
        case 1: {
            std::ostringstream oss;
            oss << "[" << hxc_c_log_basename(f) << ":" << line << " " << fn << "()] " << m;
            Logger::instance().info(oss.str());
            break;
        }
        case 2: {
            std::ostringstream oss;
            oss << "[" << hxc_c_log_basename(f) << ":" << line << " " << fn << "()] " << m;
            Logger::instance().warning(oss.str());
            break;
        }
        case 3:
        default:
            Logger::instance().error_with_location(f, line, fn, m);
            break;
    }
}

// ========== 视频渲染 API ==========

void player_core_set_render_window(PlayerCoreHandle* handle, void* window_handle) {
    if (!handle || !handle->core) {
        LOG_ERROR("player_core_set_render_window: 无效的句柄");
        return;
    }
    
    handle->core->set_render_window(window_handle);
    LOG_INFO("设置渲染窗口: ", window_handle);
}

void player_core_set_render_mode(PlayerCoreHandle* handle, RenderModeC mode) {
    if (!handle || !handle->core) {
        LOG_ERROR("player_core_set_render_mode: 无效的句柄");
        return;
    }
    
    hxcplayer::PlayerCore::RenderMode cpp_mode = 
        (mode == RENDER_MODE_AUTO) ? hxcplayer::PlayerCore::RenderMode::Auto 
                                    : hxcplayer::PlayerCore::RenderMode::Manual;
    
    handle->core->set_render_mode(cpp_mode);
    LOG_INFO("设置渲染模式: ", (mode == RENDER_MODE_AUTO ? "AUTO" : "MANUAL"));
}

RenderModeC player_core_get_render_mode(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) {
        LOG_ERROR("player_core_get_render_mode: 无效的句柄");
        return RENDER_MODE_MANUAL;
    }
    
    hxcplayer::PlayerCore::RenderMode cpp_mode = handle->core->get_render_mode();
    return (cpp_mode == hxcplayer::PlayerCore::RenderMode::Auto) ? RENDER_MODE_AUTO : RENDER_MODE_MANUAL;
}

int player_core_refresh_video(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) {
        LOG_ERROR("player_core_refresh_video: 无效的句柄");
        return -1;
    }
    
    return handle->core->refresh_video();
}

// ========== Windows D3D11/OpenGL 渲染器 API 实现 ==========
// ⚠️ 注意：这些实现在应用层（desktop/win-sdk-example）提供
// core 层仅声明，不提供实现，以避免链接冲突

#ifdef _WIN32

// 这些函数在 desktop/win_renderer_bridge.cpp 中实现
// 不在此处提供实现

#endif // _WIN32

// ========== SDK 辅助函数：获取 PlayerCore* ==========
/**
 * @brief 从 PlayerCoreHandle 获取 PlayerCore* 指针
 * @note 此函数供 Windows SDK 使用，不对外暴露
 */
hxcplayer::PlayerCore* get_player_core_from_handle(PlayerCoreHandle* handle) {
    if (!handle) {
        return nullptr;
    }
    return handle->core;
}
