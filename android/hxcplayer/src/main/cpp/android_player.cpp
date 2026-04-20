#include "android_player.h"
#include "hxc_player_core_c_bridge.h"
#include <android/log.h>
#include <cstring>
#include <chrono>
#include <thread>

#define LOG_TAG "AndroidPlayer"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

AndroidPlayer::AndroidPlayer()
    : player_core_(nullptr)
    , native_window_(nullptr)
    , surface_width_(0)
    , surface_height_(0)
    , aspect_ratio_mode_(0) // FIT
    , surface_configured_(false)
    , sws_ctx_(nullptr)
    , rgb_buffer_(nullptr)
    , rgb_buffer_size_(0)
    , last_video_width_(0)
    , last_video_height_(0)
    , last_target_width_(0)
    , last_target_height_(0)
    , render_running_(false)
    , engineObject_(nullptr)
    , engineEngine_(nullptr)
    , outputMixObject_(nullptr)
    , playerObject_(nullptr)
    , playItf_(nullptr)
    , bufferQueueItf_(nullptr)
    , volumeItf_(nullptr)
    , audio_initialized_(false)
    , audio_sample_rate_(0)
    , audio_channels_(0)
    , audio_buffer_size_(0)
{
    LOGD("AndroidPlayer created");
    
    // 创建核心播放器
    player_core_ = player_core_create();
    if (!player_core_) {
        LOGE("Failed to create player core");
    }
    
    // ⚠️ 不在这里初始化音频，等到打开视频后根据实际音频参数初始化
    LOGI("Audio will be initialized after opening video");
}

AndroidPlayer::~AndroidPlayer() {
    LOGD("AndroidPlayer destroyed");
    
    // 停止渲染线程
    render_running_ = false;
    if (render_thread_.joinable()) {
        render_thread_.join();
    }
    
    // 销毁音频输出
    destroyAudioOutput();
    
    // 释放 swscale 资源
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (rgb_buffer_) {
        av_free(rgb_buffer_);
        rgb_buffer_ = nullptr;
    }
    
    // 释放窗口
    if (native_window_) {
        ANativeWindow_release(native_window_);
        native_window_ = nullptr;
    }
    
    // 销毁核心播放器
    if (player_core_) {
        player_core_destroy(player_core_);
        player_core_ = nullptr;
    }
}

void AndroidPlayer::setSurface(ANativeWindow* window) {
    std::lock_guard<std::mutex> lock(window_mutex_);
    
    if (native_window_) {
        ANativeWindow_release(native_window_);
        surface_configured_ = false;
    }
    
    native_window_ = window;
    surface_generation_.fetch_add(1, std::memory_order_relaxed);
    
    if (native_window_) {
        ANativeWindow_acquire(native_window_);
        LOGD("Surface set: %p", native_window_);
        surface_configured_ = false;  // 新 Surface，需要重新配置
        
        // 启动渲染线程
        if (!render_running_) {
            render_running_ = true;
            render_thread_ = std::thread(&AndroidPlayer::renderLoop, this);
        }
    } else {
        LOGD("Surface cleared");
        
        // 停止渲染线程
        render_running_ = false;
        if (render_thread_.joinable()) {
            render_thread_.join();
        }
    }
}

void AndroidPlayer::updateSurfaceSize(int width, int height) {
    std::lock_guard<std::mutex> lock(window_mutex_);
    
    // 尺寸变化时，标记需要重新配置 Surface
    if (surface_width_ != width || surface_height_ != height) {
        LOGI("📐 Surface size changing: %dx%d -> %dx%d", 
             surface_width_, surface_height_, width, height);
        
        surface_width_ = width;
        surface_height_ = height;
        surface_configured_ = false;  // 标记需要重新配置
        surface_generation_.fetch_add(1, std::memory_order_relaxed);
        
        LOGI("✅ Surface size updated (will reconfigure on next frame)");
    }
}

bool AndroidPlayer::openURL(const char* url) {
    return openURL(url, 0.0);  // 默认从头开始
}

bool AndroidPlayer::openURL(const char* url, double start_position) {
    if (!player_core_) {
        LOGE("Player core not initialized");
        return false;
    }
    
    LOGI("Opening URL: %s, start_position: %.2f", url, start_position);
    
    int result;
    if (start_position > 0.0) {
        // 使用带起始位置的打开方法
        result = player_core_open_with_start_position(player_core_, url, start_position);
    } else {
        result = player_core_open(player_core_, url);
    }
    
    if (result == 0) {
        LOGI("URL opened successfully");
        
        // 🎵 获取音频参数并动态初始化音频输出
        int sample_rate = player_core_get_audio_sample_rate(player_core_);
        int channels = player_core_get_audio_channels(player_core_);
        
        LOGI("Audio info: sample_rate=%d, channels=%d", sample_rate, channels);
        
        if (sample_rate > 0 && channels > 0 && !audio_initialized_) {
            if (initAudioOutput(sample_rate, channels)) {
                audio_initialized_ = true;
                LOGI("Audio output initialized with actual parameters");
            } else {
                LOGE("Failed to initialize audio output");
            }
        } else if (audio_initialized_) {
            LOGI("Audio already initialized");
        } else {
            LOGW("No audio stream or invalid audio parameters");
        }
        
        // ⏸️ 打开后自动暂停，等待用户点击播放
        player_core_pause(player_core_);
        LOGI("Video opened and paused, waiting for user to play");
        
        return true;
    } else {
        LOGE("Failed to open URL: %s (error code: %d)", url, result);
        LOGE("Possible reasons:");
        LOGE("  1. Network connection failed");
        LOGE("  2. URL format invalid");
        LOGE("  3. Server returned error (403/404/etc)");
        LOGE("  4. Unsupported media format");
        LOGE("  5. SSL/TLS certificate issue");
        return false;
    }
}

bool AndroidPlayer::openWithCustomHTTP(const char* url, int timeout_ms, int max_retries, bool encrypted_file) {
    if (!player_core_) {
        LOGE("Player core not initialized");
        return false;
    }

    LOGI("Opening with custom HTTP: %s (encrypted_file=%d)", url, encrypted_file ? 1 : 0);

    PlayerDataSourceConfigC config;
    config.timeout_ms = timeout_ms;
    config.max_retries = max_retries;
    config.cache_size = 2 * 1024 * 1024;  // 2MB
    config.avio_buffer_size = 64 * 1024;  // 64KB
    config.encrypted_file = encrypted_file ? 1 : 0;

    int result = player_core_open_with_mode(player_core_, url, PLAYER_DATA_SOURCE_MODE_CUSTOM_HTTP, &config, 0.0);

    if (result == 0) {
        LOGI("Custom HTTP opened successfully");

        int sample_rate = player_core_get_audio_sample_rate(player_core_);
        int channels = player_core_get_audio_channels(player_core_);

        if (sample_rate > 0 && channels > 0 && !audio_initialized_) {
            if (initAudioOutput(sample_rate, channels)) {
                LOGI("Audio initialized: %d Hz, %d channels", sample_rate, channels);
            }
        }

        player_core_pause(player_core_);
        return true;
    } else {
        LOGE("Failed to open with custom HTTP: %d", result);
        return false;
    }
}

bool AndroidPlayer::openWithCustomFile(const char* path, size_t avio_buffer_size, bool encrypted_file) {
    if (!player_core_) {
        LOGE("Player core not initialized");
        return false;
    }

    LOGI("Opening with custom file: %s (avio_buffer_size=%zu, encrypted_file=%d)",
         path, static_cast<size_t>(avio_buffer_size), encrypted_file ? 1 : 0);

    PlayerDataSourceConfigC config;
    config.timeout_ms = 30000;
    config.max_retries = 3;
    config.cache_size = 2 * 1024 * 1024;
    config.avio_buffer_size = avio_buffer_size;
    config.encrypted_file = encrypted_file ? 1 : 0;

    int result = player_core_open_with_mode(player_core_, path, PLAYER_DATA_SOURCE_MODE_CUSTOM_FILE, &config, 0.0);

    if (result == 0) {
        LOGI("Custom file opened successfully");

        int sample_rate = player_core_get_audio_sample_rate(player_core_);
        int channels = player_core_get_audio_channels(player_core_);

        if (sample_rate > 0 && channels > 0 && !audio_initialized_) {
            if (initAudioOutput(sample_rate, channels)) {
                LOGI("Audio initialized: %d Hz, %d channels", sample_rate, channels);
            }
        }

        player_core_pause(player_core_);
        return true;
    } else {
        LOGE("Failed to open with custom file: %d", result);
        return false;
    }
}

void AndroidPlayer::play() {
    if (!player_core_) return;
    
    LOGI("▶️ Play called");
    player_core_play(player_core_);
    
    // 启动音频播放
    if (playItf_) {
        LOGD("Starting audio playback");
        SLresult result = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PLAYING);
        if (result != SL_RESULT_SUCCESS) {
            LOGE("Failed to set play state to PLAYING: %d", result);
        } else {
            LOGD("Audio play state set to PLAYING");
        }
    } else {
        LOGD("No audio interface (audio disabled)");
    }
    LOGI("▶️ Play completed");
}

void AndroidPlayer::pause() {
    if (!player_core_) return;
    
    LOGD("Pause");
    player_core_pause(player_core_);
    
    // 暂停音频
    if (playItf_) {
        SLresult result = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PAUSED);
        if (result != SL_RESULT_SUCCESS) {
            LOGE("Failed to set play state to PAUSED: %d", result);
        }
    }
}

void AndroidPlayer::stop() {
    if (!player_core_) return;
    
    LOGD("Stop");
    player_core_stop(player_core_);
    
    // 停止音频
    if (playItf_) {
        SLresult result = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_STOPPED);
        if (result != SL_RESULT_SUCCESS) {
            LOGE("Failed to set play state to STOPPED: %d", result);
        }
    }
}

void AndroidPlayer::seekTo(double position) {
    if (!player_core_) return;
    
    LOGD("Seek to: %f", position);
    player_core_seek(player_core_, position);
}

void AndroidPlayer::setPlaybackRate(float rate) {
    if (!player_core_) return;
    
    LOGD("Set playback rate: %f", rate);
    player_core_set_playback_rate(player_core_, rate);
}

void AndroidPlayer::setVolume(float volume) {
    if (!player_core_) return;
    
    LOGD("Set volume: %f (core only)", volume);
    // 只在 Core 层控制音量（OpenSL ES VolumeItf 会触发 AppOps 崩溃）
    player_core_set_volume(player_core_, volume);
}

void AndroidPlayer::setAspectRatioMode(int mode) {
    aspect_ratio_mode_ = mode;
    LOGI("✅ Set aspect ratio mode: %d (%s)", mode, mode == 0 ? "FIT" : "FILL");
    
    if (player_core_) {
        player_core_set_aspect_ratio_mode(player_core_, 
            mode == 1 ? ASPECT_RATIO_FILL : ASPECT_RATIO_FIT);
    }
    
    // 重置 swscale context 以应用新的宽高比模式
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
        last_video_width_ = 0;
        last_video_height_ = 0;
        last_target_width_ = 0;
        last_target_height_ = 0;
        LOGI("🔄 Reset swscale context to apply new aspect ratio mode");
    }
}

double AndroidPlayer::getDuration() const {
    if (!player_core_) return 0.0;
    return player_core_get_duration(player_core_);
}

double AndroidPlayer::getPosition() const {
    if (!player_core_) return 0.0;
    return player_core_get_position(player_core_);
}

int AndroidPlayer::getState() const {
    if (!player_core_) return 0; // IDLE
    return (int)player_core_get_state(player_core_);
}

// ========== 视频渲染 ==========

void AndroidPlayer::renderLoop() {
    // LOGD("Render loop started");  // 关闭
    
    int frame_count = 0;
    int empty_count = 0;

    while (render_running_) {
        if (!player_core_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        // 获取视频帧
        VideoFrameDataC frame_data;
        int result = player_core_get_video_frame(player_core_, &frame_data);
        if (result == 0) {
            frame_count++;
            // if (frame_count % 30 == 0) {  // 关闭
            //     LOGI("Rendered %d frames", frame_count);
            // }
            
            // ✅ 直接渲染帧，renderFrame 内部会处理锁
            renderFrame(frame_data.y_data, frame_data.u_data, frame_data.v_data,
                       frame_data.y_linesize, frame_data.u_linesize, frame_data.v_linesize,
                       frame_data.width, frame_data.height);

            // 通知核心播放器帧已消费
            player_core_consume_video_frame(player_core_);
            empty_count = 0;  // 重置空帧计数
        } else {
            // 没有新帧，等待更长时间以减少 CPU 占用
            empty_count++;
            // if (empty_count == 1 || empty_count % 100 == 0) {
            //     LOGD("No video frame available, count=%d, result=%d", empty_count, result);
            // }
            // ⚠️ 增加等待时间，从 5ms 增加到 10ms，减少 CPU 占用和内存压力
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // LOGD("Render loop stopped, total frames rendered: %d", frame_count);  // 关闭
}

void AndroidPlayer::renderFrame(void* y_data, void* u_data, void* v_data,
                                int y_linesize, int u_linesize, int v_linesize,
                                int width, int height) {
    // 🔒 获取 window 和 surface 尺寸
    ANativeWindow* window = nullptr;
    int surface_w = 0;
    int surface_h = 0;
    bool needs_configure = false;
    uint64_t captured_gen = 0;
    
    {
        std::lock_guard<std::mutex> lock(window_mutex_);
        if (!native_window_ || !y_data) {
            return;
        }
        
        window = native_window_;
        ANativeWindow_acquire(window);
        surface_w = surface_width_;
        surface_h = surface_height_;
        needs_configure = !surface_configured_;
        captured_gen = surface_generation_.load(std::memory_order_relaxed);
    }
    
    // 📐 根据 aspect_ratio_mode_ 计算实际渲染尺寸
    // FIT (0): 保持视频宽高比，适配到 Surface 内（可能有黑边）
    // FILL (1): 等比例填充整个 Surface（保持宽高比，裁剪超出部分）
    int target_width = surface_w;
    int target_height = surface_h;
    
    static int last_logged_mode = -1;  // 记录上次日志的模式
    
    if (aspect_ratio_mode_ == 0) {
        // FIT 模式：计算适配尺寸（保持宽高比，不裁剪）
        float video_aspect = (float)width / (float)height;
        float surface_aspect = (float)surface_w / (float)surface_h;
        
        if (video_aspect > surface_aspect) {
            // 视频更宽，以宽度为准
            target_width = surface_w;
            target_height = (int)(surface_w / video_aspect);
        } else {
            // 视频更高，以高度为准
            target_height = surface_h;
            target_width = (int)(surface_h * video_aspect);
        }
        
        // 模式切换时输出日志
        if (last_logged_mode != 0) {
            LOGI("📐 FIT mode: video=%dx%d (%.2f), surface=%dx%d (%.2f) -> target=%dx%d", 
                 width, height, video_aspect, surface_w, surface_h, surface_aspect, target_width, target_height);
            last_logged_mode = 0;
        }
    } else {
        // FILL 模式：等比例填充整个 Surface（保持宽高比，裁剪超出部分）
        float video_aspect = (float)width / (float)height;
        float surface_aspect = (float)surface_w / (float)surface_h;
        
        if (video_aspect > surface_aspect) {
            // 视频更宽，以高度为准（左右会被裁剪）
            target_height = surface_h;
            target_width = (int)(surface_h * video_aspect);
        } else {
            // 视频更高，以宽度为准（上下会被裁剪）
            target_width = surface_w;
            target_height = (int)(surface_w / video_aspect);
        }
        
        // 模式切换时输出日志
        if (last_logged_mode != 1) {
            LOGI("📐 FILL mode: video=%dx%d (%.2f), surface=%dx%d (%.2f) -> target=%dx%d (crop)", 
                 width, height, video_aspect, surface_w, surface_h, surface_aspect, target_width, target_height);
            last_logged_mode = 1;
        }
    }
    
    // 确保尺寸有效
    if (target_width <= 0) target_width = surface_w;
    if (target_height <= 0) target_height = surface_h;
    
    // ✅ 只在首次或尺寸变化时配置 Surface
    // 注意：swap/分屏切换时 setSurface/updateSurfaceSize 可能与渲染线程并发发生，
    // 若这里使用旧尺寸去 setBuffersGeometry，会导致后续 lock 的 buffer stride 与预期不一致并闪烁。
    if (needs_configure && surface_w > 0 && surface_h > 0) {
        // 在真正配置前再读一次“最新尺寸 + generation”，避免使用旧 snapshot 配置
        int cfg_w = surface_w;
        int cfg_h = surface_h;
        uint64_t cfg_gen = captured_gen;
        {
            std::lock_guard<std::mutex> lock(window_mutex_);
            // 若窗口已被替换/清空，直接丢帧，等待下一帧拿到正确 window
            if (!native_window_ || native_window_ != window) {
                ANativeWindow_release(window);
                return;
            }
            cfg_w = surface_width_;
            cfg_h = surface_height_;
            cfg_gen = surface_generation_.load(std::memory_order_relaxed);
        }

        // generation 变化说明期间发生过 setSurface/resize：丢弃本帧，避免配置错
        if (cfg_gen != captured_gen) {
            ANativeWindow_release(window);
            return;
        }

        LOGI("🔧 Configuring surface: %dx%d", cfg_w, cfg_h);
        int result = ANativeWindow_setBuffersGeometry(
            window, cfg_w, cfg_h, WINDOW_FORMAT_RGB_565);
        
        if (result != 0) {
            LOGE("❌ Failed to configure surface: %d", result);
            ANativeWindow_release(window);
            return;
        }
        
        std::lock_guard<std::mutex> lock(window_mutex_);
        // 只有 generation 未变化时才认为配置成功
        if (surface_generation_.load(std::memory_order_relaxed) == captured_gen) {
            surface_configured_ = true;
        }
        LOGI("✅ Surface configured successfully");
    }
    
    // 🎨 锁定 Surface 缓冲区
    ANativeWindow_Buffer buffer;
    int lock_result = ANativeWindow_lock(window, &buffer, nullptr);
    if (lock_result != 0) {
        LOGE("❌ Failed to lock window buffer: %d", lock_result);
        ANativeWindow_release(window);
        return;
    }
    
    if (!buffer.bits) {
        LOGE("❌ Buffer bits is null");
        ANativeWindow_unlockAndPost(window);
        ANativeWindow_release(window);
        return;
    }
    
    // 🖼️ 使用 FFmpeg swscale 进行硬件加速的 YUV->RGB 转换和缩放
    // 类似 iOS 的 AVSampleBufferDisplayLayer，但在软件层面实现
    
    // 检查是否需要重新创建 swscale context（只在视频尺寸或目标尺寸变化时）
    if (!sws_ctx_ || 
        width != last_video_width_ || height != last_video_height_ ||
        target_width != last_target_width_ || target_height != last_target_height_) {
        
        if (sws_ctx_) {
            sws_freeContext(sws_ctx_);
        }
        
        // 创建 swscale context (YUV420P -> RGB565)
        sws_ctx_ = sws_getContext(
            width, height, AV_PIX_FMT_YUV420P,
            target_width, target_height, AV_PIX_FMT_RGB565LE,
            SWS_FAST_BILINEAR,  // 快速双线性插值
            nullptr, nullptr, nullptr
        );
        
        if (!sws_ctx_) {
            LOGE("Failed to create swscale context");
            ANativeWindow_unlockAndPost(window);
            ANativeWindow_release(window);
            return;
        }
        
        // 记录当前尺寸
        last_video_width_ = width;
        last_video_height_ = height;
        last_target_width_ = target_width;
        last_target_height_ = target_height;
        
        LOGI("✅ Created swscale context: %dx%d -> %dx%d", width, height, target_width, target_height);
    }
    
    // 准备源数据 (YUV420P)
    const uint8_t* src_data[4] = { 
        (uint8_t*)y_data, 
        (uint8_t*)u_data, 
        (uint8_t*)v_data,
        nullptr
    };
    int src_linesize[4] = { y_linesize, u_linesize, v_linesize, 0 };
    
    // 分配临时缓冲区（避免直接写入 ANativeWindow buffer）
    int temp_stride = target_width * 2;  // RGB565 = 2 bytes per pixel
    int temp_size = temp_stride * target_height;
    
    // ⚠️ 限制最大分配大小，防止内存泄漏
    const int MAX_RGB_BUFFER_SIZE = 1920 * 1080 * 2;  // 限制为 1080p (约 4MB)
    if (temp_size > MAX_RGB_BUFFER_SIZE) {
        LOGE("❌ Frame too large: %dx%d (max: 1920x1080)", target_width, target_height);
        ANativeWindow_unlockAndPost(window);
        ANativeWindow_release(window);
        return;
    }
    
    if (!rgb_buffer_ || rgb_buffer_size_ < temp_size) {
        if (rgb_buffer_) {
            av_free(rgb_buffer_);
        }
        rgb_buffer_ = (uint8_t*)av_malloc(temp_size);
        if (!rgb_buffer_) {
            LOGE("❌ Failed to allocate RGB buffer: %d bytes", temp_size);
            ANativeWindow_unlockAndPost(window);
            ANativeWindow_release(window);
            return;
        }
        rgb_buffer_size_ = temp_size;
        LOGI("📦 Allocated RGB buffer: %d bytes (%dx%d)", temp_size, target_width, target_height);
    }
    
    // 准备目标数据（渲染到临时缓冲区）
    uint8_t* dst_data[4] = { rgb_buffer_, nullptr, nullptr, nullptr };
    int dst_linesize[4] = { temp_stride, 0, 0, 0 };
    
    // 执行转换和缩放
    auto start_time = std::chrono::high_resolution_clock::now();
    int result = sws_scale(sws_ctx_, src_data, src_linesize, 0, height, dst_data, dst_linesize);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    if (result != target_height) {
        LOGE("❌ swscale failed: expected %d lines, got %d", target_height, result);
        ANativeWindow_unlockAndPost(window);
        ANativeWindow_release(window);
        return;
    }
    
    if (duration > 16) {  // 超过一帧时间（60fps）
        LOGW("⚠️ swscale slow: %lld ms", duration);
    }
    
    // 复制到 ANativeWindow buffer（居中渲染，处理黑边或裁剪）
    uint16_t* dst_buffer = (uint16_t*)buffer.bits;
    uint16_t* src_buffer = (uint16_t*)rgb_buffer_;
    
    // 安全检查
    // stride < surface_w 说明 surface 缓冲区尚未按最新尺寸配置（常见于 swap/分屏切换的竞态）。
    // 此时丢帧并强制下帧重新配置，避免连续报错与画面闪烁。
    if (buffer.stride < surface_w) {
        LOGW("⚠️ Buffer stride too small (drop frame): stride=%d < expected_w=%d", buffer.stride, surface_w);
        {
            std::lock_guard<std::mutex> lock(window_mutex_);
            surface_configured_ = false;
            surface_generation_.fetch_add(1, std::memory_order_relaxed);
        }
        ANativeWindow_unlockAndPost(window);
        ANativeWindow_release(window);
        return;
    }
    
    // 计算居中偏移和裁剪区域
    int offset_x = 0;
    int offset_y = 0;
    int src_offset_x = 0;
    int src_offset_y = 0;
    int copy_width = target_width;
    int copy_height = target_height;
    
    if (aspect_ratio_mode_ == 0) {
        // FIT 模式：居中显示，可能有黑边
        offset_x = (surface_w - target_width) / 2;
        offset_y = (surface_h - target_height) / 2;
        
        // 清空整个 buffer（黑色背景）
        if (offset_x > 0 || offset_y > 0) {
            memset(buffer.bits, 0, buffer.stride * surface_h * 2);
        }
    } else {
        // FILL 模式：裁剪超出部分
        if (target_width > surface_w) {
            // 视频宽度超出，需要裁剪左右
            src_offset_x = (target_width - surface_w) / 2;
            copy_width = surface_w;
            offset_x = 0;
        } else {
            // 视频宽度未超出，居中显示
            offset_x = (surface_w - target_width) / 2;
            copy_width = target_width;
        }
        
        if (target_height > surface_h) {
            // 视频高度超出，需要裁剪上下
            src_offset_y = (target_height - surface_h) / 2;
            copy_height = surface_h;
            offset_y = 0;
        } else {
            // 视频高度未超出，居中显示
            offset_y = (surface_h - target_height) / 2;
            copy_height = target_height;
        }
        
        // 如果有未填充区域，清空为黑色
        if (offset_x > 0 || offset_y > 0) {
            memset(buffer.bits, 0, buffer.stride * surface_h * 2);
        }
    }
    
    // 逐行复制到目标位置（带裁剪）
    for (int i = 0; i < copy_height; i++) {
        uint16_t* dst_line = dst_buffer + (offset_y + i) * buffer.stride + offset_x;
        uint16_t* src_line = src_buffer + (src_offset_y + i) * target_width + src_offset_x;
        memcpy(dst_line, src_line, copy_width * 2);
    }
    
    static int frame_count = 0;
    frame_count++;
    if (frame_count % 60 == 0) {  // 每60帧输出一次
        LOGD("✅ Rendered %d frames successfully", frame_count);
    }
    
    ANativeWindow_unlockAndPost(window);
    ANativeWindow_release(window);
}

// ========== OpenSL ES 音频输出 ==========

bool AndroidPlayer::initAudioOutput(int sample_rate, int channels) {
    SLresult result;
    
    LOGI("Initializing audio output: %d Hz, %d channels", sample_rate, channels);
    
    // 创建引擎
    result = slCreateEngine(&engineObject_, 0, nullptr, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to create engine: %d", result);
        return false;
    }
    
    result = (*engineObject_)->Realize(engineObject_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to realize engine: %d", result);
        return false;
    }
    
    result = (*engineObject_)->GetInterface(engineObject_, SL_IID_ENGINE, &engineEngine_);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to get engine interface: %d", result);
        return false;
    }
    
    // 创建输出混音器
    result = (*engineEngine_)->CreateOutputMix(engineEngine_, &outputMixObject_, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to create output mix: %d", result);
        return false;
    }
    
    result = (*outputMixObject_)->Realize(outputMixObject_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to realize output mix: %d", result);
        return false;
    }
    
    // 配置音频源 (PCM)
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2
    };

    // ✅ 根据实际视频的音频格式动态配置
    // 采样率映射
    SLuint32 sl_sample_rate;
    switch (sample_rate) {
        case 8000:  sl_sample_rate = SL_SAMPLINGRATE_8; break;
        case 11025: sl_sample_rate = SL_SAMPLINGRATE_11_025; break;
        case 16000: sl_sample_rate = SL_SAMPLINGRATE_16; break;
        case 22050: sl_sample_rate = SL_SAMPLINGRATE_22_05; break;
        case 24000: sl_sample_rate = SL_SAMPLINGRATE_24; break;
        case 32000: sl_sample_rate = SL_SAMPLINGRATE_32; break;
        case 44100: sl_sample_rate = SL_SAMPLINGRATE_44_1; break;
        case 48000: sl_sample_rate = SL_SAMPLINGRATE_48; break;
        case 64000: sl_sample_rate = SL_SAMPLINGRATE_64; break;
        case 88200: sl_sample_rate = SL_SAMPLINGRATE_88_2; break;
        case 96000: sl_sample_rate = SL_SAMPLINGRATE_96; break;
        default:
            LOGW("Unsupported sample rate %d, using 44100", sample_rate);
            sl_sample_rate = SL_SAMPLINGRATE_44_1;
            break;
    }
    
    // 声道配置
    SLuint32 channel_mask;
    if (channels == 1) {
        channel_mask = SL_SPEAKER_FRONT_CENTER;
    } else {
        // 2 或更多声道，使用立体声
        channel_mask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
        channels = 2; // 强制为立体声
    }
    
    SLDataFormat_PCM format_pcm = {
        SL_DATAFORMAT_PCM,
        static_cast<SLuint32>(channels),
        sl_sample_rate,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        channel_mask,
        SL_BYTEORDER_LITTLEENDIAN
    };
    
    LOGI("Audio output config: %d Hz, %d channels, 16-bit", sample_rate, channels);

    SLDataSource audioSrc = {&loc_bufq, &format_pcm};
    
    // 配置音频接收器
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject_};
    SLDataSink audioSnk = {&loc_outmix, nullptr};
    
    // 创建音频播放器（不请求 VOLUME 接口，避免 AppOps 限制）
    const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    
    result = (*engineEngine_)->CreateAudioPlayer(engineEngine_, &playerObject_,
                                                 &audioSrc, &audioSnk, 1, ids, req);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to create audio player: %d", result);
        return false;
    }
    
    result = (*playerObject_)->Realize(playerObject_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to realize audio player: %d", result);
        return false;
    }
    
    // 获取播放接口
    result = (*playerObject_)->GetInterface(playerObject_, SL_IID_PLAY, &playItf_);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to get play interface: %d", result);
        return false;
    }
    
    // 获取缓冲队列接口
    result = (*playerObject_)->GetInterface(playerObject_, SL_IID_BUFFERQUEUE, &bufferQueueItf_);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to get buffer queue interface: %d", result);
        return false;
    }
    
    // 注册回调
    result = (*bufferQueueItf_)->RegisterCallback(bufferQueueItf_, audioCallback, this);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to register callback: %d", result);
        return false;
    }
    
    // ⚠️ 不再使用 VolumeItf（避免 AppOps CONTROL_AUDIO 崩溃）
    // 音量控制改为只在 Core 层处理
    volumeItf_ = nullptr;
    LOGI("Volume control disabled (using core volume only to avoid AppOps crash)");
    
    // 保存音频参数
    audio_sample_rate_ = sample_rate;
    audio_channels_ = channels;
    
    // 计算合适的缓冲区大小
    // 目标：~5ms 的音频数据（从 10ms 减少，降低内存占用）
    // 公式：bytes = sample_rate * channels * bytes_per_sample * duration
    audio_buffer_size_ = (sample_rate * channels * 2 * 5) / 1000;  // 16-bit = 2 bytes
    
    // 对齐到 4 字节边界
    audio_buffer_size_ = (audio_buffer_size_ + 3) & ~3;
    
    // 限制在最大范围内（更严格的限制）
    if (audio_buffer_size_ > MAX_AUDIO_BUFFER_SIZE) {
        audio_buffer_size_ = MAX_AUDIO_BUFFER_SIZE;
        LOGW("⚠️ Audio buffer size capped to %d bytes", MAX_AUDIO_BUFFER_SIZE);
    }
    if (audio_buffer_size_ < 960) {
        audio_buffer_size_ = 960;  // 最小 960 字节（从 1KB 减少）
    }
    
    LOGI("🎵 Audio buffer size calculated: %d bytes (%.1f ms)", 
         audio_buffer_size_, 
         (audio_buffer_size_ * 1000.0) / (sample_rate * channels * 2));
    
    // 初始填充缓冲区
    memset(audio_buffer_, 0, audio_buffer_size_);
    (*bufferQueueItf_)->Enqueue(bufferQueueItf_, audio_buffer_, audio_buffer_size_);
    
    LOGI("Audio output initialized successfully with %d Hz, %d channels", sample_rate, channels);
    return true;
}

void AndroidPlayer::destroyAudioOutput() {
    if (playerObject_) {
        (*playerObject_)->Destroy(playerObject_);
        playerObject_ = nullptr;
        playItf_ = nullptr;
        bufferQueueItf_ = nullptr;
        volumeItf_ = nullptr;
    }
    
    if (outputMixObject_) {
        (*outputMixObject_)->Destroy(outputMixObject_);
        outputMixObject_ = nullptr;
    }
    
    if (engineObject_) {
        (*engineObject_)->Destroy(engineObject_);
        engineObject_ = nullptr;
        engineEngine_ = nullptr;
    }
    
    audio_initialized_ = false;
    LOGI("Audio output destroyed");
}

void AndroidPlayer::audioCallback(SLAndroidSimpleBufferQueueItf bq, void* context) {
    auto* player = static_cast<AndroidPlayer*>(context);
    player->onAudioData(bq);
}

void AndroidPlayer::onAudioData(SLAndroidSimpleBufferQueueItf bq) {
    if (!player_core_ || audio_buffer_size_ == 0) {
        // 填充静音
        memset(audio_buffer_, 0, audio_buffer_size_ > 0 ? audio_buffer_size_ : 4096);
        (*bq)->Enqueue(bq, audio_buffer_, audio_buffer_size_ > 0 ? audio_buffer_size_ : 4096);
        return;
    }
    
    std::lock_guard<std::mutex> lock(audio_mutex_);
    
    // 循环填充缓冲区，直到填满或没有数据
    int total_bytes_read = 0;
    while (total_bytes_read < audio_buffer_size_) {
        int bytes_read = player_core_get_audio_data(
            player_core_, 
            audio_buffer_ + total_bytes_read, 
            audio_buffer_size_ - total_bytes_read
        );
        
        if (bytes_read > 0) {
            total_bytes_read += bytes_read;
        } else {
            // 没有更多数据了
            break;
        }
    }
    
    static int callback_count = 0;
    callback_count++;
    
    if (total_bytes_read > 0) {
        // 每100次回调输出一次日志（已屏蔽，日志太多）
        // if (callback_count % 100 == 0) {
        //     LOGI("🎵 Audio callback #%d: total_bytes=%d, buffer_size=%d (%.1f%%)", 
        //          callback_count, total_bytes_read, audio_buffer_size_,
        //          (total_bytes_read * 100.0) / audio_buffer_size_);
        // }
        
        // 填充剩余部分为静音
        if (total_bytes_read < audio_buffer_size_) {
            memset(audio_buffer_ + total_bytes_read, 0, audio_buffer_size_ - total_bytes_read);
            
            // 音频欠载日志（已屏蔽）
            // if (callback_count % 100 == 0) {
            //     LOGW("🎵 Audio underrun: only got %d bytes, needed %d (%.1f%%)", 
            //          total_bytes_read, audio_buffer_size_,
            //          (total_bytes_read * 100.0) / audio_buffer_size_);
            // }
        }
    } else {
        // 没有数据，填充静音
        if (callback_count % 100 == 0) {
            LOGW("🎵 No audio data available, filling silence");
        }
        memset(audio_buffer_, 0, audio_buffer_size_);
    }
    
    // 入队下一个缓冲区
    (*bq)->Enqueue(bq, audio_buffer_, audio_buffer_size_);
}
