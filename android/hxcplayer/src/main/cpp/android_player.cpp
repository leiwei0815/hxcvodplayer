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
#include <unistd.h>  // gettid()
// EGL_OPENGL_ES3_BIT may not be defined in older EGL headers
#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif


#define LOG_TAG "HXC"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Rate-limited logging: prints at most once every N calls.
// Usage: LOGI_RATE(100, "msg %d", val);
#define LOGI_RATE(N, ...) do { \
    static int _rl_cnt = 0; \
    if (++_rl_cnt % (N) == 1) { \
        LOGI(__VA_ARGS__); \
    } \
} while(0)
#define LOGW_RATE(N, ...) do { \
    static int _rl_cnt = 0; \
    if (++_rl_cnt % (N) == 1) { \
        LOGW(__VA_ARGS__); \
    } \
} while(0)

AndroidPlayer::AndroidPlayer()
    : player_core_(nullptr)
    , native_window_(nullptr)
    , pending_window_(nullptr)
    , window_changed_(false)
    , stop_requested_(false)
    , surface_width_(0)
    , surface_height_(0)
    , aspect_ratio_mode_(0)
    , decode_mode_(0)
    , egl_display_(EGL_NO_DISPLAY)
    , egl_context_(EGL_NO_CONTEXT)
    , egl_surface_(EGL_NO_SURFACE)
    , egl_config_(nullptr)
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
    , gl_pbo_y_{}
    , gl_pbo_y_sz_(0)
    , gl_pbo_uv_sz_(0)
    , gl_pbo_idx_(0)
    , gl_pbo_first_frame_(true)
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
    LOGI("[lifecycle] AndroidPlayer created this=%p", (void*)this);

    player_core_ = player_core_create();
    if (!player_core_) {
        LOGE("Failed to create player core");
    } else {
        player_core_set_loading_callback(player_core_, loadingStateCallback, this);
        player_core_set_error_callback(player_core_, errorStateCallback, this);
        player_core_set_playback_completed_callback(player_core_, playbackCompletedCallback, this);
        LOGI("Player core created, callbacks registered");
    }

    // Start render thread immediately. It will block on render_cv_ until a
    // surface is provided.
    render_running_ = true;
    render_thread_  = std::thread(&AndroidPlayer::renderLoop, this);
}

AndroidPlayer::~AndroidPlayer() {
    LOGI("[lifecycle] AndroidPlayer destroying this=%p", (void*)this);

    // 1. Stop audio callback before anything else (avoids use-after-free on player_core_)
    audio_active_ = false;

    // 2. Signal render thread to exit and wake it immediately via condvar
    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        stop_requested_ = true;
        // Release any pending_window_ we may have queued
        if (pending_window_) {
            ANativeWindow_release(pending_window_);
            pending_window_ = nullptr;
        }
    }
    render_cv_.notify_all();

    if (render_thread_.joinable()) {
        render_thread_.join();
    }
    LOGD("Render thread joined");

    // 3. Release the native_window_ we hold (render thread is gone, safe now)
    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        if (native_window_) {
            ANativeWindow_release(native_window_);
            native_window_ = nullptr;
        }
    }

    // 4. Null out player_core_ under audio_mutex_ BEFORE destroying the audio
    //    player object.  This guarantees that any audio callback which fires
    //    during or after playerObject_->Destroy() will see player_core_==nullptr
    //    and return silence instead of calling swr_convert on a freed context.
    PlayerCoreHandle* core_to_destroy = nullptr;
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        core_to_destroy = player_core_;
        player_core_ = nullptr;
    }

    // 5. Now safe to destroy the audio engine (callbacks see player_core_==nullptr)
    destroyAudioOutput();

    // 6. Tear down player core (callbacks already neutralised above)
    if (core_to_destroy) {
        player_core_set_playback_completed_callback(core_to_destroy, nullptr, nullptr);
        player_core_set_loading_callback(core_to_destroy, nullptr, nullptr);
        player_core_set_error_callback(core_to_destroy, nullptr, nullptr);
        player_core_destroy(core_to_destroy);
    }
    LOGI("[lifecycle] AndroidPlayer destroyed this=%p", (void*)this);
}

void AndroidPlayer::setSurface(ANativeWindow* window) {
    // Acquire reference before taking the lock (avoids holding lock during syscall)
    if (window) ANativeWindow_acquire(window);

    {
        std::lock_guard<std::mutex> lock(render_mutex_);

        // Discard any not-yet-consumed pending window
        if (pending_window_) {
            ANativeWindow_release(pending_window_);
            pending_window_ = nullptr;
        }

        pending_window_ = window; // null means "clear surface"
        window_changed_ = true;

        if (window) {
            int w = ANativeWindow_getWidth(window);
            int h = ANativeWindow_getHeight(window);
            if (w > 0 && h > 0) {
                surface_width_  = w;
                surface_height_ = h;
            }
            LOGI("[surface] setSurface: queued window=%p size=%dx%d", window, surface_width_, surface_height_);
        } else {
            LOGI("[surface] setSurface: queued clear (null)");
        }
    }
    render_cv_.notify_one();
}

void AndroidPlayer::updateSurfaceSize(int width, int height) {
    if (width <= 0 || height <= 0) return;
    std::lock_guard<std::mutex> lock(render_mutex_);
    if (surface_width_ != width || surface_height_ != height) {
        LOGI("[surface] updateSurfaceSize: %dx%d -> %dx%d", surface_width_, surface_height_, width, height);
        surface_width_  = width;
        surface_height_ = height;
    }
}

bool AndroidPlayer::openURL(const char* url) {
    return openURL(url, 0.0);
}

bool AndroidPlayer::openURL(const char* url, double start_position) {
    if (!player_core_) {
        LOGE("Player core not initialized");
        return false;
    }
    
    LOGI("[open] openURL start_pos=%.3f url=%s", start_position, url ? url : "(null)");
    
    player_core_set_decode_mode(player_core_,
                                decode_mode_ == 1 ? PLAYER_DECODE_MODE_HARDWARE
                                                  : PLAYER_DECODE_MODE_SOFTWARE);

    int result = player_core_open_with_start_position(player_core_, url, start_position);
    
    if (result == 0) {
        LOGI("[open] openURL OK");
        ensureAudioOutputForCurrentStream();
        player_core_pause(player_core_);
        render_cv_.notify_one(); // wake render thread -- frames may be ready
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
        player_core_pause(player_core_);
        render_cv_.notify_one();
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
        player_core_pause(player_core_);
        render_cv_.notify_one();
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
        ensureAudioOutputForCurrentStream();
        player_core_pause(player_core_);
        render_cv_.notify_one();
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
    // Legacy alias: forward to openWithSecureSession
    return openWithSecureSession(url,
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
    
    LOGI("[ctrl] play: state=%d pos=%.3f", player_core_get_state(player_core_), player_core_get_position(player_core_));
    player_core_play(player_core_);
    render_cv_.notify_one();

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
    LOGI("[ctrl] play: dispatched to core + audio");
}

void AndroidPlayer::pause() {
    if (!player_core_) return;
    
    LOGI("[ctrl] pause: state=%d pos=%.3f", player_core_get_state(player_core_), player_core_get_position(player_core_));
    player_core_pause(player_core_);

    if (playItf_) {
        SLresult result = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PAUSED);
        if (result != SL_RESULT_SUCCESS) {
            LOGE("Failed to set play state to PAUSED: %d", result);
        }
    }
}

void AndroidPlayer::stop() {
    if (!player_core_) return;

    LOGI("[ctrl] stop: state=%d pos=%.3f", player_core_get_state(player_core_), player_core_get_position(player_core_));
    // Clear any pending completion event so it doesn't fire after a subsequent open()
    has_pending_playback_completed_.store(false, std::memory_order_release);
    player_core_stop(player_core_);

    if (playItf_) {
        SLresult result = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_STOPPED);
        if (result != SL_RESULT_SUCCESS) {
            LOGE("Failed to set play state to STOPPED: %d", result);
        }
    }
}

void AndroidPlayer::seekTo(double position) {
    if (!player_core_) return;

    LOGI("[ctrl] seekTo: %.3fs (current pos=%.3f state=%d)", position, player_core_get_position(player_core_), player_core_get_state(player_core_));
    player_core_seek(player_core_, position);
    render_cv_.notify_one();
}

void AndroidPlayer::setPlaybackRate(float rate) {
    if (!player_core_) return;
    
    LOGD("Set playback rate: %f", rate);
    player_core_set_playback_rate(player_core_, rate);
}

void AndroidPlayer::setVolume(float volume) {
    if (!player_core_) return;
    
    LOGD("Set volume: %f (core only)", volume);
    // Volume is applied inside the core; we do not use OpenSL ES VolumeItf
    // to avoid triggering Android AppOps CONTROL_AUDIO permission checks.
    player_core_set_volume(player_core_, volume);
}

void AndroidPlayer::setAspectRatioMode(int mode) {
    aspect_ratio_mode_ = mode;
    LOGI("Set aspect ratio mode: %d (%s)", mode, mode == 0 ? "FIT" : "FILL");

    if (player_core_) {
        player_core_set_aspect_ratio_mode(player_core_,
            mode == 1 ? ASPECT_RATIO_FILL : ASPECT_RATIO_FIT);
    }
    // The GLSL shader uses texcoord scaling instead of a uniform for aspect ratio,
    // so no shader reload is needed here -- the change takes effect on the next frame.
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
    LOGI("[state] loading=%s pos=%.3f", is_loading ? "true" : "false",
         player->player_core_ ? player_core_get_position(player->player_core_) : 0.0);
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
    LOGE("[state] error: code=%d msg=%s", error_code, error_msg ? error_msg : "");
}

// ========== Playback-completed callback ==========

void AndroidPlayer::playbackCompletedCallback(void* user_data) {
    auto* player = static_cast<AndroidPlayer*>(user_data);
    if (!player) {
        LOGW("[playbackCompleted] player is null");
        return;
    }
    LOGI("[playbackCompleted] pos=%.3f dur=%.3f state=%d",
         player->getPosition(), player->getDuration(), player->getState());
    player->has_pending_playback_completed_.store(true, std::memory_order_release);
}

bool AndroidPlayer::consumePlaybackCompleted() {
    bool completed = has_pending_playback_completed_.exchange(false, std::memory_order_acq_rel);
    if (completed) {
        LOGI("[playbackCompleted] consumed: pos=%.3f dur=%.3f state=%d",
             getPosition(), getDuration(), getState());
    }
    return completed;
}

// ========== OpenGL ES YUV renderer ==========

// Vertex shader: two separate texcoord sets so Y and UV can have different
// stride-padding and FILL crop offsets.
static const char* kVertexShader =
    "#version 300 es\n"
    "in vec4 a_position;\n"
    "in vec2 a_texcoord;\n"
    "in vec2 a_texcoord_uv;\n"
    "out vec2 v_texcoord;\n"
    "out vec2 v_texcoord_uv;\n"
    "void main() {\n"
    "    gl_Position   = a_position;\n"
    "    v_texcoord    = a_texcoord;\n"
    "    v_texcoord_uv = a_texcoord_uv;\n"
    "}\n";

// Fragment shader: BT.601 limited-range YUV -> RGB
static const char* kFragmentShader =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec2      v_texcoord;\n"
    "in vec2      v_texcoord_uv;\n"
    "uniform sampler2D u_tex_y;\n"
    "uniform sampler2D u_tex_u;\n"
    "uniform sampler2D u_tex_v;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float y = texture(u_tex_y, v_texcoord).r;\n"
    "    float u = texture(u_tex_u, v_texcoord_uv).r - 0.5;\n"
    "    float v = texture(u_tex_v, v_texcoord_uv).r - 0.5;\n"
    "    float r = y + 1.402  * v;\n"
    "    float g = y - 0.344  * u - 0.714 * v;\n"
    "    float b = y + 1.772  * u;\n"
    "    fragColor = vec4(r, g, b, 1.0);\n"
    "}\n";

static GLuint compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[512];
        glGetShaderInfoLog(shader, sizeof(buf), nullptr, buf);
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "Shader compile error: %s", buf);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool AndroidPlayer::initEGLContext() {
    egl_display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display_ == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return false; }
    if (!eglInitialize(egl_display_, nullptr, nullptr)) { LOGE("eglInitialize failed"); return false; }

    // Choose config once and reuse for both context and surface creation.
    // Adding alpha channel (EGL_ALPHA_SIZE=8) avoids black-frame artifacts
    // when the surface is first attached to a TextureView.
    // Prefer ES3 (required for PBO); fall back to ES2 on older devices.
    EGLint attribs3[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint attribs2[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint attribs2_noalpha[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_NONE
    };
    EGLint num_configs = 0;
    int    gl_version  = 3;
    if (!eglChooseConfig(egl_display_, attribs3, &egl_config_, 1, &num_configs) || num_configs == 0) {
        gl_version = 2;
        if (!eglChooseConfig(egl_display_, attribs2, &egl_config_, 1, &num_configs) || num_configs == 0) {
            if (!eglChooseConfig(egl_display_, attribs2_noalpha, &egl_config_, 1, &num_configs) || num_configs == 0) {
                LOGE("eglChooseConfig failed"); return false;
            }
        }
    }
    EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, gl_version, EGL_NONE };
    egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT, ctx_attribs);
    if (egl_context_ == EGL_NO_CONTEXT) { LOGE("eglCreateContext failed"); return false; }
    // Create a minimal 1x1 pbuffer surface so we can call eglMakeCurrent now.
    // This makes the GL context current immediately, allowing initGLProgram()
    // to compile shaders before the real window surface arrives.
    // The pbuffer is replaced by the real window surface in initEGLSurface().
    EGLint pbuf_attribs[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    EGLSurface pbuf = eglCreatePbufferSurface(egl_display_, egl_config_, pbuf_attribs);
    if (pbuf == EGL_NO_SURFACE) {
        // Pbuffer not supported on this config - try a simpler config
        LOGW("[egl] pbuffer create failed (0x%x), GL init may fail", eglGetError());
    } else {
        if (!eglMakeCurrent(egl_display_, pbuf, pbuf, egl_context_)) {
            LOGW("[egl] eglMakeCurrent(pbuffer) failed: 0x%x", eglGetError());
            eglDestroySurface(egl_display_, pbuf);
        } else {
            // Store pbuffer so initEGLSurface can clean it up when window arrives
            egl_surface_ = pbuf;
        }
    }

    // Log key EGL config attributes for diagnostics
    EGLint r=0,g=0,b=0,a=0,d=0;
    eglGetConfigAttrib(egl_display_, egl_config_, EGL_RED_SIZE,   &r);
    eglGetConfigAttrib(egl_display_, egl_config_, EGL_GREEN_SIZE, &g);
    eglGetConfigAttrib(egl_display_, egl_config_, EGL_BLUE_SIZE,  &b);
    eglGetConfigAttrib(egl_display_, egl_config_, EGL_ALPHA_SIZE, &a);
    eglGetConfigAttrib(egl_display_, egl_config_, EGL_DEPTH_SIZE, &d);
    LOGI("[egl] context ready: RGBA=%d%d%d%d depth=%d gl_ver=ES%d config=%p pbuf=%s",
         r,g,b,a,d, gl_version, (void*)egl_config_,
         (egl_surface_ != EGL_NO_SURFACE) ? "ok" : "failed");
    return true;
}

bool AndroidPlayer::initEGLSurface(ANativeWindow* win) {
    if (egl_display_ == EGL_NO_DISPLAY || egl_context_ == EGL_NO_CONTEXT) {
        LOGE("initEGLSurface: context not ready");
        return false;
    }
    if (!win) { LOGE("initEGLSurface: win is null"); return false; }

    // Tear down any existing surface first
    if (egl_surface_ != EGL_NO_SURFACE) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(egl_display_, egl_surface_);
        egl_surface_ = EGL_NO_SURFACE;
    }

    egl_surface_ = eglCreateWindowSurface(egl_display_, egl_config_, win, nullptr);
    if (egl_surface_ == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed: 0x%x", eglGetError());
        return false;
    }
    if (!eglMakeCurrent(egl_display_, egl_surface_, egl_surface_, egl_context_)) {
        LOGE("eglMakeCurrent failed: 0x%x", eglGetError());
        eglDestroySurface(egl_display_, egl_surface_);
        egl_surface_ = EGL_NO_SURFACE;
        return false;
    }
    eglSwapInterval(egl_display_, 0); // render at video frame rate, not vsync
    EGLint sw=0, sh=0;
    eglQuerySurface(egl_display_, egl_surface_, EGL_WIDTH,  &sw);
    eglQuerySurface(egl_display_, egl_surface_, EGL_HEIGHT, &sh);
    LOGI("[egl] surface created: win=%p egl_size=%dx%d", (void*)win, sw, sh);
    return true;
}

void AndroidPlayer::destroyEGLSurface() {
    if (egl_display_ != EGL_NO_DISPLAY && egl_surface_ != EGL_NO_SURFACE) {
        eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(egl_display_, egl_surface_);
        egl_surface_ = EGL_NO_SURFACE;
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
    }
    LOGI("EGL destroyed");
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
        LOGE("GL program link error: %s", buf); return false;
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
    LOGI("GL program & textures initialized");
    return true;
}

void AndroidPlayer::destroyGLProgram() {
    // Delete PBOs before textures / program
    if (gl_pbo_y_[0]) {
        glDeleteBuffers(6, gl_pbo_y_);
        memset(gl_pbo_y_, 0, sizeof(gl_pbo_y_));
    }
    gl_pbo_y_sz_        = 0;
    gl_pbo_uv_sz_       = 0;
    gl_pbo_idx_         = 0;
    gl_pbo_first_frame_ = true;

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

void AndroidPlayer::redrawLastFrame() {
    if (last_frame_width_ <= 0 || last_frame_height_ <= 0 ||
        last_frame_y_.empty() || last_frame_u_.empty() || last_frame_v_.empty()) {
        return;
    }
    if (!gl_program_ || egl_surface_ == EGL_NO_SURFACE) return;
    renderFrame(last_frame_y_.data(), last_frame_u_.data(), last_frame_v_.data(),
                last_frame_y_stride_, last_frame_u_stride_, last_frame_v_stride_,
                last_frame_width_, last_frame_height_);
    LOGI("redrawLastFrame: %dx%d", last_frame_width_, last_frame_height_);
}


// ========== Render loop ==========
//
// Architecture (aligned with Tencent/IJKPlayer):
//
//  - The render thread owns ALL EGL objects.
//  - condition_variable (render_cv_) replaces all fixed sleeps.
//    Woken by: setSurface(), play(), seekTo(), open*(), ~AndroidPlayer().
//  - EGL context lives for the entire player lifetime (created once).
//  - EGL window surface is rebuilt whenever native_window_ changes.
//  - No warmup counter.  The A/V clock is the sole gate.
//    When audio clock == 0 (not yet started) we display immediately so the
//    first picture appears before the audio engine warms up.

void AndroidPlayer::renderLoop() {
    // One-time EGL context + GL program setup
    if (!initEGLContext()) {
        LOGE("renderLoop: initEGLContext failed");
        render_running_ = false;
        return;
    }
    if (!initGLProgram()) {
        LOGE("renderLoop: initGLProgram failed");
        destroyEGL();
        render_running_ = false;
        return;
    }
    LOGI("[render] loop started: tid=%d EGL context + GL program ready",
         (int)gettid());

    bool    surface_ready   = false;
    int     frame_count     = 0;
    int     empty_count     = 0;
    bool    in_empty_streak = false;
    int64_t empty_start_ms  = 0;

    int64_t total_render_ms = 0, total_upload_ms = 0;
    int64_t max_render_ms   = 0, max_upload_ms   = 0;
    const int kDiagInterval = 60;

    const double kSyncThreshold = 0.050; // 50 ms: drop if video is behind
    const double kMaxAhead      = 2.000; // 2 s:  hold if video is too far ahead

    auto now_ms = []() -> int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    };

    while (true) {
        // --- Wait for work, surface change, or stop ---
        {
            std::unique_lock<std::mutex> lock(render_mutex_);

            if (!window_changed_) {
                render_cv_.wait_for(lock, std::chrono::milliseconds(16),
                    [this]{ return window_changed_ || stop_requested_; });
            }

            if (stop_requested_) break;

            if (window_changed_) {
                window_changed_ = false;
                ANativeWindow* new_win = pending_window_;
                pending_window_ = nullptr;

                ANativeWindow* old_win = native_window_;
                native_window_ = new_win; // transfer ownership (ref acquired in setSurface)

                lock.unlock();

                if (new_win) {
                    if (initEGLSurface(new_win)) {
                        surface_ready = true;
                        gl_pbo_first_frame_ = true;
                        // Read the actual EGL surface size (may differ from ANativeWindow size)
                        EGLint egl_w = 0, egl_h = 0;
                        eglQuerySurface(egl_display_, egl_surface_, EGL_WIDTH,  &egl_w);
                        eglQuerySurface(egl_display_, egl_surface_, EGL_HEIGHT, &egl_h);
                        LOGI("[render] surface ready: win=%p egl=%dx%d",
                             (void*)new_win, egl_w, egl_h);
                        redrawLastFrame();
                    } else {
                        LOGE("[render] initEGLSurface failed -- will retry on next setSurface");
                        surface_ready = false;
                    }
                } else {
                    destroyEGLSurface();
                    surface_ready = false;
                    LOGI("[render] surface released (null window)");
                }

                if (old_win) ANativeWindow_release(old_win);
                continue; // lock re-acquired by unique_lock dtor is harmless; just loop
            }
        }

        if (!surface_ready || !player_core_) continue;

        // --- Fetch next decoded video frame ---
        VideoFrameDataC frame_data;
        int get_result = player_core_get_video_frame(player_core_, &frame_data);

        if (get_result == 0) {
            if (in_empty_streak) {
                LOGI("[render] buffer refilled after %" PRId64 "ms (empty_cnt=%d) frame=%dx%d",
                     now_ms() - empty_start_ms, empty_count,
                     frame_data.width, frame_data.height);
                in_empty_streak = false;
            }
            empty_count = 0;

            double pts   = frame_data.pts;
            double clock = player_core_get_position(player_core_);
            double delay = pts - clock;

            bool should_display = false;
            bool should_consume = false;

            if (std::isnan(pts) || std::isinf(pts)) {
                should_display = should_consume = true;
            } else if (delay < -kSyncThreshold) {
                should_display = false;
                should_consume = true;
                if (delay < -5.0) {
                    LOGI_RATE(30, "[sync] large drop: pts=%.3f clk=%.3f delay=%.3f", pts, clock, delay);
                }
            } else if (clock <= 0.0) {
                // Audio clock not yet started: show first frame immediately (Tencent fast-path)
                if (frame_count == 0) {
                    LOGI("[sync] first frame displayed (audio clock not yet running): pts=%.3f", pts);
                }
                should_display = should_consume = true;
            } else if (delay <= kMaxAhead) {
                should_display = should_consume = true;
            }
            // else: video > 2s ahead of audio -- hold frame

            if (should_display) {
                frame_count++;
                auto t0 = now_ms();
                int cost = renderFrame(
                    frame_data.y_data, frame_data.u_data, frame_data.v_data,
                    frame_data.y_linesize, frame_data.u_linesize, frame_data.v_linesize,
                    frame_data.width, frame_data.height,
                    &total_upload_ms, &max_upload_ms);
                int64_t render_ms = now_ms() - t0;
                total_render_ms += render_ms;
                if (render_ms > max_render_ms) max_render_ms = render_ms;

                if (cost < 0) {
                    LOGW("[render] renderFrame failed eglErr=0x%x, will reinit surface", eglGetError());
                    destroyEGLSurface();
                    surface_ready = false;
                    std::lock_guard<std::mutex> lk(render_mutex_);
                    if (native_window_) {
                        ANativeWindow_acquire(native_window_);
                        pending_window_ = native_window_;
                        window_changed_ = true;
                    }
                    continue;
                }

                if (frame_count % kDiagInterval == 0) {
                    double pos = player_core_ ? player_core_get_position(player_core_) : 0.0;
                    LOGI("[perf] %d frames pos=%.1fs | avg_render=%" PRId64 "ms max=%" PRId64
                         "ms avg_upload=%" PRId64 "ms max=%" PRId64 "ms",
                         kDiagInterval, pos,
                         total_render_ms / kDiagInterval, max_render_ms,
                         total_upload_ms / kDiagInterval, max_upload_ms);
                    total_render_ms = total_upload_ms = max_render_ms = max_upload_ms = 0;
                }
                if (render_ms > 33)
                    LOGW("[perf] slow frame %" PRId64 "ms %dx%d surf=%dx%d",
                         render_ms, frame_data.width, frame_data.height,
                         surface_width_, surface_height_);
            }

            if (should_consume) {
                player_core_consume_video_frame(player_core_);
            } else {
                // Video ahead of clock: brief sleep then re-check
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

        } else {
            empty_count++;
            if (!in_empty_streak) {
                empty_start_ms  = now_ms();
                in_empty_streak = true;
                LOGI("[render] frame queue empty: state=%d pos=%.3f",
                     player_core_get_state(player_core_),
                     player_core_get_position(player_core_));
            } else if (empty_count % 60 == 0) {
                LOGI("[render] still buffering: empty_ms=%" PRId64 " state=%d pos=%.3f",
                     now_ms() - empty_start_ms,
                     player_core_get_state(player_core_),
                     player_core_get_position(player_core_));
            }
            if (empty_count == 12) redrawLastFrame();
            // condvar 16ms timeout handles the pace; no extra sleep needed
        }
    }

    destroyEGL();
    render_running_ = false;
    LOGI("[render] loop exited: total_frames=%d", frame_count);
}

// ---------------------------------------------------------------------------
// PBO-accelerated texture upload helper
//
// We keep a double-buffer of PBOs per plane.  On each frame:
//   - eglSwapBuffers completes the draw using the texture uploaded LAST frame
//   - We fill the "next" PBO with DMA via glMapBufferRange
//   - We kick off the async GPU upload (glUnmapBuffer / glTexSubImage2D from PBO)
//
// This hides the CPU-GPU copy latency behind the draw call, which is the main
// cause of >16ms render times on 4K (3840x2160 Y plane = 8 MB per frame).
// ---------------------------------------------------------------------------

// Ensure PBOs exist and match the current frame dimensions.
// Called on the render thread (EGL context is current).
bool AndroidPlayer::ensurePBOs(int y_w, int y_h, int uv_w, int uv_h) {
    int y_sz  = y_w  * y_h;
    int uv_sz = uv_w * uv_h;

    bool need_recreate = (gl_pbo_y_sz_  != y_sz  ||
                          gl_pbo_uv_sz_ != uv_sz ||
                          gl_pbo_y_[0]  == 0);
    if (!need_recreate) return true;

    // Delete old PBOs
    if (gl_pbo_y_[0]) { glDeleteBuffers(6, gl_pbo_y_); }
    memset(gl_pbo_y_, 0, sizeof(gl_pbo_y_));
    gl_pbo_y_sz_  = 0;
    gl_pbo_uv_sz_ = 0;

    glGenBuffers(6, gl_pbo_y_);  // [0,1]=Y  [2,3]=U  [4,5]=V

    auto alloc = [&](GLuint id, int sz) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, id);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, sz, nullptr, GL_STREAM_DRAW);
    };
    alloc(gl_pbo_y_[0], y_sz);  alloc(gl_pbo_y_[1], y_sz);
    alloc(gl_pbo_y_[2], uv_sz); alloc(gl_pbo_y_[3], uv_sz);
    alloc(gl_pbo_y_[4], uv_sz); alloc(gl_pbo_y_[5], uv_sz);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    gl_pbo_y_sz_  = y_sz;
    gl_pbo_uv_sz_ = uv_sz;
    gl_pbo_idx_   = 0;
    LOGI("[PBO] Allocated 6 PBOs: Y=%d bytes UV=%d bytes", y_sz, uv_sz);
    return true;
}

// Upload one plane using the PBO double-buffer ping-pong.
// idx_write : PBO index we write CPU data into this frame
// idx_read  : PBO index the GPU reads from this frame (previous write)
static void uploadPlanePBO(GLenum tex_unit, GLuint tex_id,
                            GLuint pbo_write, GLuint pbo_read,
                            GLint  internal_fmt, GLenum fmt,
                            int tex_w, int tex_h,
                            const void* src, int sz,
                            bool size_changed) {
    // Step A: bind pbo_read and kick async upload to texture
    glActiveTexture(tex_unit);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_read);
    if (size_changed) {
        glTexImage2D(GL_TEXTURE_2D, 0, internal_fmt, tex_w, tex_h,
                     0, fmt, GL_UNSIGNED_BYTE, nullptr /* from PBO */);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_w, tex_h,
                        fmt, GL_UNSIGNED_BYTE, nullptr /* from PBO */);
    }

    // Step B: fill pbo_write with new CPU data via DMA mapping
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_write);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, sz, nullptr, GL_STREAM_DRAW); // orphan
    void* dst = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, sz,
                                  GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (dst) {
        memcpy(dst, src, sz);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    } else {
        // Fallback: upload synchronously if mapping fails
        glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, sz, src);
    }
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

int AndroidPlayer::renderFrame(void* y_data, void* u_data, void* v_data,
                               int y_linesize, int u_linesize, int v_linesize,
                               int width, int height,
                               int64_t* out_upload_ms, int64_t* out_max_upload_ms) {
    if (!y_data || width <= 0 || height <= 0) return -1;
    if (!gl_program_ || egl_surface_ == EGL_NO_SURFACE) return -1;

    int surface_w, surface_h;
    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        surface_w = surface_width_;
        surface_h = surface_height_;
    }
    if (surface_w <= 0 || surface_h <= 0) return -1;

    auto t0 = std::chrono::high_resolution_clock::now();

    int y_tex_w  = y_linesize  > 0 ? y_linesize  : width;
    int uv_w     = u_linesize  > 0 ? u_linesize  : width  / 2;
    int v_tex_w  = v_linesize  > 0 ? v_linesize  : width  / 2;
    int uv_h     = height / 2;
    int y_sz     = y_tex_w * height;
    int uv_sz    = uv_w    * uv_h;
    int v_sz     = v_tex_w * uv_h;

    bool size_changed = (width != gl_last_video_w_ || height != gl_last_video_h_);
    if (size_changed) {
        LOGI("[DIAG] Video size: %dx%d -> %dx%d | Y_stride=%d UV_stride=%d",
             gl_last_video_w_, gl_last_video_h_, width, height, y_tex_w, uv_w);
    }

    // --- Texture upload ---
    auto t_upload0 = std::chrono::high_resolution_clock::now();

    // Use PBO double-buffering for 4K (>= 1920x1080) to overlap CPU copy with GPU draw.
    // For smaller resolutions the overhead of PBO setup isn't worth it.
    const bool use_pbo = (width >= 1920) &&
                         ensurePBOs(y_tex_w, height, uv_w, uv_h);

    if (use_pbo) {
        int wi = gl_pbo_idx_;          // write index (0 or 1)
        int ri = 1 - wi;               // read  index
        // On the very first frame pbo_read is uninitialized: fall back to sync
        // upload for that one frame so the texture contains valid data.
        if (gl_pbo_first_frame_) {
            // First-frame sync upload so the texture is valid immediately
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, gl_tex_y_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, y_tex_w, height,
                         0, GL_LUMINANCE, GL_UNSIGNED_BYTE, y_data);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, gl_tex_u_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, uv_w, uv_h,
                         0, GL_LUMINANCE, GL_UNSIGNED_BYTE, u_data);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, gl_tex_v_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, v_tex_w, uv_h,
                         0, GL_LUMINANCE, GL_UNSIGNED_BYTE, v_data);
            // Pre-fill write PBOs for next frame
            auto fill_pbo = [](GLuint id, const void* src, int sz) {
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, id);
                glBufferData(GL_PIXEL_UNPACK_BUFFER, sz, nullptr, GL_STREAM_DRAW);
                void* dst = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, sz,
                                              GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
                if (dst) { memcpy(dst, src, sz); glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); }
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            };
            fill_pbo(gl_pbo_y_[wi],   y_data, y_sz);
            fill_pbo(gl_pbo_y_[2+wi], u_data, uv_sz);
            fill_pbo(gl_pbo_y_[4+wi], v_data, v_sz);
            gl_pbo_first_frame_ = false;
        } else {
            uploadPlanePBO(GL_TEXTURE0, gl_tex_y_,
                           gl_pbo_y_[wi],   gl_pbo_y_[ri],
                           GL_LUMINANCE, GL_LUMINANCE,
                           y_tex_w, height, y_data, y_sz, size_changed);
            uploadPlanePBO(GL_TEXTURE1, gl_tex_u_,
                           gl_pbo_y_[2+wi], gl_pbo_y_[2+ri],
                           GL_LUMINANCE, GL_LUMINANCE,
                           uv_w, uv_h, u_data, uv_sz, size_changed);
            uploadPlanePBO(GL_TEXTURE2, gl_tex_v_,
                           gl_pbo_y_[4+wi], gl_pbo_y_[4+ri],
                           GL_LUMINANCE, GL_LUMINANCE,
                           v_tex_w, uv_h, v_data, v_sz, size_changed);
        }
        gl_pbo_idx_ = 1 - gl_pbo_idx_; // ping-pong
    } else {
        // Standard synchronous upload (SD / HD content, or PBO not available)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gl_tex_y_);
        if (size_changed) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, y_tex_w, height,
                         0, GL_LUMINANCE, GL_UNSIGNED_BYTE, y_data);
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, y_tex_w, height,
                            GL_LUMINANCE, GL_UNSIGNED_BYTE, y_data);
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gl_tex_u_);
        if (size_changed) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, uv_w, uv_h,
                         0, GL_LUMINANCE, GL_UNSIGNED_BYTE, u_data);
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uv_w, uv_h,
                            GL_LUMINANCE, GL_UNSIGNED_BYTE, u_data);
        }
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, gl_tex_v_);
        if (size_changed) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, v_tex_w, uv_h,
                         0, GL_LUMINANCE, GL_UNSIGNED_BYTE, v_data);
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, v_tex_w, uv_h,
                            GL_LUMINANCE, GL_UNSIGNED_BYTE, v_data);
        }
    }

    auto t_upload1 = std::chrono::high_resolution_clock::now();
    int64_t upload_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        t_upload1 - t_upload0).count();
    if (out_upload_ms)     *out_upload_ms     += upload_ms;
    if (out_max_upload_ms && upload_ms > *out_max_upload_ms)
        *out_max_upload_ms = upload_ms;
    if (upload_ms > 10) {
        LOGW("[DIAG] Slow upload: %" PRId64 "ms | %dx%d Y_stride=%d pbo=%d",
             upload_ms, width, height, y_tex_w, use_pbo ? 1 : 0);
    }

    gl_last_video_w_ = width;
    gl_last_video_h_ = height;

    // --- Compute vertex / texcoord layout ---
    float y_u_scale  = (float)width      / (float)y_tex_w;
    float y_v_scale  = 1.0f;
    float uv_u_scale = (float)(width / 2) / (float)(uv_w > 0 ? uv_w : width / 2);

    float vx0 = -1.0f, vx1 = 1.0f, vy0 = -1.0f, vy1 = 1.0f;
    float tx0 = 0.0f,  tx1 = y_u_scale,  ty0 = 0.0f, ty1 = y_v_scale;
    float utx0 = 0.0f, utx1 = uv_u_scale, uty0 = 0.0f, uty1 = 1.0f;

    float video_aspect   = (float)width    / (float)height;
    float surface_aspect = (float)surface_w / (float)surface_h;

    if (aspect_ratio_mode_ == 0) {
        // FIT: letterbox / pillarbox
        if (video_aspect > surface_aspect) {
            float scale = surface_aspect / video_aspect;
            vy0 = -scale; vy1 = scale;
        } else {
            float scale = video_aspect / surface_aspect;
            vx0 = -scale; vx1 = scale;
        }
    } else {
        // FILL: crop via texcoord
        if (video_aspect > surface_aspect) {
            float ratio  = surface_aspect / video_aspect;
            float margin = (1.0f - ratio) * 0.5f;
            tx0  = margin * y_u_scale;    tx1  = (1.0f - margin) * y_u_scale;
            utx0 = margin * uv_u_scale;   utx1 = (1.0f - margin) * uv_u_scale;
        } else {
            float ratio  = video_aspect / surface_aspect;
            float margin = (1.0f - ratio) * 0.5f;
            ty0 = margin;  ty1  = 1.0f - margin;
            uty0 = margin; uty1 = 1.0f - margin;
        }
    }

    const float verts[] = {
        vx0, vy1,  tx0, ty0,  utx0, uty0,
        vx0, vy0,  tx0, ty1,  utx0, uty1,
        vx1, vy0,  tx1, ty1,  utx1, uty1,
        vx0, vy1,  tx0, ty0,  utx0, uty0,
        vx1, vy0,  tx1, ty1,  utx1, uty1,
        vx1, vy1,  tx1, ty0,  utx1, uty0,
    };

    // --- Draw ---
    glViewport(0, 0, surface_w, surface_h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(gl_program_);
    glUniform1i(gl_uniform_y_, 0);
    glUniform1i(gl_uniform_u_, 1);
    glUniform1i(gl_uniform_v_, 2);

    if (gl_attrib_pos_ < 0 || gl_attrib_tex_ < 0 || gl_attrib_tex_uv_ < 0) return -1;

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
        EGLint err = eglGetError();
        // EGL_BAD_SURFACE / EGL_CONTEXT_LOST are recoverable via re-init
        LOGE("[EGL] eglSwapBuffers failed: 0x%x", err);
        return -1;
    }

    // Cache last frame so we can redraw after a surface hot-swap
    {
        int y_stride  = y_linesize  > 0 ? y_linesize  : width;
        int uv_stride = u_linesize  > 0 ? u_linesize  : width / 2;
        int vs_stride = v_linesize  > 0 ? v_linesize  : width / 2;
        int y_cache_sz  = y_stride  * height;
        int u_cache_sz  = uv_stride * (height / 2);
        int v_cache_sz  = vs_stride * (height / 2);

        if (width != last_frame_width_ || height != last_frame_height_) {
            last_frame_y_.resize(y_cache_sz);
            last_frame_u_.resize(u_cache_sz);
            last_frame_v_.resize(v_cache_sz);
        }
        memcpy(last_frame_y_.data(), y_data, y_cache_sz);
        memcpy(last_frame_u_.data(), u_data, u_cache_sz);
        memcpy(last_frame_v_.data(), v_data, v_cache_sz);
        last_frame_width_    = width;
        last_frame_height_   = height;
        last_frame_y_stride_ = y_linesize;
        last_frame_u_stride_ = u_linesize;
        last_frame_v_stride_ = v_linesize;
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
}

// ========== OpenSL ES audio output ==========

bool AndroidPlayer::initAudioOutput(int sample_rate, int channels) {
    SLresult result;

    LOGI("Initializing audio output: %d Hz, %d channels", sample_rate, channels);

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

    result = (*engineEngine_)->CreateOutputMix(engineEngine_, &outputMixObject_, 0, nullptr, nullptr);
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
    
    // PCM buffer-queue data source
    SLDataLocator_AndroidSimpleBufferQueue loc_bufq = {
        SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2
    };

    // Map Hz -> OpenSL ES enum
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
    
    SLuint32 channel_mask;
    if (channels == 1) {
        channel_mask = SL_SPEAKER_FRONT_CENTER;
    } else {
        channel_mask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
        channels = 2; // clamp to stereo
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
    
    SLDataLocator_OutputMix loc_outmix = {SL_DATALOCATOR_OUTPUTMIX, outputMixObject_};
    SLDataSink audioSnk = {&loc_outmix, nullptr};

    // We request only BUFFERQUEUE; omitting VOLUME avoids Android AppOps CONTROL_AUDIO checks.
    const SLInterfaceID ids[1] = {SL_IID_BUFFERQUEUE};
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

    result = (*playerObject_)->GetInterface(playerObject_, SL_IID_PLAY, &playItf_);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to get play interface: %d", result);
        destroyAudioOutput();
        return false;
    }

    result = (*playerObject_)->GetInterface(playerObject_, SL_IID_BUFFERQUEUE, &bufferQueueItf_);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to get buffer queue interface: %d", result);
        destroyAudioOutput();
        return false;
    }

    result = (*bufferQueueItf_)->RegisterCallback(bufferQueueItf_, audioCallback, this);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to register callback: %d", result);
        destroyAudioOutput();
        return false;
    }
    
    // VolumeItf is intentionally omitted to avoid AppOps CONTROL_AUDIO permission checks.
    // Volume is managed entirely inside the core.
    volumeItf_ = nullptr;
    LOGI("Volume control: core-only (OpenSL ES VolumeItf disabled)");

    audio_sample_rate_ = sample_rate;
    audio_channels_    = channels;

    // Target ~5ms per callback (bytes = rate * ch * 2 bytes/sample * 0.005s)
    audio_buffer_size_ = (sample_rate * channels * 2 * 5) / 1000;
    audio_buffer_size_ = (audio_buffer_size_ + 3) & ~3;  // 4-byte align

    if (audio_buffer_size_ > MAX_AUDIO_BUFFER_SIZE) {
        audio_buffer_size_ = MAX_AUDIO_BUFFER_SIZE;
        LOGW("Audio buffer size capped to %d bytes", MAX_AUDIO_BUFFER_SIZE);
    }
    if (audio_buffer_size_ < 960) {
        audio_buffer_size_ = 960; // floor at ~10ms @ 48kHz stereo
    }

    LOGI("Audio buffer: %d bytes (%.1f ms)",
         audio_buffer_size_,
         (audio_buffer_size_ * 1000.0) / (sample_rate * channels * 2));

    memset(audio_buffer_, 0, audio_buffer_size_);
    (*bufferQueueItf_)->Enqueue(bufferQueueItf_, audio_buffer_, audio_buffer_size_);
    
    LOGI("Audio output initialized successfully with %d Hz, %d channels", sample_rate, channels);
    audio_active_ = true;
    return true;
}

void AndroidPlayer::destroyAudioOutput() {
    // Signal audio callback to stop accessing player_core_
    audio_active_ = false;

    // Stop playback first so no new callbacks are triggered
    if (playItf_) {
        (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_STOPPED);
    }

    // Acquire audio_mutex_ to ensure any in-flight callback has exited its critical section
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
    }

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
    // If player is being destroyed, output silence
    if (!audio_active_ || !player_core_ || audio_buffer_size_ == 0) {
        int sz = audio_buffer_size_ > 0 ? audio_buffer_size_ : 4096;
        memset(audio_buffer_, 0, sz);
        (*bq)->Enqueue(bq, audio_buffer_, sz);
        return;
    }
    
    std::lock_guard<std::mutex> lock(audio_mutex_);
    // Re-check after acquiring mutex
    if (!audio_active_ || !player_core_) {
        memset(audio_buffer_, 0, audio_buffer_size_);
        (*bq)->Enqueue(bq, audio_buffer_, audio_buffer_size_);
        return;
    }

    // Drain the core audio queue into the output buffer
    int total_bytes_read = 0;
    while (total_bytes_read < audio_buffer_size_) {
        int bytes_read = player_core_get_audio_data(
            player_core_,
            audio_buffer_ + total_bytes_read,
            audio_buffer_size_ - total_bytes_read);
        if (bytes_read > 0) {
            total_bytes_read += bytes_read;
        } else {
            break;
        }
    }

    audio_cb_count_++;
    int& callback_count = audio_cb_count_;
    int& underrun_count = audio_underrun_count_;
    int& partial_count  = audio_partial_count_;

    if (total_bytes_read > 0) {
        if (total_bytes_read < audio_buffer_size_) {
            partial_count++;
            // Zero-pad the tail (partial underrun)
            memset(audio_buffer_ + total_bytes_read, 0,
                   audio_buffer_size_ - total_bytes_read);
        }
    } else {
        underrun_count++;
        memset(audio_buffer_, 0, audio_buffer_size_);
    }

    // Print audio stats every 200 callbacks (~1s at 5ms/cb)
    if (callback_count % 200 == 0) {
        double pos = player_core_ ? player_core_get_position(player_core_) : 0.0;
        if (underrun_count > 0 || partial_count > 0) {
            LOGW("[audio] cb=%d pos=%.1fs underrun=%d partial=%d (last 200)",
                 callback_count, pos, underrun_count, partial_count);
        } else {
            LOGI("[audio] cb=%d pos=%.1fs ok", callback_count, pos);
        }
        underrun_count = 0;
        partial_count  = 0;
    }

    (*bq)->Enqueue(bq, audio_buffer_, audio_buffer_size_);
}
