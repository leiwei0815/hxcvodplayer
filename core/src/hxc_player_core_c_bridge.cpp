/**
 * @file player_core_c_bridge.cpp
 * @brief C 接口桥接层实现
 */

#include "hxc_player_core_c_bridge.h"
#include "hxc_player_core.h"
#include <cstring>
#include <vector>
#include "hxc_logger.h"
#if defined(__APPLE__) || defined(_WIN32) || defined(__ANDROID__)
#include <soundtouch/SoundTouch.h>
#endif

// PlayerCoreHandle 结构，包含音频处理所需的状态
struct PlayerCoreHandle {
    hxcplayer::PlayerCore* core;
    
    // 音频缓冲（用于 SoundTouch 处理后的数据）
    uint8_t* audio_buf;
    unsigned int audio_buf_size;
    unsigned int audio_buf_index;
    
    // ⚠️ 音频时钟跟踪（用于 iOS/macOS/Android 平台）
    double audio_current_pts;           // 当前音频帧的 PTS
    int audio_current_sample_rate;      // 当前音频采样率
    int audio_current_channels;         // 当前音频通道数
    
#if defined(__APPLE__) || defined(_WIN32) || defined(__ANDROID__)
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
    
    PlayerCoreHandle() 
        : core(nullptr)
        , audio_buf(nullptr)
        , audio_buf_size(0)
        , audio_buf_index(0)
        , audio_current_pts(0.0)
        , audio_current_sample_rate(0)
        , audio_current_channels(0)
#if defined(__APPLE__) || defined(_WIN32) || defined(__ANDROID__)
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
    {}
    
    ~PlayerCoreHandle() {
        if (audio_buf) {
            free(audio_buf);
            audio_buf = nullptr;
        }
#if defined(__APPLE__) || defined(_WIN32) || defined(__ANDROID__)
        if (soundtouch) {
            delete soundtouch;
            soundtouch = nullptr;
        }
#endif
    }
};

PlayerCoreHandle* player_core_create(void) {
    PlayerCoreHandle* handle = new PlayerCoreHandle();
    handle->core = new hxcplayer::PlayerCore();
    
    // 配置播放器（iOS 不使用 SDL 音频）
    hxcplayer::PlayerConfig config;
    config.enable_audio = true;
    config.enable_video = true;
    config.sync_mode = hxcplayer::SyncMode::AudioMaster;
    handle->core->set_config(config);
    
#if defined(__APPLE__) || defined(_WIN32) || defined(__ANDROID__)
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
        delete handle->core;
        delete handle;
    }
}

int player_core_open(PlayerCoreHandle* handle, const char* url) {
    if (!handle || !handle->core) return -1;
    return handle->core->open(url);  // 直接返回，0=成功，-1=失败
}

int player_core_open_with_start_position(PlayerCoreHandle* handle, const char* url, double start_pos) {
    if (!handle || !handle->core) return -1;
    
    // ✅ 优化方案：通过配置设置起始时间，在 open 内部处理
    // 这样 PlayerCore::open 会在启动解码线程前先 seek，避免解码无用数据
    if (start_pos > 0.0) {
        // 获取当前配置
        auto config = handle->core->get_config();
        
        // 设置起始时间
        config.start_time = start_pos;
        
        // 应用配置
        handle->core->set_config(config);
    }
    
    // 打开文件（内部会检查 config.start_time 并在解码前 seek）
    return handle->core->open(url);
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
    
    hxcplayer::PlayerState state = handle->core->get_state();
    switch (state) {
        case hxcplayer::PlayerState::Idle: return PLAYER_STATE_IDLE;
        case hxcplayer::PlayerState::Opening: return PLAYER_STATE_OPENING;
        case hxcplayer::PlayerState::Playing: return PLAYER_STATE_PLAYING;
        case hxcplayer::PlayerState::Paused: return PLAYER_STATE_PAUSED;
        case hxcplayer::PlayerState::Stopped: return PLAYER_STATE_STOPPED;
        case hxcplayer::PlayerState::Error: return PLAYER_STATE_ERROR;
        default: return PLAYER_STATE_ERROR;
    }
}

double player_core_get_duration(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0.0;
    return handle->core->get_duration();
}

double player_core_get_position(PlayerCoreHandle* handle) {
    if (!handle || !handle->core) return 0.0;
    return handle->core->get_position();
}

void player_core_seek(PlayerCoreHandle* handle, double pos) {
    if (handle && handle->core) {
        handle->core->seek(pos);
    }
}

void player_core_set_volume(PlayerCoreHandle* handle, float volume) {
    if (handle && handle->core) {
        handle->core->set_volume(volume);
    }
}

void player_core_set_playback_rate(PlayerCoreHandle* handle, float rate) {
    if (handle && handle->core) {
        handle->core->set_playback_rate(rate);
        
#if defined(__APPLE__) || defined(_WIN32) || defined(__ANDROID__)
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

int player_core_get_video_frame(PlayerCoreHandle* handle, VideoFrameDataC* frame_data) {
    if (!handle || !handle->core || !frame_data) {
        return -1;
    }
    
    auto* videoQueue = handle->core->get_video_queue();
    if (!videoQueue || videoQueue->size() <= 0) {
        return -1;  // 没有可用帧
    }
    
    auto* vf = videoQueue->peek_readable();
    if (!vf || !vf->frame) {
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


int player_core_get_audio_data(PlayerCoreHandle* handle, unsigned char* buffer, int buffer_size) {
    if (!handle || !handle->core || !buffer || buffer_size <= 0) {
        return 0;
    }
    
    // 从缓冲区复制数据（如果有的话）
    while (buffer_size > 0) {
        // 如果缓冲区有数据，直接复制
        if (handle->audio_buf_index < handle->audio_buf_size) {
            int len1 = handle->audio_buf_size - handle->audio_buf_index;
            if (len1 > buffer_size) {
                len1 = buffer_size;
            }
            
            memcpy(buffer, handle->audio_buf + handle->audio_buf_index, len1);
            handle->audio_buf_index += len1;
            
            // ⚠️ 【关键修复】在数据被复制给平台层后，更新音频时钟
            if (handle->audio_current_sample_rate > 0 && handle->audio_current_channels > 0) {
                // 计算实际消费的样本数
                int consumed_samples = len1 / (handle->audio_current_channels * sizeof(int16_t));
                
                // 计算消费时长（媒体时间）
                double consumed_duration = (double)consumed_samples / handle->audio_current_sample_rate;
                
                // ⚠️ SoundTouch 倍速播放：每个输出样本对应 playback_rate 倍的媒体内容时间
                // 例如：2.0x 时，512 个输出样本对应 1024 个原始样本的媒体时间
                double current_rate = handle->core->get_playback_rate();
                if (current_rate != 1.0) {
                    consumed_duration *= current_rate;
                }
                
                // 更新 PTS（逐步累加）
                handle->audio_current_pts += consumed_duration;
                
                // 更新音频时钟
                handle->core->update_audio_pts(handle->audio_current_pts, 0);
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
        
        AVFrame* frame = af->frame;
        int channels = frame->ch_layout.nb_channels;
        int samples = frame->nb_samples;
        int sample_rate = frame->sample_rate;
        double pts = af->pts;  // ⚠️ 保存 PTS，用于后续时钟更新
        
        // ⚠️ 保存当前帧的时钟信息
        handle->audio_current_pts = pts;
        handle->audio_current_sample_rate = sample_rate;
        handle->audio_current_channels = channels;
        
        // 计算原始输出数据大小（S16 格式）
        int raw_output_size = samples * channels * sizeof(int16_t);
        
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
        
        int16_t* dst = (int16_t*)handle->audio_buf;
        
        // 根据输入格式转换为 S16 交织格式
        switch (frame->format) {
            case AV_SAMPLE_FMT_U8:
            {
                uint8_t* src = frame->data[0];
                for (int i = 0; i < samples * channels; i++) {
                    dst[i] = (int16_t)((src[i] - 128) << 8);
                }
                break;
            }
            
            case AV_SAMPLE_FMT_S16:
            {
                memcpy(dst, frame->data[0], raw_output_size);
                break;
            }
            
            case AV_SAMPLE_FMT_S32:
            {
                int32_t* src = (int32_t*)frame->data[0];
                for (int i = 0; i < samples * channels; i++) {
                    dst[i] = (int16_t)(src[i] >> 16);
                }
                break;
            }
            
            case AV_SAMPLE_FMT_FLT:
            {
                float* src = (float*)frame->data[0];
                for (int i = 0; i < samples * channels; i++) {
                    float sample = src[i];
                    if (sample > 1.0f) sample = 1.0f;
                    if (sample < -1.0f) sample = -1.0f;
                    dst[i] = (int16_t)(sample * 32767.0f);
                }
                break;
            }
            
            case AV_SAMPLE_FMT_U8P:
            {
                for (int i = 0; i < samples; i++) {
                    for (int ch = 0; ch < channels; ch++) {
                        uint8_t* src = frame->data[ch];
                        dst[i * channels + ch] = (int16_t)((src[i] - 128) << 8);
                    }
                }
                break;
            }
            
            case AV_SAMPLE_FMT_S16P:
            {
                for (int i = 0; i < samples; i++) {
                    for (int ch = 0; ch < channels; ch++) {
                        int16_t* src = (int16_t*)frame->data[ch];
                        dst[i * channels + ch] = src[i];
                    }
                }
                break;
            }
            
            case AV_SAMPLE_FMT_S32P:
            {
                for (int i = 0; i < samples; i++) {
                    for (int ch = 0; ch < channels; ch++) {
                        int32_t* src = (int32_t*)frame->data[ch];
                        dst[i * channels + ch] = (int16_t)(src[i] >> 16);
                    }
                }
                break;
            }
            
            case AV_SAMPLE_FMT_FLTP:
            {
                for (int i = 0; i < samples; i++) {
                    for (int ch = 0; ch < channels; ch++) {
                        float* src = (float*)frame->data[ch];
                        float sample = src[i];
                        if (sample > 1.0f) sample = 1.0f;
                        if (sample < -1.0f) sample = -1.0f;
                        dst[i * channels + ch] = (int16_t)(sample * 32767.0f);
                    }
                }
                break;
            }
            
            case AV_SAMPLE_FMT_DBLP:
            {
                for (int i = 0; i < samples; i++) {
                    for (int ch = 0; ch < channels; ch++) {
                        double* src = (double*)frame->data[ch];
                        double sample = src[i];
                        if (sample > 1.0) sample = 1.0;
                        if (sample < -1.0) sample = -1.0;
                        dst[i * channels + ch] = (int16_t)(sample * 32767.0);
                    }
                }
                break;
            }
            
            default:
                audioQueue->next();
                return 0;
        }
        
        // 消费音频帧
        audioQueue->next();
        
#if defined(__APPLE__) || defined(_WIN32) || defined(__ANDROID__)
        // 使用 SoundTouch 处理倍速播放
        double current_rate = handle->core->get_playback_rate();
        
        if (handle->soundtouch && current_rate != 1.0) {
            // 首次使用时设置采样率和通道数
            if (!handle->soundtouch_initialized) {
                handle->soundtouch->setSampleRate(sample_rate);
                handle->soundtouch->setChannels(channels);
                handle->soundtouch_initialized = true;
            }
            
            // 转换 S16 为 float
            std::vector<float> float_input(samples * channels);
            for (int i = 0; i < samples * channels; i++) {
                float_input[i] = (float)dst[i] / 32768.0f;
            }
            
            // 送入样本到 SoundTouch
            handle->soundtouch->putSamples(float_input.data(), samples);
            
            // 获取可用样本数
            uint32_t available = handle->soundtouch->numSamples();
            
            // 计算期望输出样本数
            uint32_t expected_output = (uint32_t)(samples / current_rate);
            
            // 只有当积累的样本 >= 期望输出的 80% 时才取出
            if (available >= expected_output * 0.8) {
                uint32_t samples_to_get = available > expected_output ? available : expected_output;
                std::vector<float> output_buffer(samples_to_get * channels);
                uint32_t received = handle->soundtouch->receiveSamples(output_buffer.data(), samples_to_get);
                
                if (received > 0) {
                    // 转换回 S16
                    int out_size = received * channels * sizeof(int16_t);
                    
                    // 确保缓冲区足够大
                    if (handle->audio_buf_size < (unsigned int)out_size * 2) {
                        free(handle->audio_buf);
                        handle->audio_buf = (uint8_t*)malloc(out_size * 2);
                        if (!handle->audio_buf) {
                            return 0;
                        }
                    }
                    
                    int16_t* s16_dst = (int16_t*)handle->audio_buf;
                    for (size_t i = 0; i < received * channels; i++) {
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
    
    handle->state_changed_callback = callback;
    handle->state_changed_user_data = user_data;
    
    // 设置 C++ 层回调
    if (callback) {
        handle->core->set_state_changed_callback([handle](hxcplayer::PlayerState state) {
            if (handle->state_changed_callback) {
                // 将 C++ 枚举转换为 C 枚举
                PlayerStateC c_state = static_cast<PlayerStateC>(state);
                handle->state_changed_callback(c_state, handle->state_changed_user_data);
            }
        });
    } else {
        handle->core->set_state_changed_callback(nullptr);
    }
}

// 设置错误回调
void player_core_set_error_callback(PlayerCoreHandle* handle, ErrorCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;
    
    handle->error_callback = callback;
    handle->error_user_data = user_data;
    
    // 设置 C++ 层回调
    if (callback) {
        handle->core->set_error_callback([handle](int error_code, const std::string& error_msg) {
            if (handle->error_callback) {
                handle->error_callback(error_code, error_msg.c_str(), handle->error_user_data);
            }
        });
    } else {
        handle->core->set_error_callback(nullptr);
    }
}

// 设置播放进度回调
void player_core_set_position_changed_callback(PlayerCoreHandle* handle, PositionChangedCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;
    
    handle->position_changed_callback = callback;
    handle->position_user_data = user_data;
    
    // 设置 C++ 层回调
    if (callback) {
        handle->core->set_position_changed_callback([handle](double position) {
            if (handle->position_changed_callback) {
                handle->position_changed_callback(position, handle->position_user_data);
            }
        });
    } else {
        handle->core->set_position_changed_callback(nullptr);
    }
}

void player_core_set_buffer_progress_callback(PlayerCoreHandle* handle, BufferProgressCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;
    
    handle->buffer_progress_callback = callback;
    handle->buffer_progress_user_data = user_data;
    
    // 设置 C++ 层回调
    if (callback) {
        handle->core->set_buffer_progress_callback([handle](double position) {
            if (handle->buffer_progress_callback) {
                handle->buffer_progress_callback(position, handle->buffer_progress_user_data);
            }
        });
    } else {
        handle->core->set_buffer_progress_callback(nullptr);
    }
}

void player_core_set_playback_completed_callback(PlayerCoreHandle* handle, PlaybackCompletedCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;
    
    handle->playback_completed_callback = callback;
    handle->playback_completed_user_data = user_data;
    
    // 设置 C++ 层回调
    if (callback) {
        handle->core->set_playback_completed_callback([handle]() {
            if (handle->playback_completed_callback) {
                handle->playback_completed_callback(handle->playback_completed_user_data);
            }
        });
    } else {
        handle->core->set_playback_completed_callback(nullptr);
    }
}

void player_core_set_loading_callback(PlayerCoreHandle* handle, LoadingCallbackC callback, void* user_data) {
    if (!handle || !handle->core) return;
    
    handle->loading_callback = callback;
    handle->loading_user_data = user_data;
    
    // 设置 C++ 层回调
    if (callback) {
        handle->core->set_loading_callback([handle](bool is_loading) {
            if (handle->loading_callback) {
                handle->loading_callback(is_loading, handle->loading_user_data);
            }
        });
    } else {
        handle->core->set_loading_callback(nullptr);
    }
}

// ========== 日志配置实现 ==========

// 静态变量存储日志文件路径
static std::string g_current_log_file;

void player_core_set_log_level(int level) {
    hxcplayer::LogLevel log_level;
    switch (level) {
        case 0: log_level = hxcplayer::LogLevel::DEBUG; break;
        case 1: log_level = hxcplayer::LogLevel::INFO; break;
        case 2: log_level = hxcplayer::LogLevel::WARNING; break;
        case 3: log_level = hxcplayer::LogLevel::ERROR; break;
        default: log_level = hxcplayer::LogLevel::INFO; break;
    }
    hxcplayer::Logger::instance().set_level(log_level);
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
