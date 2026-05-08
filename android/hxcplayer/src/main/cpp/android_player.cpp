#include "android_player.h"
#include "hxc_player_core_c_bridge.h"
#include <android/log.h>
#include <android/native_window.h>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>
#include <inttypes.h>

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
    , decode_mode_(0) // 默认软解
    , egl_display_(EGL_NO_DISPLAY)
    , egl_context_(EGL_NO_CONTEXT)
    , egl_surface_(EGL_NO_SURFACE)
    , gl_program_(0)
    , gl_tex_y_(0)
    , gl_tex_u_(0)
    , gl_tex_v_(0)
    , gl_uniform_y_(-1)
    , gl_uniform_u_(-1)
    , gl_uniform_v_(-1)
    , gl_attrib_pos_(-1)
    , gl_attrib_tex_(-1)
    , gl_attrib_tex_uv_(-1)
    , gl_last_video_w_(0)
    , gl_last_video_h_(0)
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
    , is_loading_(false)
    , has_pending_error_(false)
    , last_error_code_(0)
    , has_pending_playback_completed_(false)
{
    LOGD("AndroidPlayer created");
    
    // 创建核心播放�?    player_core_ = player_core_create();
    if (!player_core_) {
        LOGE("Failed to create player core");
    } else {
        // loading callback
        player_core_set_loading_callback(player_core_, loadingStateCallback, this);
        // 注册底层错误回调（播放中错误透传给业务层�?        player_core_set_error_callback(player_core_, errorStateCallback, this);
        // 注册播放完成回调
        player_core_set_playback_completed_callback(player_core_, playbackCompletedCallback, this);
        LOGI("[播放完成] 已注�?playbackCompletedCallback");
    }
    
    // ⚠️ 不在这里初始化音频，等到打开视频后根据实际音频参数初始化
    LOGI("Audio will be initialized after opening video");
}

AndroidPlayer::~AndroidPlayer() {
    LOGD("AndroidPlayer destroyed");

    // before destroy player_core
    audio_active_.store(false, std::memory_order_seq_cst);

    // �?停止渲染线程（EGL/GL 资源在线程内销毁）
    render_running_ = false;
    if (render_thread_.joinable()) {
        render_thread_.join();
    }

    // �?销毁音频输出（Destroy 内部会等待当�?callback 执行完，
    //    此时 audio_active_=false，callback 只填静音不访�?player_core_，安全）
    destroyAudioOutput();

    // �?释放窗口
    if (native_window_) {
        ANativeWindow_release(native_window_);
        native_window_ = nullptr;
    }

    // destroy player_core
    if (player_core_) {
        player_core_set_playback_completed_callback(player_core_, nullptr, nullptr);
        player_core_set_loading_callback(player_core_, nullptr, nullptr);
        player_core_set_error_callback(player_core_, nullptr, nullptr);
        player_core_destroy(player_core_);
        player_core_ = nullptr;
    }
}

void AndroidPlayer::setSurface(ANativeWindow* window) {
    // avoid deadlock
    std::thread thread_to_join;
    {
        std::lock_guard<std::mutex> lock(window_mutex_);

        if (native_window_) {
            ANativeWindow_release(native_window_);
        }

        native_window_ = window;
        surface_generation_.fetch_add(1, std::memory_order_relaxed);

        if (native_window_) {
            ANativeWindow_acquire(native_window_);
            LOGD("Surface set: %p", native_window_);

            if (!render_running_) {
                render_running_ = true;
                render_thread_ = std::thread(&AndroidPlayer::renderLoop, this);
            }
            // Surface cleared: renderLoop detects native_window_==null and destroys EGL.
            // Surface cleared: renderLoop detects native_window_==null.
            LOGD("Surface cleared");
        }
    // Lock released; join render thread if needed
    if (thread_to_join.joinable()) {
        thread_to_join.join();
    }
}

void AndroidPlayer::updateSurfaceSize(int width, int height) {
    std::lock_guard<std::mutex> lock(window_mutex_);
    
    // 尺寸变化时，标记需要重新配�?Surface
    if (surface_width_ != width || surface_height_ != height) {
        LOGI("📐 Surface size changing: %dx%d -> %dx%d", 
             surface_width_, surface_height_, width, height);
        
        surface_width_ = width;
        surface_height_ = height;
        surface_generation_.fetch_add(1, std::memory_order_relaxed);
        
        LOGI("�?Surface size updated (will reconfigure on next frame)");
    }
}

bool AndroidPlayer::openURL(const char* url) {
    return openURL(url, 0.0);  // 默认从头开�?}

bool AndroidPlayer::openURL(const char* url, double start_position) {
    if (!player_core_) {
        LOGE("Player core not initialized");
        return false;
    }
    
    LOGI("Opening URL: %s, start_position: %.2f", url, start_position);
    
    player_core_set_decode_mode(player_core_,
                                decode_mode_ == 1 ? PLAYER_DECODE_MODE_HARDWARE
                                                  : PLAYER_DECODE_MODE_SOFTWARE);

    // Always use start_position API so start_position=0 also resets to beginning.
    int result = player_core_open_with_start_position(player_core_, url, start_position);
    
    if (result == 0) {
        LOGI("URL opened successfully");
        ensureAudioOutputForCurrentStream();
        render_warmup_frames_.store(8, std::memory_order_release);

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
    player_core_set_decode_mode(player_core_,
                                decode_mode_ == 1 ? PLAYER_DECODE_MODE_HARDWARE
                                                  : PLAYER_DECODE_MODE_SOFTWARE);

    LOGI("Opening with custom HTTP: %s (encrypted_file=%d)", url, encrypted_file ? 1 : 0);

    PlayerDataSourceConfigC config{};
    config.timeout_ms = timeout_ms;
    config.max_retries = max_retries;
    config.cache_size = 2 * 1024 * 1024;  // 2MB
    config.avio_buffer_size = 64 * 1024;  // 64KB
    PlayerDataSourceC source{};
    source.url = url;
    source.start_position = 0.0;
    source.mode = PLAYER_DATA_SOURCE_MODE_CUSTOM_HTTP;
    source.encrypted_file = encrypted_file ? 1 : 0;
    source.secure_headers = nullptr;

    int result = player_core_open_with_mode(player_core_, &source, &config);

    if (result == 0) {
        LOGI("Custom HTTP opened successfully");
        ensureAudioOutputForCurrentStream();
        render_warmup_frames_.store(8, std::memory_order_release);

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
    player_core_set_decode_mode(player_core_,
                                decode_mode_ == 1 ? PLAYER_DECODE_MODE_HARDWARE
                                                  : PLAYER_DECODE_MODE_SOFTWARE);

    LOGI("Opening with custom file: %s (avio_buffer_size=%zu, encrypted_file=%d)",
         path, static_cast<size_t>(avio_buffer_size), encrypted_file ? 1 : 0);

    PlayerDataSourceConfigC config{};
    config.timeout_ms = 30000;
    config.max_retries = 3;
    config.cache_size = 2 * 1024 * 1024;
    config.avio_buffer_size = avio_buffer_size;
    PlayerDataSourceC source{};
    source.url = path;
    source.start_position = 0.0;
    source.mode = PLAYER_DATA_SOURCE_MODE_CUSTOM_FILE;
    source.encrypted_file = encrypted_file ? 1 : 0;
    source.secure_headers = nullptr;

    int result = player_core_open_with_mode(player_core_, &source, &config);

    if (result == 0) {
        LOGI("Custom file opened successfully");
        ensureAudioOutputForCurrentStream();
        render_warmup_frames_.store(8, std::memory_order_release);

        player_core_pause(player_core_);
        return true;
    } else {
        LOGE("Failed to open with custom file: %d", result);
        return false;
    }
}

bool AndroidPlayer::openWithSecureSession(const char* url,
                                          const char* auth_token,
                                          const char* video_id,
                                          const char* device_id,
                                          const char* secret_id,
                                          const char* nonce,
                                          const char* play_session_id,
                                          const char* secure_headers,
                                          int64_t session_expire_at_ms,
                                          int key_mode,
                                          const char* key_material_b64,
                                          const char* key_iv_hex) {
    (void)auth_token;
    (void)video_id;
    (void)device_id;
    (void)secret_id;
    (void)nonce;
    (void)play_session_id;
    (void)session_expire_at_ms;
    (void)key_mode;
    (void)key_material_b64;
    (void)key_iv_hex;
    if (!player_core_) {
        LOGE("Player core not initialized");
        return false;
    }
    player_core_set_decode_mode(player_core_,
                                decode_mode_ == 1 ? PLAYER_DECODE_MODE_HARDWARE
                                                  : PLAYER_DECODE_MODE_SOFTWARE);

    PlayerDataSourceConfigC config{};
    PlayerDataSourceC source{};
    source.url = url;
    source.start_position = 0.0;
    source.mode = PLAYER_DATA_SOURCE_MODE_SECURE_HLS;
    source.encrypted_file = 0;
    source.secure_headers = secure_headers;

    int result = player_core_open_with_mode(player_core_, &source, &config);
    if (result == 0) {
        int sample_rate = player_core_get_audio_sample_rate(player_core_);
        int channels = player_core_get_audio_channels(player_core_);
        if (sample_rate > 0 && channels > 0 && !audio_initialized_) {
            if (initAudioOutput(sample_rate, channels)) {
                audio_initialized_ = true;
            }
        }
        render_warmup_frames_.store(8, std::memory_order_release);
        player_core_pause(player_core_);
        return true;
    }
    LOGE("Failed to open secure hls: %d", result);
    return false;
}

bool AndroidPlayer::openWithSecureHLS(const char* url,
                                      const char* auth_token,
                                      const char* video_id,
                                      const char* device_id,
                                      const char* secret_id,
                                      const char* nonce,
                                      const char* play_session_id,
                                      const char* secure_headers,
                                      int64_t session_expire_at_ms,
                                      int key_mode,
                                      const char* key_material_b64,
                                      const char* key_iv_hex) {
    // 兼容旧命名入口，统一收敛�?SecureSession�?    return openWithSecureSession(url,
                                 auth_token,
                                 video_id,
                                 device_id,
                                 secret_id,
                                 nonce,
                                 play_session_id,
                                 secure_headers,
                                 session_expire_at_ms,
                                 key_mode,
                                 key_material_b64,
                                 key_iv_hex);
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
    // 主动丢弃上一次播放结束的 pending 事件，避�?stop 后立�?open 新资源时
    // Consume flag atomically
    has_pending_playback_completed_.store(false, std::memory_order_release);
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
    // seek/open warmup
    render_warmup_frames_.store(8, std::memory_order_release);
}

void AndroidPlayer::setPlaybackRate(float rate) {
    if (!player_core_) return;
    
    LOGD("Set playback rate: %f", rate);
    player_core_set_playback_rate(player_core_, rate);
}

void AndroidPlayer::setVolume(float volume) {
    if (!player_core_) return;
    
    LOGD("Set volume: %f (core only)", volume);
    // 只在 Core 层控制音量（OpenSL ES VolumeItf 会触�?AppOps 崩溃�?    player_core_set_volume(player_core_, volume);
}

void AndroidPlayer::setAspectRatioMode(int mode) {
    aspect_ratio_mode_ = mode;
    LOGI("�?Set aspect ratio mode: %d (%s)", mode, mode == 0 ? "FIT" : "FILL");

    if (player_core_) {
        player_core_set_aspect_ratio_mode(player_core_,
            mode == 1 ? ASPECT_RATIO_FILL : ASPECT_RATIO_FIT);
    }
    // OpenGL ES Shader 通过 uniform 控制宽高比，下一帧自动生效，无需重置
}

void AndroidPlayer::setDecodeMode(int mode) {
    decode_mode_ = (mode == 1) ? 1 : 0;
    if (player_core_) {
        player_core_set_decode_mode(player_core_,
                                    decode_mode_ == 1 ? PLAYER_DECODE_MODE_HARDWARE
                                                      : PLAYER_DECODE_MODE_SOFTWARE);
    }
}

int AndroidPlayer::getDecodeMode() const {
    return decode_mode_;
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

int AndroidPlayer::getPipelineState() const {
    if (!player_core_) return 0; // IDLE
    return (int)player_core_get_pipeline_state(player_core_);
}

bool AndroidPlayer::getPlayWhenReady() const {
    if (!player_core_) return false;
    return player_core_get_play_when_ready(player_core_) != 0;
}

bool AndroidPlayer::isPlaying() const {
    if (!player_core_) return false;
    return player_core_is_playing(player_core_) != 0;
}

void AndroidPlayer::setPlayWhenReady(bool play_when_ready) {
    if (!player_core_) return;
    player_core_set_play_when_ready(player_core_, play_when_ready ? 1 : 0);
}

bool AndroidPlayer::isLoading() const {
    return is_loading_.load(std::memory_order_acquire);
}

bool AndroidPlayer::isHardwareDecodingActive() const {
    if (!player_core_) return false;
    return player_core_is_video_hardware_decoding(player_core_) != 0;
}

void AndroidPlayer::loadingStateCallback(bool is_loading, void* user_data) {
    auto* player = static_cast<AndroidPlayer*>(user_data);
    if (!player) {
        return;
    }
    player->is_loading_.store(is_loading, std::memory_order_release);
}

bool AndroidPlayer::consumeLastError(int& error_code, std::string& error_message) {
    if (!has_pending_error_.load(std::memory_order_acquire)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(error_mutex_);
    if (!has_pending_error_.load(std::memory_order_relaxed)) {
        return false;
    }

    error_code = last_error_code_;
    error_message = last_error_message_;
    has_pending_error_.store(false, std::memory_order_release);
    return true;
}

void AndroidPlayer::errorStateCallback(int error_code, const char* error_msg, void* user_data) {
    auto* player = static_cast<AndroidPlayer*>(user_data);
    if (!player) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(player->error_mutex_);
        player->last_error_code_ = error_code;
        player->last_error_message_ = error_msg ? error_msg : "unknown error";
    }
    player->has_pending_error_.store(true, std::memory_order_release);
}

// ========== 播放完成 ==========

void AndroidPlayer::playbackCompletedCallback(void* user_data) {
    auto* player = static_cast<AndroidPlayer*>(user_data);
    if (!player) {
        LOGW("[播放完成] android_player 层：收到回调�?player �?null");
        return;
    }
    LOGI("[播放完成] android_player 层：core 回调触发，position=%.3f duration=%.3f state=%d",
         player->getPosition(), player->getDuration(), player->getState());
    player->has_pending_playback_completed_.store(true, std::memory_order_release);
}

bool AndroidPlayer::consumePlaybackCompleted() {
    bool completed = has_pending_playback_completed_.exchange(false, std::memory_order_acq_rel);
    if (completed) {
        LOGI("[播放完成] android_player 层：consumePlaybackCompleted=true，position=%.3f duration=%.3f state=%d",
             getPosition(), getDuration(), getState());
    }
    return completed;
}

// ========== OpenGL ES YUV 渲染 ==========

// Vertex Shader
static const char* kVertexShader = R"(
attribute vec4 a_position;
attribute vec2 a_texcoord;    // Y texcoord
attribute vec2 a_texcoord_uv; // UV texcoord
varying   vec2 v_texcoord;
varying   vec2 v_texcoord_uv;
void main() {
    gl_Position    = a_position;
    v_texcoord     = a_texcoord;
    v_texcoord_uv  = a_texcoord_uv;
}
)";

// Fragment Shader：Y �?v_texcoord，U/V �?v_texcoord_uv
static const char* kFragmentShader = R"(
precision mediump float;
varying vec2      v_texcoord;
varying vec2      v_texcoord_uv;
uniform sampler2D u_tex_y;
uniform sampler2D u_tex_u;
uniform sampler2D u_tex_v;
void main() {
    float y = texture2D(u_tex_y, v_texcoord).r;
    float u = texture2D(u_tex_u, v_texcoord_uv).r - 0.5;
    float v = texture2D(u_tex_v, v_texcoord_uv).r - 0.5;
    float r = y + 1.402  * v;
    float g = y - 0.344  * u - 0.714 * v;
    float b = y + 1.772  * u;
    gl_FragColor = vec4(r, g, b, 1.0);
}
)";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(shader, sizeof(buf), nullptr, buf);
        __android_log_print(ANDROID_LOG_ERROR, "AndroidPlayer", "Shader compile error: %s", buf);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// EGL config 缓存（initEGLContext 时选定，initEGLSurface 复用�?static EGLConfig s_egl_config = nullptr;

bool AndroidPlayer::initEGLContext() {
    egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display_ == EGL_NO_DISPLAY) { LOGE("�?eglGetDisplay failed"); return false; }
    if (!eglInitialize(egl_display_, nullptr, nullptr)) { LOGE("�?eglInitialize failed"); return false; }

    EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLint num_configs = 0;
    if (!eglChooseConfig(egl_display_, attribs, &s_egl_config, 1, &num_configs) || num_configs == 0) {
        LOGE("�?eglChooseConfig failed"); return false;
    }

    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_context_ = eglCreateContext(egl_display_, s_egl_config, EGL_NO_CONTEXT, ctx_attribs);
    if (egl_context_ == EGL_NO_CONTEXT) { LOGE("�?eglCreateContext failed"); return false; }

    LOGI("�?EGL Context created");
    return true;
}

bool AndroidPlayer::initEGLSurface() {
    ANativeWindow* win = nullptr;
    { std::lock_guard<std::mutex> lock(window_mutex_); win = native_window_; }
    if (!win) { LOGE("�?initEGLSurface: native_window_ is null"); return false; }
    if (s_egl_config == nullptr) { LOGE("�?initEGLSurface: no EGL config, call initEGLContext first"); return false; }

    // 销毁旧 Surface（如有），再基于�?Window 重建
    if (egl_surface_ != EGL_NO_SURFACE) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(egl_display_, egl_surface_);
        egl_surface_ = EGL_NO_SURFACE;
    }

    egl_surface_ = eglCreateWindowSurface(egl_display_, s_egl_config, win, nullptr);
    if (egl_surface_ == EGL_NO_SURFACE) {
        LOGE("�?eglCreateWindowSurface failed: %d", eglGetError()); return false;
    }
    if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_)) {
        LOGE("�?eglMakeCurrent failed"); return false;
    }
    LOGI("�?EGL Surface (re)created");
    return true;
}

bool AndroidPlayer::initEGL() {
    if (!initEGLContext()) return false;
    if (!initEGLSurface()) return false;
    LOGI("�?EGL initialized");
    return true;
}

void AndroidPlayer::destroyEGLSurface() {
    if (egl_display_ != EGL_NO_DISPLAY && egl_surface_ != EGL_NO_SURFACE) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(egl_display_, egl_surface_);
        egl_surface_ = EGL_NO_SURFACE;
        LOGI("EGL Surface destroyed");
    }
}

void AndroidPlayer::destroyEGL() {
    destroyGLProgram();
    if (egl_display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_surface_ != EGL_NO_SURFACE) { eglDestroySurface(egl_display_, egl_surface_); egl_surface_ = EGL_NO_SURFACE; }
        if (egl_context_ != EGL_NO_CONTEXT) { eglDestroyContext(egl_display_, egl_context_); egl_context_ = EGL_NO_CONTEXT; }
        eglTerminate(egl_display_);
        egl_display_ = EGL_NO_DISPLAY;
        s_egl_config = nullptr;
    }
    LOGI("EGL destroyed");
}

void AndroidPlayer::redrawLastFrame() {
    if (last_frame_width_ <= 0 || last_frame_height_ <= 0) return;
    if (last_frame_y_.empty()) return;
    renderFrame(last_frame_y_.data(), last_frame_u_.data(), last_frame_v_.data(),
                last_frame_y_stride_, last_frame_u_stride_, last_frame_v_stride_,
                last_frame_width_, last_frame_height_, nullptr, nullptr);
}

bool AndroidPlayer::initGLProgram() {
    GLuint vs = compileShader(GL_VERTEX_SHADER,   kVertexShader);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) return false;

    gl_program_ = glCreateProgram();
    glAttachShader(gl_program_, vs);
    glAttachShader(gl_program_, fs);
    glLinkProgram(gl_program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = 0;
    glGetProgramiv(gl_program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[512]; glGetProgramInfoLog(gl_program_, sizeof(buf), nullptr, buf);
        LOGE("�?Program link error: %s", buf); return false;
    }

    gl_uniform_y_  = glGetUniformLocation(gl_program_, "u_tex_y");
    gl_uniform_u_  = glGetUniformLocation(gl_program_, "u_tex_u");
    gl_uniform_v_  = glGetUniformLocation(gl_program_, "u_tex_v");
    gl_attrib_pos_    = glGetAttribLocation(gl_program_, "a_position");
    gl_attrib_tex_    = glGetAttribLocation(gl_program_, "a_texcoord");
    gl_attrib_tex_uv_ = glGetAttribLocation(gl_program_, "a_texcoord_uv");

    GLuint textures[3];
    glGenTextures(3, textures);
    gl_tex_y_ = textures[0]; gl_tex_u_ = textures[1]; gl_tex_v_ = textures[2];
    for (int i = 0; i < 3; i++) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    LOGI("�?GL program & textures initialized");
    return true;
}

void AndroidPlayer::destroyGLProgram() {
    if (gl_tex_y_) { glDeleteTextures(1, &gl_tex_y_); gl_tex_y_ = 0; }
    if (gl_tex_u_) { glDeleteTextures(1, &gl_tex_u_); gl_tex_u_ = 0; }
    if (gl_tex_v_) { glDeleteTextures(1, &gl_tex_v_); gl_tex_v_ = 0; }
    if (gl_program_) { glDeleteProgram(gl_program_); gl_program_ = 0; }
    gl_attrib_pos_    = -1;
    gl_attrib_tex_    = -1;
    gl_attrib_tex_uv_ = -1;
    gl_last_video_w_  = 0;
    gl_last_video_h_  = 0;
}

// ========== 视频渲染 ==========

void AndroidPlayer::renderLoop() {
    int frame_count = 0;
    int empty_count = 0;
    bool gl_ready = false;
    uint64_t last_surface_gen = UINT64_MAX;  // 上次处理�?surface generation

    // 诊断统计
    int64_t total_render_ms = 0;
    int64_t total_upload_ms = 0;
    int64_t max_render_ms = 0;
    int64_t max_upload_ms = 0;
    int diag_interval = 60;   // �?60 帧打一次统�?
    // seek 等待诊断
    int64_t empty_start_ms = 0;
    bool in_empty_streak = false;

    auto now_ms = []() -> int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    };

    while (render_running_) {
        // 等待 Surface 就绪
        bool has_window = false;
        uint64_t cur_gen = 0;
        {
            std::lock_guard<std::mutex> lock(window_mutex_);
            has_window = (native_window_ != nullptr);
            cur_gen = surface_generation_.load(std::memory_order_relaxed);
        }

        if (!has_window) {
            if (gl_ready) { destroyEGL(); gl_ready = false; last_surface_gen = UINT64_MAX; }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        // 初始�?EGL（首次）
        if (!gl_ready) {
            if (!initEGL() || !initGLProgram()) {
                LOGE("�?GL init failed, retrying...");
                destroyEGL();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            gl_ready = true;
            last_surface_gen = cur_gen;
            LOGI("�?Render loop: GL ready");
            redrawLastFrame();  // 首次就绪后若有缓存帧（重新进�?Activity 等场景）立即重绘
        } else if (cur_gen != last_surface_gen) {
            // Surface 变化（swap / updateSurfaceSize）：只重�?EGL Surface，保�?Context + GL 资源
            LOGI("🔄 Surface generation changed (%llu -> %llu), rebuilding EGL Surface...",
                 (unsigned long long)last_surface_gen, (unsigned long long)cur_gen);
            if (!initEGLSurface()) {
                LOGE("�?initEGLSurface failed, doing full reinit...");
                destroyEGL();
                gl_ready = false;
                last_surface_gen = UINT64_MAX;
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }
            last_surface_gen = cur_gen;
            LOGI("�?EGL Surface rebuilt, redrawing last frame");
            redrawLastFrame();  // 立即重绘最后一帧，消除黑屏
        }

        if (!player_core_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        VideoFrameDataC frame_data;
        auto t_get0 = now_ms();
        int result = player_core_get_video_frame(player_core_, &frame_data);
        auto t_get1 = now_ms();

        if (result == 0) {
            // 空帧等待阶段结束
            if (in_empty_streak) {
                int64_t wait_ms = now_ms() - empty_start_ms;
                LOGI("🎬 [DIAG] 帧等待结�? 空帧轮询耗时 %" PRId64 " ms (empty_count=%d) | 视频尺寸=%dx%d",
                     wait_ms, empty_count, frame_data.width, frame_data.height);
                in_empty_streak = false;
            }
            empty_count = 0;

            // ── A/V sync（对�?iOS renderVideoFrame 逻辑）────────────────────
            double currentPTS   = frame_data.pts;
            double masterClock  = player_core_get_position(player_core_);
            double delay        = currentPTS - masterClock;
            // render_warmup_frames_ �?openURL/seekTo 在渲染层独立维护�?            // 不依�?core 的解码计数器（解码速度远快于渲染，不适合作渲染窗口基准）
            int    warmup       = render_warmup_frames_.load(std::memory_order_acquire);
            bool   should_display = false;
            bool   should_consume = false;

            if (std::isnan(currentPTS)) {
                // PTS 无效：直接显�?                should_display = true;
                should_consume = true;
            } else if (delay < -5.0) {
                // clock not synced (post-seek): force display
                LOGI("?? [SYNC] clock unsynced: pts=%.3f clock=%.3f delay=%.3f, force display",
                     currentPTS, masterClock, delay);
                should_display = true;
                should_consume = true;
            } else if (warmup > 0) {
                // seek 结束�?warmup 阶段：放宽同步策略，优先连续出图
                if (delay > 2.0) {
                    // 视频超前过大：等待音频追上，不消费帧
                    should_display = false;
                    should_consume = false;
                } else if (delay < -0.5) {
                    // 视频落后过多：跳帧（只消费不显示�?                    should_display = false;
                    should_consume = true;
                } else {
                    should_display = true;
                    should_consume = true;
                }
            } else {
                // 正常播放：精�?A/V sync
                // syncThreshold �?max(20ms, min(100ms, frameInterval * 1.5))
                double frame_interval = 1.0 / 30.0;
                double sync_threshold = std::min(0.10, std::max(0.02, frame_interval * 1.5));
                if (delay <= -sync_threshold) {
                    // 视频落后：跳�?                    should_display = false;
                    should_consume = true;
                } else if (delay <= sync_threshold) {
                    // 在同步窗口内：正常显�?                    should_display = true;
                    should_consume = true;
                } else if (delay > 0.35 && delay <= 1.0) {
                    // 视频轻度超前：强制显示防止长时间无画�?                    should_display = true;
                    should_consume = true;
                }
                // else: 视频超前 > 1s：等待音频追上，不消�?            }
            // ────────────────────────────────────────────────────────────────

            if (should_display) {
                frame_count++;
                auto t_render0 = now_ms();
                int cost = renderFrame(frame_data.y_data, frame_data.u_data, frame_data.v_data,
                                       frame_data.y_linesize, frame_data.u_linesize, frame_data.v_linesize,
                                       frame_data.width, frame_data.height,
                                       &total_upload_ms, &max_upload_ms);
                auto t_render1 = now_ms();
                int64_t render_ms = t_render1 - t_render0;
                total_render_ms += render_ms;
                if (render_ms > max_render_ms) max_render_ms = render_ms;

                if (cost < 0) {
                    LOGW("⚠️ renderFrame failed, reinitializing EGL...");
                    destroyEGL();
                    gl_ready = false;
                    last_surface_gen = UINT64_MAX;
                } else {
                    // cache last rendered frame for redraw after surface switch
                    int y_stride = frame_data.y_linesize > 0 ? frame_data.y_linesize : frame_data.width;
                    int uv_stride = frame_data.u_linesize > 0 ? frame_data.u_linesize : frame_data.width / 2;
                    int y_size  = y_stride  * frame_data.height;
                    int uv_size = uv_stride * (frame_data.height / 2);
                    if (y_size > 0 && uv_size > 0) {
                        last_frame_y_.assign((uint8_t*)frame_data.y_data, (uint8_t*)frame_data.y_data + y_size);
                        last_frame_u_.assign((uint8_t*)frame_data.u_data, (uint8_t*)frame_data.u_data + uv_size);
                        last_frame_v_.assign((uint8_t*)frame_data.v_data, (uint8_t*)frame_data.v_data + uv_size);
                        last_frame_width_    = frame_data.width;
                        last_frame_height_   = frame_data.height;
                        last_frame_y_stride_ = frame_data.y_linesize;
                        last_frame_u_stride_ = frame_data.u_linesize;
                        last_frame_v_stride_ = frame_data.v_linesize;
                    }
                }

                // every diag_interval frames, log stats
                if (frame_count % diag_interval == 0) {
                    LOGI("📊 [DIAG] 最�?%d �?| "
                         "avg_render=%" PRId64 "ms max_render=%" PRId64 "ms "
                         "avg_upload=%" PRId64 "ms max_upload=%" PRId64 "ms "
                         "get_frame=%" PRId64 "ms",
                         diag_interval,
                         total_render_ms / diag_interval, max_render_ms,
                         total_upload_ms / diag_interval, max_upload_ms,
                         t_get1 - t_get0);
                    total_render_ms = 0; total_upload_ms = 0;
                    max_render_ms = 0;  max_upload_ms = 0;
                }
                if (render_ms > 33) {
                    LOGW("⚠️ [DIAG] 慢帧: render_ms=%" PRId64 "ms | %dx%d surface=%dx%d",
                         render_ms, frame_data.width, frame_data.height,
                         surface_width_, surface_height_);
                }
            }

            if (should_consume) {
                player_core_consume_video_frame(player_core_);
                // warmup: decrement per rendered frame
                if (warmup > 0) {
                    render_warmup_frames_.fetch_sub(1, std::memory_order_acq_rel);
                }
            }

            // 如果本轮不消费（视频超前等待），短暂 sleep 避免空转
            if (!should_consume) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        } else {
            empty_count++;
            if (!in_empty_streak) {
                empty_start_ms = now_ms();
                in_empty_streak = true;
                if (player_core_) {
                    LOGI("�?[DIAG] 帧队列为空开始等�? state=%d pos=%.3f",
                         player_core_get_state(player_core_),
                         player_core_get_position(player_core_));
                }
            }
            // short poll after seek when queue is empty
            int wait_ms = (empty_count <= 50) ? 3 : 16;
            std::this_thread::sleep_for(std::chrono::milliseconds(wait_ms));
        }
    }

    // thread exit: clean up GL/EGL (must be on same thread)
    if (gl_ready) { destroyEGL(); }
    LOGI("Render loop exited, total frames: %d", frame_count);
}

int AndroidPlayer::renderFrame(void* y_data, void* u_data, void* v_data,
                               int y_linesize, int u_linesize, int v_linesize,
                               int width, int height,
                               int64_t* out_upload_ms, int64_t* out_max_upload_ms) {
    if (!y_data || width <= 0 || height <= 0) return -1;
    if (!gl_program_ || egl_surface_ == EGL_NO_SURFACE) return -1;

    int surface_w = 0, surface_h = 0;
    {
        std::lock_guard<std::mutex> lock(window_mutex_);
        surface_w = surface_width_;
        surface_h = surface_height_;
    }
    if (surface_w <= 0 || surface_h <= 0) return -1;

    auto t0 = std::chrono::high_resolution_clock::now();

    // --- 上传 Y/U/V 纹理�?K 时这一步是主要耗时�?--
    int uv_w = u_linesize > 0 ? u_linesize : width / 2;
    int uv_h = height / 2;
    int v_w  = v_linesize > 0 ? v_linesize : width / 2;

    bool size_changed = (width != gl_last_video_w_ || height != gl_last_video_h_);
    if (size_changed) {
        LOGI("🎬 [DIAG] 视频尺寸变化: %dx%d -> %dx%d | Y_linesize=%d UV_linesize=%d",
             gl_last_video_w_, gl_last_video_h_, width, height, y_linesize, uv_w);
    }

    auto t_upload0 = std::chrono::high_resolution_clock::now();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, gl_tex_y_);
    if (size_changed) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, y_linesize > 0 ? y_linesize : width, height,
                     0, GL_LUMINANCE, GL_UNSIGNED_BYTE, y_data);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, y_linesize > 0 ? y_linesize : width, height,
                        GL_LUMINANCE, GL_UNSIGNED_BYTE, y_data);
    }

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, gl_tex_u_);
    if (size_changed) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, uv_w, uv_h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, u_data);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uv_w, uv_h, GL_LUMINANCE, GL_UNSIGNED_BYTE, u_data);
    }

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, gl_tex_v_);
    if (size_changed) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, v_w, uv_h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, v_data);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, v_w, uv_h, GL_LUMINANCE, GL_UNSIGNED_BYTE, v_data);
    }

    auto t_upload1 = std::chrono::high_resolution_clock::now();
    int64_t upload_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_upload1 - t_upload0).count();
    if (out_upload_ms) *out_upload_ms += upload_ms;
    if (out_max_upload_ms && upload_ms > *out_max_upload_ms) *out_max_upload_ms = upload_ms;
    // 单帧纹理上传�?10ms 时立即警告（4K = 3840*2160 Y 面约 8MB，应�?5ms 内完成）
    if (upload_ms > 10) {
        LOGW("⚠️ [DIAG] 纹理上传�? upload_ms=%" PRId64 "ms | %dx%d Y_linesize=%d",
             upload_ms, width, height, y_linesize);
    }

    gl_last_video_w_ = width;
    gl_last_video_h_ = height;

    // --- 计算 texcoord（处�?stride padding�?--
    int y_tex_w = y_linesize > 0 ? y_linesize : width;
    float y_u_scale = (float)width  / (float)y_tex_w;   // 有效像素宽占纹理宽的比例
    float y_v_scale = 1.0f;
    float uv_u_scale = (float)(width / 2) / (float)(uv_w > 0 ? uv_w : width / 2);

    // --- 计算顶点坐标（FIT / FILL�?--
    float vx0 = -1.0f, vx1 = 1.0f, vy0 = -1.0f, vy1 = 1.0f;
    float tx0 = 0.0f, tx1 = y_u_scale, ty0 = 0.0f, ty1 = y_v_scale;
    float utx0 = 0.0f, utx1 = uv_u_scale, uty0 = 0.0f, uty1 = 1.0f;

    float video_aspect   = (float)width  / (float)height;
    float surface_aspect = (float)surface_w / (float)surface_h;

    if (aspect_ratio_mode_ == 0) {
        // FIT：视频完整显示，两侧或上下留黑边
        if (video_aspect > surface_aspect) {
            float scale = surface_aspect / video_aspect;
            vy0 = -scale; vy1 = scale;
        } else {
            float scale = video_aspect / surface_aspect;
            vx0 = -scale; vx1 = scale;
        }
    } else {
        // FILL：铺�?surface，通过裁剪 texcoord 来裁视频
        if (video_aspect > surface_aspect) {
            float ratio  = surface_aspect / video_aspect;
            float margin = (1.0f - ratio) * 0.5f;
            tx0 = margin * y_u_scale;    tx1 = (1.0f - margin) * y_u_scale;
            utx0 = margin * uv_u_scale;  utx1 = (1.0f - margin) * uv_u_scale;
        } else {
            float ratio  = video_aspect / surface_aspect;
            float margin = (1.0f - ratio) * 0.5f;
            ty0 = margin;  ty1 = 1.0f - margin;
            uty0 = margin; uty1 = 1.0f - margin;
        }
    }

    // --- 顶点数组：position(2) + Y-texcoord(2) + UV-texcoord(2)，stride = 6 floats ---
    // Y and UV use separate texcoord columns to handle stride padding and FILL clipping
    const float verts[] = {
        // pos_x  pos_y   Y_s   Y_t   UV_s   UV_t
        vx0, vy1,  tx0, ty0,  utx0, uty0,  // 左上
        vx0, vy0,  tx0, ty1,  utx0, uty1,  // 左下
        vx1, vy0,  tx1, ty1,  utx1, uty1,  // 右下
        vx0, vy1,  tx0, ty0,  utx0, uty0,  // 左上（第二个三角形）
        vx1, vy0,  tx1, ty1,  utx1, uty1,  // 右下
        vx1, vy1,  tx1, ty0,  utx1, uty0,  // 右上
    };

    glViewport(0, 0, surface_w, surface_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(gl_program_);
    glUniform1i(gl_uniform_y_, 0);
    glUniform1i(gl_uniform_u_, 1);
    glUniform1i(gl_uniform_v_, 2);

    // 使用 initGLProgram 缓存�?attrib location，避免每帧调�?glGetAttribLocation
    if (gl_attrib_pos_ < 0 || gl_attrib_tex_ < 0 || gl_attrib_tex_uv_ < 0) return -1;

    // stride = 6 floats：[pos_x, pos_y, Y_s, Y_t, UV_s, UV_t]
    const GLsizei stride = 6 * sizeof(float);
    glVertexAttribPointer(gl_attrib_pos_,    2, GL_FLOAT, GL_FALSE, stride, verts);
    glEnableVertexAttribArray(gl_attrib_pos_);
    glVertexAttribPointer(gl_attrib_tex_,    2, GL_FLOAT, GL_FALSE, stride, verts + 2);
    glEnableVertexAttribArray(gl_attrib_tex_);
    glVertexAttribPointer(gl_attrib_tex_uv_, 2, GL_FLOAT, GL_FALSE, stride, verts + 4);
    glEnableVertexAttribArray(gl_attrib_tex_uv_);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisableVertexAttribArray(gl_attrib_pos_);
    glDisableVertexAttribArray(gl_attrib_tex_);
    glDisableVertexAttribArray(gl_attrib_tex_uv_);

    if (!eglSwapBuffers(egl_display_, egl_surface_)) {
        LOGE("�?eglSwapBuffers failed: 0x%x", eglGetError());
        return -1;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
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
        destroyAudioOutput();
        return false;
    }

    // 创建输出混音�?    result = (*engineEngine_)->CreateOutputMix(engineEngine_, &outputMixObject_, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to create output mix: %d", result);
        destroyAudioOutput();
        return false;
    }

    result = (*outputMixObject_)->Realize(outputMixObject_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to realize output mix: %d", result);
        destroyAudioOutput();
        return false;
    }
    
    // 配置音频�?(PCM)
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2
    };

    // �?根据实际视频的音频格式动态配�?    // 采样率映�?    SLuint32 sl_sample_rate;
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
        // 2 或更多声道，使用立体�?        channel_mask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
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
    
    // 配置音频接收�?    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject_};
    SLDataSink audioSnk = {&loc_outmix, nullptr};
    
    // 创建音频播放器（不请�?VOLUME 接口，避�?AppOps 限制�?    const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    
    result = (*engineEngine_)->CreateAudioPlayer(engineEngine_, &playerObject_,
                                                 &audioSrc, &audioSnk, 1, ids, req);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to create audio player: %d", result);
        destroyAudioOutput();
        return false;
    }

    result = (*playerObject_)->Realize(playerObject_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to realize audio player: %d", result);
        destroyAudioOutput();
        return false;
    }

    // 获取播放接口
    result = (*playerObject_)->GetInterface(playerObject_, SL_IID_PLAY, &playItf_);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to get play interface: %d", result);
        destroyAudioOutput();
        return false;
    }

    // 获取缓冲队列接口
    result = (*playerObject_)->GetInterface(playerObject_, SL_IID_BUFFERQUEUE, &bufferQueueItf_);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to get buffer queue interface: %d", result);
        destroyAudioOutput();
        return false;
    }

    // 注册回调
    result = (*bufferQueueItf_)->RegisterCallback(bufferQueueItf_, audioCallback, this);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to register callback: %d", result);
        destroyAudioOutput();
        return false;
    }
    
    // ⚠️ 不再使用 VolumeItf（避�?AppOps CONTROL_AUDIO 崩溃�?    // 音量控制改为只在 Core 层处�?    volumeItf_ = nullptr;
    LOGI("Volume control disabled (using core volume only to avoid AppOps crash)");
    
    // 保存音频参数
    audio_sample_rate_ = sample_rate;
    audio_channels_ = channels;
    
    // 计算合适的缓冲区大�?    // 目标：~5ms 的音频数据（�?10ms 减少，降低内存占用）
    // 公式：bytes = sample_rate * channels * bytes_per_sample * duration
    audio_buffer_size_ = (sample_rate * channels * 2 * 5) / 1000;  // 16-bit = 2 bytes
    
    // 对齐�?4 字节边界
    audio_buffer_size_ = (audio_buffer_size_ + 3) & ~3;
    
    // 限制在最大范围内（更严格的限制）
    if (audio_buffer_size_ > MAX_AUDIO_BUFFER_SIZE) {
        audio_buffer_size_ = MAX_AUDIO_BUFFER_SIZE;
        LOGW("⚠️ Audio buffer size capped to %d bytes", MAX_AUDIO_BUFFER_SIZE);
    }
    if (audio_buffer_size_ < 960) {
        audio_buffer_size_ = 960;  // 最�?960 字节（从 1KB 减少�?    }
    
    LOGI("🎵 Audio buffer size calculated: %d bytes (%.1f ms)", 
         audio_buffer_size_, 
         (audio_buffer_size_ * 1000.0) / (sample_rate * channels * 2));
    
    // 初始填充缓冲�?    memset(audio_buffer_, 0, audio_buffer_size_);
    (*bufferQueueItf_)->Enqueue(bufferQueueItf_, audio_buffer_, audio_buffer_size_);

    // 允许 onAudioData 访问 player_core_
    audio_active_.store(true, std::memory_order_seq_cst);

    LOGI("Audio output initialized successfully with %d Hz, %d channels", sample_rate, channels);
    return true;
}

void AndroidPlayer::destroyAudioOutput() {
    // �?标记 audio_active_=false，callback 检测到后只填静音、不访问 player_core_
    audio_active_.store(false, std::memory_order_seq_cst);

    if (playerObject_) {
        // �?STOPPED：防�?AudioTrack 继续排队�?callback
        if (playItf_) {
            (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_STOPPED);
        }

        // Wait for any in-flight callback to exit critical section
        {
            std::lock_guard<std::mutex> lk(audio_mutex_);
            // callback 已退出或不会再进入临界区，此�?Destroy 安全
        }

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
    audio_sample_rate_ = 0;
    audio_channels_ = 0;
    audio_buffer_size_ = 0;
    LOGI("Audio output destroyed");
}

void AndroidPlayer::ensureAudioOutputForCurrentStream() {
    if (!player_core_) {
        return;
    }

    int sample_rate = player_core_get_audio_sample_rate(player_core_);
    int channels = player_core_get_audio_channels(player_core_);
    LOGI("Audio info: sample_rate=%d, channels=%d", sample_rate, channels);

    if (sample_rate <= 0 || channels <= 0) {
        if (audio_initialized_) {
            LOGI("No valid audio stream, destroying previous audio output");
            destroyAudioOutput();
        } else {
            LOGW("No audio stream or invalid audio parameters");
        }
        return;
    }

    const bool need_recreate = !audio_initialized_
                            || audio_sample_rate_ != sample_rate
                            || audio_channels_ != channels;
    if (!need_recreate) {
        LOGI("Audio output already matched stream parameters");
        return;
    }

    if (audio_initialized_) {
        LOGI("Audio format changed, rebuilding output: %d/%d -> %d/%d",
             audio_sample_rate_, audio_channels_, sample_rate, channels);
        destroyAudioOutput();
    }

    if (!initAudioOutput(sample_rate, channels)) {
        LOGE("Failed to initialize audio output");
        audio_initialized_ = false;
        return;
    }

    audio_initialized_ = true;
    LOGI("Audio output initialized with stream parameters");
}

void AndroidPlayer::audioCallback(SLAndroidSimpleBufferQueueItf bq, void* context) {
    auto* player = static_cast<AndroidPlayer*>(context);
    player->onAudioData(bq);
}

void AndroidPlayer::onAudioData(SLAndroidSimpleBufferQueueItf bq) {
    int buf_size = audio_buffer_size_ > 0 ? audio_buffer_size_ : 4096;
    // audio_active_=false means destructor is running; just output silence
    if (!audio_active_.load(std::memory_order_acquire) || !player_core_ || audio_buffer_size_ == 0) {
        memset(audio_buffer_, 0, buf_size);
        (*bq)->Enqueue(bq, audio_buffer_, buf_size);
        return;
    }

    std::lock_guard<std::mutex> lock(audio_mutex_);
    // 持锁后再次检查（destroyAudioOutput 在锁�?set false，但 player_core_ 此时仍有效）
    if (!audio_active_.load(std::memory_order_acquire) || !player_core_) {
        memset(audio_buffer_, 0, buf_size);
        (*bq)->Enqueue(bq, audio_buffer_, buf_size);
        return;
    }

    // fill buffer in a loop until full or no more data
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
            break;
        }
    }

    
    static int callback_count = 0;
    callback_count++;
    
    if (total_bytes_read > 0) {
        // log every 100 callbacks (disabled: too noisy)
        // if (callback_count % 100 == 0) {
        //     LOGI("🎵 Audio callback #%d: total_bytes=%d, buffer_size=%d (%.1f%%)", 
        //          callback_count, total_bytes_read, audio_buffer_size_,
        //          (total_bytes_read * 100.0) / audio_buffer_size_);
        // }
        
        // fill remaining bytes with silence
        if (total_bytes_read < audio_buffer_size_) {
            memset(audio_buffer_ + total_bytes_read, 0, audio_buffer_size_ - total_bytes_read);
            
            // audio underrun log (disabled)
            // if (callback_count % 100 == 0) {
            //     LOGW("🎵 Audio underrun: only got %d bytes, needed %d (%.1f%%)", 
            //          total_bytes_read, audio_buffer_size_,
            //          (total_bytes_read * 100.0) / audio_buffer_size_);
            // }
        }
    } else {
        // no data: output silence
        if (callback_count % 100 == 0) {
            LOGW("🎵 No audio data available, filling silence");
        }
        memset(audio_buffer_, 0, audio_buffer_size_);
    }
    
    // 入队下一个缓冲区
    (*bq)->Enqueue(bq, audio_buffer_, audio_buffer_size_);
}
