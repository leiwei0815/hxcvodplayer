/**
 * @file player_core.cpp
 * @brief 播放器核心实现（参照 ffplay）
 */

#include "hxc_player_core.h"
#include "hxc_player_core_c_bridge.h"  // 引入错误码定义
#include "hxc_logger.h"
#include "hxc_debug_helper.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>
#include <sstream>
#include <cstring>
extern "C" {
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libavutil/hwcontext.h>
#include <libavformat/avformat.h>
}

// FFmpeg 错误码定义
#ifndef AVERROR_PATCHWELCOME
#define AVERROR_PATCHWELCOME (-MKTAG('P','A','W','E'))  // "功能未实现"
#endif

// 跨平台延迟宏
#ifndef NO_SDL
    #define PLAYER_DELAY(ms) SDL_Delay(ms)
#else
    #define PLAYER_DELAY(ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms))
#endif

namespace hxcplayer {

namespace {

static bool hxc_is_http_client_error(int ret) {
    return ret == AVERROR_HTTP_BAD_REQUEST ||
           ret == AVERROR_HTTP_NOT_FOUND ||
           ret == AVERROR_HTTP_UNAUTHORIZED ||
           ret == AVERROR_HTTP_FORBIDDEN;
}

static bool hxc_is_retryable_network_error(int ret) {
    return ret == AVERROR(ETIMEDOUT) ||
           ret == AVERROR(ECONNREFUSED) ||
           ret == AVERROR(ENETUNREACH) ||
           ret == AVERROR(EIO) ||
           ret == AVERROR(EAGAIN) ||
           ret == AVERROR_HTTP_SERVER_ERROR;
}

static int hxc_calc_retry_delay_ms(int retry_count, int base_delay_ms, int max_delay_ms) {
    if (retry_count <= 0) {
        return base_delay_ms;
    }
    int capped_power = retry_count > 6 ? 6 : retry_count;
    int candidate = base_delay_ms << capped_power;
    return std::min(candidate, max_delay_ms);
}

// seek 对齐窗口：允许少量回退（关键帧对齐）和有限前跳（时间戳抖动）。
static constexpr double kSeekAnchorBackwardToleranceMinSec = 0.2;
static constexpr double kSeekAnchorBackwardToleranceMaxSec = 0.5;
static constexpr double kSeekAnchorForwardToleranceSec = 5.0;

static double hxc_calc_seek_backward_tolerance_sec(const MediaInfo& media_info) {
    // 自适应容差：3 * frame interval，并限制在 [0.2s, 0.5s]。
    // - 高帧率视频避免窗口过大导致旧帧漏过
    // - 低帧率视频保留足够回退窗口，避免过度丢帧导致首帧慢
    double fps = media_info.video_fps;
    if (!(fps > 1.0 && fps < 120.0)) {
        fps = 30.0; // 无可靠帧率时采用保守默认值
    }
    double adaptive = 3.0 / fps;
    return std::max(kSeekAnchorBackwardToleranceMinSec,
                    std::min(kSeekAnchorBackwardToleranceMaxSec, adaptive));
}

static bool hxc_is_network_like_url(const char* url) {
    if (!url || !*url) {
        return false;
    }
    std::string u(url);
    for (char& c : u) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return (u.rfind("http://", 0) == 0) ||
           (u.rfind("https://", 0) == 0) ||
           (u.rfind("rtmp://", 0) == 0) ||
           (u.rfind("rtsp://", 0) == 0) ||
           (u.rfind("udp://", 0) == 0) ||
           (u.rfind("tcp://", 0) == 0);
}

static std::string hxc_av_err_to_string(int err) {
    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, errbuf, sizeof(errbuf));
    return std::string(errbuf);
}

static std::string hxc_build_hls_invaliddata_hint(const char* url, int io_error) {
    std::string hint = "检测到 HLS 输入数据无效（常见原因：分片/密钥请求失败或返回非媒体数据）";
    if (url && *url) {
        hint += "，当前URL=" + std::string(url);
    }
    if (io_error != 0) {
        hint += "，底层I/O错误=" + std::to_string(io_error) + " (" + hxc_av_err_to_string(io_error) + ")";
    }
    return hint;
}

static enum AVHWDeviceType hxc_platform_hw_device_type() {
#if defined(__ANDROID__)
    return av_hwdevice_find_type_by_name("mediacodec");
#elif defined(__APPLE__)
    return av_hwdevice_find_type_by_name("videotoolbox");
#else
    return AV_HWDEVICE_TYPE_NONE;
#endif
}

static enum AVPixelFormat hxc_find_hw_pix_fmt_for_codec(const AVCodec *codec, enum AVHWDeviceType device_type) {
    if (!codec || device_type == AV_HWDEVICE_TYPE_NONE) {
        return AV_PIX_FMT_NONE;
    }
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config) {
            break;
        }
        if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
            config->device_type == device_type) {
            return config->pix_fmt;
        }
    }
    return AV_PIX_FMT_NONE;
}

static enum AVPixelFormat hxc_find_hw_pix_fmt_from_codec_ctx(const AVCodecContext *ctx) {
    if (!ctx || !ctx->codec) {
        return AV_PIX_FMT_NONE;
    }
    return hxc_find_hw_pix_fmt_for_codec(ctx->codec, hxc_platform_hw_device_type());
}

static enum AVPixelFormat hxc_hw_get_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    enum AVPixelFormat hw_fmt = hxc_find_hw_pix_fmt_from_codec_ctx(ctx);
    for (const enum AVPixelFormat *p = pix_fmts; p && *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == hw_fmt) {
            return *p;
        }
    }
    LOG_WARNING("硬解像素格式不可用，回退到解码器首选软解格式");
    return pix_fmts ? pix_fmts[0] : AV_PIX_FMT_NONE;
}

static bool hxc_codec_supports_hw_device(const AVCodec *codec, enum AVHWDeviceType device_type) {
    return hxc_find_hw_pix_fmt_for_codec(codec, device_type) != AV_PIX_FMT_NONE;
}
static bool hxc_try_enable_hw_decode(AVCodecContext *codec_ctx, const AVCodec *codec) {
    if (!codec_ctx || !codec) {
        return false;
    }
    enum AVHWDeviceType device_type = hxc_platform_hw_device_type();
    if (device_type == AV_HWDEVICE_TYPE_NONE) {
        return false;
    }
    if (!hxc_codec_supports_hw_device(codec, device_type)) {
        return false;
    }

    AVBufferRef *hw_device_ctx = nullptr;
    int ret = av_hwdevice_ctx_create(&hw_device_ctx, device_type, nullptr, nullptr, 0);
    if (ret < 0 || !hw_device_ctx) {
        LOG_WARNING("创建硬件解码设备失败，回退软解 ret=", ret);
        return false;
    }
    codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    av_buffer_unref(&hw_device_ctx);
    if (!codec_ctx->hw_device_ctx) {
        LOG_WARNING("硬件解码设备引用失败，回退软解");
        return false;
    }
    codec_ctx->get_format = hxc_hw_get_format;
    return true;
}

}  // namespace

PlayerCore::PlayerCore()
    : state_(PlayerState::Idle)
    , format_ctx_(nullptr)
    , video_stream_(-1)
    , audio_stream_(-1)
    , subtitle_stream_(-1)
    , video_stream_opened_(false)
    , audio_stream_opened_(false)
    , video_codec_ctx_(nullptr)
    , audio_codec_ctx_(nullptr)
    , abort_request_(false)
    , pause_request_(false)
    , seek_request_(false)
    , seek_pos_(0.0)
    , seeking_(false)  // ⚠️ 初始化 seeking 标志
    , seek_target_pos_(0.0)  // ⚠️ 初始化 seek 目标位置
    , decode_finished_(false)  // ⚠️ 初始化解码结束标志
    , playback_completed_notified_(false)  // ⚠️ 初始化播放完成通知标志
#ifndef NO_SDL
    , audio_dev_(0)
#endif
    , volume_(100)
    , swr_ctx_(nullptr)
    , audio_buf_(nullptr)
    , audio_buf_size_(0)
    , audio_buf_index_(0)
    , audio_current_pts_(0.0)
    , audio_current_pts_drift_(0.0)
    , render_window_(nullptr)
    , render_mode_(RenderMode::Auto)
    , aspect_ratio_mode_(AspectRatioMode::Fit)  // ⚠️ 默认 Fit 模式
#ifndef NO_SDL
    , sdl_renderer_(nullptr)
    , sdl_texture_(nullptr)
    , texture_width_(0)
    , texture_height_(0)
    , last_frame_width_(0)
    , last_frame_height_(0)
    , has_last_frame_(false)
#endif
#ifdef HAS_SOUNDTOUCH
    , soundtouch_(nullptr)
    , soundtouch_buffer_index_(0)
#endif
    , playback_rate_(1.0) {  // ⚠️ 默认正常速度
#if defined(__ANDROID__)
    io_interrupt_timeout_us_ = 6000000;   // Android 移动网络抖动更明显，超时阈值略短
#else
    io_interrupt_timeout_us_ = 10000000;  // 其它平台默认 10 秒
#endif
    
    LOG_INFO("初始化 PlayerCore...");
    
    // 初始化 FFmpeg 网络组件（必须在使用网络协议前调用）
    avformat_network_init();
    
    // 初始化 FFmpeg
    av_log_set_level(AV_LOG_WARNING);
    
#ifndef NO_SDL
    // 初始化 SDL（仅桌面平台）
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        LOG_ERROR("SDL初始化失败: ", SDL_GetError());
        std::cerr << "SDL初始化失败: " << SDL_GetError() << std::endl;
        emit_error(PLAYER_ERROR_SDL_INIT_FAILED, std::string("SDL 初始化失败: ") + SDL_GetError());
    } else {
        LOG_INFO("SDL 初始化成功");
    }
#else
    LOG_INFO("iOS 平台，跳过 SDL 初始化");
#endif
    
    LOG_INFO("PlayerCore 初始化完成");
}

PlayerCore::~PlayerCore() {
    LOG_INFO("销毁 PlayerCore...");
    close();
    cleanup_sdl_renderer();  // 清理渲染器

#ifdef HAS_SOUNDTOUCH
    // 释放 SoundTouch
    if (soundtouch_) {
        delete soundtouch_;
        soundtouch_ = nullptr;
    }
#endif
    
#ifndef NO_SDL
    SDL_Quit();
#endif
    
    // 反初始化 FFmpeg 网络组件
    avformat_network_deinit();
    
    LOG_INFO("PlayerCore 已销毁");
}

int PlayerCore::open(const std::string& filename) {
    auto open_begin = std::chrono::steady_clock::now();
    PlayerState current_state = get_state();
    if (current_state != PlayerState::Idle && current_state != PlayerState::Stopped) {
        LOG_WARNING("播放器状态错误，无法打开文件");
        set_state(PlayerState::Error);
        return -1;
    }
    LOG_INFO("========================================");
    LOG_INFO("开始打开文件");
    LOG_INFO("========================================");
    LOG_INFO("URL: ", filename);
    LOG_INFO("当前状态: ", (int)current_state);
    LOG_INFO("配置信息:");
    LOG_INFO("  - 启用音频: ", config_.enable_audio ? "是" : "否");
    LOG_INFO("  - 启用视频: ", config_.enable_video ? "是" : "否");
    LOG_INFO("  - 音频队列大小: ", config_.audio_queue_size);
    LOG_INFO("  - 视频队列大小: ", config_.video_queue_size);
    LOG_INFO("  - 开始播放时间: ", config_.start_time, " 秒");
    
    // 🔍 检测 URL 协议
    const char* proto = avio_find_protocol_name(filename.c_str());
    if (proto) {
        LOG_INFO("检测到的协议: ", proto);
    } else {
        LOG_ERROR("⚠️ 无法识别 URL 协议！URL: ", filename);
    }

    // ⚠️ 重置终止标志（重要！否则之前 close() 设置的 true 会导致新线程立即退出）
    abort_request_ = false;
    decode_finished_ = false;  // ⚠️ 重置解码结束标志
    video_hw_decode_active_.store(false, std::memory_order_release);
    playback_completed_notified_ = false;  // ⚠️ 重置播放完成通知标志
    io_last_packet_us_.store(av_gettime_relative(), std::memory_order_release);

    set_state(PlayerState::Opening);
    set_play_when_ready_internal(true);
    first_video_frame_ready_.store(false, std::memory_order_release);
    first_audio_frame_ready_.store(false, std::memory_order_release);
    // 打开输入文件
    format_ctx_ = avformat_alloc_context();
    // 🔧 设置中断回调（防止网络卡住导致无限等待）
    format_ctx_->interrupt_callback.callback = [](void* ctx) -> int {
        PlayerCore* player = static_cast<PlayerCore*>(ctx);
        // 如果用户请求中止，返回 1
        if (player->abort_request_.load()) {
            return 1;
        }
        int64_t last_packet_us = player->io_last_packet_us_.load(std::memory_order_acquire);
        if (last_packet_us > 0) {
            int64_t idle_us = av_gettime_relative() - last_packet_us;
            if (idle_us >= player->io_interrupt_timeout_us_) {
                return 1;
            }
        }
        return 0;
    };
    format_ctx_->interrupt_callback.opaque = this;
    
    // 🔧 设置网络超时和重试参数（对于网络流很重要）
    AVDictionary* options = nullptr;
    
    // 设置超时时间（微秒）
#if defined(__ANDROID__)
    // Android 移动网络抖动明显，适当降低默认等待时间，避免单次卡住过久。
    av_dict_set(&options, "timeout", "8000000", 0);      // 8 秒
    av_dict_set(&options, "stimeout", "3000000", 0);     // 3 秒
#else
    av_dict_set(&options, "timeout", "15000000", 0);     // 15 秒
    av_dict_set(&options, "stimeout", "5000000", 0);     // 5 秒
#endif
    
    // 设置重连次数
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);
    
    // 设置 User-Agent（根据平台自适应）
#if defined(__APPLE__) && TARGET_OS_IOS
    // iOS 平台使用 Mobile Safari User-Agent
    av_dict_set(&options, "user_agent", "Mozilla/5.0 (iPhone; CPU iPhone OS 15_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/15.0 Mobile/15E148 Safari/604.1", 0);
#elif defined(__APPLE__) && TARGET_OS_OSX
    // macOS 平台使用 Safari User-Agent
    av_dict_set(&options, "user_agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/15.0 Safari/605.1.15", 0);
#elif defined(__ANDROID__)
    // Android 平台使用 Chrome Mobile User-Agent
    av_dict_set(&options, "user_agent", "Mozilla/5.0 (Linux; Android 11) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.120 Mobile Safari/537.36", 0);
#elif defined(_WIN32)
    // Windows 平台使用 Chrome User-Agent
    av_dict_set(&options, "user_agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36", 0);
#else
    // 其他平台使用通用 User-Agent
    av_dict_set(&options, "user_agent", "HXCPlayer/1.0", 0);
#endif
    if (!secure_session_.request_headers.empty()) {
        av_dict_set(&options, "headers", secure_session_.request_headers.c_str(), 0);
//        av_dict_set(&options, "encryption_key_hex", "fe2bbaaa1b8af866bbdef997dae028b5", 0);
//        av_dict_set(&options, "encryption_iv_hex", "00000000000000000000000000000000", 0);
        LOG_INFO("SecureHLS 注入 headers 成功");
    }
    
    // 🔧 增强重定向支持（处理 302 等重定向）
    av_dict_set(&options, "follow_redirects", "1", 0);
    av_dict_set(&options, "max_redirects", "10", 0);
    
    // 🔧 HTTPS/TLS 配置（解决证书信任问题）
    // 检查是否是 HTTPS 链接
    bool is_https = (filename.find("https://") == 0);
    if (is_https) {
        LOG_INFO("检测到 HTTPS 链接，配置 TLS 参数...");
        
        // 禁用 TLS 证书验证（用于解决 iOS 不信任某些 CA 的问题）
        av_dict_set(&options, "tls_verify", "0", 0);
        
        // 设置 HTTP 协议选项
        av_dict_set(&options, "multiple_requests", "1", 0);
        // ⚠️ 移除 seekable=0，该选项会导致 HTTPS 失败
        // av_dict_set(&options, "seekable", "0", 0);
        
        // 增加超时设置（避免卡住）
#if defined(__ANDROID__)
        av_dict_set(&options, "rw_timeout", "6000000", 0);      // 读写超时 6 秒
#else
        av_dict_set(&options, "rw_timeout", "10000000", 0);     // 读写超时 10 秒
#endif
        
        LOG_INFO("TLS 参数配置完成（证书验证已禁用）");
    }
    
    // 🔧 设置缓冲区大小（对于网络流很重要）
    av_dict_set(&options, "buffer_size", "1024000", 0);  // 1MB 缓冲
    
    // 🔧 设置分析时长（快速开始播放）
    av_dict_set(&options, "analyzeduration", "5000000", 0);  // 5秒
    av_dict_set(&options, "probesize", "5000000", 0);  // 5MB
    
    LOG_INFO("网络参数配置完成，开始打开流...");
    LOG_INFO("调用 avformat_open_input，URL: ", filename);
    
    // 🔍 在调用前打印所有配置的选项
//    LOG_INFO("========== FFmpeg 选项配置 ==========");
//    AVDictionaryEntry* debug_entry = nullptr;
//    while ((debug_entry = av_dict_get(options, "", debug_entry, AV_DICT_IGNORE_SUFFIX))) {
//        LOG_INFO("  ", debug_entry->key, " = ", debug_entry->value);
//    }
//    LOG_INFO("=====================================");
    
    // ⚠️ 对于网络协议，很多选项需要通过 URL 参数传递
    // 构建带参数的 URL
    std::string url_with_params = filename;
    
    // 🔄 重试机制配置
    // Android 上降低重试次数，减少最坏场景等待时间
#if defined(__ANDROID__)
    const int MAX_RETRY_COUNT = 1;          // 最大重试次数（总尝试 2 次）
#else
    const int MAX_RETRY_COUNT = 3;          // 最大重试次数（总尝试 4 次）
#endif
    const int RETRY_DELAY_MS = 500;         // 重试基础间隔（毫秒）
    const int RETRY_DELAY_MAX_MS = 3000;    // 重试最大间隔（毫秒）
    int retry_count = 0;
    int ret = -1;
    
    // 尝试打开文件（支持重试）
    while (retry_count <= MAX_RETRY_COUNT) {
        if (retry_count > 0) {
            LOG_WARNING("正在重试打开文件... (第 ", retry_count, "/", MAX_RETRY_COUNT, " 次)");
            
            // 重试前等待一段时间（指数退避，避免弱网时高频请求）
            int delay_ms = hxc_calc_retry_delay_ms(retry_count - 1, RETRY_DELAY_MS, RETRY_DELAY_MAX_MS);
            LOG_INFO("打开重试退避等待: ", delay_ms, " ms");
            PLAYER_DELAY(delay_ms);
            
            // 检查是否被中止
            if (abort_request_.load()) {
                LOG_INFO("用户取消操作，停止重试");
                
                // 用户取消，回调错误
                emit_error(AVERROR_EXIT, "用户取消了播放操作");
                set_state(PlayerState::Stopped);
                av_dict_free(&options);
                return -1;
            }
            
            // 重新分配 format_ctx（之前的可能已损坏）
            if (format_ctx_) {
                avformat_close_input(&format_ctx_);
                format_ctx_ = nullptr;
            }
            format_ctx_ = avformat_alloc_context();
            
            // 重新设置中断回调
            format_ctx_->interrupt_callback.callback = [](void* ctx) -> int {
                PlayerCore* player = static_cast<PlayerCore*>(ctx);
                if (player->abort_request_.load()) {
                    return 1;
                }
                int64_t last_packet_us = player->io_last_packet_us_.load(std::memory_order_acquire);
                if (last_packet_us > 0) {
                    int64_t idle_us = av_gettime_relative() - last_packet_us;
                    if (idle_us >= player->io_interrupt_timeout_us_) {
                        return 1;
                    }
                }
                return 0;
            };
            format_ctx_->interrupt_callback.opaque = this;
        }
        // 每次 open 尝试前刷新 watchdog 基线，避免因上一次耗时叠加导致本次被立即中断。
        io_last_packet_us_.store(av_gettime_relative(), std::memory_order_release);
        
        auto attempt_begin = std::chrono::steady_clock::now();
        // 尝试打开
        ret = avformat_open_input(&format_ctx_, url_with_params.c_str(), nullptr, &options);
        auto attempt_cost_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - attempt_begin).count();
        LOG_INFO("avformat_open_input 耗时: ", attempt_cost_ms, " ms (尝试 ",
                 retry_count + 1, "/", MAX_RETRY_COUNT + 1, ")");
        
        // 成功则退出循环
        if (ret == 0) {
            if (retry_count > 0) {
                LOG_INFO("重试成功！文件已打开");
            } else {
                LOG_INFO("文件打开成功");
            }
            break;
        }
        
        // 失败，分析错误类型
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        
        LOG_ERROR("打开文件失败 (尝试 ", retry_count + 1, "/", MAX_RETRY_COUNT + 1, ")");
        LOG_ERROR("FFmpeg 错误码: ", ret, ", 错误信息: ", errbuf);
        
        // 判断是否应该重试
        bool should_retry = false;
        std::string error_category = "未知错误";
        const bool network_like_input = hxc_is_network_like_url(filename.c_str());
        
        // 网络相关错误应该重试
        if (hxc_is_retryable_network_error(ret)) {
            should_retry = true;
            error_category = "网络错误";
            LOG_INFO("检测到网络错误，将进行重试");
        }
        // 网络流首开阶段偶发 EOF（常见于 TLS 抖动/边缘节点返回不完整）先按可恢复处理。
        if (ret == AVERROR_EOF && network_like_input) {
            should_retry = true;
            error_category = "网络流EOF";
            LOG_WARNING("检测到网络流 EOF，按可恢复错误重试");
        }
        
        // HTTP 4xx 通常为请求或鉴权问题，不建议重试
        if (hxc_is_http_client_error(ret)) {
            should_retry = false;
            error_category = "客户端请求错误";
            LOG_ERROR("检测到不可恢复 HTTP 4xx，停止重试");
        }
        
        // 某些错误不应该重试 - 立即回调并退出
        if (ret == AVERROR(ENOENT)) {           // 文件不存在
            should_retry = false;
            error_category = "文件不存在";
            LOG_ERROR("检测到不可恢复错误：文件不存在，立即停止");
            
            // 立即回调错误
            emit_error(ERROR_INVALID_URL, "文件不存在: " + filename + " (错误码: " + std::to_string(ERROR_INVALID_URL) + ", " + std::string(errbuf) + ")");
            set_state(PlayerState::Error);
            av_dict_free(&options);
            return -1;
        }
        
        if (ret == AVERROR(EACCES)) {           // 权限拒绝
            should_retry = false;
            error_category = "权限拒绝";
            LOG_ERROR("检测到不可恢复错误：权限拒绝，立即停止");
            
            // 立即回调错误
            emit_error(ERROR_OPEN_INPUT_FAILED, "无权限访问: " + filename + " (错误码: " + std::to_string(ERROR_OPEN_INPUT_FAILED) + ", " + std::string(errbuf) + ")");
            set_state(PlayerState::Error);
            av_dict_free(&options);
            return -1;
        }
        
        if (ret == AVERROR_INVALIDDATA) {       // 无效数据
            if (network_like_input) {
                should_retry = true;
                error_category = "网络流无效数据（可重试）";
                LOG_WARNING("检测到网络流 INVALIDDATA，先重试");
            } else {
            should_retry = false;
            error_category = "无效数据";
            LOG_ERROR("检测到不可恢复错误：无效数据格式，立即停止");
            
            // 立即回调错误
            emit_error(ERROR_INPUT_INVALID_DATA, "文件格式无效或损坏: " + filename + " (错误码: " + std::to_string(ERROR_INPUT_INVALID_DATA) + ", " + std::string(errbuf) + ")");
            set_state(PlayerState::Error);
            av_dict_free(&options);
            return -1;
            }
        }
        
        if (ret == AVERROR_PATCHWELCOME) {      // 功能未实现
            should_retry = false;
            error_category = "功能未实现";
            LOG_ERROR("检测到不可恢复错误：功能未实现，立即停止");
            
            // 立即回调错误
            emit_error(ERROR_NOT_SUPPORT, "不支持的格式或协议: " + filename + " (错误码: " + std::to_string(ERROR_NOT_SUPPORT) + ", " + std::string(errbuf) + ")");
            set_state(PlayerState::Error);
            av_dict_free(&options);
            return -1;
        }
        
        // 如果不应该重试或已达到最大重试次数，退出循环
        if (!should_retry || retry_count >= MAX_RETRY_COUNT) {
            // 记录错误类别，用于最终错误消息
            if (retry_count >= MAX_RETRY_COUNT) {
                LOG_ERROR("已达到最大重试次数，停止重试");
            }
            break;
        }
        
        retry_count++;
    }
    
    // 🔍 打印未使用的选项（用于诊断）
//    AVDictionaryEntry* entry = nullptr;
//    while ((entry = av_dict_get(options, "", entry, AV_DICT_IGNORE_SUFFIX))) {
//        LOG_WARNING("未使用的 FFmpeg 选项: ", entry->key, " = ", entry->value);
//    }
    
    // 释放 options
    av_dict_free(&options);
    
    // 最终检查结果
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        auto open_total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - open_begin).count();
        
        // 构建详细的错误消息
        std::string error_message;
        
        if (retry_count > 0) {
            LOG_ERROR("重试 ", retry_count, " 次后仍然失败");
            error_message = "重试 " + std::to_string(retry_count) + " 次后仍然失败: ";
        } else {
            error_message = "打开文件失败: ";
        }
        PlayerErrorCode code = ERROR_NONE;
        // 根据错误类型提供更友好的错误消息
        if (ret == AVERROR(ETIMEDOUT)) {
            error_message += "连接超时，请检查网络连接";
            code = ERROR_NET_CONNECTION_TIMEOUT;
        } else if (ret == AVERROR_EOF && hxc_is_network_like_url(filename.c_str())) {
            error_message += "网络连接中断（首包读取失败）";
            code = ERROR_NET_UNREACHABLE;
        } else if (ret == AVERROR(ECONNREFUSED)) {
            error_message += "服务器拒绝连接";
            code = ERROR_NET_CONNECTION_REFUSED;
        } else if (ret == AVERROR(ENETUNREACH)) {
            error_message += "网络不可达，请检查网络设置";
            code = ERROR_INPUT_INVALID_DATA;
        } else if (ret == AVERROR(EIO)) {
            code = ERROR_OPEN_INPUT_FAILED;
            error_message += "I/O 错误，可能是网络问题";
        } else if (ret == AVERROR_HTTP_BAD_REQUEST) {
            code = ERROR_HTTP_BAD_REQUEST;
            error_message += "HTTP 请求错误（400）";
        } else if (ret == AVERROR_HTTP_NOT_FOUND) {
            code = ERROR_HTTP_NOT_FOUND;
            error_message += "文件不存在（404）";
        } else if (ret == AVERROR_HTTP_SERVER_ERROR) {
            code = ERROR_HTTP_SERVER_ERROR;
            error_message += "服务器内部错误（5xx）";
        } else if (ret == AVERROR_HTTP_UNAUTHORIZED) {
            code = ERROR_HTTP_UNAUTHORIZED;
            error_message += "需要身份验证（401）";
        } else if (ret == AVERROR_HTTP_FORBIDDEN) {
            code = ERROR_HTTP_FORBIDDEN;
            error_message += "访问被禁止（403）";
        } else if (ret == AVERROR_INVALIDDATA) {
            code = ERROR_INPUT_INVALID_DATA;
            error_message += hxc_build_hls_invaliddata_hint(filename.c_str(),
                                                           (format_ctx_ && format_ctx_->pb) ? format_ctx_->pb->error : 0);
        } else {
            error_message += filename;
            code = ERROR_UNKNOWN;
        }
        
        error_message += " (错误码: " + std::to_string(code) + ", " + std::string(errbuf) + ")";
        
        LOG_ERROR("无法打开文件: ", filename);
        LOG_ERROR("FFmpeg 错误码: ", ret, ", 错误信息: ", errbuf);
        LOG_ERROR("open() 总耗时: ", open_total_ms, " ms");
        
        // 发送错误回调给外层
        emit_error(code, error_message);
        set_state(PlayerState::Error);
        return -1;
    }

    auto open_common_begin = std::chrono::steady_clock::now();
    int open_common_ret = open_common_process(filename);
    auto open_common_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - open_common_begin).count();
    auto open_total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - open_begin).count();
    LOG_INFO("open_common_process 耗时: ", open_common_ms, " ms");
    LOG_INFO("open() 总耗时: ", open_total_ms, " ms");

    return open_common_ret;
}

// 使用自定义数据源打开
int PlayerCore::open_with_custom_io(std::unique_ptr<CustomAVIOContext> custom_io,
                                    const std::string& url_for_format) {
    PlayerState current_state = get_state();
    if (current_state != PlayerState::Idle && current_state != PlayerState::Stopped) {
        LOG_WARNING("播放器状态错误，无法打开文件");
        set_state(PlayerState::Error);
        return -1;
    }
    
    LOG_INFO("========================================");
    LOG_INFO("使用自定义数据源打开文件");
    LOG_INFO("========================================");
    LOG_INFO("配置信息:");
    LOG_INFO("  - 启用音频: ", config_.enable_audio ? "是" : "否");
    LOG_INFO("  - 启用视频: ", config_.enable_video ? "是" : "否");
    
    // 重置标志
    abort_request_ = false;
    decode_finished_ = false;
    playback_completed_notified_ = false;
    io_last_packet_us_.store(av_gettime_relative(), std::memory_order_release);
    
    // 保存自定义 IO
    custom_io_ = std::move(custom_io);
    
    set_state(PlayerState::Opening);
    set_play_when_ready_internal(true);
    first_video_frame_ready_.store(false, std::memory_order_release);
    first_audio_frame_ready_.store(false, std::memory_order_release);
    
    // 分配 AVFormatContext
    format_ctx_ = avformat_alloc_context();
    
    // 使用自定义 AVIOContext（须配合 AVFMT_FLAG_CUSTOM_IO，避免 close 时误关我们持有的 pb）
    format_ctx_->pb = custom_io_->get_avio_context();
    format_ctx_->flags |= AVFMT_FLAG_CUSTOM_IO;
    
    // 设置中断回调
    format_ctx_->interrupt_callback.callback = [](void* ctx) -> int {
        PlayerCore* player = static_cast<PlayerCore*>(ctx);
        if (player->abort_request_.load()) {
            return 1;
        }
        int64_t last_packet_us = player->io_last_packet_us_.load(std::memory_order_acquire);
        if (last_packet_us > 0) {
            int64_t idle_us = av_gettime_relative() - last_packet_us;
            if (idle_us >= player->io_interrupt_timeout_us_) {
                return 1;
            }
        }
        return 0;
    };
    format_ctx_->interrupt_callback.opaque = this;
    
    LOG_INFO("开始打开输入流（使用自定义 AVIOContext）...");
    const char* url_for_probe = url_for_format.empty() ? nullptr : url_for_format.c_str();
    if (url_for_probe) {
        LOG_INFO("探测/解析用 URL（与自定义 IO 数据源一致）: ", url_for_format);
    }

    // 重试机制
    const int MAX_RETRY = 3;
    int retry = 0;
    int ret = -1;

    while (retry <= MAX_RETRY) {
        if (retry > 0) {
            LOG_WARNING("重试打开... (", retry, "/", MAX_RETRY, ")");
            PLAYER_DELAY(1000);
            if (abort_request_.load()) {
                emit_error(AVERROR_EXIT, "用户取消");
                set_state(PlayerState::Stopped);
                return -1;
            }
            if (format_ctx_) {
                avformat_close_input(&format_ctx_);
            }
            format_ctx_ = avformat_alloc_context();
            format_ctx_->pb = custom_io_->get_avio_context();
            format_ctx_->flags |= AVFMT_FLAG_CUSTOM_IO;
            format_ctx_->interrupt_callback.callback = [](void* ctx) -> int {
                PlayerCore* player = static_cast<PlayerCore*>(ctx);
                return player->abort_request_.load() ? 1 : 0;
            };
            format_ctx_->interrupt_callback.opaque = this;
        }

        ret = avformat_open_input(&format_ctx_, url_for_probe, nullptr, nullptr);
        if (ret >= 0) {
            LOG_INFO("输入流打开成功");
            break;
        }

        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        LOG_ERROR("打开失败 (", retry + 1, "/", MAX_RETRY + 1, "): ", errbuf);
        retry++;
    }

    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        emit_error(ret, "无法打开输入流: " + std::string(errbuf));
        set_state(PlayerState::Error);
        return -1;
    }

    // 创建队列
//    video_packet_queue_ = std::make_unique<PacketQueue>();
//    audio_packet_queue_ = std::make_unique<PacketQueue>();
//    subtitle_packet_queue_ = std::make_unique<PacketQueue>();
//    video_queue_ = std::make_unique<FrameQueue<VideoFrame>>(config_.video_queue_size);
//    audio_queue_ = std::make_unique<FrameQueue<AudioFrame>>(config_.audio_queue_size);
    return open_common_process("custom_io_stream");
}

// 使用指定数据源模式打开
int PlayerCore::open_with_mode(const std::string& url, DataSourceMode mode, const CustomDataSourceConfig& config) {
    PlayerState current_state = get_state();
    if (current_state != PlayerState::Idle && current_state != PlayerState::Stopped) {
        LOG_WARNING("播放器状态错误，无法打开文件");
        set_state(PlayerState::Error);
        return -1;
    }

    auto looks_like_hls = [](const std::string& u) -> bool {
        // HLS playlist 典型为 .m3u8（也可能带 query）
        auto pos = u.find(".m3u8");
        return pos != std::string::npos;
    };
    
    LOG_INFO("========================================");
    LOG_INFO("使用数据源模式打开文件");
    LOG_INFO("  URL: ", url);
    LOG_INFO("  模式: ", static_cast<int>(mode));
    LOG_INFO("========================================");
    
    // 根据模式选择打开方式
    switch (mode) {
        case DataSourceMode::Default:
            // 使用默认的 FFmpeg 直接打开
            LOG_INFO("使用默认模式（FFmpeg 直接打开）");
            return open(url);
            
        case DataSourceMode::CustomHTTP: {
            // ⚠️ CustomHTTP 目前仅提供“单一自定义 AVIOContext(pb)”读取能力。
            // 对 HLS（m3u8）这类“主清单 + 多分片 URL”场景，FFmpeg demuxer 仍会用自身的 http(s)
            // 去打开分片请求，无法复用我们这里的 RangeDownloader/自定义 IO。
            // 因此遇到 m3u8 时自动降级到 Default，让 FFmpeg 自己完成所有网络请求。
            if (looks_like_hls(url)) {
                LOG_WARNING("CustomHTTP 不支持 HLS(m3u8) 多请求分片读取，已自动降级到 Default 模式");
                return open(url);
            }

            // 自动创建 HttpRangeDataSource 和 CustomAVIOContext
            LOG_INFO("使用自定义 HTTP Range 下载器模式");
            LOG_INFO("配置参数:");
            LOG_INFO("  - 超时时间: ", config.timeout_ms, " ms");
            LOG_INFO("  - 最大重试: ", config.max_retries, " 次");
            LOG_INFO("  - 缓存大小: ", config.cache_size / 1024, " KB");
            LOG_INFO("  - AVIO 缓冲区: ", config.avio_buffer_size / 1024, " KB");
            
            try {
                // 1. 创建 HttpRangeDataSource
                auto dataSource = std::make_unique<HttpRangeDataSource>();
                
                // 2. 配置下载器参数
                auto* downloader = dataSource->get_downloader();
                // RangeDownloader 的 timeout 单位就是毫秒，直接透传。
                downloader->set_timeout(config.timeout_ms);
                downloader->set_max_retries(config.max_retries);
                dataSource->set_cache_size(config.cache_size);
                dataSource->set_encrypted_file(config.encrypted_file);
                
                // 3. 设置下载进度回调
                auto last_percent = std::make_shared<int>(-1);
                downloader->set_progress_callback([this, last_percent](int64_t downloaded, int64_t total) {
                    if (total > 0 && downloaded >= 0) {
                        int percent = (int)((double)downloaded / (double)total * 100.0);
                        if (percent != *last_percent) {
                            *last_percent = percent;
                            LOG_DEBUG("下载进度: ", percent, "% (", 
                                     downloaded / 1024 / 1024, "MB/", 
                                     total / 1024 / 1024, "MB)");
                        }
                    }
                });
                
                // 4. 打开数据源（获取文件大小等信息）
                if (dataSource->open(url) < 0) {
                    LOG_ERROR("无法打开数据源: ", url);
                    emit_error(ERROR_OPEN_INPUT_FAILED, "无法打开数据源");
                    set_state(PlayerState::Error);
                    return -1;
                }
                
                // 重定向后的 URL：HLS 分片多为相对路径，须与 m3u8 实际地址一致（否则 FFmpeg 拼错分片 URL 会 404）
                const std::string format_url = downloader->effective_url();
                
                LOG_INFO("数据源打开成功");
                LOG_INFO("  - 文件大小: ", dataSource->size() / 1024 / 1024, " MB");
                LOG_INFO("  - 支持 Seek: ", dataSource->seekable() ? "是" : "否");
                LOG_INFO("  - 解析/基址 URL: ", format_url);
                
                // 5. 创建 CustomAVIOContext
                auto customIO = std::make_unique<CustomAVIOContext>(
                    std::move(dataSource),
                    config.avio_buffer_size
                );
                
                if (!customIO->get_avio_context()) {
                    LOG_ERROR("无法创建 AVIOContext");
                    emit_error(ERROR_ALLOC_CONTEXT_FAILED, "无法创建 AVIOContext");
                    set_state(PlayerState::Error);
                    return -1;
                }
                
                LOG_INFO("CustomAVIOContext 创建成功");
                
                // 6. 使用自定义 IO 打开（传入与数据源一致的最终 URL，供 HLS 等解析相对分片路径）
                return open_with_custom_io(std::move(customIO), format_url);
                
            } catch (const std::exception& e) {
                LOG_ERROR("创建自定义数据源失败: ", e.what());
                emit_error(ERROR_OPEN_INPUT_FAILED, std::string("创建自定义数据源失败: ") + e.what());
                set_state(PlayerState::Error);
                return -1;
            }
        }

        case DataSourceMode::CustomFile: {
            // 本地文件：通过自定义 IO 读取（可选：对文件头前 100 字节解密）
            LOG_INFO("使用本地文件自定义读取模式");
            LOG_INFO("配置参数:");
            LOG_INFO("  - AVIO 缓冲区: ", config.avio_buffer_size / 1024, " KB");
            LOG_INFO("  - 加密文件: ", config.encrypted_file ? "是" : "否");

            try {
                auto dataSource = std::make_unique<LocalFileDataSource>();
                dataSource->set_encrypted_file(config.encrypted_file);

                if (dataSource->open(url) < 0) {
                    LOG_ERROR("无法打开本地数据源: ", url);
                    emit_error(ERROR_OPEN_INPUT_FAILED, "无法打开本地数据源");
                    set_state(PlayerState::Error);
                    return -1;
                }

                auto customIO = std::make_unique<CustomAVIOContext>(
                    std::move(dataSource),
                    config.avio_buffer_size
                );

                if (!customIO->get_avio_context()) {
                    LOG_ERROR("无法创建 AVIOContext");
                    emit_error(ERROR_ALLOC_CONTEXT_FAILED, "无法创建 AVIOContext");
                    set_state(PlayerState::Error);
                    return -1;
                }

                return open_with_custom_io(std::move(customIO), url);

            } catch (const std::exception& e) {
                LOG_ERROR("创建本地自定义数据源失败: ", e.what());
                emit_error(ERROR_OPEN_INPUT_FAILED, std::string("创建本地自定义数据源失败: ") + e.what());
                set_state(PlayerState::Error);
                return -1;
            }
        }
        case DataSourceMode::SecureHLS: {
            LOG_INFO("使用 SecureHLS Header 透传模式");
            secure_session_ = SecureHLSSession{};
            secure_session_.m3u8_url = url;
            if (config.secure_headers) {
                secure_session_.request_headers = config.secure_headers;
            }
            return open(url);
        }
            
        default:
            LOG_ERROR("不支持的数据源模式: ", static_cast<int>(mode));
            emit_error(ERROR_NOT_SUPPORT, "不支持的数据源模式");
            set_state(PlayerState::Error);
            return -1;
    }
}

int PlayerCore::open_common_process(const std::string &filename) {
    auto common_begin = std::chrono::steady_clock::now();
    video_stream_opened_ = false;
    audio_stream_opened_ = false;
    // 获取流信息
    auto find_stream_begin = std::chrono::steady_clock::now();
    int ret = avformat_find_stream_info(format_ctx_, nullptr);
    auto find_stream_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - find_stream_begin).count();
    LOG_INFO("avformat_find_stream_info 耗时: ", find_stream_ms, " ms");
    if (ret < 0) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, errbuf, sizeof(errbuf));
        emit_error(ERROR_FIND_STREAM_INFO_FAILED, "无法获取流信息 (错误: " + std::string(errbuf) + ")");
        set_state(PlayerState::Error);
        return -1;
    }
    
    // 打印媒体信息
    av_dump_format(format_ctx_, 0, filename.c_str(), 0);
    
    // 打印详细的媒体信息（调试用）
    DebugHelper::print_media_info(format_ctx_);
    
    // 查找视频流和音频流
    video_stream_ = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audio_stream_ = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    
    // 填充媒体信息
    media_info_.filename = filename;
    media_info_.duration = format_ctx_->duration;
    media_info_.bitrate = format_ctx_->bit_rate;
    
    if (video_stream_ >= 0) {
        AVStream* stream = format_ctx_->streams[video_stream_];
        media_info_.video_width = stream->codecpar->width;
        media_info_.video_height = stream->codecpar->height;
        media_info_.video_codec = stream->codecpar->codec_id;
        
        AVRational frame_rate = av_guess_frame_rate(format_ctx_, stream, nullptr);
        media_info_.video_fps = av_q2d(frame_rate);
    }
    
    if (audio_stream_ >= 0) {
        AVStream* stream = format_ctx_->streams[audio_stream_];
        media_info_.audio_sample_rate = stream->codecpar->sample_rate;
        media_info_.audio_channels = stream->codecpar->ch_layout.nb_channels;
        media_info_.audio_codec = stream->codecpar->codec_id;
    }
    
    // ⚠️ 必须先创建队列，再启动解码线程！
    // 创建数据包队列
    video_packet_queue_ = std::make_unique<PacketQueue>();
    audio_packet_queue_ = std::make_unique<PacketQueue>();
    subtitle_packet_queue_ = std::make_unique<PacketQueue>();
    
    // 创建帧队列
    video_queue_ = std::make_unique<FrameQueue<VideoFrame>>(config_.video_queue_size);
    audio_queue_ = std::make_unique<FrameQueue<AudioFrame>>(config_.audio_queue_size);
    
    LOG_INFO("队列创建完成");
    
    // ⚠️ 参考 ffplay：如果配置了开始播放时间，在打开流之前先 seek
    // 这样可以避免解码不需要的数据，显著提高启动速度
    if (config_.start_time > 0.0) {
        double duration = get_duration();
        if (duration > 0 && config_.start_time < duration) {
            int64_t seek_target = config_.start_time * AV_TIME_BASE;
            LOG_INFO("配置了开始播放时间: ", config_.start_time, " 秒，在启动线程前先 seek...");
            
            // ⚠️ 使用 avformat_seek_file（比 av_seek_frame 更精确）
            int ret = avformat_seek_file(format_ctx_, -1,
                                         INT64_MIN,      // min_ts
                                         seek_target,     // ts (目标时间)
                                         seek_target,     // max_ts
                                         0);              // flags
            if (ret < 0) {
                LOG_WARNING("初始 seek 失败，将从头开始播放");
            } else {
                LOG_INFO("初始 seek 成功，将从 ", config_.start_time, " 秒开始播放");
                // ⚠️ 清空解复用器的内部缓冲区
                avformat_flush(format_ctx_);
                // 更新时钟到 seek 目标位置，避免音视频同步判断异常
                audio_clock_.set_clock(config_.start_time, 0);
                video_clock_.set_clock(config_.start_time, 0);
                external_clock_.set_clock(config_.start_time, 0);
                // 初始 startPosition 也走与 seek 相同的“首帧锚点”流程，避免旧帧把主时钟拉回。
                seek_target_pos_.store(config_.start_time, std::memory_order_release);
                seeking_.store(true, std::memory_order_release);
                set_seek_loading(true);
            }
        } else {
            LOG_WARNING("开始播放时间 ", config_.start_time, " 无效或超过视频时长，忽略");
        }
    }
    
    // 打开流组件（会创建解码器，但不启动线程）
    if (config_.enable_video && video_stream_ >= 0) {
        LOG_INFO("打开视频流...");
        auto open_video_begin = std::chrono::steady_clock::now();
        if (stream_component_open(video_stream_) < 0) {
            LOG_ERROR("无法打开视频流");
            emit_error(ERROR_NO_VIDEO_STREAM, "无法打开视频流");
            video_stream_opened_ = false;
            // 注意：继续尝试打开音频流，不直接返回错误
        } else {
            auto open_video_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - open_video_begin).count();
            LOG_INFO("视频流打开成功, 分辨率: ", media_info_.video_width, "x", media_info_.video_height);
            LOG_INFO("stream_component_open(video) 耗时: ", open_video_ms, " ms");
            video_stream_opened_ = true;
        }
    }
    
    if (config_.enable_audio && audio_stream_ >= 0) {
        LOG_INFO("打开音频流...");
        auto open_audio_begin = std::chrono::steady_clock::now();
        if (stream_component_open(audio_stream_) < 0) {
            LOG_ERROR("无法打开音频流");
            emit_error(ERROR_NO_AUDIO_STREAM, "无法打开音频流");
            audio_stream_opened_ = false;
            // 注意：继续播放，不直接返回错误（可能是纯视频文件）
        } else {
            auto open_audio_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - open_audio_begin).count();
            LOG_INFO("音频流打开成功, 采样率: ", media_info_.audio_sample_rate, " Hz");
            LOG_INFO("stream_component_open(audio) 耗时: ", open_audio_ms, " ms");
            audio_stream_opened_ = true;
        }
    }
    
    // ⚠️ 如果视频和音频都没有打开成功，则报错
    if (!video_decoder_ && !audio_decoder_) {
        LOG_ERROR("无法打开任何媒体流");
        emit_error(ERROR_FIND_STREAM_INFO_FAILED, "无法打开任何视频或音频流");
        set_state(PlayerState::Error);
        return -1;
    }
    
    // 启动读取线程（此时已经 seek 到正确位置）
    abort_request_ = false;
    read_thread_ = std::thread(&PlayerCore::read_thread, this);
    
    // ⚠️ 自动开始播放（恢复解码器）
    if (video_decoder_) {
        video_decoder_->resume();
    }
    if (audio_decoder_) {
        audio_decoder_->resume();
    }
    
    LOG_INFO("解码器已恢复，等待首帧就绪后进入 Playing");
    set_pipeline_state(PipelineState::Buffering);
    refresh_effective_playing_state();
    
    // ⚠️ 启动播放进度回调定时器线程
    if (position_changed_callback_) {
        progress_timer_thread_ = std::thread(&PlayerCore::progress_timer_thread, this);
        LOG_INFO("播放进度回调定时器线程已启动");
    }

    auto common_total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - common_begin).count();
    LOG_INFO("open_common_process 总耗时: ", common_total_ms, " ms");
    
    return 0;
}

void PlayerCore::close() {
    if (get_state() == PlayerState::Idle) {
        return;
    }
    
    LOG_INFO("正在关闭播放器...");
    
    // ⚠️ 第一步：停止标志（让所有线程知道要停止）
    abort_request_ = true;
    
    // ⚠️ 第二步：立即停止SDL音频设备（防止音频回调继续执行）
#ifndef NO_SDL
    if (audio_dev_) {
        SDL_PauseAudioDevice(audio_dev_, 1);  // 暂停音频
        LOG_INFO("音频设备已暂停");
    }
#endif
    
    // ⚠️ 第三步：中止所有队列（唤醒阻塞的线程）
    if (video_packet_queue_) video_packet_queue_->abort();
    if (audio_packet_queue_) audio_packet_queue_->abort();
    if (subtitle_packet_queue_) subtitle_packet_queue_->abort();
    
    if (video_queue_) video_queue_->abort();
    if (audio_queue_) audio_queue_->abort();
    
    // ⚠️ 第四步：等待读取线程结束
    if (read_thread_.joinable()) {
        LOG_INFO("等待读取线程结束...");
        read_thread_.join();
        LOG_INFO("读取线程已结束");
    }
    
    if (video_thread_.joinable()) {
        LOG_INFO("等待视频线程结束...");
        video_thread_.join();
        LOG_INFO("视频线程已结束");
    }
    
    if (audio_thread_.joinable()) {
        LOG_INFO("等待音频线程结束...");
        audio_thread_.join();
        LOG_INFO("音频线程已结束");
    }
    
    if (progress_timer_thread_.joinable()) {
        LOG_INFO("等待播放进度回调线程结束...");
        progress_timer_thread_.join();
        LOG_INFO("播放进度回调线程已结束");
    }
    
    if (video_refresh_thread_.joinable()) {
        LOG_INFO("等待视频刷新线程结束...");
        video_refresh_thread_.join();
        LOG_INFO("视频刷新线程已结束");
    }
    
    // ⚠️ 第五步：现在可以安全地关闭流组件
    if (video_stream_ >= 0) stream_component_close(video_stream_);
    if (audio_stream_ >= 0) stream_component_close(audio_stream_);
    
    // 关闭格式上下文
    if (format_ctx_) {
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
    }
    
    // 清理自定义数据源
    if (custom_io_) {
        LOG_INFO("关闭自定义数据源...");
        custom_io_->close();
        custom_io_.reset();
        LOG_INFO("自定义数据源已关闭");
    }
    secure_session_ = SecureHLSSession{};
    
    // 清理队列
    video_packet_queue_.reset();
    audio_packet_queue_.reset();
    subtitle_packet_queue_.reset();
    video_queue_.reset();
    audio_queue_.reset();
    
    // 清理音频缓冲（设备已在 stream_component_close 中关闭）
    if (audio_buf_) {
        av_free(audio_buf_);
        audio_buf_ = nullptr;
        audio_buf_size_ = 0;
        audio_buf_index_ = 0;
    }
    
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }

    // 关闭时确保结束 loading 回调，避免上层 UI 长时间停留在 loading。
    set_seek_loading(false);
    set_io_loading(false);
    set_starvation_loading(false);
    io_last_packet_us_.store(0, std::memory_order_release);
    set_play_when_ready_internal(false);
    first_video_frame_ready_.store(false, std::memory_order_release);
    first_audio_frame_ready_.store(false, std::memory_order_release);
    
    LOG_INFO("播放器已关闭");
    // 若由 stop() 触发 close，则外层应只感知一次 Stopped 回调；
    // 此处仅静默回落到 Idle，避免再触发一次 didChangeState(Idle)。
    if (get_state() == PlayerState::Stopped) {
        state_.store(PlayerState::Idle, std::memory_order_release);
        set_pipeline_state(PipelineState::Idle);
    } else {
        set_state(PlayerState::Idle);
    }
}

void PlayerCore::play() {
    PlayerState st = get_state();
    if (st == PlayerState::Idle || st == PlayerState::Stopped || st == PlayerState::Error) {
        return;
    }

    pause_request_ = false;
    set_play_when_ready_internal(true);
    LOG_INFO("恢复播放/设置 playWhenReady=true ...");

    // 恢复解码器
    if (video_decoder_) {
        video_decoder_->resume();
        LOG_INFO("视频解码器已恢复");
    }
    if (audio_decoder_) {
        audio_decoder_->resume();
        LOG_INFO("音频解码器已恢复");
    }

    // 恢复音频设备
#ifndef NO_SDL
    if (audio_dev_) {
        SDL_PauseAudioDevice(audio_dev_, 0);
    }
#endif

    update_pipeline_state_from_runtime();
    refresh_effective_playing_state();
    LOG_INFO("playWhenReady 已置为 true，等待流水线 Ready 后进入 Playing");
}

void PlayerCore::pause() {
    PlayerState st = get_state();
    if (st == PlayerState::Idle || st == PlayerState::Stopped || st == PlayerState::Error) {
        return;
    }

    pause_request_ = true;
    set_play_when_ready_internal(false);
    LOG_INFO("暂停播放（playWhenReady=false）...");

    // 暂停解码器（停止解码，节省 CPU）
    if (video_decoder_) {
        video_decoder_->pause();
        LOG_INFO("视频解码器已暂停");
    }
    if (audio_decoder_) {
        audio_decoder_->pause();
        LOG_INFO("音频解码器已暂停");
    }

    // 暂停音频设备
#ifndef NO_SDL
    if (audio_dev_) {
        SDL_PauseAudioDevice(audio_dev_, 1);
    }
#endif

    set_state(PlayerState::Paused);
    refresh_effective_playing_state();
    LOG_INFO("播放已暂停");
}

void PlayerCore::stop() {
    PlayerState current = get_state();
    if (current == PlayerState::Idle || current == PlayerState::Stopped) {
        return;
    }
    set_play_when_ready_internal(false);
    // 先对外发布停止态，再执行资源回收；close() 内会静默落回 Idle。
    set_state(PlayerState::Stopped);
    close();
}

void PlayerCore::seek(double pos) {
    LOG_INFO("========================================");
    LOG_INFO("请求跳转到位置: ", pos, " 秒");
    LOG_INFO("当前位置: ", get_position(), " 秒");
    LOG_INFO("视频时长: ", get_duration(), " 秒");
    LOG_INFO("========================================");
    
    if (pos < 0) {
        LOG_WARNING("⚠️ 跳转位置小于 0，修正为 0");
        pos = 0;
    }
    
    double duration = get_duration();
    if (duration > 0 && pos > duration) {
        LOG_WARNING("⚠️ 跳转位置超过视频时长，修正为时长值");
        pos = duration;
    }
    
    // ⚠️ 【关键修复】保存 seek 目标位置，在 seeking 期间直接返回此值
    seek_target_pos_.store(pos, std::memory_order_release);
    
    // ⚠️ 使用 store(Release) 确保 seeking_ 的修改对其他线程立即可见
    seeking_.store(true, std::memory_order_release);
    LOG_INFO("设置 seeking 标志（Release），暂停进度回调");
    set_seek_loading(true);
    set_pipeline_state(PipelineState::Buffering);
    if (video_stream_opened_) {
        first_video_frame_ready_.store(false, std::memory_order_release);
    }
    if (audio_stream_opened_) {
        first_audio_frame_ready_.store(false, std::memory_order_release);
    }
    
    seek_pos_ = pos;
    seek_request_ = true;
    LOG_INFO("跳转请求已设置，等待读取线程处理...");
}

double PlayerCore::get_position() const {
    return get_master_clock();
}

double PlayerCore::get_duration() const {
    if (format_ctx_) {
        return format_ctx_->duration / (double)AV_TIME_BASE;
    }
    return 0.0;
}

void PlayerCore::set_volume(int volume) {
    volume_ = std::max(0, std::min(100, volume));
}

void PlayerCore::read_thread() {
    LOG_INFO("读取线程已启动");
    
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        LOG_ERROR("无法分配 AVPacket");
        emit_error(PLAYER_ERROR_OUT_OF_MEMORY, "内存不足：无法分配 AVPacket");
        set_state(PlayerState::Error);
        return;
    }
    
    int packet_count = 0;
    int read_error_count = 0;
    int invalid_data_retry_count = 0;
    auto read_error_begin = std::chrono::steady_clock::time_point{};
    int soft_reconnect_attempt_count = 0;
    int64_t next_soft_reconnect_try_us = 0;
    bool pending_disconnect_error_after_drain = false;
    
    while (!abort_request_) {
        // 处理 seek
        if (seek_request_) {
            // ⚠️ 立即保存 seek 位置并清除请求标志，防止多次 seek 冲突
            double target_pos = seek_pos_;
            seek_request_ = false;  // ⚠️ 立即重置，允许新的 seek 请求排队
            
            // 注意：seeking_ 标志已在 seek() 方法中设置为 true，这里不需要重复设置
            
            int64_t seek_target = target_pos * AV_TIME_BASE;
            
            LOG_INFO("开始 Seek 到: ", target_pos, " 秒");
            if (av_seek_frame(format_ctx_, -1, seek_target, AVSEEK_FLAG_BACKWARD) < 0) {
                LOG_ERROR("Seek 失败");
                emit_error(PLAYER_ERROR_SEEK_FAILED, "跳转到指定位置失败");
                seeking_.store(false, std::memory_order_release);  // Seek 失败也要清除标志
                set_seek_loading(false);
            } else {
                LOG_INFO("Seek 成功");
                
                // ⚠️ 清除播放完成标志，允许重新播放
                decode_finished_.store(false, std::memory_order_release);
                playback_completed_notified_ = false;
                LOG_INFO("清除解码结束标志和播放完成通知标志");
                
                // ⚠️ 1. 清空数据包队列
                if (video_packet_queue_) video_packet_queue_->flush();
                if (audio_packet_queue_) audio_packet_queue_->flush();
                
                // ⚠️ 2. 清空帧队列（关键！）
                if (video_queue_) {
                    video_queue_->flush();
                    video_queue_->restart();  // 重启队列
                    LOG_INFO("视频帧队列已清空并重启");
                }
                if (audio_queue_) {
                    audio_queue_->flush();
                    audio_queue_->restart();  // 重启队列
                    LOG_INFO("音频帧队列已清空并重启");
                }
                
                // ⚠️ 3. 刷新解码器
                if (video_decoder_) {
                    video_decoder_->flush();
                    video_decoder_->resume();  // 确保解码器不在暂停状态
                    LOG_INFO("视频解码器已刷新并恢复");
                }
                if (audio_decoder_) {
                    audio_decoder_->flush();
                    audio_decoder_->resume();  // 确保解码器不在暂停状态
                    LOG_INFO("音频解码器已刷新并恢复");
                }
                
                // ⚠️ 4. 发送刷新包（告诉解码器已清空）
                if (video_stream_ >= 0 && video_stream_opened_ && video_packet_queue_) {
                    video_packet_queue_->put_nullpacket(video_stream_);
                }
                if (audio_stream_ >= 0 && audio_stream_opened_ && audio_packet_queue_) {
                    audio_packet_queue_->put_nullpacket(audio_stream_);
                }
                
                // ⚠️ 5. 【关键修复】立即更新时钟到目标位置，避免进度条跳动
                // 这样 get_master_clock() 会立即返回正确的 seek 位置
                if (audio_stream_ >= 0) {
                    audio_clock_.set_clock(target_pos, 0);
                    LOG_INFO("音频时钟已更新到 seek 位置: ", target_pos);
                }
                if (video_stream_ >= 0) {
                    video_clock_.set_clock(target_pos, 0);
                    LOG_INFO("视频时钟已更新到 seek 位置: ", target_pos);
                }
                external_clock_.set_clock(target_pos, 0);

#ifndef NO_SDL
                // 清理音频输出侧残留，避免 seek 后短暂回放到旧时钟位置。
                if (audio_dev_) {
                    SDL_LockAudioDevice(audio_dev_);
                }
                audio_buf_index_ = 0;
                audio_buf_size_ = 0;
                audio_current_pts_ = target_pos;
                audio_current_pts_drift_ = audio_current_pts_ - av_gettime_relative() / 1000000.0;
#ifdef HAS_SOUNDTOUCH
                if (soundtouch_) {
                    soundtouch_->clear();
                }
#endif
                if (audio_dev_) {
                    SDL_UnlockAudioDevice(audio_dev_);
                }
#endif
                
                // ⚠️ 6. 【关键修复】立即触发一次进度回调，通知 UI 新位置
                if (position_changed_callback_) {
                    position_changed_callback_(target_pos);
                    LOG_INFO("立即触发进度回调: ", target_pos);
                }
                
                // ⚠️ 注意：不在这里清除 seeking_ 标志
                // 等待音频解码线程解码第一帧后自动清除，确保时钟稳定
                
                LOG_INFO("Seek 清理完成，等待解码新帧后恢复进度回调");
            }
            // seek 之后重置读错误统计，避免旧的网络错误计数影响后续判定。
            read_error_count = 0;
            invalid_data_retry_count = 0;
            read_error_begin = std::chrono::steady_clock::time_point{};
        }

        // 重连已耗尽后：先允许现有缓冲继续播放，等帧队列耗尽再对外报错。
        if (pending_disconnect_error_after_drain) {
            const bool video_drained = (!video_stream_opened_ || !video_queue_ || video_queue_->nb_remaining() <= 0);
            const bool audio_drained = (!audio_stream_opened_ || !audio_queue_ || audio_queue_->nb_remaining() <= 0);
            if (video_drained && audio_drained) {
                LOG_ERROR("读取线程：重连失败且缓冲已耗尽，回调网络断开错误");
                emit_error(ERROR_NET_UNREACHABLE, "网络连接已断开，重连失败");
                set_io_loading(false);
                set_starvation_loading(false);
                set_state(PlayerState::Error);
                break;
            }
            // 仍有缓冲可播：不再继续拉流重试，等待消费。
            PLAYER_DELAY(50);
            continue;
        }
        
        // ⚠️ 检查队列总大小（参考 ffplay）
        // ⚠️ 减少缓冲区大小，从 15MB/30MB 降低到 5MB/10MB，防止内存泄漏
        // 只有当两个队列都满时才等待，避免阻塞单个队列
        int total_size = 0;
        bool video_full = false;
        bool audio_full = false;
        
        if (video_packet_queue_) {
            int vs = video_packet_queue_->get_size();
            total_size += vs;
            video_full = (vs > 5 * 1024 * 1024);  // 从 15MB 降低到 5MB
        }
        
        if (audio_packet_queue_) {
            int as = audio_packet_queue_->get_size();
            total_size += as;
            audio_full = (as > 5 * 1024 * 1024);  // 从 15MB 降低到 5MB
        }
        
        // ⚠️ 任一队列满或总大小超过 10MB 就等待（从 30MB 降低）
        if ((video_full || audio_full) || total_size > 10 * 1024 * 1024) {
            PLAYER_DELAY(10);
            continue;
        }
        
        // 读取包
        int ret = av_read_frame(format_ctx_, pkt);
        
        if (ret < 0) {
            int io_error = 0;
            if (format_ctx_->pb && format_ctx_->pb->error) {
                io_error = format_ctx_->pb->error;
            }
            const bool network_like_input = hxc_is_network_like_url(format_ctx_ ? format_ctx_->url : nullptr);
            const bool eof_signal = (ret == AVERROR_EOF) || (format_ctx_->pb && avio_feof(format_ctx_->pb));
            const bool has_io_error = (io_error != 0);

            // 网络流出现 TLS/IO 错误时，FFmpeg 可能同时给出 EOF 信号；此时不能按“正常读完文件”处理，
            // 否则会误进入“等待 seek/终止”分支，看起来像卡住。
            if (eof_signal && !(network_like_input && has_io_error)) {
                // 文件结束，发送结束包给解码器
                LOG_INFO("读取线程：文件读取结束");
                if (video_stream_ >= 0 && video_stream_opened_ && video_packet_queue_) {
                    video_packet_queue_->put_nullpacket(video_stream_);
                }
                if (audio_stream_ >= 0 && audio_stream_opened_ && audio_packet_queue_) {
                    audio_packet_queue_->put_nullpacket(audio_stream_);
                }
                
                // ⚠️ 不要退出线程！等待可能的 seek 操作或终止信号
                LOG_INFO("读取线程：进入等待状态，等待 seek 或终止信号");
                while (!abort_request_ && !seek_request_) {
                    PLAYER_DELAY(100);  // 等待 100ms 后再检查
                }
                
                // 如果是 abort 导致退出，则真正退出线程
                if (abort_request_) {
                    LOG_INFO("读取线程：收到终止信号，退出");
                    break;
                }
                
                // 如果是 seek，则继续循环（seek 会在循环开头处理）
                LOG_INFO("读取线程：检测到 seek 请求，继续读取");
                continue;
            }

            int effective_ret = (ret != 0) ? ret : io_error;
            if (ret == AVERROR_EOF && has_io_error) {
                effective_ret = io_error;
            }

            // watchdog 中断会返回 AVERROR_EXIT（Immediate exit requested）。
            // 若不是用户主动 stop/close，则按“可恢复超时”处理，并重置中断计时基线，
            // 避免下一轮 av_read_frame 立即再次被 interrupt_callback 打断而形成循环。
            bool interrupted_by_watchdog = (effective_ret == AVERROR_EXIT && !abort_request_.load());
            if (interrupted_by_watchdog) {
                effective_ret = AVERROR(ETIMEDOUT);
                io_last_packet_us_.store(av_gettime_relative(), std::memory_order_release);
            }

            bool retryable_error = hxc_is_retryable_network_error(effective_ret);
            bool non_retryable_error = (effective_ret == AVERROR(ENOENT) ||
                                        effective_ret == AVERROR(EACCES) ||
                                        effective_ret == AVERROR_PATCHWELCOME ||
                                        hxc_is_http_client_error(effective_ret));
            const bool is_invalid_data = (effective_ret == AVERROR_INVALIDDATA);
            const int MAX_INVALIDDATA_FAST_RETRY = 5;
            if (is_invalid_data && network_like_input) {
                invalid_data_retry_count++;
                if (invalid_data_retry_count <= MAX_INVALIDDATA_FAST_RETRY) {
                    // 网络流偶发 partial/corrupt chunk 时，先按可恢复处理，避免“同片偶发秒失败”。
                    non_retryable_error = false;
                    retryable_error = true;
                    LOG_WARNING("读取线程：网络流出现 INVALIDDATA，先快速重试（",
                                invalid_data_retry_count, "/", MAX_INVALIDDATA_FAST_RETRY, "）");
                } else {
                    // 超过快速重试阈值后继续走可恢复链路（软重连/超时兜底）。
                    non_retryable_error = false;
                    retryable_error = true;
                    LOG_WARNING("读取线程：INVALIDDATA 持续出现，转入软重连/超时恢复链路");
                }
            }

            if (non_retryable_error) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(effective_ret, errbuf, sizeof(errbuf));
                LOG_ERROR("读取线程：检测到不可恢复错误，停止读取。ret=", effective_ret, ", err=", errbuf);

                if (effective_ret == AVERROR_HTTP_NOT_FOUND || effective_ret == AVERROR(ENOENT)) {
                    emit_error(ERROR_HTTP_NOT_FOUND, "读取失败：资源不存在或已失效");
                } else if (effective_ret == AVERROR_HTTP_FORBIDDEN || effective_ret == AVERROR(EACCES)) {
                    emit_error(ERROR_HTTP_FORBIDDEN, "读取失败：访问被拒绝");
                } else if (effective_ret == AVERROR_HTTP_UNAUTHORIZED) {
                    emit_error(ERROR_HTTP_UNAUTHORIZED, "读取失败：鉴权失效");
                } else if (effective_ret == AVERROR_HTTP_BAD_REQUEST) {
                    emit_error(ERROR_HTTP_BAD_REQUEST, "读取失败：请求参数非法");
                } else if (effective_ret == AVERROR_INVALIDDATA) {
                    emit_error(ERROR_INPUT_INVALID_DATA,
                               "读取失败：" + hxc_build_hls_invaliddata_hint(format_ctx_ ? format_ctx_->url : "",
                                                                          io_error));
                } else {
                    emit_error(ERROR_READ_FRAME_FAILED, std::string("读取失败：") + errbuf);
                }
                set_io_loading(false);
                set_state(PlayerState::Error);
                break;
            }

            // 读取失败（通常是弱网/暂时断流），进入加载状态。
            set_io_loading(true);

            if (read_error_count == 0) {
                read_error_begin = std::chrono::steady_clock::now();
            }
            read_error_count++;

            // 周期性打印失败详情，便于线上定位具体错误码
            if (read_error_count == 1 || read_error_count % 20 == 0) {
                char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(effective_ret, errbuf, sizeof(errbuf));
                auto stall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - read_error_begin).count();
                LOG_WARNING("读取线程：av_read_frame 连续失败，ret=", effective_ret,
                            ", err=", errbuf,
                            ", 连续失败次数=", read_error_count,
                            ", 卡顿时长=", stall_ms, " ms",
                            ", 是否可重试=", retryable_error ? "是" : "未知按可重试");
            }

            // 网络类错误给更长恢复窗口；未知错误维持较短窗口，避免无限挂起。
            const int MAX_STALL_MS_RETRYABLE = 30000;      // 可重试错误最多等 30 秒
            const int MAX_STALL_MS_NON_CLASSIFIED = 15000; // 其它错误最多等 15 秒
            const int MAX_STALL_MS = retryable_error ? MAX_STALL_MS_RETRYABLE : MAX_STALL_MS_NON_CLASSIFIED;
            auto stall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - read_error_begin).count();

            // 软重连兜底：连续失败后最多重连 3 次，重试间隔递增（指数退避）。
            const int SOFT_RECONNECT_TRIGGER_MS = 5000;
            const int MAX_SOFT_RECONNECT_ATTEMPTS = 3;
            if (retryable_error && stall_ms >= SOFT_RECONNECT_TRIGGER_MS &&
                soft_reconnect_attempt_count < MAX_SOFT_RECONNECT_ATTEMPTS) {
                int64_t now_us = av_gettime_relative();
                if (next_soft_reconnect_try_us > 0 && now_us < next_soft_reconnect_try_us) {
                    int wait_ms = static_cast<int>((next_soft_reconnect_try_us - now_us) / 1000);
                    if (wait_ms > 0) {
                        PLAYER_DELAY(wait_ms);
                    }
                    continue;
                }

                double resume_pos = get_master_clock();
                if (!isnan(resume_pos) && resume_pos < 0.0) {
                    resume_pos = 0.0;
                }
                int64_t resume_ts = static_cast<int64_t>(std::max(0.0, resume_pos) * AV_TIME_BASE);
                LOG_WARNING("读取线程：触发软重连尝试(", soft_reconnect_attempt_count + 1, "/",
                            MAX_SOFT_RECONNECT_ATTEMPTS, "), resume_pos=", resume_pos, "s");

                if (format_ctx_) {
                    avformat_flush(format_ctx_);
                }
                int reconnect_ret = av_seek_frame(format_ctx_, -1, resume_ts, AVSEEK_FLAG_BACKWARD);
                if (reconnect_ret >= 0) {
                    if (video_packet_queue_) video_packet_queue_->flush();
                    if (audio_packet_queue_) audio_packet_queue_->flush();
                    if (video_queue_) { video_queue_->flush(); video_queue_->restart(); }
                    if (audio_queue_) { audio_queue_->flush(); audio_queue_->restart(); }
                    if (video_decoder_) video_decoder_->flush();
                    if (audio_decoder_) audio_decoder_->flush();
                    soft_reconnect_attempt_count = 0;
                    next_soft_reconnect_try_us = 0;
                    io_last_packet_us_.store(av_gettime_relative(), std::memory_order_release);
                    read_error_count = 0;
                    read_error_begin = std::chrono::steady_clock::time_point{};
                    LOG_INFO("读取线程：软重连成功，继续读取");
                    PLAYER_DELAY(50);
                    continue;
                } else {
                    LOG_WARNING("读取线程：软重连失败，ret=", reconnect_ret);
                    soft_reconnect_attempt_count++;
                    int reconnect_delay_ms = hxc_calc_retry_delay_ms(soft_reconnect_attempt_count - 1, 500, 4000);
                    next_soft_reconnect_try_us = av_gettime_relative() + static_cast<int64_t>(reconnect_delay_ms) * 1000;
                }
            }

            // 达到最大软重连次数后仍无法恢复：对外回调网络断开错误并结束播放。
            if (retryable_error && soft_reconnect_attempt_count >= MAX_SOFT_RECONNECT_ATTEMPTS &&
                stall_ms >= SOFT_RECONNECT_TRIGGER_MS) {
                LOG_ERROR("读取线程：软重连已达上限(", MAX_SOFT_RECONNECT_ATTEMPTS,
                          ")，标记等待缓冲耗尽后报网络断开");
                pending_disconnect_error_after_drain = true;
                continue;
            }

            if (stall_ms >= MAX_STALL_MS) {
                LOG_ERROR("读取线程：连续读取失败超时，退出。stall_ms=", stall_ms,
                          ", threshold=", MAX_STALL_MS);
                emit_error(ERROR_NET_CONNECTION_TIMEOUT,
                           "网络读取超时（连续读取失败），建议重试或切换线路");
                set_io_loading(false);
                set_state(PlayerState::Error);
                break;
            }

            // 指数退避，避免弱网场景忙等抢占 CPU；首次快速重试，后续逐步拉长。
            int retry_delay_ms = hxc_calc_retry_delay_ms(read_error_count - 1, 10, 800);
            PLAYER_DELAY(retry_delay_ms);
            continue;
        }

        // 读取恢复后，重置连续失败计数并结束加载状态
        if (read_error_count > 0) {
            auto stall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - read_error_begin).count();
            LOG_INFO("读取线程：读取恢复，连续失败次数=", read_error_count, ", 卡顿时长=", stall_ms, " ms");
            read_error_count = 0;
            invalid_data_retry_count = 0;
            read_error_begin = std::chrono::steady_clock::time_point{};
        }
        
        // ⚠️ 读取成功，结束加载状态
        set_io_loading(false);
        soft_reconnect_attempt_count = 0;
        next_soft_reconnect_try_us = 0;
        io_last_packet_us_.store(av_gettime_relative(), std::memory_order_release);
        if (!secure_session_.request_headers.empty()) {
            if (resource_delegate_.on_request_segment) {
                resource_delegate_.on_request_segment(format_ctx_ && format_ctx_->url ? format_ctx_->url : "");
            }
            if (resource_delegate_.on_store_segment && pkt->size > 0) {
                resource_delegate_.on_store_segment(format_ctx_ && format_ctx_->url ? format_ctx_->url : "", pkt->size);
            }
        }
        
        // 分发包到对应队列
        if (pkt->stream_index == video_stream_ && video_stream_opened_ && video_packet_queue_) {
            video_packet_queue_->put(pkt);
            packet_count++;
//            if (packet_count % 100 == 0) {
//                LOG_INFO("已读取 ", packet_count, " 个视频包");
//            }
        } else if (pkt->stream_index == audio_stream_ && audio_stream_opened_ && audio_packet_queue_) {
            audio_packet_queue_->put(pkt);
        } else {
            av_packet_unref(pkt);
        }
    }
    
    av_packet_free(&pkt);
}

int PlayerCore::stream_component_open(int stream_index) {
    if (stream_index < 0 || stream_index >= (int)format_ctx_->nb_streams) {
        return -1;
    }
    
    AVStream* stream = format_ctx_->streams[stream_index];
    AVCodecParameters* codecpar = stream->codecpar;
    
    // 查找解码器
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::ostringstream oss;
        oss << "找不到解码器 codecID:" << codecpar->codec_id;
        LOG_ERROR(oss.str());
        emit_error(ERROR_CODEC_NOT_FOUND, oss.str());
        return -1;
    }
    
    auto alloc_codec_context = [&]() -> AVCodecContext* {
        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        if (!ctx) {
            return nullptr;
        }
        if (avcodec_parameters_to_context(ctx, codecpar) < 0) {
            avcodec_free_context(&ctx);
            return nullptr;
        }
        return ctx;
    };

    // 创建解码器上下文
    AVCodecContext* codec_ctx = alloc_codec_context();
    if (!codec_ctx) {
        emit_error(ERROR_ALLOC_CONTEXT_FAILED, "创建解码器上下文失败");
        LOG_ERROR("创建或初始化解码器上下文失败 codecID:", codec->id, " codecName:", codec->long_name);
        return -1;
    }

    bool request_video_hw_decode =
        (codecpar->codec_type == AVMEDIA_TYPE_VIDEO && config_.decode_mode == DecodeMode::Hardware);
    bool hw_decode_enabled = false;
    if (request_video_hw_decode) {
        hw_decode_enabled = hxc_try_enable_hw_decode(codec_ctx, codec);
        LOG_INFO("视频解码模式：请求硬解，启用结果=", hw_decode_enabled ? 1 : 0);
    } else if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        LOG_INFO("视频解码模式：软解");
    }

    // 打开解码器（若硬解打开失败，自动回退软解）
    int open_ret = avcodec_open2(codec_ctx, codec, nullptr);
    if (open_ret < 0 && request_video_hw_decode && hw_decode_enabled) {
        LOG_WARNING("硬解打开失败，自动回退软解。ret=", open_ret);
        avcodec_free_context(&codec_ctx);
        codec_ctx = alloc_codec_context();
        if (!codec_ctx) {
            emit_error(ERROR_ALLOC_CONTEXT_FAILED, "硬解回退软解时创建解码器失败");
            LOG_ERROR("硬解回退软解失败：无法重新创建解码器上下文");
            return -1;
        }
        open_ret = avcodec_open2(codec_ctx, codec, nullptr);
        hw_decode_enabled = false;
    }
    if (open_ret < 0) {
        LOG_ERROR("打开解码器失败~ ret=", open_ret);
        emit_error(ERROR_CODEC_OPEN_FAILED, "打开解码器失败");
        avcodec_free_context(&codec_ctx);
        return -1;
    }

    if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_codec_ctx_ = codec_ctx;
        video_hw_decode_active_.store(hw_decode_enabled, std::memory_order_release);
        LOG_INFO("视频解码最终模式: ", hw_decode_enabled ? "硬解" : "软解");
        
        LOG_INFO("创建视频解码器...");
        video_decoder_ = std::make_unique<VideoDecoder>();
        video_decoder_->init(codec_ctx, video_packet_queue_.get());
        LOG_INFO("启动视频线程...");
        video_thread_ = std::thread(&PlayerCore::video_thread, this);
        LOG_INFO("视频线程已启动");
        video_stream_opened_ = true;
        
    } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        audio_codec_ctx_ = codec_ctx;
        
        // ⚠️ 先创建解码器，再启动音频设备！
        audio_decoder_ = std::make_unique<AudioDecoder>();
        audio_decoder_->init(codec_ctx, audio_packet_queue_.get());

        LOG_INFO("音频解码器已创建");

#ifdef HAS_SOUNDTOUCH
        // 初始化 SoundTouch（用于倍速播放）
        if (!soundtouch_) {
            soundtouch_ = new soundtouch::SoundTouch();
            
            soundtouch_->setSampleRate(codec_ctx->sample_rate);
            soundtouch_->setChannels(codec_ctx->ch_layout.nb_channels);
            
            // 优化设置
            soundtouch_->setSetting(SETTING_USE_QUICKSEEK, 0);
            soundtouch_->setSetting(SETTING_USE_AA_FILTER, 1);
            soundtouch_->setSetting(SETTING_SEQUENCE_MS, 40);
            soundtouch_->setSetting(SETTING_SEEKWINDOW_MS, 15);
            soundtouch_->setSetting(SETTING_OVERLAP_MS, 8);
            
            // 设置初始播放速率
            double current_rate = playback_rate_.load();
            soundtouch_->setTempo(current_rate);
            
            LOG_INFO("SoundTouch 初始化完成 (采样率: ", codec_ctx->sample_rate, 
                     ", 通道: ", codec_ctx->ch_layout.nb_channels, ")");
        }
#endif

#ifndef NO_SDL
        // 配置 SDL 音频（仅桌面平台）
        SDL_AudioSpec wanted_spec, spec;
        wanted_spec.freq = codec_ctx->sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = codec_ctx->ch_layout.nb_channels;
        wanted_spec.silence = 0;
        wanted_spec.samples = config_.audio_buffer_size;
        wanted_spec.callback = audio_callback;
        wanted_spec.userdata = this;
        
        audio_dev_ = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (audio_dev_ == 0) {
            LOG_ERROR("SDL_OpenAudioDevice 失败: ", SDL_GetError());
            emit_error(PLAYER_ERROR_AUDIO_DEVICE_OPEN_FAILED, std::string("打开音频设备失败: ") + SDL_GetError());
            audio_decoder_.reset();
            avcodec_free_context(&audio_codec_ctx_);
            audio_stream_opened_ = false;
            return -1;
        }
        
        LOG_INFO("音频设备已打开，准备启动...");
        
        // 启动音频设备（此时解码器已就绪）
        SDL_PauseAudioDevice(audio_dev_, 0);
        
        LOG_INFO("音频设备已打开");
#else
        LOG_INFO("iOS 平台，音频由上层渲染");
#endif
        
        // 启动音频解码线程（提前解码到队列）
        LOG_INFO("启动音频线程...");
        audio_thread_ = std::thread(&PlayerCore::audio_thread, this);
        LOG_INFO("音频线程已启动");
        audio_stream_opened_ = true;
        
#ifndef NO_SDL
        // 启动音频设备（此时解码器已就绪）
        SDL_PauseAudioDevice(audio_dev_, 0);
        LOG_INFO("音频设备已启动");
#endif
    }
    
    return 0;
}

void PlayerCore::stream_component_close(int stream_index) {
    if (stream_index < 0 || stream_index >= (int)format_ctx_->nb_streams) {
        return;
    }
    
    AVCodecParameters* codecpar = format_ctx_->streams[stream_index]->codecpar;
    
    if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        // 解码器不再有内部线程，直接释放即可
        if (video_decoder_) {
            video_decoder_.reset();
            LOG_INFO("视频解码器已释放");
        }
        if (video_codec_ctx_) {
            avcodec_free_context(&video_codec_ctx_);
            video_codec_ctx_ = nullptr;
        }
        video_stream_opened_ = false;
        video_stream_ = -1;
        video_hw_decode_active_.store(false, std::memory_order_release);
        
    } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
#ifndef NO_SDL
        // 先停止音频设备（防止回调继续访问解码器）
        if (audio_dev_) {
            SDL_PauseAudioDevice(audio_dev_, 1);  // 暂停
            SDL_CloseAudioDevice(audio_dev_);      // 关闭
            audio_dev_ = 0;
            LOG_INFO("音频设备已关闭");
        }
#endif
        
        // 现在可以安全地释放解码器
        if (audio_decoder_) {
            audio_decoder_.reset();
            LOG_INFO("音频解码器已释放");
        }
        
        if (audio_codec_ctx_) {
            avcodec_free_context(&audio_codec_ctx_);
            audio_codec_ctx_ = nullptr;
        }
        
        audio_stream_opened_ = false;
        audio_stream_ = -1;
    }
}

void PlayerCore::video_thread() {
    LOG_INFO("视频线程已启动");
    
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        LOG_ERROR("无法分配视频帧");
        emit_error(PLAYER_ERROR_OUT_OF_MEMORY, "内存不足：无法分配视频帧");
        set_state(PlayerState::Error);
        return;
    }
    
    double pts;
    double duration;
//    int frame_count = 0;
    int error_count = 0;  // 添加错误计数
    
    LOG_INFO("[视频线程] 进入主循环, video_decoder_ exists=", (video_decoder_ != nullptr), ", abort_request_=", abort_request_);
    
    while (!abort_request_) {
        // ⚠️ 检查暂停状态
        while (video_decoder_ && video_decoder_->should_pause() && !abort_request_) {
            if (error_count == 0) {
                LOG_INFO("视频解码器暂停中，等待恢复...");
                error_count++;  // 只打印一次
            }
            PLAYER_DELAY(10);
        }
        
        // 重置错误计数
        if (video_decoder_ && !video_decoder_->should_pause()) {
            if (error_count > 0) {
                LOG_INFO("视频解码器已恢复，开始解码...");
            }
            error_count = 0;
        }
        
        if (abort_request_) {
            break;
        }
        // 解码视频帧
        int ret = video_decoder_->decode_frame(frame);
        
        if (ret < 0) {
            // EAGAIN/中止类错误按可恢复处理，避免误报致命错误。
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EXIT || ret == -1) {
                if (abort_request_) {
                    break;
                }
                PLAYER_DELAY(10);
                continue;
            }

            error_count++;
            if (error_count <= 3 || error_count % 100 == 0) {
                LOG_ERROR("视频解码错误: ", ret, " (错误次数: ", error_count, ")");
            }

            // 连续错误达到阈值才上报致命，避免瞬时抖动触发错误风暴。
            const int MAX_CONSECUTIVE_FATAL_DECODE_ERRORS = 50;
            if (error_count >= MAX_CONSECUTIVE_FATAL_DECODE_ERRORS) {
                emit_error(ERROR_DECODE_FAILED, "视频解码持续失败，已达到错误阈值");
                set_state(PlayerState::Error);
                break;
            }
            PLAYER_DELAY(10);
            continue;  // 继续尝试
        } else if (ret == 0) {
            LOG_INFO("视频解码结束");
            // ⚠️ 设置解码结束标志
            decode_finished_.store(true, std::memory_order_release);
            LOG_INFO("设置解码结束标志");
            
            // ⚠️ 不要退出线程！等待可能的 seek 操作或终止信号
            while (!abort_request_) {
                // 检查是否有 seek 请求（队列被清空会触发 restart）
                if (video_queue_ && video_queue_->nb_remaining() > 0) {
                    // 队列有新数据，说明 seek 后开始解码了，清除结束标志
                    decode_finished_.store(false, std::memory_order_release);
                    LOG_INFO("检测到 seek 后新数据，清除解码结束标志，继续解码");
                    break;  // 跳出等待循环，继续解码
                }
                PLAYER_DELAY(100);  // 等待 100ms 后再检查
            }
            
            // 如果是 abort 导致退出，则真正退出线程
            if (abort_request_) {
                break;
            }
            
            // 否则继续解码循环
            continue;
        }
        
        // 计算帧时间戳
        pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(format_ctx_->streams[video_stream_]->time_base);
        if (seeking_.load(std::memory_order_acquire) && !isnan(pts)) {
            double target = seek_target_pos_.load(std::memory_order_acquire);
            const double backward_tolerance_sec = hxc_calc_seek_backward_tolerance_sec(media_info_);
            // seeking 期间，先丢弃明显早于目标的旧帧，避免把渲染时钟拉回。
            if (pts < (target - backward_tolerance_sec)) {
                av_frame_unref(frame);
                continue;
            }
            // 无音频流时，使用视频首帧判断 seek 完成，避免 seeking 状态卡住。
            bool use_video_as_seek_anchor = (audio_stream_ < 0 || !audio_stream_opened_);
            if (use_video_as_seek_anchor) {
                bool in_seek_window = (pts >= (target - backward_tolerance_sec) &&
                                       pts <= (target + kSeekAnchorForwardToleranceSec));
                if (in_seek_window) {
                    seeking_.store(false, std::memory_order_release);
                    set_seek_loading(false);
                    LOG_INFO("视频线程：检测到 seek 后有效首帧，结束 seeking。target=",
                             target, ", pts=", pts);
                }
            }
        }
        
        // 计算帧持续时间
        duration = av_q2d(format_ctx_->streams[video_stream_]->time_base);
        
        // 获取可写帧
        VideoFrame* vf = video_queue_->peek_writable();
        if (!vf) {
            LOG_WARNING("视频队列满，等待消费...");
            PLAYER_DELAY(10);
            continue;  // ⚠️ 不要 break，继续尝试
        }
        
        // 复制帧数据
        vf->frame = av_frame_clone(frame);
        if (!vf->frame) {
            LOG_ERROR("视频帧克隆失败（内存不足），丢弃当前帧");
            emit_error(ERROR_OUT_OF_MEMORY, "视频帧克隆失败（内存不足）");
            av_frame_unref(frame);
            PLAYER_DELAY(5);
            continue;
        }
        vf->pts = pts;
        vf->duration = duration;
        vf->width = frame->width;
        vf->height = frame->height;
        vf->serial = video_packet_queue_ ? video_packet_queue_->get_serial() : 0;
        
        // 推入队列
        video_queue_->push();
        if (!first_video_frame_ready_.exchange(true, std::memory_order_acq_rel)) {
            LOG_INFO("视频首帧已就绪");
            update_pipeline_state_from_runtime();
        }
        
        // 更新视频时钟
        update_video_pts(pts, video_packet_queue_->get_serial());
        
        // ⚠️ 音画同步控制（参考 ffplay）
        if (!isnan(pts)) {
            double diff = pts - get_master_clock();
            
            if (!isnan(diff)) {
                if (diff <= -0.1) {
                    // 视频太慢（落后音频超过 100ms），丢帧
//                    LOG_INFO("视频落后，丢帧 diff=", diff);
                    av_frame_unref(frame);
                    video_queue_->next();  // 从队列中移除
                    continue;
                } else if (diff > 0.01) {
                    // 视频太快（领先音频），等待
                    // diff 是媒体时间差，需要转换为物理等待时间（除以 playback_rate）
                    double rate = playback_rate_.load();
                    double physical_delay = diff / rate;
                    if (physical_delay > 0.1) {
                        physical_delay = 0.1;  // 最多等待 100ms
                    }
                    PLAYER_DELAY((int)(physical_delay * 1000));
                }
            }
        }
        
        av_frame_unref(frame);
        
        // 暂停控制
        while (pause_request_ && !abort_request_) {
            PLAYER_DELAY(10);
        }
    }
    
    av_frame_free(&frame);
}

// ⚠️ 新增：音频解码线程（提前解码到队列）
void PlayerCore::audio_thread() {
    LOG_INFO("音频线程已启动");
    
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        LOG_ERROR("无法分配音频帧");
        emit_error(PLAYER_ERROR_OUT_OF_MEMORY, "内存不足：无法分配音频帧");
        set_state(PlayerState::Error);
        return;
    }
    
    int frame_count = 0;
    int error_count = 0;
    
    LOG_INFO("[音频线程] 进入主循环, audio_decoder_ exists=", (audio_decoder_ != nullptr), ", audio_packet_queue_ exists=", (audio_packet_queue_ != nullptr));
    
    while (!abort_request_) {
        // 检查暂停状态
        if (audio_decoder_ && audio_decoder_->should_pause()) {
            if (error_count == 0) {
                LOG_INFO("音频解码器暂停中...");
            }
            while (audio_decoder_->should_pause() && !abort_request_) {
                PLAYER_DELAY(10);
            }
            LOG_INFO("音频解码器恢复");
            error_count = 0;
        }
        
        if (abort_request_) {
            LOG_INFO("[音频线程] 收到终止请求");
            break;
        }
        
        // 检查解码器和队列状态
        if (!audio_decoder_) {
            LOG_ERROR("[音频线程] audio_decoder_ 为空");
            break;
        }
        if (!audio_packet_queue_) {
            LOG_ERROR("[音频线程] audio_packet_queue_ 为空");
            break;
        }
        if (!audio_queue_) {
            LOG_ERROR("[音频线程] audio_queue_ 为空");
            break;
        }
        
        // 解码音频帧
        int ret = audio_decoder_->decode_frame(frame);
        
        if (ret < 0) {
            // EAGAIN 常见于队列暂时无包，不应直接当成错误上报。
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EXIT || ret == -1) {
                if (abort_request_) {
                    break;
                }
                PLAYER_DELAY(10);
                continue;  // 继续尝试
            }

            error_count++;
            if (error_count == 1 || error_count % 100 == 0) {
                LOG_ERROR("音频解码错误: ", ret, " (连续 ", error_count, " 次)");
            }

            const int MAX_CONSECUTIVE_FATAL_DECODE_ERRORS = 50;
            if (error_count >= MAX_CONSECUTIVE_FATAL_DECODE_ERRORS) {
                emit_error(ERROR_DECODE_FAILED, "音频解码持续失败，已达到错误阈值");
                set_state(PlayerState::Error);
                break;
            }

            PLAYER_DELAY(10);
            continue;  // 继续尝试
        } else if (ret == 0) {
            LOG_INFO("音频解码结束");
            
            // ⚠️ 不要退出线程！等待可能的 seek 操作或终止信号
            while (!abort_request_) {
                // 检查是否有 seek 请求（队列被清空会触发 restart）
                if (audio_queue_ && audio_queue_->nb_remaining() > 0) {
                    // 队列有新数据，说明 seek 后开始解码了
                    LOG_INFO("检测到 seek 后新数据（音频），继续解码");
                    break;  // 跳出等待循环，继续解码
                }
                PLAYER_DELAY(100);  // 等待 100ms 后再检查
            }
            
            // 如果是 abort 导致退出，则真正退出线程
            if (abort_request_) {
                break;
            }
            
            // 否则继续解码循环
            continue;
        }
        
        // 解码成功，重置错误计数
        error_count = 0;
        
        frame_count++;
//        if (frame_count == 1 || frame_count % 100 == 0) {
//            LOG_INFO("已解码 ", frame_count, " 个音频帧");
//        }
        
        // 防止 frame_count 溢出（每100万帧重置一次，约6小时）
        if (frame_count > 1000000) {
            frame_count = 0;
        }
        
        // 计算时间戳
        double pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : 
                     frame->pts * av_q2d(format_ctx_->streams[audio_stream_]->time_base);
        if (seeking_.load(std::memory_order_acquire) && !isnan(pts)) {
            double target = seek_target_pos_.load(std::memory_order_acquire);
            const double backward_tolerance_sec = hxc_calc_seek_backward_tolerance_sec(media_info_);
            // seeking 期间，先丢弃明显早于目标的旧帧，避免音频主时钟回退。
            if (pts < (target - backward_tolerance_sec)) {
                av_frame_unref(frame);
                continue;
            }
            // 默认 AudioMaster：以音频首个有效帧作为 seek 完成锚点。
            bool in_seek_window = (pts >= (target - backward_tolerance_sec) &&
                                   pts <= (target + kSeekAnchorForwardToleranceSec));
            if (in_seek_window) {
                seeking_.store(false, std::memory_order_release);
                set_seek_loading(false);
                LOG_INFO("音频线程：检测到 seek 后有效首帧，结束 seeking。target=",
                         target, ", pts=", pts);
            }
        }
        
        // ⚠️ 注意：不要在这里更新音频时钟！
        // 音频时钟应该在 audio_callback_impl 中，音频数据真正被消费时更新
        // 这里的 pts 是解码位置，不是播放位置
        
        // ⚠️ 触发缓冲进度回调（报告音频解码位置）
        if (!isnan(pts) && buffer_progress_callback_ && frame_count % 10 == 0) {
            buffer_progress_callback_(pts);
        }
        
        // 获取可写帧
        AudioFrame* af = audio_queue_->peek_writable();
        if (!af) {
            LOG_WARNING("音频队列满，等待消费...");
            PLAYER_DELAY(10);
            continue;  // ⚠️ 不要 break，继续尝试
        }
        
        // 复制帧数据
        af->frame = av_frame_clone(frame);
        if (!af->frame) {
            LOG_ERROR("音频帧克隆失败（内存不足），丢弃当前帧");
            emit_error(ERROR_OUT_OF_MEMORY, "音频帧克隆失败（内存不足）");
            av_frame_unref(frame);
            PLAYER_DELAY(5);
            continue;
        }
        af->pts = pts;
        af->serial = audio_packet_queue_ ? audio_packet_queue_->get_serial() : 0;
        
        // 推入队列
        audio_queue_->push();
        if (!first_audio_frame_ready_.exchange(true, std::memory_order_acq_rel)) {
            LOG_INFO("音频首帧已就绪");
            update_pipeline_state_from_runtime();
        }
        
        av_frame_unref(frame);
        
        // 暂停控制
        while (pause_request_ && !abort_request_) {
            PLAYER_DELAY(10);
        }
    }
    
    av_frame_free(&frame);
    LOG_INFO("音频线程已结束");
}

// ⚠️ 播放进度回调定时器线程
// 定期读取 master clock 并触发回调，报告真实播放进度
void PlayerCore::progress_timer_thread() {
    LOG_INFO("播放进度回调定时器线程已启动");
    const int CALLBACK_INTERVAL_MS = 200;  // 每 200ms 触发一次回调（每秒 5 次）
    double last_position = -1.0;
    int position_unchanged_count = 0;  // ⚠️ 位置未变化的次数
    int starvation_ticks = 0;          // 低水位+时钟停滞持续计数

    while (!abort_request_) {
        // 检查播放器状态
        PlayerState current_state = get_state();
        if (current_state == PlayerState::Playing || current_state == PlayerState::Paused) {
            double current_position;
            
            // ⚠️ 【关键修复】如果正在 seek，直接返回 seek 目标位置
            if (seeking_.load(std::memory_order_acquire)) {
                current_position = seek_target_pos_.load(std::memory_order_acquire);
                position_unchanged_count = 0;  // 重置计数
            } else {
                // 获取真实播放进度（从 master clock）
                current_position = get_master_clock();
            }
            
            // 只在播放进度有效时触发回调
            if (!isnan(current_position) && current_position >= 0.0) {
                // 播放完成后停止进度回调，避免 UI 收到超出时长的位置值
                if (!playback_completed_notified_) {
                    if (position_changed_callback_) {
                        position_changed_callback_(current_position);
                    }
                }
                
                // ⚠️ 检测位置是否变化
                if (current_state == PlayerState::Playing) {  // 只在播放状态下检测
                    if (fabs(current_position - last_position) < 0.01) {  // 位置几乎没变化
                        position_unchanged_count++;
                    } else {
                        position_unchanged_count = 0;  // 位置有变化，重置计数
                    }
                }
                
                // 低水位 loading：播放中若队列枯竭且进度停滞，快速进入 loading（不用等读失败超时）。
                bool queue_starved = false;
                if (current_state == PlayerState::Playing && !seeking_.load(std::memory_order_acquire)) {
                    bool video_starved = video_stream_opened_ && video_queue_ && video_queue_->nb_remaining() <= 0;
                    bool audio_starved = audio_stream_opened_ && audio_queue_ && audio_queue_->nb_remaining() <= 0;
                    bool progress_stalled = !isnan(last_position) && fabs(current_position - last_position) < 0.01;
                    queue_starved = (video_starved || audio_starved) && progress_stalled;
                }
                if (queue_starved) {
                    starvation_ticks++;
                } else {
                    starvation_ticks = 0;
                }
                set_starvation_loading(starvation_ticks >= 3);
                last_position = current_position;
                
                // ⚠️ 【关键修复】判断播放完成的多个条件：
                // 条件1：解码已结束 + 播放位置接近时长
                // 条件2：播放中 + 位置连续 3 次（600ms）不变 + 位置接近时长
                if (!playback_completed_notified_) {
                    double duration = get_duration();
                    bool near_end = duration > 0 && (duration - current_position) < 1.0;  // 最后 1 秒内
                    
                    bool condition1 = decode_finished_.load(std::memory_order_acquire) && near_end;
                    bool condition2 = (current_state == PlayerState::Playing) && 
                                     (position_unchanged_count >= 3) && 
                                     near_end;
                    
                    if (condition1 || condition2) {
                        playback_completed_notified_ = true;  // 避免重复通知
                        set_pipeline_state(PipelineState::Ended);
                        refresh_effective_playing_state();
                        LOG_INFO("检测到播放完成: 当前位置=", current_position, 
                                ", 时长=", duration, 
                                ", decode_finished=", decode_finished_.load(),
                                ", position_unchanged_count=", position_unchanged_count);
                        if (playback_completed_callback_) {
                            playback_completed_callback_();
                        }
                    }
                }
            }
        }
        if (!(current_state == PlayerState::Playing || current_state == PlayerState::Paused)) {
            starvation_ticks = 0;
            set_starvation_loading(false);
        }

        // 等待一段时间后再次检查
        PLAYER_DELAY(CALLBACK_INTERVAL_MS);
    }

    LOG_INFO("播放进度回调定时器线程已结束");
}

#ifndef NO_SDL
void PlayerCore::audio_callback(void* userdata, uint8_t* stream, int len) {
    PlayerCore* player = static_cast<PlayerCore*>(userdata);
    player->audio_callback_impl(stream, len);
}

// ⚠️ 重写：只从队列取数据，不解码
void PlayerCore::audio_callback_impl(uint8_t* stream, int len) {
    static int callback_count = 0;
    callback_count++;
    
    SDL_memset(stream, 0, len);
    
    if (!audio_queue_ || !audio_codec_ctx_) {
        if (callback_count % 100 == 0) {
            LOG_WARNING("audio_callback: 队列或上下文为空");
        }
        return;
    }
    
    if (abort_request_) {
        return;
    }
    
    while (len > 0) {
        // 如果缓冲区为空，从队列获取新帧
        if (audio_buf_index_ >= audio_buf_size_) {
            // 检查队列中是否有帧
            int queue_size = audio_queue_->size();
            if (queue_size <= 0) {
                // 队列空，输出静音
                if (callback_count % 100 == 0) {
                    LOG_WARNING("audio_callback: 队列为空，输出静音");
                }
                return;
            }
            
            if (callback_count % 100 == 0) {
                LOG_INFO("audio_callback: 队列大小 = ", queue_size);
            }
            
            AudioFrame* af = audio_queue_->peek_readable();
            if (!af || !af->frame) {
                return;
            }
            
            AVFrame* frame = af->frame;
            
            // 重采样音频
            if (!swr_ctx_) {
                AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
                av_channel_layout_copy(&out_ch_layout, &audio_codec_ctx_->ch_layout);

                swr_alloc_set_opts2(&swr_ctx_,
                    &out_ch_layout,
                    AV_SAMPLE_FMT_S16,
                    audio_codec_ctx_->sample_rate,
                    &audio_codec_ctx_->ch_layout,
                    audio_codec_ctx_->sample_fmt,
                    audio_codec_ctx_->sample_rate,
                    0, nullptr);

                if (!swr_ctx_) {
                    LOG_ERROR("重采样上下文分配失败");
                    emit_error(ERROR_ALLOC_CONTEXT_FAILED, "音频重采样上下文分配失败");
                    audio_queue_->next();
                    return;
                }
                
                if (swr_init(swr_ctx_) < 0) {
                    LOG_ERROR("重采样上下文初始化失败");
                    emit_error(ERROR_ALLOC_CONTEXT_FAILED, "音频重采样上下文初始化失败");
                    swr_free(&swr_ctx_);
                    swr_ctx_ = nullptr;
                    audio_queue_->next();
                    return;
                }
            }
            
            // 计算输出缓冲区大小
            int out_samples = static_cast<int>(av_rescale_rnd(
                swr_get_delay(swr_ctx_, audio_codec_ctx_->sample_rate) + frame->nb_samples,
                audio_codec_ctx_->sample_rate,
                audio_codec_ctx_->sample_rate,
                AV_ROUND_UP));

            int out_size = av_samples_get_buffer_size(
                nullptr,
                audio_codec_ctx_->ch_layout.nb_channels,
                out_samples,
                AV_SAMPLE_FMT_S16,
                1);

            if (out_size < 0) {
                LOG_ERROR("计算音频缓冲区大小失败: ", out_size);
                emit_error(ERROR_OUT_OF_MEMORY, "计算音频缓冲区大小失败");
                audio_queue_->next();
                return;
            }

            // 分配输出缓冲区
            if (!audio_buf_ || audio_buf_size_ < (unsigned int)out_size) {
                av_free(audio_buf_);
                audio_buf_ = (uint8_t*)av_malloc(out_size);
                if (!audio_buf_) {
                    LOG_ERROR("音频缓冲区内存分配失败: ", out_size, " 字节");
                    emit_error(ERROR_OUT_OF_MEMORY, "音频缓冲区内存分配失败");
                    audio_buf_size_ = 0;
                    audio_queue_->next();
                    return;
                }
                audio_buf_size_ = out_size;
            }

            // 执行重采样
            uint8_t* out[] = {audio_buf_};
            int samples = swr_convert(swr_ctx_, out, out_samples,
                                     (const uint8_t**)frame->data, frame->nb_samples);

            if (samples < 0) {
                LOG_ERROR("音频重采样失败: ", samples);
                emit_error(ERROR_DECODE_FAILED, "音频重采样失败");
                audio_queue_->next();
                return;
            }
            
            // ⚠️ 保存当前帧的 PTS，用于后续时钟更新
            audio_current_pts_ = af->pts;
            audio_current_pts_drift_ = audio_current_pts_ - av_gettime_relative() / 1000000.0;
            
            // 消费队列中的帧
            audio_queue_->next();

#ifdef HAS_SOUNDTOUCH
            // 使用 SoundTouch 处理倍速播放
            double current_rate = playback_rate_.load();
            if (soundtouch_ && current_rate != 1.0) {
                int channels = audio_codec_ctx_->ch_layout.nb_channels;
                
                // 验证 channels 有效性
                if (channels <= 0 || channels > 8) {
                    LOG_ERROR("无效的音频通道数: ", channels);
                    emit_error(ERROR_DECODE_FAILED, "音频通道数无效 (channels: " + std::to_string(channels) + ")");
                    // 降级：使用原始音频
                    audio_buf_size_ = samples * std::max(2, channels) * sizeof(int16_t);
                    audio_buf_index_ = 0;
                } else {
                    // 转换为 float
                    std::vector<float> float_input(samples * channels);
                    int16_t* s16_src = (int16_t*)audio_buf_;
                    for (int i = 0; i < samples * channels; i++) {
                        float_input[i] = (float)s16_src[i] / 32768.0f;
                    }
                    
                    try {
                        // 送入样本到 SoundTouch
                        soundtouch_->putSamples(float_input.data(), samples);
                        
                        // 获取可用样本数
                        uint32_t available = soundtouch_->numSamples();
                        
                        // 计算期望输出样本数（基于输入和速率）
                        uint32_t expected_output = (uint32_t)(samples / current_rate);
                        
                        // 只有当积累的样本 >= 期望输出的 80% 时才取出
                        if (available >= expected_output * 0.8) {
                            uint32_t samples_to_get = std::max(available, expected_output);
                            std::vector<float> output_buffer(samples_to_get * channels);
                            uint32_t received = soundtouch_->receiveSamples(output_buffer.data(), samples_to_get);
                            
                            if (received > 0) {
                                // 转换回 S16
                                int out_size = received * channels * sizeof(int16_t);
                                if (audio_buf_size_ < (unsigned int)out_size) {
                                    av_free(audio_buf_);
                                    audio_buf_ = (uint8_t*)av_malloc(out_size * 2);
                                    if (!audio_buf_) {
                                        LOG_ERROR("SoundTouch 输出缓冲区内存分配失败: ", out_size * 2, " 字节");
                                        emit_error(ERROR_OUT_OF_MEMORY, "SoundTouch 输出缓冲区内存分配失败");
                                        // 降级：输出静音
                                        audio_buf_size_ = 0;
                                        audio_buf_index_ = 0;
                                        return;
                                    }
                                }
                                
                                int16_t* s16_dst = (int16_t*)audio_buf_;
                                for (size_t i = 0; i < received * channels; i++) {
                                    float sample = output_buffer[i];
                                    if (sample > 1.0f) sample = 1.0f;
                                    if (sample < -1.0f) sample = -1.0f;
                                    s16_dst[i] = (int16_t)(sample * 32767.0f);
                                }
                                
                                audio_buf_size_ = out_size;
                                audio_buf_index_ = 0;
                            } else {
                                // 降级：使用原始音频
                                audio_buf_size_ = samples * channels * sizeof(int16_t);
                                audio_buf_index_ = 0;
                            }
                        } else {
                            // SoundTouch 积累中，输出静音
                            audio_buf_size_ = 0;
                            audio_buf_index_ = 0;
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("SoundTouch 处理异常: ", e.what());
                        emit_error(ERROR_DECODE_FAILED, std::string("SoundTouch 音频处理失败: ") + e.what());
                        // 降级：使用原始音频
                        audio_buf_size_ = samples * channels * sizeof(int16_t);
                        audio_buf_index_ = 0;
                    } catch (...) {
                        LOG_ERROR("SoundTouch 处理未知异常");
                        emit_error(ERROR_DECODE_FAILED, "SoundTouch 音频处理发生未知错误");
                        // 降级：使用原始音频
                        audio_buf_size_ = samples * channels * sizeof(int16_t);
                        audio_buf_index_ = 0;
                    }
                }
            } else {
                // 1.0x 速度或 SoundTouch 未启用，使用原始音频
                audio_buf_size_ = samples * audio_codec_ctx_->ch_layout.nb_channels * sizeof(int16_t);
                audio_buf_index_ = 0;
            }
#else
            // 非 macOS/Windows 平台，直接使用原始音频
            audio_buf_size_ = samples * audio_codec_ctx_->ch_layout.nb_channels * sizeof(int16_t);
            audio_buf_index_ = 0;
#endif
        }
        
        // 从缓冲区复制数据到输出流
        int len1 = audio_buf_size_ - audio_buf_index_;
        if (len1 > len) {
            len1 = len;
        }
        
        // 应用音量
        if (volume_ != 100 && volume_ > 0) {
            int16_t* samples = (int16_t*)(audio_buf_ + audio_buf_index_);
            int sample_count = len1 / sizeof(int16_t);
            for (int i = 0; i < sample_count; i++) {
                samples[i] = (int16_t)(samples[i] * volume_ / 100);
            }
        }
        
        SDL_MixAudioFormat(stream, audio_buf_ + audio_buf_index_, AUDIO_S16SYS, len1, SDL_MIX_MAXVOLUME);
        
        // ⚠️ 【关键修复】在这里更新音频时钟！
        // 只有在数据真正被复制到输出流后，才更新时钟
        if (!isnan(audio_current_pts_)) {
            int channels = audio_codec_ctx_->ch_layout.nb_channels;
            if (channels > 0) {
                // 计算实际消费的样本数
                int consumed_samples = len1 / (channels * sizeof(int16_t));
                
                // 计算消费时长（媒体时间）
                double consumed_duration = (double)consumed_samples / audio_codec_ctx_->sample_rate;
                
                // ⚠️ 关键修复：SoundTouch 输出的每个样本对应 playback_rate 倍的媒体内容时间
                // 例如：2.0x 时，1024 个原始样本压缩为 512 个输出样本
                //      这 512 个输出样本对应的媒体时间 = 512 * 2.0 / sample_rate（不是 512 / sample_rate）
                // 因此时钟必须乘以 playback_rate，才能让主时钟以 2x 的速度推进
                {
                    double current_rate = playback_rate_.load();
                    if (current_rate != 1.0) {
                        consumed_duration *= current_rate;
                    }
                }
                
                // 更新 PTS（逐步累加）
                audio_current_pts_ += consumed_duration;
                audio_current_pts_drift_ = audio_current_pts_ - av_gettime_relative() / 1000000.0;
                
                // 更新音频时钟
                update_audio_pts(audio_current_pts_, 0);
            }
        }
        
        len -= len1;
        stream += len1;
        audio_buf_index_ += len1;
    }
}
#endif // NO_SDL

double PlayerCore::get_master_clock() const {
    switch (config_.sync_mode) {
        case SyncMode::AudioMaster:
            return audio_clock_.get_clock();
        case SyncMode::VideoMaster:
            return video_clock_.get_clock();
        case SyncMode::ExternalClock:
            return external_clock_.get_clock();
        default:
            return 0.0;
    }
}

void PlayerCore::update_video_pts(double pts, int serial) {
    video_clock_.set_clock(pts, serial);
}

void PlayerCore::update_audio_pts(double pts, int serial) {
    audio_clock_.set_clock(pts, serial);
}

bool PlayerCore::has_first_renderable_frame_ready() const {
    if (video_stream_opened_) {
        return first_video_frame_ready_.load(std::memory_order_acquire);
    }
    if (audio_stream_opened_) {
        return first_audio_frame_ready_.load(std::memory_order_acquire);
    }
    return false;
}

bool PlayerCore::is_playing() const {
    return play_when_ready_.load(std::memory_order_acquire) &&
           pipeline_state_.load(std::memory_order_acquire) == PipelineState::Ready;
}

void PlayerCore::set_state(PlayerState state) {
    PlayerState old_state = state_.load(std::memory_order_acquire);
    if (old_state != state) {
        state_.store(state, std::memory_order_release);
        if (state_changed_callback_) {
            state_changed_callback_(state);
        }
    }
    if (state == PlayerState::Opening) {
        set_pipeline_state(PipelineState::Preparing);
    } else if (state == PlayerState::Idle || state == PlayerState::Stopped) {
        set_pipeline_state(PipelineState::Idle);
    } else if (state == PlayerState::Error) {
        set_pipeline_state(PipelineState::Error);
    } else {
        refresh_effective_playing_state();
    }
}

void PlayerCore::set_pipeline_state(PipelineState state) {
    PipelineState old_state = pipeline_state_.load(std::memory_order_acquire);
    if (old_state != state) {
        pipeline_state_.store(state, std::memory_order_release);
        if (pipeline_state_changed_callback_) {
            pipeline_state_changed_callback_(state);
        }
    }
    refresh_effective_playing_state();
}

void PlayerCore::set_play_when_ready_internal(bool play_when_ready) {
    bool old = play_when_ready_.exchange(play_when_ready, std::memory_order_acq_rel);
    if (old != play_when_ready) {
        refresh_effective_playing_state();
    }
}

void PlayerCore::refresh_effective_playing_state() {
    const bool playing_now = is_playing();
    const bool old_playing = effective_is_playing_.exchange(playing_now, std::memory_order_acq_rel);
    if (old_playing != playing_now && playing_changed_callback_) {
        playing_changed_callback_(playing_now);
    }

    PlayerState st = get_state();
    if (st == PlayerState::Idle || st == PlayerState::Stopped || st == PlayerState::Error) {
        return;
    }

    if (playing_now && st != PlayerState::Playing) {
        set_state(PlayerState::Playing);
        return;
    }
    PipelineState ps = pipeline_state_.load(std::memory_order_acquire);
    if (!playing_now && st == PlayerState::Playing &&
        (!play_when_ready_.load(std::memory_order_acquire) || ps == PipelineState::Ended)) {
        set_state(PlayerState::Paused);
    }
}

void PlayerCore::update_pipeline_state_from_runtime() {
    PlayerState st = get_state();
    if (st == PlayerState::Idle || st == PlayerState::Stopped) {
        set_pipeline_state(PipelineState::Idle);
        return;
    }
    if (st == PlayerState::Error) {
        set_pipeline_state(PipelineState::Error);
        return;
    }
    if (pipeline_state_.load(std::memory_order_acquire) == PipelineState::Ended) {
        return;
    }

    const bool loading =
        seek_loading_.load(std::memory_order_acquire) ||
        io_loading_.load(std::memory_order_acquire);
    if (loading) {
        set_pipeline_state(PipelineState::Buffering);
        return;
    }

    if (has_first_renderable_frame_ready()) {
        set_pipeline_state(PipelineState::Ready);
    } else if (st == PlayerState::Opening) {
        set_pipeline_state(PipelineState::Preparing);
    } else {
        set_pipeline_state(PipelineState::Buffering);
    }
}

void PlayerCore::set_seek_loading(bool is_loading) {
    seek_loading_.store(is_loading, std::memory_order_release);
    refresh_loading_state();
}

void PlayerCore::set_io_loading(bool is_loading) {
    io_loading_.store(is_loading, std::memory_order_release);
    refresh_loading_state();
}

void PlayerCore::set_starvation_loading(bool is_loading) {
    starvation_loading_.store(is_loading, std::memory_order_release);
    refresh_loading_state();
}

void PlayerCore::refresh_loading_state() {
    const bool merged_loading =
        seek_loading_.load(std::memory_order_acquire) ||
        io_loading_.load(std::memory_order_acquire) ||
        starvation_loading_.load(std::memory_order_acquire);
    const bool prev = loading_notified_.exchange(merged_loading, std::memory_order_acq_rel);
    update_pipeline_state_from_runtime();
    if (prev != merged_loading && loading_callback_) {
        loading_callback_(merged_loading);
    }
}

void PlayerCore::emit_error(int error_code, const std::string& error_msg) {
    if (error_callback_) {
        error_callback_(error_code, error_msg);
    }
}

// 设置播放速率（倍速播放）
void PlayerCore::set_playback_rate(double rate) {
    // 限制范围在 0.5 ~ 3.0
    if (rate < 0.5) rate = 0.5;
    if (rate > 3.0) rate = 3.0;

    playback_rate_.store(rate, std::memory_order_release);

#ifdef HAS_SOUNDTOUCH
    // 更新 SoundTouch 的速度设置
    if (soundtouch_) {
        // 先清空缓冲区，避免旧数据影响新速度
        soundtouch_->clear();

        // 设置新速度
        soundtouch_->setTempo(rate);
    }
#endif
}

// ========== 视频渲染相关实现 ==========

void PlayerCore::set_render_window(void* window_handle) {
    render_window_ = window_handle;
    
    // 如果是自动渲染模式，初始化 SDL 渲染器
    if (render_mode_ == RenderMode::Auto && window_handle != nullptr) {
        init_sdl_renderer();
        
        // 启动视频刷新线程
        if (!video_refresh_thread_.joinable()) {
            video_refresh_thread_ = std::thread(&PlayerCore::video_refresh_thread_func, this);
            LOG_INFO("视频刷新线程已启动");
        }
    }
}

void PlayerCore::init_sdl_renderer() {
#ifndef NO_SDL
    // ⚠️ 不要在这里调用 cleanup，保留最后一帧数据
    
    if (!render_window_) {
        LOG_WARNING("未设置渲染窗口，无法初始化渲染器");
        return;
    }
    
    // 清理旧的渲染器和纹理（但保留帧数据）
    if (sdl_texture_) {
        SDL_DestroyTexture(sdl_texture_);
        sdl_texture_ = nullptr;
    }
    
    if (sdl_renderer_) {
        SDL_DestroyRenderer(sdl_renderer_);
        sdl_renderer_ = nullptr;
    }
    
    // 初始化 SDL 视频子系统（如果还未初始化）
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        if (SDL_InitSubSystem(SDL_INIT_VIDEO) < 0) {
            LOG_ERROR("SDL 视频子系统初始化失败: ", SDL_GetError());
            return;
        }
        LOG_INFO("SDL 视频子系统初始化成功");
    }
    
    // 从窗口句柄创建 SDL 窗口
#ifdef _WIN32
    // Windows: HWND
    SDL_Window* sdl_window = SDL_CreateWindowFrom(render_window_);
#elif defined(__APPLE__)
    // macOS: NSView*
    SDL_Window* sdl_window = SDL_CreateWindowFrom(render_window_);
#else
    // Linux: X11 Window
    SDL_Window* sdl_window = SDL_CreateWindowFrom(render_window_);
#endif
    
    if (!sdl_window) {
        LOG_ERROR("创建 SDL 窗口失败: ", SDL_GetError());
        return;
    }
    
    // 创建渲染器
    sdl_renderer_ = SDL_CreateRenderer(sdl_window, -1, 
                                        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl_renderer_) {
        LOG_ERROR("创建 SDL 渲染器失败: ", SDL_GetError());
        SDL_DestroyWindow(sdl_window);
        return;
    }
    
    LOG_INFO("SDL 渲染器初始化成功");
    
    // ⚠️ 如果有缓存的最后一帧，立即渲染它以避免黑屏
    if (has_last_frame_ && last_frame_width_ > 0 && last_frame_height_ > 0) {
        LOG_INFO("重建渲染器后，立即渲染缓存的最后一帧");
        
        // 重新创建纹理
        sdl_texture_ = SDL_CreateTexture(
            sdl_renderer_,
            SDL_PIXELFORMAT_IYUV,
            SDL_TEXTUREACCESS_STREAMING,
            last_frame_width_, last_frame_height_
        );
        
        if (sdl_texture_) {
            texture_width_ = last_frame_width_;
            texture_height_ = last_frame_height_;
            
            // 更新纹理
            int y_size = last_frame_width_ * last_frame_height_;
            int uv_size = (last_frame_width_ / 2) * (last_frame_height_ / 2);
            
            SDL_UpdateYUVTexture(
                sdl_texture_,
                nullptr,
                last_frame_y_.data(), last_frame_width_,
                last_frame_u_.data(), last_frame_width_ / 2,
                last_frame_v_.data(), last_frame_width_ / 2
            );
            
            // 渲染到屏幕
            SDL_RenderClear(sdl_renderer_);
            SDL_RenderCopy(sdl_renderer_, sdl_texture_, nullptr, nullptr);
            SDL_RenderPresent(sdl_renderer_);
            
            LOG_INFO("最后一帧已渲染，避免黑屏");
        }
    }
#endif
}

void PlayerCore::cleanup_sdl_renderer() {
#ifndef NO_SDL
    if (sdl_texture_) {
        SDL_DestroyTexture(sdl_texture_);
        sdl_texture_ = nullptr;
    }
    
    if (sdl_renderer_) {
        SDL_Window* window = SDL_RenderGetWindow(sdl_renderer_);
        SDL_DestroyRenderer(sdl_renderer_);
        sdl_renderer_ = nullptr;
        
        // 注意：不销毁窗口，因为窗口是外部管理的
        // SDL_DestroyWindow(window);
    }
#endif
}

void PlayerCore::render_video_frame(const VideoFrame& frame) {
#ifndef NO_SDL
    if (!sdl_renderer_) {
        static bool logged = false;
        if (!logged) {
            LOG_ERROR("render_video_frame: sdl_renderer_ 为空，无法渲染");
            logged = true;
        }
        return;
    }
    
    // 首次渲染时输出日志
    static bool first_render = true;
    if (first_render) {
        LOG_INFO("开始渲染第一帧: ", frame.frame->width, "x", frame.frame->height);
        first_render = false;
    }
    
    // ⚠️ 缓存当前帧数据（用于 resize 时避免黑屏）
    int width = frame.frame->width;
    int height = frame.frame->height;
    int y_size = width * height;
    int uv_size = (width / 2) * (height / 2);
    
    if (last_frame_width_ != width || last_frame_height_ != height) {
        last_frame_y_.resize(y_size);
        last_frame_u_.resize(uv_size);
        last_frame_v_.resize(uv_size);
        last_frame_width_ = width;
        last_frame_height_ = height;
    }
    
    // 复制帧数据
    memcpy(last_frame_y_.data(), frame.frame->data[0], y_size);
    memcpy(last_frame_u_.data(), frame.frame->data[1], uv_size);
    memcpy(last_frame_v_.data(), frame.frame->data[2], uv_size);
    has_last_frame_ = true;
    
    // 创建或更新纹理
    if (!sdl_texture_ || 
        frame.frame->width != texture_width_ ||
        frame.frame->height != texture_height_) {
        
        if (sdl_texture_) {
            SDL_DestroyTexture(sdl_texture_);
        }
        
        sdl_texture_ = SDL_CreateTexture(
            sdl_renderer_,
            SDL_PIXELFORMAT_IYUV,  // YUV420P
            SDL_TEXTUREACCESS_STREAMING,
            frame.frame->width,
            frame.frame->height
        );
        
        if (!sdl_texture_) {
            LOG_ERROR("创建 SDL 纹理失败: ", SDL_GetError());
            return;
        }
        
        texture_width_ = frame.frame->width;
        texture_height_ = frame.frame->height;
        LOG_INFO("SDL 纹理已创建: ", frame.frame->width, "x", frame.frame->height);
    }
    
    // 更新纹理数据
    SDL_UpdateYUVTexture(
        sdl_texture_,
        nullptr,
        frame.frame->data[0], frame.frame->linesize[0],  // Y
        frame.frame->data[1], frame.frame->linesize[1],  // U
        frame.frame->data[2], frame.frame->linesize[2]   // V
    );
    
    // 获取窗口大小
    int window_width, window_height;
    SDL_RenderGetLogicalSize(sdl_renderer_, &window_width, &window_height);
    if (window_width == 0 || window_height == 0) {
        SDL_GetRendererOutputSize(sdl_renderer_, &window_width, &window_height);
    }
    
    // 计算显示矩形（考虑宽高比）
    SDL_Rect dst_rect;
    int video_width = frame.frame->width;
    int video_height = frame.frame->height;
    
    if (aspect_ratio_mode_ == AspectRatioMode::Fit) {
        // Fit 模式：保持宽高比，可能有黑边
        float video_aspect = (float)video_width / video_height;
        float window_aspect = (float)window_width / window_height;
        
        if (video_aspect > window_aspect) {
            // 视频更宽，以宽度为准
            dst_rect.w = window_width;
            dst_rect.h = (int)(window_width / video_aspect);
            dst_rect.x = 0;
            dst_rect.y = (window_height - dst_rect.h) / 2;
        } else {
            // 视频更高，以高度为准
            dst_rect.h = window_height;
            dst_rect.w = (int)(window_height * video_aspect);
            dst_rect.x = (window_width - dst_rect.w) / 2;
            dst_rect.y = 0;
        }
    } else {
        // Fill 模式：填充整个窗口，可能裁剪
        dst_rect.x = 0;
        dst_rect.y = 0;
        dst_rect.w = window_width;
        dst_rect.h = window_height;
    }
    
    // 渲染
    SDL_RenderClear(sdl_renderer_);
    SDL_RenderCopy(sdl_renderer_, sdl_texture_, nullptr, &dst_rect);
    SDL_RenderPresent(sdl_renderer_);
#endif
}

int PlayerCore::refresh_video() {
    if (!render_window_) {
        return -2;  // 未设置窗口
    }
    
    if (render_mode_ == RenderMode::Manual) {
        // 手动模式下，用户需要自己调用 get_video_frame
        return -1;
    }
    
    // 自动渲染模式
    if (!video_queue_ || video_queue_->nb_remaining() == 0) {
        static int log_count = 0;
        if (++log_count % 1000 == 0) {
            LOG_DEBUG("refresh_video: 视频队列为空 (video_queue_=", (void*)video_queue_.get(), 
                     ", remaining=", (video_queue_ ? video_queue_->nb_remaining() : -1), ")");
        }
        return -1;  // 无帧可用
    }
    
    // 获取当前帧
    VideoFrame* frame = video_queue_->peek_readable();
    if (!frame) {
        return -1;
    }
    
    // 检查是否到达显示时间
    double delay = frame->pts - get_master_clock();
    if (delay > 0) {
        return -1;  // 还没到显示时间
    }
    
    // 渲染帧
    render_video_frame(*frame);
    
    // 移到下一帧
    video_queue_->next();
    
    return 0;
}

void PlayerCore::video_refresh_thread_func() {
    LOG_INFO("视频刷新线程已启动");
    
    int frame_count = 0;
    int no_frame_count = 0;
    
    while (!abort_request_) {
        // 如果不是自动渲染模式，退出
        if (render_mode_ != RenderMode::Auto) {
            PLAYER_DELAY(100);
            continue;
        }
        
        // 如果没有渲染窗口，退出
        if (!render_window_) {
            PLAYER_DELAY(100);
            continue;
        }
        
        // 尝试刷新视频
        int ret = refresh_video();
        
        // 调试：定期输出状态
        if (ret == 0) {
            frame_count++;
            no_frame_count = 0;
            if (frame_count % 100 == 0) {
                LOG_INFO("视频刷新线程：已渲染 ", frame_count, " 帧");
            }
        } else {
            no_frame_count++;
            if (no_frame_count == 1000) {  // 5 秒没有帧
                LOG_WARNING("视频刷新线程：5秒内无帧可渲染 (ret=", ret, ")");
                no_frame_count = 0;  // 重置计数器
            }
        }
        
        // 根据返回值决定等待时间
        if (ret == 0) {
            // 成功渲染一帧，短暂等待
            PLAYER_DELAY(1);  // 1ms，让出 CPU
        } else {
            // 没有帧可渲染或还没到显示时间，稍长等待
            PLAYER_DELAY(5);  // 5ms
        }
    }
    
    LOG_INFO("视频刷新线程已退出，共渲染 ", frame_count, " 帧");
}

} // namespace hxcplayer
