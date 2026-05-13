#include "android_player.h"
#include "hxc_player_core_c_bridge.h"
#include <android/log.h>
#include <android/native_window.h>
#include <cstring>
#include <cmath>
#include <limits>
#include <algorithm>
#include <chrono>
#include <thread>
#include <inttypes.h>
#include <unistd.h>  // gettid()
// EGL_OPENGL_ES3_BIT may not be defined in older EGL headers
#ifndef EGL_OPENGL_ES3_BIT
#define EGL_OPENGL_ES3_BIT 0x00000040
#endif


#define LOG_TAG "HXCSDK"
// Runtime log level:
//   0 = ERROR only (release default)
//   1 = WARN + ERROR
//   2 = INFO + WARN + ERROR
//   3 = DEBUG + INFO + WARN + ERROR
#ifndef HXC_PLAYER_RUNTIME_LOG_LEVEL
#define HXC_PLAYER_RUNTIME_LOG_LEVEL 2
#endif

#if HXC_PLAYER_RUNTIME_LOG_LEVEL >= 3
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#else
#define LOGD(...) ((void)0)
#endif

#if HXC_PLAYER_RUNTIME_LOG_LEVEL >= 2
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) ((void)0)
#endif

#if HXC_PLAYER_RUNTIME_LOG_LEVEL >= 1
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#else
#define LOGW(...) ((void)0)
#endif

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Independent tags for focused troubleshooting (adb logcat -s HXCSDK_PERF HXCSDK_SYNC ...).
#define LOG_TAG_PERF   "HXCSDK_PERF"
#define LOG_TAG_SYNC   "HXCSDK_SYNC"
#define LOG_TAG_DECODE "HXCSDK_DECODE"
#define LOG_TAG_PBO    "HXCSDK_PBO"

#if HXC_PLAYER_RUNTIME_LOG_LEVEL >= 3
#define TAGD(TAG, ...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#else
#define TAGD(TAG, ...) ((void)0)
#endif

#if HXC_PLAYER_RUNTIME_LOG_LEVEL >= 2
#define TAGI(TAG, ...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#else
#define TAGI(TAG, ...) ((void)0)
#endif

#if HXC_PLAYER_RUNTIME_LOG_LEVEL >= 1
#define TAGW(TAG, ...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#else
#define TAGW(TAG, ...) ((void)0)
#endif

#define TAGE(TAG, ...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#define TAGI_RATE(TAG, N, ...) do { \
    static int _tag_rl_cnt = 0; \
    if (++_tag_rl_cnt % (N) == 1) { \
        TAGI(TAG, __VA_ARGS__); \
    } \
} while(0)

#define TAGW_RATE(TAG, N, ...) do { \
    static int _tag_rl_cnt = 0; \
    if (++_tag_rl_cnt % (N) == 1) { \
        TAGW(TAG, __VA_ARGS__); \
    } \
} while(0)

#define PERFI(...)      TAGI(LOG_TAG_PERF, __VA_ARGS__)
#define PERFW(...)      TAGW(LOG_TAG_PERF, __VA_ARGS__)
#define PERFI_RATE(...) TAGI_RATE(LOG_TAG_PERF, __VA_ARGS__)
#define PERFW_RATE(...) TAGW_RATE(LOG_TAG_PERF, __VA_ARGS__)
#define SYNCI(...)      TAGI(LOG_TAG_SYNC, __VA_ARGS__)
#define SYNCW(...)      TAGW(LOG_TAG_SYNC, __VA_ARGS__)
#define SYNCI_RATE(...) TAGI_RATE(LOG_TAG_SYNC, __VA_ARGS__)
#define SYNCW_RATE(...) TAGW_RATE(LOG_TAG_SYNC, __VA_ARGS__)
#define DECODEI(...)    TAGI(LOG_TAG_DECODE, __VA_ARGS__)
#define PBOD(...)       TAGD(LOG_TAG_PBO, __VA_ARGS__)
#define PBOI(...)       TAGI(LOG_TAG_PBO, __VA_ARGS__)
#define PBOW(...)       TAGW(LOG_TAG_PBO, __VA_ARGS__)
#define PBOI_RATE(...)  TAGI_RATE(LOG_TAG_PBO, __VA_ARGS__)
#define PBOW_RATE(...)  TAGW_RATE(LOG_TAG_PBO, __VA_ARGS__)

// Rate-limited logging: prints at most once every N calls.
// Usage: LOGI_RATE(100, "msg %d", val);
#define LOGI_RATE(N, ...) do { \
    static int _rl_cnt = 0; \
    if (++_rl_cnt % (N) == 1) { \
        LOGI(__VA_ARGS__); \
    } \
} while(0)
#define LOGD_RATE(N, ...) do { \
    static int _rl_cnt = 0; \
    if (++_rl_cnt % (N) == 1) { \
        LOGD(__VA_ARGS__); \
    } \
} while(0)
#define LOGW_RATE(N, ...) do { \
    static int _rl_cnt = 0; \
    if (++_rl_cnt % (N) == 1) { \
        LOGW(__VA_ARGS__); \
    } \
} while(0)

namespace {
constexpr float kMinPlaybackRate = 0.5f;
constexpr float kMaxPlaybackRate = 3.0f;
constexpr float kMaxPlaybackRateSnapEpsilon = 0.01f;

inline float normalize_playback_rate(float rate) {
    if (!std::isfinite(rate)) return 1.0f;
    if (rate < kMinPlaybackRate) return kMinPlaybackRate;
    if (rate > kMaxPlaybackRate) return kMaxPlaybackRate;
    // Snap near-upper-bound values to exact 3.0x so "3.0" never falls back
    // to 2.99x because of float precision / formatting jitter.
    if (std::fabs(rate - kMaxPlaybackRate) <= kMaxPlaybackRateSnapEpsilon) {
        return kMaxPlaybackRate;
    }
    return rate;
}

inline float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

inline float rgb_clip_penalty(float y, float u, float v) {
    float r = y + 1.402f * v;
    float g = y - 0.344f * u - 0.714f * v;
    float b = y + 1.772f * u;
    float penalty = 0.0f;
    penalty += std::fabs(r - clamp01(r));
    penalty += std::fabs(g - clamp01(g));
    penalty += std::fabs(b - clamp01(b));
    return penalty;
}

// Returns:
//   +1 => prefer NV21 (VU, swap UV)
//   -1 => prefer NV12 (UV, no swap)
//    0 => inconclusive
int estimate_uv_swap_vote(const uint8_t* y_data,
                          int y_stride,
                          const uint8_t* uv_data,
                          int uv_stride,
                          int width,
                          int height) {
    if (!y_data || !uv_data || y_stride <= 0 || uv_stride <= 1 || width <= 2 || height <= 2) {
        return 0;
    }
    const int uv_w = width / 2;
    const int uv_h = height / 2;
    if (uv_w <= 2 || uv_h <= 2 || uv_stride < uv_w * 2) {
        return 0;
    }

    const int sample_rows = std::min(uv_h, 8);
    const int sample_cols = std::min(uv_w, 12);
    float score_nv12 = 0.0f;
    float score_nv21 = 0.0f;
    int samples = 0;

    for (int sr = 0; sr < sample_rows; ++sr) {
        int r = (sr * uv_h) / sample_rows;
        const uint8_t* uv_row = uv_data + r * uv_stride;
        int y_row = std::min(height - 1, r * 2);
        const uint8_t* y_row_ptr = y_data + y_row * y_stride;
        for (int sc = 0; sc < sample_cols; ++sc) {
            int c = (sc * uv_w) / sample_cols;
            int uv_idx = c * 2;
            int y_col = std::min(width - 1, c * 2);
            float y = static_cast<float>(y_row_ptr[y_col]) / 255.0f;
            float u0 = static_cast<float>(uv_row[uv_idx]) / 255.0f - 0.5f;
            float v0 = static_cast<float>(uv_row[uv_idx + 1]) / 255.0f - 0.5f;
            score_nv12 += rgb_clip_penalty(y, u0, v0);
            score_nv21 += rgb_clip_penalty(y, v0, u0);
            samples++;
        }
    }

    if (samples <= 0) return 0;
    float avg_nv12 = score_nv12 / samples;
    float avg_nv21 = score_nv21 / samples;
    float diff = avg_nv12 - avg_nv21;
    const float eps = 0.015f;
    if (std::fabs(diff) <= eps) return 0;
    return (diff > 0.0f) ? +1 : -1;
}
}  // namespace

AndroidPlayer::AndroidPlayer()
    : player_core_(nullptr)
    , native_window_(nullptr)
    , pending_window_(nullptr)
    , window_changed_(false)
    , stop_requested_(false)
    , surface_width_(0)
    , surface_height_(0)
    , aspect_ratio_mode_(0)
    , decode_mode_(1) // Exo/IJK-like default: prefer hardware decode on Android
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
    , gl_uniform_uv_interleaved_(-1)
    , gl_uniform_uv_swap_(-1)
    , gl_attrib_pos_(-1)
    , gl_attrib_tex_(-1)
    , gl_attrib_tex_uv_(-1)
    , gl_last_video_w_(0)
    , gl_last_video_h_(0)
    , gl_last_uv_interleaved_(false)
    , gl_last_uv_swap_(false)
    , gl_last_uv_tex_w_(0)
    , gl_uv_swap_decided_(false)
    , gl_uv_swap_selected_(false)
    , gl_uv_swap_votes_(0)
    , gl_uv_swap_probe_budget_(24)
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

    // 4. Acquire audio_mutex_ to wait for any in-flight audio callback to finish,
    //    then mark audio as inactive and null out player_core_.
    //    Because onAudioData holds audio_mutex_ for its entire body (including
    //    swr_convert), taking this lock here guarantees no callback is running
    //    when we proceed to destroy the core.
    PlayerCoreHandle* core_to_destroy = nullptr;
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        audio_active_ = false;   // redundant but explicit
        core_to_destroy = player_core_;
        player_core_ = nullptr;
    }

    // 5. Destroy OpenSL ES engine.  Any callback that fires AFTER we release the
    //    lock above will see audio_active_==false / player_core_==nullptr and
    //    return silence without touching the core.
    destroyAudioOutput();

    // 6. Tear down player core - safe because audio_mutex_ already serialised us.
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
            surface_width_ = 0;
            surface_height_ = 0;
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

    // Always stop first so the core FSM is in a clean IDLE state before open.
    // This is critical for replay: when playback ends the core reaches a terminal
    // state (state==-1) and a fresh open() will fail unless we reset it first.
    int cur_state = player_core_get_state(player_core_);
    if (cur_state != 0) { // 0 == IDLE
        LOGI("[open] pre-stop core (state=%d) before open", cur_state);

        // 1. Stop OpenSL ES output to prevent new callbacks from being queued.
        audio_start_pending_.store(false, std::memory_order_release);
        if (playItf_) {
            (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_STOPPED);
        }

        // 2. Acquire audio_mutex_ to wait for any currently-executing callback
        //    (which holds this lock for its full body) to finish before we call
        //    player_core_stop(), which resets/frees the SwrContext.
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            // Lock acquired means no callback is in swr_convert right now.
        }

        // 3. Now safe to stop/reset the player core.
        player_core_stop(player_core_);
    }

    DECODEI("evt=open method=openURL decode_mode=%s",
            decode_mode_ == 1 ? "hardware" : "software");
    player_core_set_decode_mode(player_core_,
                                decode_mode_ == 1 ? PLAYER_DECODE_MODE_HARDWARE
                                                  : PLAYER_DECODE_MODE_SOFTWARE);

    int result = player_core_open_with_start_position(player_core_, url, start_position);
    
    if (result == 0) {
        LOGI("[open] openURL OK");
        ensureAudioOutputForCurrentStream();
        player_core_pause(player_core_);
        bool hw_active = player_core_is_video_hardware_decoding(player_core_) != 0;
        const char* decode_diag = player_core_get_video_decode_diagnostic(player_core_);
        DECODEI("evt=open_result method=openURL requested=%s hw_active=%d final_mode=%s diag=%s",
                decode_mode_ == 1 ? "hardware" : "software",
                hw_active ? 1 : 0,
                hw_active ? "hardware" : "software",
                decode_diag ? decode_diag : "");
        // Reset sync state for the new stream
        sync_warmup_frames_.store(20, std::memory_order_release);
        last_sync_video_pts_ = std::numeric_limits<double>::quiet_NaN();
        audio_rebuffer_pending_.store(false, std::memory_order_release);
        audio_rebuffer_deadline_ms_ = 0;
        audio_rebuffer_paused_at_ms_ = 0;
        audio_rebuffer_min_resume_at_ms_ = 0;
        seek_target_sec_.store(-1.0, std::memory_order_release);
        seek_from_sec_.store(-1.0, std::memory_order_release);
        seek_fast_catchup_frames_.store(0, std::memory_order_release);
        seek_catchup_deadline_ms_ = 0;
        seek_lower_bound_active_.store(false, std::memory_order_release);
        seek_lower_bound_deadline_ms_ = 0;
        seek_recovery_active_.store(false, std::memory_order_release);
        seek_recovery_deadline_ms_ = 0;
        seek_audio_wait_video_.store(false, std::memory_order_release);
        seek_audio_wait_deadline_ms_ = 0;
        seek_started_at_ms_ = 0;
        seek_lower_bound_drop_count_ = 0;
        consecutive_drop_count_ = 0;
        severe_lag_start_ms_ = 0;
        last_soft_reanchor_ms_ = 0;
        soft_reanchor_count_ = 0;
        severe_lag_audio_pause_start_ms_ = 0;
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
    DECODEI("evt=open method=openWithCustomHTTP decode_mode=%s",
            decode_mode_ == 1 ? "hardware" : "software");
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
        bool hw_active = player_core_is_video_hardware_decoding(player_core_) != 0;
        const char* decode_diag = player_core_get_video_decode_diagnostic(player_core_);
        DECODEI("evt=open_result method=openWithCustomHTTP requested=%s hw_active=%d final_mode=%s diag=%s",
                decode_mode_ == 1 ? "hardware" : "software",
                hw_active ? 1 : 0,
                hw_active ? "hardware" : "software",
                decode_diag ? decode_diag : "");
        sync_warmup_frames_.store(20, std::memory_order_release);
        last_sync_video_pts_ = std::numeric_limits<double>::quiet_NaN();
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
    DECODEI("evt=open method=openWithCustomFile decode_mode=%s",
            decode_mode_ == 1 ? "hardware" : "software");
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
        bool hw_active = player_core_is_video_hardware_decoding(player_core_) != 0;
        const char* decode_diag = player_core_get_video_decode_diagnostic(player_core_);
        DECODEI("evt=open_result method=openWithCustomFile requested=%s hw_active=%d final_mode=%s diag=%s",
                decode_mode_ == 1 ? "hardware" : "software",
                hw_active ? 1 : 0,
                hw_active ? "hardware" : "software",
                decode_diag ? decode_diag : "");
        sync_warmup_frames_.store(20, std::memory_order_release);
        last_sync_video_pts_ = std::numeric_limits<double>::quiet_NaN();
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
    static std::atomic<bool> secure_param_warned{false};
    bool has_extra_secure_args =
        (auth_token && *auth_token) ||
        (video_id && *video_id) ||
        (device_id && *device_id) ||
        (secret_id && *secret_id) ||
        (nonce && *nonce) ||
        (play_session_id && *play_session_id) ||
        session_expire_at_ms > 0 ||
        key_mode != 0 ||
        (key_material_b64 && *key_material_b64) ||
        (key_iv_hex && *key_iv_hex);
    if (has_extra_secure_args &&
        !secure_param_warned.exchange(true, std::memory_order_acq_rel)) {
        LOGW("[secure] openWithSecureSession currently applies secure_headers only; "
             "other secure args are accepted for API compatibility but not yet forwarded to core");
    }
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
    DECODEI("evt=open method=openWithSecureSession decode_mode=%s",
            decode_mode_ == 1 ? "hardware" : "software");
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
        bool hw_active = player_core_is_video_hardware_decoding(player_core_) != 0;
        const char* decode_diag = player_core_get_video_decode_diagnostic(player_core_);
        DECODEI("evt=open_result method=openWithSecureSession requested=%s hw_active=%d final_mode=%s diag=%s",
                decode_mode_ == 1 ? "hardware" : "software",
                hw_active ? 1 : 0,
                hw_active ? "hardware" : "software",
                decode_diag ? decode_diag : "");
        sync_warmup_frames_.store(20, std::memory_order_release);
        last_sync_video_pts_ = std::numeric_limits<double>::quiet_NaN();
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
    DECODEI("evt=play_start decode_mode=%s hw_active=%d",
            decode_mode_ == 1 ? "hardware" : "software",
            player_core_is_video_hardware_decoding(player_core_) ? 1 : 0);
    SYNCI("evt=play_start pos=%.3f rate=%.2f",
          player_core_get_position(player_core_),
          player_core_get_playback_rate(player_core_));
    player_core_play(player_core_);
    render_cv_.notify_one();
    audio_rebuffer_pending_.store(false, std::memory_order_release);
    audio_rebuffer_deadline_ms_ = 0;
    audio_rebuffer_paused_at_ms_ = 0;
    audio_rebuffer_min_resume_at_ms_ = 0;
    seek_recovery_active_.store(false, std::memory_order_release);
    seek_recovery_deadline_ms_ = 0;
    seek_audio_wait_video_.store(false, std::memory_order_release);
    seek_audio_wait_deadline_ms_ = 0;
    seek_started_at_ms_ = 0;
    seek_lower_bound_drop_count_ = 0;
    consecutive_drop_count_ = 0;
    severe_lag_start_ms_ = 0;
    severe_lag_audio_pause_start_ms_ = 0;

    if (playItf_) {
        // Defer audio start until the first video frame is rendered to prevent
        // audible sound before any picture (especially noticeable in dual-player).
        // A slightly longer deadline reduces "loading hides then black frame" on first play.
        audio_start_pending_.store(true, std::memory_order_release);
        audio_start_deadline_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() + 1200;
        LOGI("[ctrl] play: audio deferred until first video frame (deadline +1200ms)");
    } else {
        LOGD("No audio interface (audio disabled)");
    }
    LOGI("[ctrl] play: dispatched to core + audio");
}

void AndroidPlayer::pause() {
    if (!player_core_) return;

    LOGI("[ctrl] pause: state=%d pos=%.3f", player_core_get_state(player_core_), player_core_get_position(player_core_));
    // Cancel any pending deferred audio start.
    audio_start_pending_.store(false, std::memory_order_release);
    audio_rebuffer_pending_.store(false, std::memory_order_release);
    audio_rebuffer_deadline_ms_ = 0;
    audio_rebuffer_paused_at_ms_ = 0;
    audio_rebuffer_min_resume_at_ms_ = 0;
    seek_recovery_active_.store(false, std::memory_order_release);
    seek_recovery_deadline_ms_ = 0;
    seek_audio_wait_video_.store(false, std::memory_order_release);
    seek_audio_wait_deadline_ms_ = 0;
    seek_started_at_ms_ = 0;
    seek_lower_bound_drop_count_ = 0;
    consecutive_drop_count_ = 0;
    severe_lag_start_ms_ = 0;
    severe_lag_audio_pause_start_ms_ = 0;
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
    audio_start_pending_.store(false, std::memory_order_release);
    audio_rebuffer_pending_.store(false, std::memory_order_release);
    audio_rebuffer_deadline_ms_ = 0;
    audio_rebuffer_paused_at_ms_ = 0;
    audio_rebuffer_min_resume_at_ms_ = 0;
    seek_recovery_active_.store(false, std::memory_order_release);
    seek_recovery_deadline_ms_ = 0;
    seek_audio_wait_video_.store(false, std::memory_order_release);
    seek_audio_wait_deadline_ms_ = 0;
    seek_started_at_ms_ = 0;
    seek_lower_bound_drop_count_ = 0;
    consecutive_drop_count_ = 0;
    severe_lag_start_ms_ = 0;
    severe_lag_audio_pause_start_ms_ = 0;
    has_pending_playback_completed_.store(false, std::memory_order_release);

    // Stop OpenSL ES first to prevent new callbacks from being queued,
    // then acquire audio_mutex_ to wait for any running callback to finish,
    // then stop the core (which resets the SwrContext used by swr_convert).
    if (playItf_) {
        (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_STOPPED);
    }
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        // Holding the lock means no callback body is executing right now.
    }
    player_core_stop(player_core_);
}

void AndroidPlayer::seekTo(double position) {
    if (!player_core_) return;

    double seek_from = player_core_get_position(player_core_);
    double seek_span = std::fabs(position - seek_from);
    bool very_large_seek = seek_span > 180.0;
    LOGI("[ctrl] seekTo: %.3fs (current pos=%.3f state=%d)", position, seek_from, player_core_get_state(player_core_));
    seek_just_happened_.store(true, std::memory_order_release);
    sync_warmup_frames_.store(40, std::memory_order_release); // wider warmup after seek
    seek_from_sec_.store(seek_from, std::memory_order_release);
    seek_target_sec_.store(position, std::memory_order_release);
    // 约 1 秒左右的渲染 tick 追赶窗口，避免 seek 后先看到大量旧帧。
    seek_fast_catchup_frames_.store(very_large_seek ? 128 : 96, std::memory_order_release);
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    double playback_rate = player_core_get_playback_rate(player_core_);
    if (playback_rate <= 0.0) playback_rate = 1.0;
    seek_started_at_ms_ = now;
    seek_lower_bound_drop_count_ = 0;
    // Clear stale redraw cache on render thread before presenting post-seek frames.
    clear_last_frame_cache_pending_.store(true, std::memory_order_release);
    seek_catchup_deadline_ms_ = now + (very_large_seek ? 1200 : 900);
    seek_lower_bound_active_.store(true, std::memory_order_release);
    // Avoid long black/frozen waits after large forward seeks on 4K.
    seek_lower_bound_deadline_ms_ = now + (very_large_seek ? 1700 : 2200);
    seek_recovery_active_.store(true, std::memory_order_release);
    seek_recovery_deadline_ms_ = now + (very_large_seek ? 3600 : 4200);
    seek_audio_wait_video_.store(true, std::memory_order_release);
    // 兼顾“出画速度”和“首帧同步”：保留同步窗口，但避免超长等待造成卡死体感。
    int64_t seek_audio_wait_ms = very_large_seek ? 2600 : 3000;
    if (seek_span > 120.0) {
        seek_audio_wait_ms += 300;
    }
    if (playback_rate >= 2.0) {
        seek_audio_wait_ms += 400;
    }
    seek_audio_wait_deadline_ms_ = now + seek_audio_wait_ms;
    audio_rebuffer_pending_.store(false, std::memory_order_release);
    audio_rebuffer_deadline_ms_ = 0;
    audio_rebuffer_paused_at_ms_ = 0;
    audio_rebuffer_min_resume_at_ms_ = 0;
    if (playItf_) {
        std::lock_guard<std::mutex> audio_lock(audio_mutex_);
        SLresult r = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PAUSED);
        if (bufferQueueItf_) {
            SLresult clear_r = (*bufferQueueItf_)->Clear(bufferQueueItf_);
            LOGI("[sync] seek pause audio waiting first target frame: pause=%d clear=%d", r, clear_r);
        } else {
            LOGI("[sync] seek pause audio waiting first target frame: pause=%d clear=skip(no_bq)", r);
        }
    }
    consecutive_drop_count_ = 0;
    severe_lag_start_ms_ = 0;
    severe_lag_audio_pause_start_ms_ = 0;
    last_sync_video_pts_ = std::numeric_limits<double>::quiet_NaN();
    player_core_seek(player_core_, position);
    render_cv_.notify_one();
}

void AndroidPlayer::setPlaybackRate(float rate) {
    if (!player_core_) return;

    float previous_rate = requested_playback_rate_.load(std::memory_order_relaxed);
    float normalized_rate = normalize_playback_rate(rate);
    LOGD("Set playback rate: req=%f normalized=%f", rate, normalized_rate);
    SYNCI("evt=playback_rate_request req=%.3f normalized=%.3f", rate, normalized_rate);
    requested_playback_rate_.store(normalized_rate, std::memory_order_relaxed);
    if (previous_rate >= 2.0f && normalized_rate <= 1.5f) {
        // Allow fresh recovery opportunities after a big rate drop (e.g. 2.75x -> 1.25x).
        soft_reanchor_count_ = 0;
        severe_lag_start_ms_ = 0;
        severe_lag_audio_pause_start_ms_ = 0;
        last_soft_reanchor_ms_ = 0;
        SYNCI("evt=reanchor_budget_reset reason=rate_drop prev=%.3f now=%.3f",
              previous_rate, normalized_rate);
    }
    player_core_set_playback_rate(player_core_, normalized_rate);
}

void AndroidPlayer::setVolume(float volume) {
    if (!player_core_) return;
    float prev = current_volume_.exchange(volume, std::memory_order_relaxed);
    LOGD("Set volume: %f (core only)", volume);
    // Volume is applied inside the core; we do not use OpenSL ES VolumeItf
    // to avoid triggering Android AppOps CONTROL_AUDIO permission checks.
    player_core_set_volume(player_core_, volume);

    // If volume is raised from 0 to non-zero while the player is running,
    // ensure the OpenSL ES audio output is actually playing (it may have been
    // skipped during the deferred-start path when the player was muted).
    // Guard with core playing state to avoid resuming OpenSL while app is paused.
    if (prev <= 0.0f
        && volume > 0.0f
        && playItf_
        && audio_active_
        && player_core_is_playing(player_core_)) {
        SLuint32 state = 0;
        if ((*playItf_)->GetPlayState(playItf_, &state) == SL_RESULT_SUCCESS &&
            state != SL_PLAYSTATE_PLAYING) {
            (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PLAYING);
            LOGI("[ctrl] audio resumed on volume un-mute");
        }
    }
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
    DECODEI("evt=set_decode_mode decode_mode=%s", decode_mode_ == 1 ? "hardware" : "software");
    if (player_core_) {
        int core_state = player_core_get_state(player_core_);
        if (core_state != 0) {
            LOGW("[decode] decode mode changed while state=%d; new mode applies on next open", core_state);
        }
    }
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
    bool core_loading = is_loading_.load(std::memory_order_acquire);
    bool seek_loading =
        seek_audio_wait_video_.load(std::memory_order_acquire) ||
        seek_recovery_active_.load(std::memory_order_acquire) ||
        seek_lower_bound_active_.load(std::memory_order_acquire);
    // Avoid hiding loading before the first post-play frame is ready.
    bool first_frame_loading = audio_start_pending_.load(std::memory_order_acquire);
    return core_loading || seek_loading || first_frame_loading;
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
    "uniform int u_uv_interleaved;\n"
    "uniform int u_uv_swap;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    float y = texture(u_tex_y, v_texcoord).r;\n"
    "    float u;\n"
    "    float v;\n"
    "    if (u_uv_interleaved == 1) {\n"
    "        vec2 uv = texture(u_tex_u, v_texcoord_uv).rg;\n"
    "        if (u_uv_swap == 1) {\n"
    "            u = uv.g - 0.5;\n"
    "            v = uv.r - 0.5;\n"
    "        } else {\n"
    "            u = uv.r - 0.5;\n"
    "            v = uv.g - 0.5;\n"
    "        }\n"
    "    } else {\n"
    "        u = texture(u_tex_u, v_texcoord_uv).r - 0.5;\n"
    "        v = texture(u_tex_v, v_texcoord_uv).r - 0.5;\n"
    "    }\n"
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
    gl_uniform_uv_interleaved_ = glGetUniformLocation(gl_program_, "u_uv_interleaved");
    gl_uniform_uv_swap_ = glGetUniformLocation(gl_program_, "u_uv_swap");
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
    gl_last_uv_interleaved_ = false;
    gl_last_uv_swap_ = false;
    gl_last_uv_tex_w_ = 0;
    gl_uv_swap_decided_ = false;
    gl_uv_swap_selected_ = false;
    gl_uv_swap_votes_ = 0;
    gl_uv_swap_probe_budget_ = 24;
}

void AndroidPlayer::redrawLastFrame() {
    if (last_frame_width_ <= 0 || last_frame_height_ <= 0 ||
        last_frame_y_.empty() || last_frame_u_.empty()) {
        return;
    }
    if (last_frame_v_stride_ > 0 && last_frame_v_.empty()) {
        return;
    }
    if (!gl_program_ || egl_surface_ == EGL_NO_SURFACE) return;
    void* cache_v = (last_frame_v_stride_ > 0 && !last_frame_v_.empty()) ? last_frame_v_.data() : nullptr;
    renderFrame(last_frame_y_.data(), last_frame_u_.data(), cache_v,
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
    PERFI("evt=render_loop_start tid=%d", (int)gettid());

    bool    surface_ready   = false;
    int     frame_count     = 0;
    int     empty_count     = 0;
    bool    in_empty_streak = false;
    int64_t empty_start_ms  = 0;
    // Software-decode 4K high-rate safeguard:
    // if severe lag pause happens repeatedly in a short window, apply a
    // temporary rate cap to pull A/V back into a recoverable range.
    int64_t adaptive_rate_cap_until_ms = 0;
    float   adaptive_rate_cap_value = 0.0f;
    int64_t adaptive_rate_cap_recovery_start_ms = 0;
    // For software 4K over-2x playback, downgrade rate in steps when sustained lag is detected.
    int64_t sw4k_over2_last_lag_ms = 0;
    int     sw4k_over2_lag_count = 0;
    // After seek lower-bound hit, some devices may report a temporary clock rollback
    // (audio clock catches up from an older anchor), producing large positive delay
    // and a short "hold/freeze". Keep a brief bypass window to avoid that freeze.
    int64_t post_seek_ahead_bypass_until_ms = 0;
    int post_seek_bypass_skip_count = 0;
    int seek_resume_stable_frames = 0;
    // Sync-stall watchdog: if pts/clock stay almost unchanged for too long while
    // playback is active, force one consume+display to break potential deadlock.
    double stall_watchdog_last_pts = std::numeric_limits<double>::quiet_NaN();
    double stall_watchdog_last_clk = std::numeric_limits<double>::quiet_NaN();
    int64_t stall_watchdog_since_ms = 0;
    int64_t stall_watchdog_last_break_ms = 0;
    const int64_t kSyncStallWatchdogMs = 1200;
    const int64_t kSyncStallWatchdogCooldownMs = 450;
    const double kSyncStallWatchdogEps = 0.002;

    int64_t total_render_ms = 0, total_upload_ms = 0;
    int64_t max_render_ms   = 0, max_upload_ms   = 0;
    const int kDiagInterval = 60;

    const double kSyncThreshold = 0.050; // 50 ms: drop if video is behind
    const double kMaxAhead      = 2.000; // 2 s:  hold if video is too far ahead

    // Adaptive wait: shorten when we know there's work to do soon.
    // Default 16ms (~60fps). Lengthened when queue is empty (decoder filling).
    int wait_ms = 16;

    auto now_ms = []() -> int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    };

    while (true) {
        // --- Wait for work, surface change, or stop ---
        {
            std::unique_lock<std::mutex> lock(render_mutex_);

            if (!window_changed_) {
                render_cv_.wait_for(lock, std::chrono::milliseconds(wait_ms),
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

        if (clear_last_frame_cache_pending_.exchange(false, std::memory_order_acq_rel)) {
            last_frame_y_.clear();
            last_frame_u_.clear();
            last_frame_v_.clear();
            last_frame_width_ = 0;
            last_frame_height_ = 0;
            last_frame_y_stride_ = 0;
            last_frame_u_stride_ = 0;
            last_frame_v_stride_ = 0;
            last_frame_cache_ms_ = 0;
            LOGI("[sync] clear last-frame cache after seek");
        }

        // --- Fetch next decoded video frame ---
        VideoFrameDataC frame_data;
        int get_result = player_core_get_video_frame(player_core_, &frame_data);

        if (get_result == 0) {
            if (in_empty_streak) {
                // Hot path in dual-player scenes: keep as debug + rate-limited.
                LOGD_RATE(30, "[render] buffer refilled after %" PRId64 "ms (empty_cnt=%d) frame=%dx%d",
                          now_ms() - empty_start_ms, empty_count,
                          frame_data.width, frame_data.height);
                in_empty_streak = false;
            }
            empty_count = 0;

            double pts   = frame_data.pts;
            // Subtract estimated hardware output-queue latency so we compare
            // video PTS against "what the user is actually hearing", not "what
            // has been submitted to the OpenSL ES driver".  Mirrors iOS logic.
            double clock = player_core_get_position(player_core_);
            if (audio_output_latency_sec_ > 0.0) {
                clock -= audio_output_latency_sec_;
                if (clock < 0.0) clock = 0.0;
            }
            // seek_audio_wait_video_ 期间音频被 Android 层暂停，但 Core 的 audio_clock
            // 仍按真实时间流逝（paused=false），导致 clock 越来越大、delay 越来越负，
            // 所有恢复门控条件永远无法满足（冻结根因）。
            // 修复：等待期间改用 seek_target_sec_ 作为时钟基准，使 delay 仅反映
            // 视频帧与目标 seek 位置的差距，不受等待时长影响。
            if (seek_audio_wait_video_.load(std::memory_order_acquire)) {
                double seek_target_now = seek_target_sec_.load(std::memory_order_acquire);
                if (seek_target_now >= 0.0) {
                    clock = seek_target_now;
                }
            }
            double delay = pts - clock;
            int64_t now = now_ms();
            double playback_rate = player_core_get_playback_rate(player_core_);
            if (playback_rate <= 0.0) playback_rate = 1.0;
            bool core_playing_now = player_core_is_playing(player_core_);
            float requested_rate = requested_playback_rate_.load(std::memory_order_relaxed);
            if (requested_rate <= 0.0f) requested_rate = (float)playback_rate;
            requested_rate = normalize_playback_rate(requested_rate);

            bool snapshot_stalled = false;
            if (!std::isnan(stall_watchdog_last_pts) && !std::isnan(stall_watchdog_last_clk)) {
                snapshot_stalled =
                    std::fabs(pts - stall_watchdog_last_pts) <= kSyncStallWatchdogEps &&
                    std::fabs(clock - stall_watchdog_last_clk) <= kSyncStallWatchdogEps;
            }
            if (snapshot_stalled) {
                if (stall_watchdog_since_ms == 0) stall_watchdog_since_ms = now;
            } else {
                stall_watchdog_since_ms = now;
            }
            stall_watchdog_last_pts = pts;
            stall_watchdog_last_clk = clock;

            bool should_display = false;
            bool should_consume = false;
            bool in_seek_recovery = seek_recovery_active_.load(std::memory_order_acquire);
            bool likely_4k = (frame_data.width >= 3840 || gl_last_video_w_ >= 3840 || gl_last_video_h_ >= 2160);
            bool hw_decode_active = player_core_is_video_hardware_decoding(player_core_) != 0;
            bool sw_decode_4k = likely_4k && !hw_decode_active;
            if (adaptive_rate_cap_until_ms > 0 && now >= adaptive_rate_cap_until_ms) {
                SYNCI("evt=adaptive_rate_cap_expire cap=%.2f req=%.2f delay=%.3f",
                      adaptive_rate_cap_value, requested_rate, delay);
                adaptive_rate_cap_until_ms = 0;
                adaptive_rate_cap_value = 0.0f;
                adaptive_rate_cap_recovery_start_ms = 0;
            }
            float target_rate = requested_rate;
            if (sw_decode_4k && adaptive_rate_cap_until_ms > now &&
                adaptive_rate_cap_value > 0.0f && requested_rate > adaptive_rate_cap_value) {
                target_rate = adaptive_rate_cap_value;
            }
            // Keep playback rate stable, but allow temporary cap for software 4K severe-lag recovery.
            if (std::fabs(playback_rate - target_rate) > 0.005f) {
                player_core_set_playback_rate(player_core_, target_rate);
                playback_rate = target_rate;
            }
            bool high_rate_4k = (playback_rate >= 2.0f) && likely_4k;
            bool ultra_high_rate_4k = high_rate_4k && playback_rate >= 2.5f;
            bool very_high_rate_4k = high_rate_4k && playback_rate >= 2.75f;
            bool mid_rate_4k = false; // rollback: avoid mid-rate (1.25~1.75x) aggressive sync policy
            SYNCI_RATE(120, "evt=sync_snapshot pts=%.3f clk=%.3f delay=%.3f rate=%.2f video_w=%d video_h=%d is_4k=%d is_high_rate_4k=%d is_ultra_high_rate_4k=%d",
                       pts, clock, delay, playback_rate, frame_data.width, frame_data.height,
                       likely_4k ? 1 : 0, high_rate_4k ? 1 : 0, ultra_high_rate_4k ? 1 : 0);

            // Soft re-anchor safeguard: if we stay severely behind for too long under
            // 4K high/mid-rate playback, perform one bounded seek near audio clock to
            // break out of endless drop loops (especially after rate changes).
            bool reanchor_candidate_4k = likely_4k && playback_rate >= 2.0f;
            double reanchor_delay_threshold = -3.2;
            int64_t reanchor_lag_persistent_ms = 3000;
            int64_t reanchor_cooldown_ms = 6000;
            int reanchor_budget = 3;
            if (very_high_rate_4k) {
                // Tencent-like: at >=2.75x prioritize fast convergence over long-tail smoothness.
                reanchor_delay_threshold = -2.6;
                reanchor_lag_persistent_ms = 1200;
                reanchor_cooldown_ms = 6500;
                reanchor_budget = 3;
            } else if (ultra_high_rate_4k) {
                reanchor_delay_threshold = -3.6;
                reanchor_lag_persistent_ms = 1600;
                reanchor_cooldown_ms = 8000;
                reanchor_budget = 2;
            } else if (high_rate_4k) {
                reanchor_delay_threshold = -4.0;
                reanchor_lag_persistent_ms = 2600;
                reanchor_cooldown_ms = 7000;
            }

            if (!in_seek_recovery && reanchor_candidate_4k && delay < reanchor_delay_threshold &&
                player_core_is_playing(player_core_)) {
                if (severe_lag_start_ms_ == 0) severe_lag_start_ms_ = now;
                bool lag_persistent = (now - severe_lag_start_ms_) >= reanchor_lag_persistent_ms;
                bool reanchor_cooldown_ok = (last_soft_reanchor_ms_ == 0) ||
                                            (now - last_soft_reanchor_ms_) >= reanchor_cooldown_ms;
                bool reanchor_budget_ok = soft_reanchor_count_ < reanchor_budget;
                if (lag_persistent && reanchor_cooldown_ok && reanchor_budget_ok) {
                    double target = clock - 0.15;
                    if (target < 0.0) target = 0.0;
                    seek_just_happened_.store(true, std::memory_order_release);
                    seek_target_sec_.store(target, std::memory_order_release);
                    seek_lower_bound_active_.store(true, std::memory_order_release);
                    seek_lower_bound_deadline_ms_ = now + 3500;
                    seek_recovery_active_.store(true, std::memory_order_release);
                    seek_recovery_deadline_ms_ = now + 4500;
                    seek_audio_wait_video_.store(true, std::memory_order_release);
                    seek_audio_wait_deadline_ms_ = now + 4200;
                    seek_fast_catchup_frames_.store(36, std::memory_order_release);
                    sync_warmup_frames_.store(36, std::memory_order_release);
                    if (playItf_ && current_volume_.load(std::memory_order_relaxed) > 0.0f) {
                        (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PAUSED);
                    }
                    player_core_seek(player_core_, target);
                    render_cv_.notify_one();
                    last_soft_reanchor_ms_ = now;
                    severe_lag_start_ms_ = 0;
                    soft_reanchor_count_++;
                    LOGW("[sync] soft re-anchor seek: target=%.3f clk=%.3f delay=%.3f count=%d",
                         target, clock, delay, soft_reanchor_count_);
                    SYNCW("evt=soft_reanchor_seek target=%.3f clk=%.3f delay=%.3f count=%d rate=%.2f",
                          target, clock, delay, soft_reanchor_count_, playback_rate);
                    continue;
                }
            } else if (!in_seek_recovery) {
                severe_lag_start_ms_ = 0;
            }

            // Exo-like catch-up guard: if video queue is not empty but 4K video is
            // still far behind for a sustained period, pause audio temporarily so
            // users don't hear "audio keeps moving while picture is frozen".
            if (!in_seek_recovery && !seek_audio_wait_video_.load(std::memory_order_acquire) &&
                !audio_rebuffer_pending_.load(std::memory_order_acquire) &&
                player_core_is_playing(player_core_) && high_rate_4k) {
                double lag_pause_threshold = very_high_rate_4k ? -2.2 : ((playback_rate >= 2.5) ? -2.5 : ((playback_rate >= 2.0) ? -2.8 : -2.2));
                int64_t lag_pause_trigger_ms = very_high_rate_4k ? 220 : ((playback_rate >= 2.5) ? 320 : ((playback_rate >= 2.0) ? 420 : 700));
                int64_t lag_pause_fallback_ms = very_high_rate_4k ? 1700 : ((playback_rate >= 2.0) ? 1400 : 1800);
                int64_t lag_pause_min_hold_ms = very_high_rate_4k ? 1100 : ((playback_rate >= 2.5) ? 900 : ((playback_rate >= 2.0) ? 800 : 600));
                if (delay <= lag_pause_threshold) {
                    if (severe_lag_audio_pause_start_ms_ == 0) severe_lag_audio_pause_start_ms_ = now;
                    int64_t severe_lag_ms = now - severe_lag_audio_pause_start_ms_;
                    if (severe_lag_ms >= lag_pause_trigger_ms &&
                        playItf_ && current_volume_.load(std::memory_order_relaxed) > 0.0f) {
                        SLresult r = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PAUSED);
                        if (r == SL_RESULT_SUCCESS) {
                            audio_rebuffer_pending_.store(true, std::memory_order_release);
                            audio_rebuffer_paused_at_ms_ = now;
                            audio_rebuffer_min_resume_at_ms_ = now + lag_pause_min_hold_ms;
                            audio_rebuffer_deadline_ms_ = now + lag_pause_fallback_ms;
                            SYNCW("evt=severe_lag_pause_audio lag_ms=%" PRId64 " delay=%.3f rate=%.2f trigger_ms=%" PRId64 " hold_ms=%" PRId64 " fallback_ms=%" PRId64,
                                  severe_lag_ms, delay, playback_rate, lag_pause_trigger_ms, lag_pause_min_hold_ms, lag_pause_fallback_ms);
                            if (sw_decode_4k && requested_rate > 2.0f) {
                                const int64_t over2_window_ms = 5000;
                                if (sw4k_over2_last_lag_ms > 0 && (now - sw4k_over2_last_lag_ms) <= over2_window_ms) {
                                    sw4k_over2_lag_count++;
                                } else {
                                    sw4k_over2_lag_count = 1;
                                }
                                sw4k_over2_last_lag_ms = now;

                                float new_cap = 0.0f;
                                const char* reason = "";
                                if (sw4k_over2_lag_count >= 3 && delay <= -3.0) {
                                    new_cap = 1.75f;
                                    reason = "sw4k_over2_persistent_lag";
                                } else if (sw4k_over2_lag_count >= 2) {
                                    new_cap = 2.00f;
                                    reason = "sw4k_over2_unstable";
                                }

                                if (new_cap > 0.0f) {
                                    bool need_apply = adaptive_rate_cap_value <= 0.0f ||
                                                      new_cap < adaptive_rate_cap_value - 0.01f ||
                                                      adaptive_rate_cap_until_ms <= now;
                                    if (need_apply) {
                                        adaptive_rate_cap_value = new_cap;
                                        adaptive_rate_cap_until_ms = now + 15000;
                                        adaptive_rate_cap_recovery_start_ms = 0;
                                        SYNCI("evt=sw4k_auto_downrate req=%.2f cap=%.2f lag_count=%d window_ms=%" PRId64 " delay=%.3f reason=%s",
                                              requested_rate, adaptive_rate_cap_value,
                                              sw4k_over2_lag_count, over2_window_ms, delay, reason);
                                    }
                                }
                            }
                        }
                    }
                } else {
                    severe_lag_audio_pause_start_ms_ = 0;
                }
            } else if (!in_seek_recovery) {
                severe_lag_audio_pause_start_ms_ = 0;
            }

            // Tencent-like behavior: after seek, drop frames older than target PTS
            // (lower bound) until we hit the first eligible frame.
            bool lower_bound_active = seek_lower_bound_active_.load(std::memory_order_acquire);
            double seek_target_now = seek_target_sec_.load(std::memory_order_acquire);
            if (in_seek_recovery && lower_bound_active && seek_target_now >= 0.0 &&
                !std::isnan(pts) && !std::isinf(pts)) {
                double seek_epsilon_sec = 0.035;
                if (ultra_high_rate_4k) {
                    // At 2.5x/3x 4K, avoid waiting for strict PTS equality.
                    seek_epsilon_sec = 0.10;
                }
                if (sw_decode_4k && playback_rate >= 2.0) {
                    // Software-decode 4K high-rate seek can take long to catch target.
                    // Allow a wider lower-bound window to avoid prolonged frozen waits.
                    seek_epsilon_sec = std::max(seek_epsilon_sec, 0.35);
                }
                double seek_from_now = seek_from_sec_.load(std::memory_order_acquire);
                bool is_backward_seek = seek_from_now >= 0.0 &&
                                        (seek_from_now - seek_target_now) > 0.5;
                int64_t seek_elapsed_ms = seek_started_at_ms_ > 0 ? (now - seek_started_at_ms_) : 0;
                double seek_span_sec = (seek_target_now >= 0.0 && seek_from_now >= 0.0)
                                       ? (seek_target_now - seek_from_now) : 0.0;
                double seek_span_abs_sec = std::fabs(seek_span_sec);
                bool large_forward_seek = !is_backward_seek && seek_span_sec > 25.0;
                bool large_seek_any_direction = seek_span_abs_sec > 25.0;
                bool very_large_seek = seek_span_abs_sec > 180.0;
                double stale_future_margin_sec = ultra_high_rate_4k ? 4.0 : 2.2;
                double forward_future_block_margin_sec = ultra_high_rate_4k ? 1.10 : 0.70;
                if (large_seek_any_direction) {
                    forward_future_block_margin_sec += 0.20;
                }
                auto mark_seek_lower_bound_hit = [&]() {
                    seek_lower_bound_active_.store(false, std::memory_order_release);
                    seek_fast_catchup_frames_.store(0, std::memory_order_release);
                    seek_catchup_deadline_ms_ = 0;
                    seek_recovery_active_.store(false, std::memory_order_release);
                    seek_recovery_deadline_ms_ = 0;
                    in_seek_recovery = false;
                    seek_lower_bound_drop_count_ = 0;
                    seek_resume_stable_frames = 0;
                    sync_warmup_frames_.store(24, std::memory_order_release);
                    // Seek 恢复后允许短暂 bypass ahead-hold，但窗口过长会放大音画分离体感。
                    // 在高倍速下保留一定缓冲，常速下尽量缩短恢复时间。
                    int64_t bypass_ms = is_backward_seek ? 1300 : 900;
                    if (playback_rate >= 2.0) {
                        bypass_ms += 400;
                    }
                    post_seek_ahead_bypass_until_ms = now + bypass_ms;
                    post_seek_bypass_skip_count = 0;
                    if (seek_audio_wait_video_.load(std::memory_order_acquire)) {
                        // 命中 lower-bound 后仍保留一小段“同步等待窗口”，
                        // 避免 deadline 过早触发导致刚出画面就开声而不同步。
                        int64_t post_hit_sync_wait_ms = is_backward_seek ? 1800 : 1200;
                        if (playback_rate >= 2.0) {
                            post_hit_sync_wait_ms += 400;
                        }
                        seek_audio_wait_deadline_ms_ = std::max<int64_t>(
                            seek_audio_wait_deadline_ms_, now + post_hit_sync_wait_ms);
                    }
                    should_display = should_consume = true;
                    LOGI("[sync] seek lower-bound hit: pts=%.3f target=%.3f delay=%.3f backward=%d bypass_ms=%" PRId64 " elapsed_ms=%" PRId64,
                         pts, seek_target_now, delay, is_backward_seek ? 1 : 0, bypass_ms, seek_elapsed_ms);
                    SYNCI("evt=seek_lower_bound_hit pts=%.3f target=%.3f delay=%.3f backward=%d bypass_ms=%" PRId64 " elapsed_ms=%" PRId64,
                          pts, seek_target_now, delay, is_backward_seek ? 1 : 0, bypass_ms, seek_elapsed_ms);
                };
                if (large_forward_seek) {
                    // Keep seek hit tight even for large forward jumps.
                    // Over-relaxing epsilon causes "jump forward by seconds".
                    seek_epsilon_sec = std::max(seek_epsilon_sec, 0.18);
                    if (seek_span_sec > 120.0) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, 0.28);
                    }
                }
                // If seek recovery is slow, relax lower-bound gradually instead of
                // exiting gate and resuming audio too early.
                bool lower_bound_deadline_elapsed = now >= seek_lower_bound_deadline_ms_;
                if (lower_bound_deadline_elapsed) {
                    seek_epsilon_sec = 0.250;
                    if (ultra_high_rate_4k) {
                        seek_epsilon_sec = 0.350;
                    }
                    if (sw_decode_4k && playback_rate >= 2.0) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, 0.55);
                    }
                    if (large_forward_seek) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, 0.48);
                    }
                }
                if (seek_elapsed_ms >= 1200) {
                    seek_epsilon_sec = std::max(seek_epsilon_sec, is_backward_seek ? 0.70 : (large_seek_any_direction ? 0.45 : 0.30));
                    if (is_backward_seek && likely_4k && very_large_seek) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, 2.20);
                    }
                }
                if (seek_elapsed_ms >= 2200) {
                    if (is_backward_seek) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, very_large_seek ? 1.30 : 0.95);
                        if (likely_4k && very_large_seek) {
                            seek_epsilon_sec = std::max(seek_epsilon_sec, 3.00);
                        }
                    } else {
                        double forward_relax_eps = very_large_seek ? 0.85 : (large_seek_any_direction ? 0.62 : 0.45);
                        if (playback_rate >= 2.0) {
                            forward_relax_eps = very_large_seek ? 0.72 : (large_seek_any_direction ? 0.58 : 0.42);
                        }
                        seek_epsilon_sec = std::max(seek_epsilon_sec, forward_relax_eps);
                    }
                }
                if (seek_elapsed_ms >= 3200) {
                    if (is_backward_seek) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, very_large_seek ? 1.60 : 1.20);
                        if (likely_4k && very_large_seek) {
                            seek_epsilon_sec = std::max(seek_epsilon_sec, 3.60);
                        }
                    } else {
                        double forward_relax_eps = very_large_seek ? 1.00 : (large_seek_any_direction ? 0.78 : 0.55);
                        if (playback_rate >= 2.0) {
                            forward_relax_eps = very_large_seek ? 0.85 : (large_seek_any_direction ? 0.72 : 0.50);
                        }
                        seek_epsilon_sec = std::max(seek_epsilon_sec, forward_relax_eps);
                    }
                }
                bool lower_bound_force_relax = seek_lower_bound_drop_count_ >= (very_large_seek ? 180 : 120);
                if (lower_bound_force_relax) {
                    double forced_eps = is_backward_seek ? 1.50 : (very_large_seek ? 1.00 : 0.72);
                    if (!is_backward_seek && playback_rate >= 2.0) {
                        forced_eps = very_large_seek ? 0.86 : 0.66;
                    }
                    seek_epsilon_sec = std::max(seek_epsilon_sec, forced_eps);
                }
                if (!is_backward_seek) {
                    double forward_eps_cap = 0.75;
                    if (large_seek_any_direction) {
                        forward_eps_cap = 0.95;
                    }
                    if (very_large_seek) {
                        forward_eps_cap = 1.10;
                    }
                    if (playback_rate >= 2.0) {
                        forward_eps_cap += 0.12;
                    }
                    seek_epsilon_sec = std::min(seek_epsilon_sec, forward_eps_cap);
                }
                if (pts + seek_epsilon_sec < seek_target_now) {
                    should_consume = true;
                    seek_lower_bound_drop_count_++;
                    LOGW_RATE(20, "[sync] seek lower-bound drop: pts=%.3f target=%.3f",
                              pts, seek_target_now);
                    SYNCW_RATE(20, "evt=seek_lower_bound_drop pts=%.3f target=%.3f elapsed_ms=%" PRId64 " drop_count=%d eps=%.3f",
                               pts, seek_target_now, seek_elapsed_ms, seek_lower_bound_drop_count_, seek_epsilon_sec);
                } else if (is_backward_seek &&
                           !lower_bound_deadline_elapsed &&
                           pts > seek_target_now + stale_future_margin_sec) {
                    // Backward seek: before lower-bound deadline, do not accept
                    // far-future stale frames as the first target hit.
                    should_consume = true;
                    seek_lower_bound_drop_count_++;
                    LOGW_RATE(20, "[sync] seek lower-bound stale-future drop: pts=%.3f target=%.3f from=%.3f margin=%.3f",
                              pts, seek_target_now, seek_from_now, stale_future_margin_sec);
                    SYNCW_RATE(20, "evt=seek_lower_bound_stale_future_drop pts=%.3f target=%.3f from=%.3f margin=%.3f elapsed_ms=%" PRId64 " drop_count=%d",
                               pts, seek_target_now, seek_from_now, stale_future_margin_sec, seek_elapsed_ms, seek_lower_bound_drop_count_);
                } else if (!is_backward_seek &&
                           !lower_bound_deadline_elapsed &&
                           pts > seek_target_now + forward_future_block_margin_sec) {
                    // Forward seek: do not accept far-future frame as first hit.
                    // This prevents "seek lands but immediately jumps several seconds ahead".
                    should_consume = true;
                    seek_lower_bound_drop_count_++;
                    SYNCW_RATE(20, "evt=seek_lower_bound_forward_future_drop pts=%.3f target=%.3f margin=%.3f elapsed_ms=%" PRId64 " drop_count=%d eps=%.3f",
                               pts, seek_target_now, forward_future_block_margin_sec, seek_elapsed_ms, seek_lower_bound_drop_count_, seek_epsilon_sec);
                } else if (!is_backward_seek &&
                           delay < -3.2 &&
                           seek_elapsed_ms < 5200) {
                    // Avoid exiting seek gate too early when frame still trails
                    // audio clock by a lot; otherwise users see "loading gone but frozen".
                    should_consume = true;
                    seek_lower_bound_drop_count_++;
                    SYNCW_RATE(20, "evt=seek_lower_bound_delay_guard pts=%.3f target=%.3f delay=%.3f elapsed_ms=%" PRId64 " drop_count=%d",
                               pts, seek_target_now, delay, seek_elapsed_ms, seek_lower_bound_drop_count_);
                } else if (is_backward_seek) {
                    // backward seek 在 4K 下也可能“命中过早”，随后出现几秒前冲卡顿。
                    // 增加 delay 门槛，保证命中时音画已经进入可恢复区间。
                    double backward_hit_min_delay = likely_4k ? -1.2 : -0.9;
                    if (large_seek_any_direction) {
                        backward_hit_min_delay -= likely_4k ? 0.20 : 0.10;
                    }
                    if (playback_rate >= 2.0) {
                        backward_hit_min_delay = likely_4k ? -1.6 : -1.2;
                    }
                    if (likely_4k && very_large_seek) {
                        // For very large 4K backward seeks, avoid strict hold gate.
                        // Audio resume gate already protects A/V sync before unmute.
                        backward_hit_min_delay = std::min(backward_hit_min_delay, -4.5);
                    }
                    // Avoid prolonged "loading freeze" on backward seeks.
                    // Gradually relax hold window as elapsed grows, and cap max hold time.
                    int64_t backward_hold_max_ms = likely_4k ? 1800 : 1400;
                    if (large_seek_any_direction) {
                        backward_hold_max_ms += likely_4k ? 400 : 300;
                    }
                    if (very_large_seek) {
                        backward_hold_max_ms += likely_4k ? 300 : 200;
                    }
                    if (seek_elapsed_ms >= 1200) {
                        backward_hit_min_delay += likely_4k ? 0.20 : 0.28;
                    }
                    if (seek_elapsed_ms >= 2000) {
                        backward_hit_min_delay += likely_4k ? 0.25 : 0.35;
                    }
                    if (seek_elapsed_ms >= 2800) {
                        backward_hit_min_delay += likely_4k ? 0.20 : 0.22;
                    }
                    if (delay < backward_hit_min_delay &&
                        seek_elapsed_ms < backward_hold_max_ms &&
                        seek_lower_bound_drop_count_ < 90) {
                        should_consume = true;
                        seek_lower_bound_drop_count_++;
                        SYNCW_RATE(20, "evt=seek_lower_bound_hit_hold_backward delay=%.3f min=%.3f elapsed_ms=%" PRId64 " drop_count=%d",
                                   delay, backward_hit_min_delay, seek_elapsed_ms, seek_lower_bound_drop_count_);
                    } else {
                        mark_seek_lower_bound_hit();
                    }
                } else if (!is_backward_seek) {
                    // 稳定性优先：前向 seek 命中前要求 delay 不得过度落后，
                    // 否则即便命中 lower-bound，也会出现“先播放但音画不同步数秒”。
                    double forward_hit_min_delay = likely_4k ? -1.3 : -1.0;
                    if (large_seek_any_direction) {
                        forward_hit_min_delay -= likely_4k ? 0.25 : 0.15;
                    }
                    if (playback_rate >= 2.0) {
                        // 体验优先：高倍速不再过度等待 lower-bound，避免 loading 后长时间“假播放卡住”。
                        forward_hit_min_delay = likely_4k ? -2.2 : -1.6;
                    }
                    if (playback_rate >= 2.5) {
                        forward_hit_min_delay = likely_4k ? -2.6 : -1.9;
                    }
                    if (delay < forward_hit_min_delay && seek_elapsed_ms < 7000) {
                        should_consume = true;
                        seek_lower_bound_drop_count_++;
                        SYNCW_RATE(20, "evt=seek_lower_bound_hit_hold delay=%.3f min=%.3f elapsed_ms=%" PRId64 " drop_count=%d",
                                   delay, forward_hit_min_delay, seek_elapsed_ms, seek_lower_bound_drop_count_);
                    } else {
                        mark_seek_lower_bound_hit();
                    }
                } else {
                    mark_seek_lower_bound_hit();
                }
            }

            // During seek recovery, disable normal sync branches entirely.
            if (in_seek_recovery && !should_consume && !should_display) {
                if (now >= seek_recovery_deadline_ms_) {
                    // Timeout fallback: stop recovery loop and render latest frame.
                    seek_recovery_active_.store(false, std::memory_order_release);
                    seek_recovery_deadline_ms_ = 0;
                    seek_lower_bound_active_.store(false, std::memory_order_release);
                    seek_started_at_ms_ = 0;
                    seek_lower_bound_drop_count_ = 0;
                    should_display = should_consume = true;
                    LOGW("[sync] seek recovery timeout fallback: pts=%.3f target=%.3f", pts, seek_target_now);
                    SYNCW("evt=seek_recovery_timeout_fallback pts=%.3f target=%.3f", pts, seek_target_now);
                    in_seek_recovery = false;
                } else {
                    should_consume = true;
                }
            }

            // --- First/post-seek fast-path ---
            bool is_first_or_seek = (frame_count == 0) ||
                                     seek_just_happened_.exchange(false, std::memory_order_acq_rel);
            int seek_catchup_left = seek_fast_catchup_frames_.load(std::memory_order_acquire);
            double seek_target = seek_target_sec_.load(std::memory_order_acquire);
            bool seek_catchup_deadline_ok = seek_catchup_deadline_ms_ <= 0 || now < seek_catchup_deadline_ms_;
            bool seek_catchup_enabled = seek_catchup_left > 0
                    && seek_target >= 0.0
                    && (playback_rate >= 1.75 || frame_data.width >= 3840)
                    && seek_catchup_deadline_ok;
            if (!in_seek_recovery && !should_consume && seek_catchup_enabled && !std::isnan(pts) && !std::isinf(pts)) {
                double behind_seek_target = seek_target - pts;
                // 高倍速下阈值更紧一些，减少“先播旧画面再追上”的时间。
                double seek_drop_threshold = (playback_rate >= 2.25) ? 0.45 : 0.60;
                if (frame_data.width >= 3840) {
                    seek_drop_threshold = std::min(seek_drop_threshold, 0.50);
                }
                if (behind_seek_target > seek_drop_threshold) {
                    // 关键借鉴：高倍速 seek 后先快速丢弃“明显早于目标位点”的旧帧，
                    // 降低“先回放旧画面再追上”的体感卡顿。
                    should_consume = true;
                    seek_fast_catchup_frames_.fetch_sub(1, std::memory_order_acq_rel);
                    LOGW_RATE(20, "[sync] seek catchup drop: pts=%.3f target=%.3f behind=%.3f thr=%.3f left=%d",
                              pts, seek_target, behind_seek_target, seek_drop_threshold, seek_catchup_left);
                } else {
                    seek_fast_catchup_frames_.store(0, std::memory_order_release);
                    seek_catchup_deadline_ms_ = 0;
                }
            } else if (seek_catchup_left > 0 && !seek_catchup_enabled) {
                seek_fast_catchup_frames_.store(0, std::memory_order_release);
                seek_catchup_deadline_ms_ = 0;
            }

            if (!in_seek_recovery && !should_consume && (std::isnan(pts) || std::isinf(pts))) {
                should_display = should_consume = true;
            } else if (!in_seek_recovery && !should_consume && is_first_or_seek) {
                // Force-display only when video is close enough to the audio clock.
                // If the first/post-seek frame is more than 1s behind the clock,
                // keep dropping frames to catch up first; otherwise the viewer
                // sees a flash of old content before the picture snaps to the
                // correct position.
                if (delay < -1.0) {
                    // Still too far behind -- drop silently and keep catching up.
                    should_consume = true;
                    LOGI_RATE(30, "[sync] %s catching up: pts=%.3f clk=%.3f delay=%.3f",
                              frame_count == 0 ? "first frame" : "post-seek frame",
                              pts, clock, delay);
                } else {
                    LOGI("[sync] %s forced: pts=%.3f clk=%.3f delay=%.3f",
                         frame_count == 0 ? "first frame" : "post-seek frame",
                         pts, clock, delay);
                    should_display = should_consume = true;
                }
            } else if (!in_seek_recovery && !should_consume && delay < -5.0) {
                if (high_rate_4k || mid_rate_4k) {
                    // Avoid endless drop loops after seek: burst-drop + periodic latest-frame display.
                    consecutive_drop_count_++;
                    int severe_cadence = 8;
                    if (ultra_high_rate_4k) {
                        severe_cadence = 3;
                    } else if (mid_rate_4k) {
                        // Exo-like conservative cadence at 1.25x~2.0x 4K:
                        // avoid all-drop loops that feel like freeze.
                        severe_cadence = 5;
                    }
                    if (consecutive_drop_count_ >= severe_cadence) {
                        should_display = should_consume = true;
                        consecutive_drop_count_ = 0;
                        LOGI_RATE(10, "[sync] severe behind cadence display: pts=%.3f clk=%.3f delay=%.3f rate=%.2f",
                                  pts, clock, delay, playback_rate);
                    } else {
                        should_consume = true;
                    }
                } else {
                    should_consume = true;
                    LOGI_RATE(10, "[sync] severe behind drop: pts=%.3f clk=%.3f delay=%.3f rate=%.2f",
                              pts, clock, delay, playback_rate);
                }
            } else if (!in_seek_recovery && !should_consume) {
                // Dynamic sync threshold based on actual frame interval
                // (same algorithm as iOS HXCPlayerControl.mm).
                double frame_interval = 1.0 / 30.0;
                if (!std::isnan(last_sync_video_pts_) && pts > last_sync_video_pts_) {
                    double delta = pts - last_sync_video_pts_;
                    if (delta > 0.0 && delta < 0.2) frame_interval = delta;
                }
                double sync_threshold = std::min(0.100, std::max(0.020,
                                            frame_interval * 1.5)) / playback_rate;
                // 高倍速+4K 下若阈值过小会导致“持续只丢帧不出画面”。
                if (playback_rate >= 2.0 || frame_data.width >= 3840) {
                    sync_threshold = std::max(sync_threshold, 0.035);
                }
                // Exo-style tolerance widening:
                // at ultra-high-rate 4K, allow a slightly larger late window
                // before discarding, reducing excessive drop loops.
                if (ultra_high_rate_4k) {
                    sync_threshold = std::max(sync_threshold, 0.060);
                } else if (high_rate_4k) {
                    sync_threshold = std::max(sync_threshold, 0.045);
                } else if (mid_rate_4k) {
                    // 1.25x~2.0x 4K：适当放宽阈值，避免轻微抖动就连续丢帧。
                    sync_threshold = std::max(sync_threshold, 0.050);
                }

                int warmup = sync_warmup_frames_.load(std::memory_order_acquire);
                if (warmup > 0) {
                    // Relaxed window after open/seek: mirror iOS warmup logic.
                    // Tolerate ?500ms without dropping; >500ms ahead: wait.
                    if (delay > 0.5) {
                        // video too far ahead -- hold
                    } else if (delay < -0.5) {
                        should_consume = true; // drop silently
                    } else {
                        should_display = should_consume = true;
                    }
                    sync_warmup_frames_.store(warmup - 1, std::memory_order_release);
                } else if (delay <= -sync_threshold) {
                    if (high_rate_4k && delay > -3.0) {
                        // Exo-style dynamic dropping:
                        // tolerate slight lateness, then progressively increase
                        // discard cadence as lag grows.
                        if (ultra_high_rate_4k && delay > -0.25) {
                            should_display = should_consume = true;
                        } else {
                            consecutive_drop_count_++;
                            int cadence_step = 4;
                            if (ultra_high_rate_4k) {
                                // At >=2.5x 4K, keep a stronger cadence floor so video
                                // can continuously catch up with fast-moving audio clock.
                                int min_step_by_rate = (playback_rate >= 2.75)
                                                       ? ((delay <= -1.4) ? 4 : 3)
                                                       : 2;
                                if (delay <= -2.5) {
                                    cadence_step = 4; // drop 3, show 1
                                } else if (delay <= -1.8 && playback_rate >= 2.75) {
                                    cadence_step = 5; // drop 4, show 1
                                } else if (delay <= -1.2) {
                                    cadence_step = 3; // drop 2, show 1
                                } else if (delay <= -0.6) {
                                    cadence_step = 2; // drop 1, show 1
                                } else {
                                    cadence_step = 1; // near-sync: keep smoothness
                                }
                                cadence_step = std::max(cadence_step, min_step_by_rate);
                            } else {
                                cadence_step = (delay <= -2.0) ? 4 : 3;
                            }
                            if (consecutive_drop_count_ >= cadence_step) {
                                should_display = should_consume = true;
                                consecutive_drop_count_ = 0;
                                LOGI_RATE(20, "[sync] high-rate cadence display: pts=%.3f clk=%.3f delay=%.3f step=%d",
                                          pts, clock, delay, cadence_step);
                            } else {
                                should_consume = true;
                            }
                        }
                    } else if (mid_rate_4k && delay > -2.2) {
                        // 1.5x 左右的 4K 如果持续只丢帧，体感会像“卡死”。
                        // 用温和 cadence：保留追赶能力，同时周期性展示最新帧。
                        consecutive_drop_count_++;
                        int cadence_step = 3; // default: drop 2, show 1
                        if (delay > -0.9) {
                            cadence_step = 2; // near-sync: drop 1, show 1
                        } else if (delay <= -1.6) {
                            cadence_step = 4; // farther behind: drop 3, show 1
                        }
                        if (consecutive_drop_count_ >= cadence_step) {
                            should_display = should_consume = true;
                            consecutive_drop_count_ = 0;
                            LOGI_RATE(20, "[sync] mid-rate cadence display: pts=%.3f clk=%.3f delay=%.3f step=%d",
                                      pts, clock, delay, cadence_step);
                        } else {
                            should_consume = true;
                        }
                    } else if (sw_decode_4k && playback_rate < 2.0 && delay > -2.0) {
                        // Stability-first path for software-decoded 4K below 2x:
                        // avoid long drop streaks and keep motion continuity.
                        consecutive_drop_count_++;
                        int cadence_step = (delay > -0.9) ? 2 : 3;
                        if (consecutive_drop_count_ >= cadence_step) {
                            should_display = should_consume = true;
                            consecutive_drop_count_ = 0;
                            LOGI_RATE(20, "[sync] sw4k_sub2x cadence display: pts=%.3f clk=%.3f delay=%.3f step=%d",
                                      pts, clock, delay, cadence_step);
                        } else {
                            should_consume = true;
                        }
                    } else {
                        // Video is behind: drop frame (don't display)
                        should_consume = true;
                        if (delay < -1.0) {
                            LOGI_RATE(30, "[sync] drop: pts=%.3f clk=%.3f delay=%.3f thr=%.3f",
                                      pts, clock, delay, sync_threshold);
                        }
                    }
                } else if (delay <= kMaxAhead) {
                    should_display = should_consume = true;
                } else if (!in_seek_recovery &&
                           post_seek_ahead_bypass_until_ms > now) {
                    double max_bypass_ahead_sec = high_rate_4k ? 2.6 : 1.8;
                    if (delay > max_bypass_ahead_sec) {
                        // Ahead 过大时继续追赶时钟，但避免 pure-drop 导致“画面卡住”体感。
                        // 在可控的 ahead 区间按 cadence 出帧，优先保证可见进度。
                        should_consume = true;
                        post_seek_bypass_skip_count++;
                        double cadence_display_limit = max_bypass_ahead_sec + (likely_4k ? 1.0 : 0.8);
                        if (delay <= cadence_display_limit &&
                            (post_seek_bypass_skip_count % 3 == 0)) {
                            should_display = true;
                            SYNCI_RATE(20, "evt=post_seek_ahead_bypass_cadence_display delay=%.3f max_ahead=%.3f skip_count=%d rate=%.2f",
                                       delay, max_bypass_ahead_sec, post_seek_bypass_skip_count, playback_rate);
                        }
                        int64_t bypass_left_ms = std::max<int64_t>(0, post_seek_ahead_bypass_until_ms - now);
                        if (post_seek_bypass_skip_count >= 3 && bypass_left_ms > 260) {
                            int64_t shrink_ms = (post_seek_bypass_skip_count >= 8) ? 360 : 220;
                            int64_t shortened_left_ms = std::max<int64_t>(220, bypass_left_ms - shrink_ms);
                            post_seek_ahead_bypass_until_ms = now + shortened_left_ms;
                            SYNCI_RATE(20, "evt=post_seek_ahead_bypass_shrink delay=%.3f left_ms=%" PRId64 " new_left_ms=%" PRId64 " skip_count=%d rate=%.2f",
                                       delay, bypass_left_ms, shortened_left_ms, post_seek_bypass_skip_count, playback_rate);
                        }
                        SYNCI_RATE(30, "evt=post_seek_ahead_bypass_skip delay=%.3f max_ahead=%.3f rate=%.2f",
                                   delay, max_bypass_ahead_sec, playback_rate);
                    } else {
                    // Seek just recovered, but clock may temporarily roll back or lag.
                    // Bypass "ahead hold" briefly so users don't see frozen picture
                    // (not only for high-rate; 1.0x backward seek can also hit this).
                    post_seek_bypass_skip_count = 0;
                    should_display = should_consume = true;
                    SYNCI_RATE(30, "evt=post_seek_ahead_bypass pts=%.3f clk=%.3f delay=%.3f rate=%.2f bypass_left_ms=%" PRId64,
                               pts, clock, delay, playback_rate,
                               (int64_t)std::max<int64_t>(0, post_seek_ahead_bypass_until_ms - now));
                    }
                } else if (!seek_audio_wait_video_.load(std::memory_order_acquire) &&
                           audio_rebuffer_pending_.load(std::memory_order_acquire) &&
                           now >= audio_rebuffer_min_resume_at_ms_) {
                    // Deadlock guard: when audio was paused for starvation, clock can
                    // stop advancing. If we keep "ahead hold" here, video never gets
                    // presented again and audio never reaches resume path.
                    // Force one display+consume to re-enter normal recovery.
                    should_display = should_consume = true;
                    SYNCI_RATE(30, "evt=audio_rebuffer_break_ahead_hold pts=%.3f clk=%.3f delay=%.3f rate=%.2f",
                               pts, clock, delay, playback_rate);
                } else if (!in_seek_recovery &&
                           core_playing_now &&
                           likely_4k &&
                           snapshot_stalled &&
                           stall_watchdog_since_ms > 0 &&
                           (now - stall_watchdog_since_ms) >= kSyncStallWatchdogMs &&
                           (now - stall_watchdog_last_break_ms) >= kSyncStallWatchdogCooldownMs) {
                    // If clocks are stuck while "playing", force progress once to
                    // avoid a long pseudo-playing freeze in high-load scenarios.
                    should_display = should_consume = true;
                    stall_watchdog_last_break_ms = now;
                    SYNCI_RATE(20, "evt=sync_stall_watchdog_break pts=%.3f clk=%.3f delay=%.3f stalled_ms=%" PRId64 " rate=%.2f",
                               pts, clock, delay, (int64_t)(now - stall_watchdog_since_ms), playback_rate);
                }
                // else: video > 2s ahead of audio -- hold frame (don't consume)
            }

            if (should_display) {
                consecutive_drop_count_ = 0;
                if (seek_audio_wait_video_.load(std::memory_order_acquire)) {
                    // seek 后允许 loading 多保持一小段时间，优先等待音画接近同步再恢复音频，
                    // 避免用户感知到“先出画面再跟音频对齐”的明显不同步。
                    double seek_target_now = seek_target_sec_.load(std::memory_order_acquire);
                    double seek_target_reach_margin = likely_4k ? 0.18 : 0.12;
                    bool frame_reached_target = seek_target_now < 0.0 ||
                                                pts >= (seek_target_now - seek_target_reach_margin);
                    double seek_resume_sync_threshold =
                        (playback_rate >= 2.5) ? 0.90 :
                        ((playback_rate >= 2.0) ? 0.70 : 0.45);
                    bool seek_resume_sync_ready = std::fabs(delay) <= seek_resume_sync_threshold;
                    int seek_resume_need_stable_frames = likely_4k ? 8 : 6;
                    if (playback_rate >= 2.0) {
                        seek_resume_need_stable_frames += 2;
                    }
                    bool lower_bound_cleared = !seek_lower_bound_active_.load(std::memory_order_acquire);
                    int64_t post_seek_wait_ms = seek_started_at_ms_ > 0 ? (now - seek_started_at_ms_) : 0;
                    if (lower_bound_cleared && post_seek_wait_ms >= 1400) {
                        seek_resume_sync_threshold = std::max(seek_resume_sync_threshold, likely_4k ? 0.75 : 0.60);
                        seek_resume_need_stable_frames = std::min(seek_resume_need_stable_frames, likely_4k ? 5 : 4);
                    }
                    if (lower_bound_cleared && post_seek_wait_ms >= 2200) {
                        seek_resume_sync_threshold = std::max(seek_resume_sync_threshold, likely_4k ? 0.95 : 0.80);
                        seek_resume_need_stable_frames = std::min(seek_resume_need_stable_frames, 3);
                    }
                    seek_resume_sync_ready = std::fabs(delay) <= seek_resume_sync_threshold;
                    // Don't count stable frames when video is persistently behind audio:
                    // resuming audio in that state would make the gap worse, not better.
                    double sync_stable_min_delay = likely_4k ? -0.40 : -0.30;
                    if (lower_bound_cleared && post_seek_wait_ms >= 2200) {
                        sync_stable_min_delay = likely_4k ? -0.70 : -0.55;
                    }
                    bool sync_stable_direction_ok = delay >= sync_stable_min_delay;
                    if (!frame_reached_target) {
                        seek_resume_stable_frames = 0;
                    } else if (seek_resume_sync_ready && sync_stable_direction_ok) {
                        seek_resume_stable_frames = std::min(seek_resume_stable_frames + 1,
                                                             seek_resume_need_stable_frames + 3);
                    } else {
                        seek_resume_stable_frames = 0;
                    }
                    bool seek_resume_sync_stable =
                        seek_resume_sync_ready &&
                        seek_resume_stable_frames >= seek_resume_need_stable_frames;
                    bool seek_resume_deadline = seek_audio_wait_deadline_ms_ > 0 &&
                                                now >= seek_audio_wait_deadline_ms_;
                    double seek_resume_deadline_guard = likely_4k ? 1.20 : 0.90;
                    if (playback_rate >= 2.0) {
                        seek_resume_deadline_guard = likely_4k ? 1.50 : 1.10;
                    }
                    bool seek_resume_deadline_safe = seek_resume_deadline &&
                                                     std::fabs(delay) <= seek_resume_deadline_guard;
                    // slow/fast path require delay >= min_negative to ensure video
                    // is not persistently behind audio; if video still lags, resuming
                    // audio now would widen the gap and cause a frozen-picture stall.
                    double fast_path_min_delay = likely_4k ? -0.35 : -0.25;
                    double slow_path_min_delay = likely_4k ? -0.60 : -0.45;
                    // After lower-bound hit, if a lot of time has passed, be more lenient
                    // so we don't block indefinitely (8K/high-load corner cases).
                    if (lower_bound_cleared && post_seek_wait_ms >= 3000) {
                        fast_path_min_delay = likely_4k ? -0.75 : -0.60;
                        slow_path_min_delay = likely_4k ? -1.10 : -0.90;
                    }
                    bool seek_resume_slow_path_ready = lower_bound_cleared &&
                                                       post_seek_wait_ms >= 2200 &&
                                                       std::fabs(delay) <= (likely_4k ? 1.00 : 0.85) &&
                                                       delay >= slow_path_min_delay;
                    bool seek_resume_fast_path_ready = lower_bound_cleared &&
                                                       post_seek_wait_ms >= 900 &&
                                                       std::fabs(delay) <= (likely_4k ? 0.65 : 0.50) &&
                                                       delay >= fast_path_min_delay;
                    bool seek_resume_force_timeout = seek_started_at_ms_ > 0 &&
                                                     (now - seek_started_at_ms_) >= 8000;
                    if (frame_reached_target &&
                        (seek_resume_sync_stable || seek_resume_deadline_safe ||
                         seek_resume_slow_path_ready || seek_resume_fast_path_ready ||
                         seek_resume_force_timeout)) {
                        if (seek_audio_wait_video_.exchange(false, std::memory_order_acq_rel)) {
                            // 重锚 master clock 到当前视频帧 pts，消除 seek 期间音频暂停
                            // 导致 audio_clock 自动流逝的累积偏差（腾讯播放器 ptsShift=0 等效操作）。
                            if (player_core_) {
                                player_core_anchor_clock(player_core_, pts);
                            }
                            if (playItf_ && current_volume_.load(std::memory_order_relaxed) > 0.0f) {
                                SLresult r = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PLAYING);
                                LOGI("[sync] seek first-frame resume audio: result=%d anchor_pts=%.3f", r, pts);
                            }
                            SYNCI("evt=seek_audio_resume_gate delay=%.3f threshold=%.3f stable=%d/%d by_deadline=%d fast_path=%d deadline_guard=%.3f force_timeout=%d rate=%.2f anchor_pts=%.3f",
                                  delay, seek_resume_sync_threshold, seek_resume_stable_frames, seek_resume_need_stable_frames,
                                  seek_resume_deadline ? 1 : 0, seek_resume_fast_path_ready ? 1 : 0, seek_resume_deadline_guard,
                                  seek_resume_force_timeout ? 1 : 0, playback_rate, pts);
                        }
                        seek_resume_stable_frames = 0;
                        seek_audio_wait_deadline_ms_ = 0;
                        seek_started_at_ms_ = 0;
                    } else {
                        if (seek_resume_deadline) {
                            // deadline 到点但仍明显不同步时，短延一档再等，稳定性优先。
                            seek_audio_wait_deadline_ms_ = now + 800;
                            SYNCI_RATE(20, "evt=seek_audio_resume_deadline_extend delay=%.3f guard=%.3f extend_ms=%d rate=%.2f",
                                       delay, seek_resume_deadline_guard, 800, playback_rate);
                        }
                        SYNCI_RATE(30, "evt=seek_audio_resume_wait delay=%.3f threshold=%.3f stable=%d/%d reached_target=%d deadline_left_ms=%" PRId64 " rate=%.2f",
                                   delay, seek_resume_sync_threshold,
                                   seek_resume_stable_frames, seek_resume_need_stable_frames,
                                   frame_reached_target ? 1 : 0,
                                   (int64_t)std::max<int64_t>(0, seek_audio_wait_deadline_ms_ - now),
                                   playback_rate);
                    }
                } else {
                    seek_resume_stable_frames = 0;
                }
                // If we previously paused audio due to prolonged video starvation,
                // resume audio as soon as video starts presenting again.
                if (!seek_audio_wait_video_.load(std::memory_order_acquire) &&
                    audio_rebuffer_pending_.load(std::memory_order_acquire)) {
                    if (now >= audio_rebuffer_min_resume_at_ms_) {
                        bool recover_enough = delay >= (very_high_rate_4k ? -0.55 : ((playback_rate >= 2.5) ? -0.75 : ((playback_rate >= 2.0) ? -1.0 : -0.75)));
                        bool resume_by_fallback = now >= audio_rebuffer_deadline_ms_;
                        if (!recover_enough && !resume_by_fallback) {
                            SYNCI_RATE(60, "evt=audio_rebuffer_resume_wait delay=%.3f rate=%.2f min_resume_in_ms=%" PRId64 " fallback_in_ms=%" PRId64,
                                       delay, playback_rate,
                                       (int64_t)std::max<int64_t>(0, audio_rebuffer_min_resume_at_ms_ - now),
                                       (int64_t)std::max<int64_t>(0, audio_rebuffer_deadline_ms_ - now));
                        } else if (audio_rebuffer_pending_.exchange(false, std::memory_order_acq_rel)) {
                            audio_rebuffer_paused_at_ms_ = 0;
                            audio_rebuffer_min_resume_at_ms_ = 0;
                            if (playItf_ && current_volume_.load(std::memory_order_relaxed) > 0.0f) {
                                SLresult r = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PLAYING);
                                LOGI("[sync] audio resumed after video rebuffer: result=%d", r);
                            }
                        }
                    }
                }
                if (sw_decode_4k && adaptive_rate_cap_until_ms > now &&
                    adaptive_rate_cap_value > 0.0f) {
                    if (delay >= -0.6) {
                        if (adaptive_rate_cap_recovery_start_ms == 0) {
                            adaptive_rate_cap_recovery_start_ms = now;
                        } else if ((now - adaptive_rate_cap_recovery_start_ms) >= 2200) {
                            SYNCI("evt=adaptive_rate_cap_release req=%.2f cap=%.2f delay=%.3f",
                                  requested_rate, adaptive_rate_cap_value, delay);
                            adaptive_rate_cap_until_ms = 0;
                            adaptive_rate_cap_value = 0.0f;
                            adaptive_rate_cap_recovery_start_ms = 0;
                        }
                    } else {
                        adaptive_rate_cap_recovery_start_ms = 0;
                    }
                } else {
                    adaptive_rate_cap_recovery_start_ms = 0;
                }
                // If this is the first rendered frame and audio start was deferred,
                // start audio now so picture and sound appear together.
                // Skip if volume is 0 (muted player, e.g. small window in split-screen).
                if (frame_count == 0 && audio_start_pending_.exchange(false, std::memory_order_acq_rel)) {
                    if (playItf_ && current_volume_.load(std::memory_order_relaxed) > 0.0f) {
                        SLresult r = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PLAYING);
                        LOGI("[sync] audio started on first frame: result=%d", r);
                    } else {
                        LOGI("[sync] audio start skipped: volume=0 (muted)");
                    }
                }
                frame_count++;
                last_sync_video_pts_ = pts; // track for dynamic frame-interval estimation
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
                    PERFI("evt=render_window_stats frames=%d pos=%.1f avg_render_ms=%" PRId64 " max_render_ms=%" PRId64
                          " avg_upload_ms=%" PRId64 " max_upload_ms=%" PRId64,
                          kDiagInterval, pos,
                          total_render_ms / kDiagInterval, max_render_ms,
                          total_upload_ms / kDiagInterval, max_upload_ms);
                    total_render_ms = total_upload_ms = max_render_ms = max_upload_ms = 0;
                }
                if (render_ms > 33)
                    LOGW("[perf] slow frame %" PRId64 "ms %dx%d surf=%dx%d",
                         render_ms, frame_data.width, frame_data.height,
                         surface_width_, surface_height_);
                if (render_ms > 33) {
                    PERFW("evt=slow_frame render_ms=%" PRId64 " video_w=%d video_h=%d surface_w=%d surface_h=%d rate=%.2f delay=%.3f",
                          render_ms, frame_data.width, frame_data.height,
                          surface_width_, surface_height_, playback_rate, delay);
                }
            }

            if (should_consume) {
                player_core_consume_video_frame(player_core_);
                // We just consumed a frame; likely another one is ready soon.
                // In catch-up mode (high rate / far behind), don't sleep so seek recovery
                // can drain stale frames quickly.
                if (!should_display &&
                    (delay < -0.20 || delay > 1.20 || playback_rate >= 1.75)) {
                    wait_ms = 0;
                } else {
                    wait_ms = 8;
                }
                // Safety: if frames are continuously dropped (should_display=false) while
                // seek_audio_wait_video_ is still set, the should_display branch never runs
                // and the audio resume gate is never checked. Detect this via force_timeout
                // so audio doesn't stay muted indefinitely even under persistent video lag.
                if (!should_display && seek_audio_wait_video_.load(std::memory_order_acquire)) {
                    bool force_timeout = seek_started_at_ms_ > 0 &&
                                        (now - seek_started_at_ms_) >= 8000;
                    if (force_timeout) {
                        if (seek_audio_wait_video_.exchange(false, std::memory_order_acq_rel)) {
                            if (playItf_ && current_volume_.load(std::memory_order_relaxed) > 0.0f) {
                                SLresult r = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PLAYING);
                                LOGI("[sync] seek audio force-resume (drop loop): result=%d delay=%.3f", r, delay);
                            }
                            SYNCI("evt=seek_audio_force_resume_drop_loop delay=%.3f elapsed_ms=%" PRId64 " rate=%.2f",
                                  delay, seek_started_at_ms_ > 0 ? (now - seek_started_at_ms_) : 0LL, playback_rate);
                            seek_audio_wait_deadline_ms_ = 0;
                            seek_started_at_ms_ = 0;
                            seek_resume_stable_frames = 0;
                        }
                    }
                }
            } else {
                // Video ahead of clock: sleep proportional to how far ahead,
                // capped at 16ms. This lets the audio clock advance and avoids
                // wasting CPU while waiting for the right presentation time.
                int ahead_ms = (int)(delay * 1000.0);
                wait_ms = std::min(16, std::max(4, ahead_ms / 2));
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }

        } else {
            empty_count++;
            // Safety valve: if audio start was deferred but no video frame arrived
            // within the deadline, start audio anyway to avoid permanent mute.
            if (!seek_audio_wait_video_.load(std::memory_order_acquire) &&
                audio_start_pending_.load(std::memory_order_acquire) &&
                now_ms() >= audio_start_deadline_ms_) {
                if (audio_start_pending_.exchange(false, std::memory_order_acq_rel)) {
                    if (playItf_ && current_volume_.load(std::memory_order_relaxed) > 0.0f) {
                        SLresult r = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PLAYING);
                        LOGI("[sync] audio deadline fallback: result=%d", r);
                    }
                }
            }
            // High-rate/4K starvation path:
            // if video queue stays empty for a while, pause OpenSL audio so user
            // doesn't hear a long "audio-only" segment and then permanent A/V drift.
            int64_t now = now_ms();
            if (seek_audio_wait_video_.load(std::memory_order_acquire) &&
                !seek_recovery_active_.load(std::memory_order_acquire) &&
                !seek_lower_bound_active_.load(std::memory_order_acquire) &&
                now >= seek_audio_wait_deadline_ms_) {
                if (seek_audio_wait_video_.exchange(false, std::memory_order_acq_rel)) {
                    if (playItf_ && current_volume_.load(std::memory_order_relaxed) > 0.0f) {
                        SLresult r = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PLAYING);
                        LOGI("[sync] seek wait-video fallback resume audio: result=%d", r);
                    }
                    seek_audio_wait_deadline_ms_ = 0;
                    seek_started_at_ms_ = 0;
                    seek_resume_stable_frames = 0;
                }
            }
            double playback_rate = player_core_get_playback_rate(player_core_);
            if (playback_rate <= 0.0) playback_rate = 1.0;
            bool core_playing = player_core_is_playing(player_core_);
            bool high_rate = playback_rate >= 2.0;
            bool likely_4k = gl_last_video_w_ >= 3840 || gl_last_video_h_ >= 2160;
            bool in_loading = is_loading_.load(std::memory_order_acquire);
            bool in_sync_warmup = sync_warmup_frames_.load(std::memory_order_acquire) > 0;
            int64_t rebuffer_trigger_ms = high_rate ? 180 : 260;
            if (likely_4k) rebuffer_trigger_ms = std::max<int64_t>(160, rebuffer_trigger_ms - 20);
            int64_t rebuffer_fallback_ms = high_rate ? 850 : 700;
            if (likely_4k) rebuffer_fallback_ms += 150;
            int64_t rebuffer_min_hold_ms = high_rate ? 650 : 450;
            if (likely_4k) rebuffer_min_hold_ms += 150;
            if (!seek_audio_wait_video_.load(std::memory_order_acquire) &&
                high_rate && in_empty_streak &&
                !in_loading &&
                !in_sync_warmup &&
                core_playing &&
                !audio_rebuffer_pending_.load(std::memory_order_acquire)) {
                int64_t empty_ms = now - empty_start_ms;
                if (empty_ms >= rebuffer_trigger_ms && playItf_ && current_volume_.load(std::memory_order_relaxed) > 0.0f) {
                    SLresult r = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PAUSED);
                    if (r == SL_RESULT_SUCCESS) {
                        audio_rebuffer_pending_.store(true, std::memory_order_release);
                        audio_rebuffer_paused_at_ms_ = now;
                        audio_rebuffer_min_resume_at_ms_ = now + rebuffer_min_hold_ms;
                        audio_rebuffer_deadline_ms_ = now + rebuffer_fallback_ms;
                        LOGW("[sync] video starvation -> pause audio: empty_ms=%" PRId64 " trig=%" PRId64 " fallback=%" PRId64 " rate=%.2f last=%dx%d",
                             empty_ms, rebuffer_trigger_ms, rebuffer_fallback_ms, playback_rate, gl_last_video_w_, gl_last_video_h_);
                        SYNCW("evt=video_starvation_pause_audio empty_ms=%" PRId64 " trigger_ms=%" PRId64
                              " hold_ms=%" PRId64 " fallback_ms=%" PRId64 " rate=%.2f last_video_w=%d last_video_h=%d",
                              empty_ms, rebuffer_trigger_ms, rebuffer_min_hold_ms, rebuffer_fallback_ms,
                              playback_rate, gl_last_video_w_, gl_last_video_h_);
                    }
                }
            }
            // Deadline fallback: if starvation persists too long, resume audio
            // to avoid extended silence under poor network/device conditions.
            if (!seek_audio_wait_video_.load(std::memory_order_acquire) &&
                audio_rebuffer_pending_.load(std::memory_order_acquire) &&
                now >= audio_rebuffer_deadline_ms_) {
                if (audio_rebuffer_pending_.exchange(false, std::memory_order_acq_rel)) {
                    audio_rebuffer_paused_at_ms_ = 0;
                    audio_rebuffer_min_resume_at_ms_ = 0;
                    if (core_playing && playItf_ && current_volume_.load(std::memory_order_relaxed) > 0.0f) {
                        SLresult r = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_PLAYING);
                        LOGI("[sync] audio rebuffer fallback resume: result=%d", r);
                    }
                }
            }
            if (!in_empty_streak) {
                empty_start_ms  = now_ms();
                in_empty_streak = true;
                // Hot path in dual-player scenes: keep as debug + rate-limited.
                LOGD_RATE(30, "[render] frame queue empty: state=%d pos=%.3f",
                          player_core_get_state(player_core_),
                          player_core_get_position(player_core_));
            } else if (empty_count % 300 == 0) {
                LOGD_RATE(10, "[render] still buffering: empty_ms=%" PRId64 " state=%d pos=%.3f",
                          now_ms() - empty_start_ms,
                          player_core_get_state(player_core_),
                          player_core_get_position(player_core_));

            }
            if (empty_count == 12) redrawLastFrame();
            // When the frame queue is empty the decoder is still filling it.
            // Sleep briefly to yield CPU to decode threads, then use a longer
            // condvar wait so we don't spin faster than decoding produces frames.
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            wait_ms = 16; // reset to normal pace; decoder needs time
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
bool AndroidPlayer::ensurePBOs(int y_w, int y_h, int uv_w, int uv_h, int uv_bpp) {
    int y_sz  = y_w  * y_h;
    int uv_sz = uv_w * uv_h * uv_bpp;

    bool need_recreate = (gl_pbo_y_sz_  != y_sz  ||
                          gl_pbo_uv_sz_ != uv_sz ||
                          gl_pbo_y_[0]  == 0);
    if (!need_recreate) return true;

    // Delete old PBOs
    if (gl_pbo_y_[0]) { glDeleteBuffers(6, gl_pbo_y_); }
    memset(gl_pbo_y_, 0, sizeof(gl_pbo_y_));
    gl_pbo_y_sz_  = 0;
    gl_pbo_uv_sz_ = 0;

    glGenBuffers(6, gl_pbo_y_);  // [0,1]=Y  [2,3]=U/UV  [4,5]=V(planar only)

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
    PBOI("evt=pbo_allocate y_bytes=%d uv_bytes=%d y_w=%d y_h=%d uv_w=%d uv_h=%d uv_bpp=%d",
         y_sz, uv_sz, y_w, y_h, uv_w, uv_h, uv_bpp);
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
    if (!src || sz <= 0 || tex_w <= 0 || tex_h <= 0) {
        return;
    }
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
    if (dst && reinterpret_cast<uintptr_t>(dst) > 4096) {
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
    auto isLikelyValidPtr = [](const void* p) -> bool {
        return p && reinterpret_cast<uintptr_t>(p) > 4096;
    };
    if (!isLikelyValidPtr(y_data) || width <= 1 || height <= 1 ||
        y_linesize < 0 || u_linesize < 0 || v_linesize < 0) {
        LOGW("[render] drop invalid frame args y=%p u=%p v=%p w=%d h=%d yls=%d uls=%d vls=%d",
             y_data, u_data, v_data, width, height, y_linesize, u_linesize, v_linesize);
        return -1;
    }
    bool has_u = isLikelyValidPtr(u_data);
    bool has_v = isLikelyValidPtr(v_data);
    if (!has_u) {
        LOGW("[render] drop frame: missing U plane y=%p u=%p v=%p w=%d h=%d",
             y_data, u_data, v_data, width, height);
        return -1;
    }
    if (!gl_program_ || egl_surface_ == EGL_NO_SURFACE) return -1;

    int surface_w, surface_h;
    {
        std::lock_guard<std::mutex> lock(render_mutex_);
        surface_w = surface_width_;
        surface_h = surface_height_;
    }
    if (surface_w <= 0 || surface_h <= 0) return -1;

    auto t0 = std::chrono::high_resolution_clock::now();

    int y_tex_w  = y_linesize > 0 ? y_linesize : width;
    int uv_default_w = (width + 1) / 2;
    int uv_h     = (height + 1) / 2;
    bool uv_interleaved = !has_v;
    bool uv_swap = gl_uv_swap_selected_;
    int uv_tex_w = 0;
    int v_tex_w  = 0;
    int uv_upload_bpp = uv_interleaved ? 2 : 1;
    int uv_upload_stride = 0;
    const void* uv_upload_data = u_data;

    if (uv_interleaved) {
        uv_upload_stride = u_linesize > 0 ? u_linesize : width;
        if (uv_upload_stride <= 0 || (uv_upload_stride % 2) != 0) {
            LOGW("[render] drop frame: invalid interleaved UV stride=%d w=%d h=%d",
                 uv_upload_stride, width, height);
            return -1;
        }
        int uv_plane_w = uv_default_w;
        if (uv_upload_stride < uv_plane_w * 2) {
            LOGW("[render] drop frame: interleaved UV stride too small stride=%d need>=%d w=%d h=%d",
                 uv_upload_stride, uv_plane_w * 2, width, height);
            return -1;
        }
        uv_tex_w = uv_upload_stride / 2; // RG texel width
        if (width != gl_last_video_w_ || height != gl_last_video_h_) {
            // New stream size/layout: reopen UV order probe window.
            gl_uv_swap_decided_ = false;
            gl_uv_swap_votes_ = 0;
            gl_uv_swap_probe_budget_ = 24;
        }
        if (!gl_uv_swap_decided_ && gl_uv_swap_probe_budget_ > 0) {
            int vote = estimate_uv_swap_vote(
                static_cast<const uint8_t*>(y_data),
                y_linesize > 0 ? y_linesize : width,
                static_cast<const uint8_t*>(u_data),
                uv_upload_stride,
                width,
                height);
            if (vote != 0) {
                gl_uv_swap_votes_ += vote;
                if (gl_uv_swap_votes_ > 24) gl_uv_swap_votes_ = 24;
                if (gl_uv_swap_votes_ < -24) gl_uv_swap_votes_ = -24;
            }
            if (!gl_uv_swap_decided_ && std::abs(gl_uv_swap_votes_) >= 6) {
                gl_uv_swap_selected_ = (gl_uv_swap_votes_ > 0);
                gl_uv_swap_decided_ = true;
                SYNCI("evt=uv_order_probe_decided decided=1 swap=%d votes=%d w=%d h=%d",
                      gl_uv_swap_selected_ ? 1 : 0, gl_uv_swap_votes_, width, height);
            }
            if (gl_uv_swap_probe_budget_ > 0) {
                gl_uv_swap_probe_budget_--;
            }
            SYNCI_RATE(180, "evt=uv_order_probe status=%s swap=%d votes=%d budget=%d w=%d h=%d",
                       gl_uv_swap_decided_ ? "decided" : "probing",
                       gl_uv_swap_selected_ ? 1 : 0,
                       gl_uv_swap_votes_, gl_uv_swap_probe_budget_, width, height);
        } else if (!gl_uv_swap_decided_ && gl_uv_swap_probe_budget_ <= 0) {
            // Probe budget exhausted: lock to default NV12 mapping (swap=0)
            // to stop repeated probing logs/work on every frame.
            gl_uv_swap_selected_ = false;
            gl_uv_swap_decided_ = true;
            SYNCI("evt=uv_order_probe_timeout fallback=nv12 swap=%d votes=%d w=%d h=%d",
                  gl_uv_swap_selected_ ? 1 : 0, gl_uv_swap_votes_, width, height);
        }
        uv_swap = gl_uv_swap_selected_;
        LOGI_RATE(120, "[render] interleaved UV upload path w=%d h=%d uv_stride=%d uv_tex_w=%d",
                  width, height, uv_upload_stride, uv_tex_w);
    } else {
        uv_tex_w = u_linesize > 0 ? u_linesize : uv_default_w;
        v_tex_w  = v_linesize > 0 ? v_linesize : uv_default_w;
        uv_upload_stride = uv_tex_w;
        gl_uv_swap_decided_ = false;
        gl_uv_swap_selected_ = false;
        gl_uv_swap_votes_ = 0;
        gl_uv_swap_probe_budget_ = 24;
        uv_swap = false;
    }

    int64_t y_sz64  = static_cast<int64_t>(y_tex_w) * height;
    int64_t uv_sz64 = static_cast<int64_t>(uv_tex_w) * uv_h * uv_upload_bpp;
    int64_t v_sz64  = uv_interleaved ? 0 : static_cast<int64_t>(v_tex_w) * uv_h;
    if (y_tex_w <= 0 || uv_tex_w <= 0 || uv_h <= 0 ||
        y_sz64 <= 0 || uv_sz64 <= 0 ||
        (!uv_interleaved && (v_tex_w <= 0 || v_sz64 <= 0)) ||
        y_sz64 > INT_MAX || uv_sz64 > INT_MAX || (!uv_interleaved && v_sz64 > INT_MAX)) {
        LOGW("[render] drop invalid frame size w=%d h=%d ytw=%d uvw=%d vw=%d uh=%d uv_interleaved=%d",
             width, height, y_tex_w, uv_tex_w, v_tex_w, uv_h, uv_interleaved ? 1 : 0);
        return -1;
    }
    int y_sz = static_cast<int>(y_sz64);
    int uv_upload_sz = static_cast<int>(uv_sz64);
    int v_sz = uv_interleaved ? 0 : static_cast<int>(v_sz64);

    bool size_changed = (width != gl_last_video_w_ || height != gl_last_video_h_);
    bool uv_layout_changed = (uv_interleaved != gl_last_uv_interleaved_) ||
                             (uv_swap != gl_last_uv_swap_) ||
                             (uv_tex_w != gl_last_uv_tex_w_);
    if (size_changed) {
        LOGI("[DIAG] Video size: %dx%d -> %dx%d | Y_stride=%d UV_stride=%d",
             gl_last_video_w_, gl_last_video_h_, width, height, y_tex_w, uv_upload_stride);
    }

    // --- Texture upload ---
    auto t_upload0 = std::chrono::high_resolution_clock::now();

    // Use PBO double-buffering for 4K (>= 1920x1080) to overlap CPU copy with GPU draw.
    // For smaller resolutions the overhead of PBO setup isn't worth it.
    const bool use_pbo = (width >= 1920) &&
                         ensurePBOs(y_tex_w, height, uv_tex_w, uv_h, uv_upload_bpp);
    PBOD("evt=upload_path use_pbo=%d video_w=%d video_h=%d y_stride=%d uv_stride=%d v_stride=%d uv_interleaved=%d",
         use_pbo ? 1 : 0, width, height, y_tex_w, uv_upload_stride, v_tex_w, uv_interleaved ? 1 : 0);

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
            if (uv_interleaved) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, uv_tex_w, uv_h,
                             0, GL_RG, GL_UNSIGNED_BYTE, uv_upload_data);
            } else {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, uv_tex_w, uv_h,
                             0, GL_LUMINANCE, GL_UNSIGNED_BYTE, u_data);
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, gl_tex_v_);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, v_tex_w, uv_h,
                             0, GL_LUMINANCE, GL_UNSIGNED_BYTE, v_data);
            }
            // Pre-fill write PBOs for next frame
            auto fill_pbo = [](GLuint id, const void* src, int sz) {
                if (!src || sz <= 0) {
                    return;
                }
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, id);
                glBufferData(GL_PIXEL_UNPACK_BUFFER, sz, nullptr, GL_STREAM_DRAW);
                void* dst = glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, sz,
                                              GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
                if (dst && reinterpret_cast<uintptr_t>(dst) > 4096) {
                    memcpy(dst, src, sz);
                    glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
                } else {
                    glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, sz, src);
                }
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            };
            fill_pbo(gl_pbo_y_[wi],   y_data, y_sz);
            fill_pbo(gl_pbo_y_[2+wi], uv_upload_data, uv_upload_sz);
            if (!uv_interleaved) {
                fill_pbo(gl_pbo_y_[4+wi], v_data, v_sz);
            }
            gl_pbo_first_frame_ = false;
        } else {
            uploadPlanePBO(GL_TEXTURE0, gl_tex_y_,
                           gl_pbo_y_[wi],   gl_pbo_y_[ri],
                           GL_LUMINANCE, GL_LUMINANCE,
                           y_tex_w, height, y_data, y_sz, size_changed);
            if (uv_interleaved) {
                uploadPlanePBO(GL_TEXTURE1, gl_tex_u_,
                               gl_pbo_y_[2+wi], gl_pbo_y_[2+ri],
                               GL_RG8, GL_RG,
                               uv_tex_w, uv_h, uv_upload_data, uv_upload_sz,
                               size_changed || uv_layout_changed);
            } else {
                uploadPlanePBO(GL_TEXTURE1, gl_tex_u_,
                               gl_pbo_y_[2+wi], gl_pbo_y_[2+ri],
                               GL_LUMINANCE, GL_LUMINANCE,
                               uv_tex_w, uv_h, u_data, uv_upload_sz,
                               size_changed || uv_layout_changed);
                uploadPlanePBO(GL_TEXTURE2, gl_tex_v_,
                               gl_pbo_y_[4+wi], gl_pbo_y_[4+ri],
                               GL_LUMINANCE, GL_LUMINANCE,
                               v_tex_w, uv_h, v_data, v_sz,
                               size_changed || uv_layout_changed);
            }
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
        if (size_changed || uv_layout_changed) {
            if (uv_interleaved) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, uv_tex_w, uv_h,
                             0, GL_RG, GL_UNSIGNED_BYTE, uv_upload_data);
            } else {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, uv_tex_w, uv_h,
                             0, GL_LUMINANCE, GL_UNSIGNED_BYTE, u_data);
            }
        } else {
            if (uv_interleaved) {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uv_tex_w, uv_h,
                                GL_RG, GL_UNSIGNED_BYTE, uv_upload_data);
            } else {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, uv_tex_w, uv_h,
                                GL_LUMINANCE, GL_UNSIGNED_BYTE, u_data);
            }
        }
        if (!uv_interleaved) {
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
        PBOW("evt=slow_upload upload_ms=%" PRId64 " video_w=%d video_h=%d y_stride=%d use_pbo=%d",
             upload_ms, width, height, y_tex_w, use_pbo ? 1 : 0);
    }

    gl_last_video_w_ = width;
    gl_last_video_h_ = height;
    gl_last_uv_interleaved_ = uv_interleaved;
    gl_last_uv_swap_ = uv_swap;
    gl_last_uv_tex_w_ = uv_tex_w;

    // --- Compute vertex / texcoord layout ---
    float y_u_scale  = (float)width      / (float)y_tex_w;
    float y_v_scale  = 1.0f;
    float uv_u_scale = (float)(width / 2) / (float)(uv_tex_w > 0 ? uv_tex_w : width / 2);

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
    if (gl_uniform_uv_interleaved_ >= 0) {
        glUniform1i(gl_uniform_uv_interleaved_, uv_interleaved ? 1 : 0);
    }
    if (gl_uniform_uv_swap_ >= 0) {
        glUniform1i(gl_uniform_uv_swap_, uv_swap ? 1 : 0);
    }

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

    // Cache last frame for surface hot-swap redraw.
    // 4K + high-rate playback is sensitive to extra memcpy bandwidth, so we
    // throttle cache updates for large frames instead of copying every frame.
    {
        int y_stride  = y_linesize > 0 ? y_linesize : width;
        int uv_stride = u_linesize > 0 ? u_linesize : (width + 1) / 2;
        int vs_stride = uv_interleaved ? 0 : (v_linesize > 0 ? v_linesize : (width + 1) / 2);
        int y_cache_h = height;
        int uv_cache_h = (height + 1) / 2;
        int y_cache_sz  = y_stride  * y_cache_h;
        int u_cache_sz  = uv_stride * uv_cache_h;
        int v_cache_sz  = uv_interleaved ? 0 : (vs_stride * uv_cache_h);

        auto now_cache_ms = []() -> int64_t {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        };
        int64_t now_ms = now_cache_ms();
        bool size_changed_for_cache = (width != last_frame_width_ || height != last_frame_height_);
        bool empty_cache = last_frame_y_.empty() || last_frame_u_.empty() || last_frame_v_.empty();
        int64_t cache_interval_ms = 0;
        if (width >= 3840 || height >= 2160) {
            cache_interval_ms = 120;
        } else if (width >= 2560 || height >= 1440) {
            cache_interval_ms = 66;
        }
        bool should_cache = size_changed_for_cache || empty_cache ||
                            cache_interval_ms <= 0 ||
                            (now_ms - last_frame_cache_ms_) >= cache_interval_ms;

        if (should_cache) {
            if (size_changed_for_cache ||
                static_cast<int>(last_frame_y_.size()) != y_cache_sz ||
                static_cast<int>(last_frame_u_.size()) != u_cache_sz ||
                static_cast<int>(last_frame_v_.size()) != v_cache_sz) {
                last_frame_y_.resize(y_cache_sz);
                last_frame_u_.resize(u_cache_sz);
                last_frame_v_.resize(v_cache_sz);
            }
            if (uv_interleaved) {
                if (isLikelyValidPtr(y_data) && isLikelyValidPtr(u_data)) {
                    memcpy(last_frame_y_.data(), y_data, y_cache_sz);
                    memcpy(last_frame_u_.data(), u_data, u_cache_sz);
                }
            } else if (isLikelyValidPtr(y_data) && isLikelyValidPtr(u_data) && isLikelyValidPtr(v_data)) {
                memcpy(last_frame_y_.data(), y_data, y_cache_sz);
                memcpy(last_frame_u_.data(), u_data, u_cache_sz);
                memcpy(last_frame_v_.data(), v_data, v_cache_sz);
            }
            last_frame_width_    = width;
            last_frame_height_   = height;
            last_frame_y_stride_ = y_linesize;
            last_frame_u_stride_ = u_linesize;
            last_frame_v_stride_ = v_linesize;
            last_frame_cache_ms_ = now_ms;
        }
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

    // Estimate the hardware output queue latency (bytes already enqueued but
    // not yet heard).  The render thread subtracts this from the audio master
    // clock so it compares video PTS against "what the user is actually hearing"
    // rather than "what has been submitted to the driver".  Mirrors iOS logic in
    // HXCPlayerControl.mm (_audioOutputLatencySec).
    {
        double bytes_per_sec = (double)sample_rate * channels * 2.0; // 16-bit PCM
        double queued_sec    = (bytes_per_sec > 0.0)
                               ? (double)audio_buffer_size_ / bytes_per_sec
                               : 0.0;
        audio_output_latency_sec_ = std::min(0.200, std::max(0.0, queued_sec * 0.85));
        LOGI("Audio output latency estimate: %.1f ms", audio_output_latency_sec_ * 1000.0);
    }

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
    // Hold audio_mutex_ for the entire callback body.
    // The destructor and openURL both acquire this lock BEFORE destroying/stopping
    // the core, so swr_convert() inside player_core_get_audio_data() is guaranteed
    // to finish before the SwrContext is freed.
    // Keep audio_mutex_ held until after Enqueue:
    // destroyAudioOutput/openURL acquire the same lock before tearing down core/OpenSL,
    // so this prevents enqueue-on-destroy races.
    std::lock_guard<std::mutex> lock(audio_mutex_);

    if (!audio_active_ || !player_core_ || audio_buffer_size_ == 0) {
        int sz = audio_buffer_size_ > 0 ? audio_buffer_size_ : 4096;
        memset(audio_buffer_, 0, sz);
        (*bq)->Enqueue(bq, audio_buffer_, sz);
        return;
    }

    // Drain the core audio queue into the output buffer (swr_convert runs here,
    // safely protected by audio_mutex_).
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
    if (total_bytes_read > 0) {
        if (total_bytes_read < audio_buffer_size_) {
            audio_partial_count_++;
            memset(audio_buffer_ + total_bytes_read, 0,
                   audio_buffer_size_ - total_bytes_read);
        }
    } else {
        audio_underrun_count_++;
        memset(audio_buffer_, 0, audio_buffer_size_);
    }

    if (audio_cb_count_ % 200 == 0) {
        double pos = player_core_get_position(player_core_);
        if (audio_underrun_count_ > 0 || audio_partial_count_ > 0) {
            LOGW("[audio] cb=%d pos=%.1fs underrun=%d partial=%d (last 200)",
                 audio_cb_count_, pos, audio_underrun_count_, audio_partial_count_);
        }
        audio_underrun_count_ = 0;
        audio_partial_count_  = 0;
    }

    (*bq)->Enqueue(bq, audio_buffer_, audio_buffer_size_);
}
