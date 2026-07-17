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
//   0 = DEBUG + INFO + WARN + ERROR
//   1 = INFO  + WARN + ERROR
//   2 = WARN  + ERROR
//   3 = ERROR only
// Keep default as WARN for release.
#ifndef HXC_PLAYER_RUNTIME_LOG_LEVEL
#define HXC_PLAYER_RUNTIME_LOG_LEVEL 2
#endif
static std::atomic<int> g_hxc_runtime_log_level{HXC_PLAYER_RUNTIME_LOG_LEVEL};

#define LOGD(...) do { \
    if (g_hxc_runtime_log_level.load(std::memory_order_relaxed) <= 0) { \
        __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__); \
    } \
} while (0)

#define LOGI(...) do { \
    if (g_hxc_runtime_log_level.load(std::memory_order_relaxed) <= 1) { \
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); \
    } \
} while (0)

#define LOGW(...) do { \
    if (g_hxc_runtime_log_level.load(std::memory_order_relaxed) <= 2) { \
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__); \
    } \
} while (0)

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Independent tags for focused troubleshooting (adb logcat -s HXCSDK_PERF HXCSDK_SYNC ...).
#define LOG_TAG_PERF   "HXCSDK_PERF"
#define LOG_TAG_SYNC   "HXCSDK_SYNC"
#define LOG_TAG_DECODE "HXCSDK_DECODE"
#define LOG_TAG_PBO    "HXCSDK_PBO"
#define LOG_TAG_SEEK_WATCH "HXCSDK_SEEK_WATCH"

#define TAGD(TAG, ...) do { \
    if (g_hxc_runtime_log_level.load(std::memory_order_relaxed) <= 0) { \
        __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__); \
    } \
} while (0)

#define TAGI(TAG, ...) do { \
    if (g_hxc_runtime_log_level.load(std::memory_order_relaxed) <= 1) { \
        __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__); \
    } \
} while (0)

#define TAGW(TAG, ...) do { \
    if (g_hxc_runtime_log_level.load(std::memory_order_relaxed) <= 2) { \
        __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__); \
    } \
} while (0)

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
#define DECODEW(...)    TAGW(LOG_TAG_DECODE, __VA_ARGS__)
#define PBOD(...)       TAGD(LOG_TAG_PBO, __VA_ARGS__)
#define PBOI(...)       ((void)0)
#define PBOW(...)       TAGW(LOG_TAG_PBO, __VA_ARGS__)
#define SEEKW(...)      TAGW(LOG_TAG_SEEK_WATCH, __VA_ARGS__)
#define PBOI_RATE(...)  ((void)0)
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

extern "C" void hxc_sdk_set_runtime_log_level(int level) {
    int clamped = level;
    if (clamped < 0) clamped = 0;
    if (clamped > 3) clamped = 3;
    g_hxc_runtime_log_level.store(clamped, std::memory_order_relaxed);
}

extern "C" int hxc_sdk_get_runtime_log_level() {
    return g_hxc_runtime_log_level.load(std::memory_order_relaxed);
}

namespace {
// Process-wide guard for player_core_open_*.
// Some FFmpeg/mbedtls paths are unstable under multi-instance concurrent opens.
std::mutex g_player_core_open_mutex;

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

inline double clampd(double value, double min_value, double max_value) {
    return std::max(min_value, std::min(max_value, value));
}

inline int64_t clampi64(int64_t value, int64_t min_value, int64_t max_value) {
    return std::max(min_value, std::min(max_value, value));
}

inline int normalize_audio_output_channels(int source_channels) {
    if (source_channels <= 0) return 0;
    // OpenSL ES buffer queue is most stable with mono/stereo across Android devices.
    // Decode-side swr downmixes 2+ source channels (3.0, 5.1, etc.) to stereo.
    return source_channels == 1 ? 1 : 2;
}

inline bool is_likely_local_uri(const char* url) {
    if (!url || !url[0]) return false;
    if (strncmp(url, "file://", 7) == 0) return true;
    if (strncmp(url, "content://", 10) == 0) return true;
    if (url[0] == '/') return true;
    if (((url[0] >= 'a' && url[0] <= 'z') || (url[0] >= 'A' && url[0] <= 'Z')) &&
        url[1] == ':' && (url[2] == '\\' || url[2] == '/')) return true;
    return false;
}

inline const char* seek_phase_name(int phase) {
    switch (phase) {
        case AndroidPlayer::SEEK_PHASE_PRIME: return "PRIME";
        case AndroidPlayer::SEEK_PHASE_CONVERGE: return "CONVERGE";
        case AndroidPlayer::SEEK_PHASE_VERIFY: return "VERIFY";
        case AndroidPlayer::SEEK_PHASE_RESUME: return "RESUME";
        case AndroidPlayer::SEEK_PHASE_FAILOVER: return "FAILOVER";
        case AndroidPlayer::SEEK_PHASE_IDLE:
        default:
            return "IDLE";
    }
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

// RAII guard for open lifecycle marker; guarantees cleanup on all return paths.
struct OpenLifecycleGuard {
    std::atomic<bool>& open_in_progress;
    std::atomic<bool>& pending_play_after_open;

    OpenLifecycleGuard(std::atomic<bool>& open_flag, std::atomic<bool>& pending_flag)
        : open_in_progress(open_flag), pending_play_after_open(pending_flag) {
        open_in_progress.store(true, std::memory_order_release);
        pending_play_after_open.store(false, std::memory_order_release);
    }

    ~OpenLifecycleGuard() {
        open_in_progress.store(false, std::memory_order_release);
    }
};
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
    std::lock_guard<std::mutex> api_lock(api_mutex_);
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
    std::lock_guard<std::mutex> api_lock(api_mutex_);
    if (!player_core_) {
        LOGE("Player core not initialized");
        return false;
    }
    OpenLifecycleGuard open_lifecycle_guard(open_in_progress_, pending_play_after_open_);

    LOGI("[open] openURL start_pos=%.3f url=%s", start_position, url ? url : "(null)");
    has_pending_playback_completed_.store(false, std::memory_order_release);
    playback_completed_latched_.store(false, std::memory_order_release);
    has_pending_error_.store(false, std::memory_order_release);
    suppress_secure_hw_probe_errors_.store(false, std::memory_order_release);
    open_start_position_sec_.store(start_position, std::memory_order_release);
    open_requested_at_ms_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(),
            std::memory_order_release);
    resetAudioHealthStateForOpen("openURL");
    bool local_source = is_likely_local_uri(url);
    source_local_active_.store(local_source, std::memory_order_release);
    source_encrypted_active_.store(false, std::memory_order_release);
    SYNCI("evt=open_source_profile method=openURL local=%d encrypted=%d secure=%d",
          local_source ? 1 : 0, 0, 0);
    // Start every open session from a clean loading baseline.
    // If previous session ended in seek/replay edge paths, stale loading=true
    // can leak into the next session and block upper-layer replay guards.
    bool stale_loading = is_loading_.exchange(false, std::memory_order_acq_rel);
    if (stale_loading) {
        LOGI("[open] clear stale loading=true before open switch");
    }

    // Always stop first so the core FSM is in a clean IDLE state before open.
    // This is critical for replay: when playback ends the core reaches a terminal
    // state (state==-1) and a fresh open() will fail unless we reset it first.
    int cur_state = player_core_get_state(player_core_);
    if (cur_state != 0) { // 0 == IDLE
        LOGI("[open] pre-stop core (state=%d) before open", cur_state);
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        suppress_transient_loading_false_.store(true, std::memory_order_release);
        suppress_transient_loading_false_until_ms_.store(now_ms + 1200, std::memory_order_release);

        // 1. Stop OpenSL ES output to prevent new callbacks from being queued.
        audio_start_pending_.store(false, std::memory_order_release);
        setOpenSLESPlayState(SL_PLAYSTATE_STOPPED, false);

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
    resetRenderStateForStreamSwitch();

    DECODEI("evt=open method=openURL decode_mode=%s",
            decode_mode_ == 1 ? "hardware" : "software");
    player_core_set_decode_mode(player_core_,
                                decode_mode_ == 1 ? PLAYER_DECODE_MODE_HARDWARE
                                                  : PLAYER_DECODE_MODE_SOFTWARE);

    int result = -1;
    {
        std::lock_guard<std::mutex> open_guard(g_player_core_open_mutex);
        result = player_core_open_with_start_position(player_core_, url, start_position);
    }
    
    if (result == 0) {
        LOGI("[open] openURL OK");
        ensureAudioOutputForCurrentStream();
        player_core_pause(player_core_);
        bool hw_active = player_core_is_video_hardware_decoding(player_core_) != 0;
        const char* decode_diag = player_core_get_video_decode_diagnostic(player_core_);
        int open_video_w = player_core_get_video_width(player_core_);
        int open_video_h = player_core_get_video_height(player_core_);
        DECODEI("evt=open_result method=openURL requested=%s hw_active=%d final_mode=%s diag=%s",
                decode_mode_ == 1 ? "hardware" : "software",
                hw_active ? 1 : 0,
                hw_active ? "hardware" : "software",
                decode_diag ? decode_diag : "");
        if (!hw_active && (open_video_w >= 7680 || open_video_h >= 4320)) {
            DECODEW("evt=openurl_8k_software_decode_risk video_w=%d video_h=%d requested=%s diag=%s",
                    open_video_w,
                    open_video_h,
                    decode_mode_ == 1 ? "hardware" : "software",
                    decode_diag ? decode_diag : "");
        }
        // Reset sync state for the new stream
        sync_warmup_frames_.store(20, std::memory_order_release);
        last_sync_video_pts_ = std::numeric_limits<double>::quiet_NaN();
        secure_session_active_.store(false, std::memory_order_release);
        source_local_active_.store(local_source, std::memory_order_release);
        source_encrypted_active_.store(false, std::memory_order_release);
        secure_forward_seek_bias_sec_.store(0.0, std::memory_order_release);
        secure_forward_seek_bias_hits_.store(0, std::memory_order_release);
        secure_forward_seek_bias_last_update_ms_.store(0, std::memory_order_release);
        audio_rebuffer_pending_.store(false, std::memory_order_release);
        audio_rebuffer_deadline_ms_ = 0;
        audio_rebuffer_paused_at_ms_ = 0;
        audio_rebuffer_min_resume_at_ms_ = 0;
        audio_rebuffer_cooldown_until_ms_ = 0;
        audio_underrun_streak_started_ms_.store(0, std::memory_order_release);
        last_audio_underrun_ms_.store(0, std::memory_order_release);
        audio_av_split_started_ms_.store(0, std::memory_order_release);
        audio_av_split_forced_pause_.store(false, std::memory_order_release);
        resetVideoEmptyStallRecovery("openURL_ok");
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
        seek_resume_stable_hits_.store(0, std::memory_order_release);
        seek_started_at_ms_ = 0;
        seek_lower_bound_drop_count_ = 0;
        consecutive_drop_count_ = 0;
        first_frame_rendered_.store(false, std::memory_order_release);
        first_frame_wait_started_ms_ = 0;
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

bool AndroidPlayer::openWithCustomHTTP(const char* url, int timeout_ms, int max_retries,
                                       bool encrypted_file, double start_position) {
    std::lock_guard<std::mutex> api_lock(api_mutex_);
    if (!player_core_) {
        LOGE("Player core not initialized");
        return false;
    }
    const double normalized_start_position =
            std::isfinite(start_position) && start_position > 0.0 ? start_position : 0.0;
    OpenLifecycleGuard open_lifecycle_guard(open_in_progress_, pending_play_after_open_);
    LOGI("[open] openWithCustomHTTP start_pos=%.3f url=%s encrypted=%d",
         normalized_start_position, url ? url : "(null)", encrypted_file ? 1 : 0);
    has_pending_playback_completed_.store(false, std::memory_order_release);
    playback_completed_latched_.store(false, std::memory_order_release);
    open_start_position_sec_.store(normalized_start_position, std::memory_order_release);
    open_requested_at_ms_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(),
            std::memory_order_release);
    resetAudioHealthStateForOpen("openWithCustomHTTP");
    source_local_active_.store(false, std::memory_order_release);
    source_encrypted_active_.store(encrypted_file, std::memory_order_release);
    SYNCI("evt=open_source_profile method=openWithCustomHTTP local=%d encrypted=%d secure=%d",
          0, encrypted_file ? 1 : 0, 0);
    bool stale_loading = is_loading_.exchange(false, std::memory_order_acq_rel);
    if (stale_loading) {
        LOGI("[open] clear stale loading=true before custom HTTP open switch");
    }
    int cur_state = player_core_get_state(player_core_);
    if (cur_state != 0) { // 0 == IDLE
        LOGI("[open] pre-stop core (state=%d) before custom HTTP open", cur_state);
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        suppress_transient_loading_false_.store(true, std::memory_order_release);
        suppress_transient_loading_false_until_ms_.store(now_ms + 1200, std::memory_order_release);
        audio_start_pending_.store(false, std::memory_order_release);
        setOpenSLESPlayState(SL_PLAYSTATE_STOPPED, false);
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            // Lock acquired means no callback is in swr_convert right now.
        }
        player_core_stop(player_core_);
    }
    resetRenderStateForStreamSwitch();
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
    source.start_position = normalized_start_position;
    source.mode = PLAYER_DATA_SOURCE_MODE_CUSTOM_HTTP;
    source.encrypted_file = encrypted_file ? 1 : 0;
    source.secure_headers = nullptr;

    int result = -1;
    {
        std::lock_guard<std::mutex> open_guard(g_player_core_open_mutex);
        result = player_core_open_with_mode(player_core_, &source, &config);
    }

    if (result == 0) {
        LOGI("Custom HTTP opened successfully");
        player_core_seek(player_core_, normalized_start_position);
        player_core_anchor_clock(player_core_, normalized_start_position);
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
        secure_session_active_.store(false, std::memory_order_release);
        source_local_active_.store(false, std::memory_order_release);
        source_encrypted_active_.store(encrypted_file, std::memory_order_release);
        secure_forward_seek_bias_sec_.store(0.0, std::memory_order_release);
        secure_forward_seek_bias_hits_.store(0, std::memory_order_release);
        secure_forward_seek_bias_last_update_ms_.store(0, std::memory_order_release);
        audio_av_split_started_ms_.store(0, std::memory_order_release);
        audio_av_split_forced_pause_.store(false, std::memory_order_release);
        resetVideoEmptyStallRecovery("openWithCustomHTTP_ok");
        render_cv_.notify_one();
        return true;
    } else {
        LOGE("Failed to open with custom HTTP: %d", result);
        return false;
    }
}

bool AndroidPlayer::openWithCustomFile(const char* path, size_t avio_buffer_size,
                                       bool encrypted_file, double start_position) {
    std::lock_guard<std::mutex> api_lock(api_mutex_);
    if (!player_core_) {
        LOGE("Player core not initialized");
        return false;
    }
    const double normalized_start_position =
            std::isfinite(start_position) && start_position > 0.0 ? start_position : 0.0;
    OpenLifecycleGuard open_lifecycle_guard(open_in_progress_, pending_play_after_open_);
    LOGI("[open] openWithCustomFile path=%s start=%.3f encrypted=%d",
         path ? path : "(null)", normalized_start_position, encrypted_file ? 1 : 0);
    has_pending_playback_completed_.store(false, std::memory_order_release);
    playback_completed_latched_.store(false, std::memory_order_release);
    open_start_position_sec_.store(normalized_start_position, std::memory_order_release);
    open_requested_at_ms_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(),
            std::memory_order_release);
    resetAudioHealthStateForOpen("openWithCustomFile");
    source_local_active_.store(true, std::memory_order_release);
    source_encrypted_active_.store(encrypted_file, std::memory_order_release);
    SYNCI("evt=open_source_profile method=openWithCustomFile local=%d encrypted=%d secure=%d",
          1, encrypted_file ? 1 : 0, 0);
    bool stale_loading = is_loading_.exchange(false, std::memory_order_acq_rel);
    if (stale_loading) {
        LOGI("[open] clear stale loading=true before custom file open switch");
    }
    int cur_state = player_core_get_state(player_core_);
    if (cur_state != 0) { // 0 == IDLE
        LOGI("[open] pre-stop core (state=%d) before custom file open", cur_state);
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        suppress_transient_loading_false_.store(true, std::memory_order_release);
        suppress_transient_loading_false_until_ms_.store(now_ms + 1200, std::memory_order_release);
        audio_start_pending_.store(false, std::memory_order_release);
        setOpenSLESPlayState(SL_PLAYSTATE_STOPPED, false);
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            // Lock acquired means no callback is in swr_convert right now.
        }
        player_core_stop(player_core_);
    }
    resetRenderStateForStreamSwitch();
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
    source.start_position = normalized_start_position;
    source.mode = PLAYER_DATA_SOURCE_MODE_CUSTOM_FILE;
    source.encrypted_file = encrypted_file ? 1 : 0;
    source.secure_headers = nullptr;

    int result = -1;
    {
        std::lock_guard<std::mutex> open_guard(g_player_core_open_mutex);
        result = player_core_open_with_mode(player_core_, &source, &config);
    }

    if (result == 0) {
        LOGI("Custom file opened successfully");
        player_core_seek(player_core_, normalized_start_position);
        player_core_anchor_clock(player_core_, normalized_start_position);
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
        secure_session_active_.store(false, std::memory_order_release);
        source_local_active_.store(true, std::memory_order_release);
        source_encrypted_active_.store(encrypted_file, std::memory_order_release);
        secure_forward_seek_bias_sec_.store(0.0, std::memory_order_release);
        secure_forward_seek_bias_hits_.store(0, std::memory_order_release);
        secure_forward_seek_bias_last_update_ms_.store(0, std::memory_order_release);
        audio_av_split_started_ms_.store(0, std::memory_order_release);
        audio_av_split_forced_pause_.store(false, std::memory_order_release);
        resetVideoEmptyStallRecovery("openWithCustomFile_ok");
        render_cv_.notify_one();
        return true;
    } else {
        LOGE("Failed to open with custom file: %d", result);
        return false;
    }
}

bool AndroidPlayer::openWithSecureSession(const char* url,
                                          double start_position,
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
    std::lock_guard<std::mutex> api_lock(api_mutex_);
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
    OpenLifecycleGuard open_lifecycle_guard(open_in_progress_, pending_play_after_open_);
    LOGI("[open] openWithSecureSession start_pos=%.3f url=%s", start_position, url ? url : "(null)");
    has_pending_playback_completed_.store(false, std::memory_order_release);
    playback_completed_latched_.store(false, std::memory_order_release);
    has_pending_error_.store(false, std::memory_order_release);
    suppress_secure_hw_probe_errors_.store(false, std::memory_order_release);
    open_start_position_sec_.store(start_position, std::memory_order_release);
    open_requested_at_ms_.store(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count(),
            std::memory_order_release);
    resetAudioHealthStateForOpen("openWithSecureSession");
    source_local_active_.store(false, std::memory_order_release);
    source_encrypted_active_.store(true, std::memory_order_release);
    SYNCI("evt=open_source_profile method=openWithSecureSession local=%d encrypted=%d secure=%d",
          0, 1, 1);
    // Keep secure-open behavior consistent with openURL:
    // clear stale loading and force core back to IDLE before reopen.
    bool stale_loading = is_loading_.exchange(false, std::memory_order_acq_rel);
    if (stale_loading) {
        LOGI("[open] clear stale loading=true before secure open switch");
    }
    int cur_state = player_core_get_state(player_core_);
    if (cur_state != 0) { // 0 == IDLE
        LOGI("[open] pre-stop core (state=%d) before secure open", cur_state);
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        suppress_transient_loading_false_.store(true, std::memory_order_release);
        suppress_transient_loading_false_until_ms_.store(now_ms + 1200, std::memory_order_release);
        audio_start_pending_.store(false, std::memory_order_release);
        setOpenSLESPlayState(SL_PLAYSTATE_STOPPED, false);
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
            // Lock acquired means no callback is in swr_convert right now.
        }
        player_core_stop(player_core_);
    }
    resetRenderStateForStreamSwitch();
    bool user_pref_hw = (decode_mode_ == 1);
    PlayerDataSourceConfigC config{};
    config.timeout_ms = 30000;
    config.max_retries = 3;
    config.cache_size = 3 * 1024 * 1024;
    config.avio_buffer_size = 256 * 1024;
    PlayerDataSourceC source{};
    source.url = url;
    source.start_position = start_position;
    source.mode = PLAYER_DATA_SOURCE_MODE_SECURE_HLS;
    source.encrypted_file = 0;
    source.secure_headers = secure_headers;

    auto stop_core_after_failed_open = [this](const char* reason) {
        LOGW("[open] secure attempt cleanup: %s", reason ? reason : "");
        suppress_secure_hw_probe_errors_.store(false, std::memory_order_release);
        audio_start_pending_.store(false, std::memory_order_release);
        audio_rebuffer_pending_.store(false, std::memory_order_release);
        audio_underrun_streak_started_ms_.store(0, std::memory_order_release);
        last_audio_underrun_ms_.store(0, std::memory_order_release);
        audio_av_split_started_ms_.store(0, std::memory_order_release);
        audio_av_split_forced_pause_.store(false, std::memory_order_release);
        resetVideoEmptyStallRecovery("secure_failed_open_cleanup");
        first_frame_wait_started_ms_ = 0;
        audio_start_deadline_ms_ = 0;
        setOpenSLESPlayState(SL_PLAYSTATE_STOPPED, false);
        {
            std::lock_guard<std::mutex> lock(audio_mutex_);
        }
        if (player_core_) {
            player_core_stop(player_core_);
        }
        resetRenderStateForStreamSwitch();
    };

    player_core_apply_secure_playback_profile(player_core_);
    // SecureHLS stability policy: keep software decode fixed. Current encrypted HLS
    // streams are not compatible with the Android MediaCodec path in this SDK build.
    PlayerDecodeModeC secure_effective_mode = PLAYER_DECODE_MODE_SOFTWARE;
    DECODEI("evt=secure_decode_policy mode=fixed_software user_pref=%s reason=secure_hls_hw_unsupported",
            user_pref_hw ? "hardware" : "software");
    DECODEI("evt=open method=openWithSecureSession decode_mode=software user_pref=%s start=%.3f",
            user_pref_hw ? "hardware" : "software",
            start_position);
    player_core_set_decode_mode(player_core_, secure_effective_mode);

    int result = -1;
    {
        std::lock_guard<std::mutex> open_guard(g_player_core_open_mutex);
        result = player_core_open_with_mode(player_core_, &source, &config);
    }
    bool attempt_video_opened = result == 0 && player_core_is_video_stream_opened(player_core_) != 0;
    bool attempt_audio_opened = result == 0 && player_core_is_audio_stream_opened(player_core_) != 0;
    bool attempt_hw_active = result == 0 && player_core_is_video_hardware_decoding(player_core_) != 0;
    const char* attempt_decode_diag = player_core_get_video_decode_diagnostic(player_core_);
    DECODEI("evt=secure_open_attempt_result mode=software reason=secure_hls_fixed_software ret=%d video_opened=%d audio_opened=%d hw_active=%d diag=%s",
            result,
            attempt_video_opened ? 1 : 0,
            attempt_audio_opened ? 1 : 0,
            attempt_hw_active ? 1 : 0,
            attempt_decode_diag ? attempt_decode_diag : "");
    if (result == 0) {
        bool video_opened = player_core_is_video_stream_opened(player_core_) != 0;
        if (!video_opened) {
            const char* decode_diag = player_core_get_video_decode_diagnostic(player_core_);
            LOGE("SecureHLS opened without video stream after final attempt, diag=%s",
                 decode_diag ? decode_diag : "");
            stop_core_after_failed_open("secure_final_no_video_stream");
            return false;
        }
        if (start_position > 0.001) {
            // Fallback guard: some secure-open paths may ignore initial start_time.
            // Apply a post-open seek to enforce first-start progress.
            player_core_seek(player_core_, start_position);
            SYNCI("evt=secure_open_apply_startpos start=%.3f", start_position);
        }
        ensureAudioOutputForCurrentStream();
        player_core_pause(player_core_);
        bool hw_active = player_core_is_video_hardware_decoding(player_core_) != 0;
        const char* decode_diag = player_core_get_video_decode_diagnostic(player_core_);
        DECODEI("evt=open_result method=openWithSecureSession requested=%s user_pref=%s hw_active=%d final_mode=%s diag=%s",
                secure_effective_mode == PLAYER_DECODE_MODE_HARDWARE ? "hardware" : "software",
                user_pref_hw ? "hardware" : "software",
                hw_active ? 1 : 0,
                hw_active ? "hardware" : "software",
                decode_diag ? decode_diag : "");
        sync_warmup_frames_.store(20, std::memory_order_release);
        last_sync_video_pts_ = std::numeric_limits<double>::quiet_NaN();
        secure_session_active_.store(true, std::memory_order_release);
        source_local_active_.store(false, std::memory_order_release);
        source_encrypted_active_.store(true, std::memory_order_release);
        secure_forward_seek_bias_sec_.store(0.0, std::memory_order_release);
        secure_forward_seek_bias_hits_.store(0, std::memory_order_release);
        secure_forward_seek_bias_last_update_ms_.store(0, std::memory_order_release);
        audio_av_split_started_ms_.store(0, std::memory_order_release);
        audio_av_split_forced_pause_.store(false, std::memory_order_release);
        resetVideoEmptyStallRecovery("openWithSecureSession_ok");
        render_cv_.notify_one();
        return true;
    }
    LOGE("Failed to open secure hls: %d", result);
    stop_core_after_failed_open("secure_final_open_failed");
    return false;
}

bool AndroidPlayer::openWithSecureHLS(const char* url,
                                      double start_position,
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
                                 start_position,
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
    auto do_play_locked = [this]() {
        if (!player_core_) return;
        pending_play_after_open_.store(false, std::memory_order_release);
        user_manual_pause_.store(false, std::memory_order_release);
        user_manual_pause_block_until_ms_.store(0, std::memory_order_release);
        has_pending_playback_completed_.store(false, std::memory_order_release);
        playback_completed_latched_.store(false, std::memory_order_release);

        int core_decode_mode = player_core_get_decode_mode(player_core_);
        LOGI("[ctrl] play: state=%d pos=%.3f", player_core_get_state(player_core_), player_core_get_position(player_core_));
        DECODEI("evt=play_start decode_mode=%s hw_active=%d",
                core_decode_mode == PLAYER_DECODE_MODE_HARDWARE ? "hardware" : "software",
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
        audio_rebuffer_cooldown_until_ms_ = 0;
        audio_av_split_started_ms_.store(0, std::memory_order_release);
        audio_av_split_forced_pause_.store(false, std::memory_order_release);
        resetVideoEmptyStallRecovery("play");
        bool seek_flow_active =
                seek_lower_bound_active_.load(std::memory_order_acquire) ||
                seek_recovery_active_.load(std::memory_order_acquire) ||
                seek_audio_wait_video_.load(std::memory_order_acquire) ||
                seek_force_resume_pending_.load(std::memory_order_acquire) ||
                seek_session_active_id_.load(std::memory_order_acquire) != 0;
        // Keep failover lifecycle decided by real seek gates/session flags below.
        // Forcing seek_flow_active=false in failover can prematurely clear session context.
        bool seek_started_while_paused = seek_started_while_paused_.load(std::memory_order_acquire);
        bool manual_play_upgrades_seek_resume = seek_flow_active && seek_started_while_paused;
        if (manual_play_upgrades_seek_resume) {
            // User explicitly pressed play during a paused-origin seek.
            // Upgrade current seek intent to resume-on-complete instead of waiting for timeout paths.
            seek_started_while_paused_.store(false, std::memory_order_release);
            seek_started_while_paused = false;
            player_core_set_play_when_ready(player_core_, 1);
            SYNCI_RATE(20, "evt=seek_manual_play_upgrade sid=%" PRIu64 " phase=%s",
                       seek_session_active_id_.load(std::memory_order_acquire),
                       seek_phase_name(seek_phase_.load(std::memory_order_acquire)));
        }
        bool allow_seek_autoresume = !(seek_flow_active && seek_started_while_paused);
        seek_resume_on_complete_.store(allow_seek_autoresume, std::memory_order_release);
        if (!allow_seek_autoresume) {
            SYNCI_RATE(20, "evt=seek_autoresume_suppressed_on_manual_play sid=%" PRIu64 " phase=%s",
                       seek_session_active_id_.load(std::memory_order_acquire),
                       seek_phase_name(seek_phase_.load(std::memory_order_acquire)));
        }
        if (!seek_flow_active) {
            resetSeekFlowState(true, true, false, false);
        } else {
            SYNCI_RATE(30, "evt=play_keep_seek_context sid=%" PRIu64 " phase=%s",
                       seek_session_active_id_.load(std::memory_order_acquire),
                       seek_phase_name(seek_phase_.load(std::memory_order_acquire)));
        }
        consecutive_drop_count_ = 0;
        severe_lag_start_ms_ = 0;
        severe_lag_audio_pause_start_ms_ = 0;

        if (playItf_) {
            bool video_stream_opened = player_core_is_video_stream_opened(player_core_) != 0;
            // For initial open: defer audio until first frame rendered to avoid
            // "loading hidden but black screen". For resumed playback (already rendered),
            // start audio immediately.
            int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (video_stream_opened && !first_frame_rendered_.load(std::memory_order_acquire)) {
                audio_start_pending_.store(true, std::memory_order_release);
                first_frame_wait_started_ms_ = now;
                // Keep deadline as a hard safety valve, but render loop will not use it
                // in early open phase until first-frame hard timeout is reached.
                audio_start_deadline_ms_ = now + 2600;
                LOGI("[ctrl] play: audio deferred until first video frame (deadline +2600ms)");
            } else {
                audio_start_pending_.store(false, std::memory_order_release);
                first_frame_wait_started_ms_ = 0;
                if (isAudioOutputEnabled()) {
                    primeAudioBufferQueue("play_immediate_resume", true);
                    SLresult r = setOpenSLESPlayStateWithRetry(SL_PLAYSTATE_PLAYING, true);
                    LOGI("[ctrl] play: audio immediate resume (video_opened=%d already_rendered=%d), result=%d",
                         video_stream_opened ? 1 : 0,
                         first_frame_rendered_.load(std::memory_order_acquire) ? 1 : 0,
                         r);
                }
            }
        } else {
            LOGD("No audio interface (audio disabled)");
        }
        LOGI("[ctrl] play: dispatched to core + audio");
    };

    std::unique_lock<std::mutex> api_lock(api_mutex_, std::try_to_lock);
    if (!api_lock.owns_lock()) {
        bool opening = open_in_progress_.load(std::memory_order_acquire);
        if (opening) {
            pending_play_after_open_.store(true, std::memory_order_release);
            user_manual_pause_.store(false, std::memory_order_release);
            user_manual_pause_block_until_ms_.store(0, std::memory_order_release);
            has_pending_playback_completed_.store(false, std::memory_order_release);
            playback_completed_latched_.store(false, std::memory_order_release);
            LOGI("[ctrl] play deferred: open_in_progress=1");
            render_cv_.notify_one();
            return;
        }
        api_lock.lock();
    }
    do_play_locked();
}

void AndroidPlayer::pause() {
    std::lock_guard<std::mutex> api_lock(api_mutex_);
    if (!player_core_) return;
    pending_play_after_open_.store(false, std::memory_order_release);

    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    user_manual_pause_.store(true, std::memory_order_release);
    // Manual pause should hard-block seek failover auto-resume attempts briefly.
    user_manual_pause_block_until_ms_.store(now + 5000, std::memory_order_release);
    LOGI("[ctrl] pause: state=%d pos=%.3f", player_core_get_state(player_core_), player_core_get_position(player_core_));
    // Cancel any pending deferred audio start.
    audio_start_pending_.store(false, std::memory_order_release);
    audio_rebuffer_pending_.store(false, std::memory_order_release);
    audio_rebuffer_deadline_ms_ = 0;
    audio_rebuffer_paused_at_ms_ = 0;
    audio_rebuffer_min_resume_at_ms_ = 0;
    audio_rebuffer_cooldown_until_ms_ = 0;
    audio_underrun_streak_started_ms_.store(0, std::memory_order_release);
    last_audio_underrun_ms_.store(0, std::memory_order_release);
    audio_av_split_started_ms_.store(0, std::memory_order_release);
    audio_av_split_forced_pause_.store(false, std::memory_order_release);
    resetVideoEmptyStallRecovery("pause");
    seek_resume_on_complete_.store(false, std::memory_order_release);
    is_loading_.store(false, std::memory_order_release);
    suppress_stale_loading_true_until_ms_.store(now + 5000, std::memory_order_release);
    // Keep pause semantic explicit: playWhenReady must be false after manual pause.
    player_core_set_play_when_ready(player_core_, 0);
    resetSeekFlowState(true, true, true, false);
    consecutive_drop_count_ = 0;
    first_frame_wait_started_ms_ = 0;
    severe_lag_start_ms_ = 0;
    severe_lag_audio_pause_start_ms_ = 0;
    player_core_pause(player_core_);

    if (playItf_) {
        SLresult result = setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, false);
        if (result != SL_RESULT_SUCCESS) {
            LOGE("Failed to set play state to PAUSED: %d", result);
        }
    }
}

void AndroidPlayer::stop() {
    std::lock_guard<std::mutex> api_lock(api_mutex_);
    if (!player_core_) return;
    pending_play_after_open_.store(false, std::memory_order_release);

    user_manual_pause_.store(false, std::memory_order_release);
    user_manual_pause_block_until_ms_.store(0, std::memory_order_release);
    LOGI("[ctrl] stop: state=%d pos=%.3f", player_core_get_state(player_core_), player_core_get_position(player_core_));
    audio_start_pending_.store(false, std::memory_order_release);
    audio_rebuffer_pending_.store(false, std::memory_order_release);
    audio_rebuffer_deadline_ms_ = 0;
    audio_rebuffer_paused_at_ms_ = 0;
    audio_rebuffer_min_resume_at_ms_ = 0;
    audio_rebuffer_cooldown_until_ms_ = 0;
    audio_underrun_streak_started_ms_.store(0, std::memory_order_release);
    last_audio_underrun_ms_.store(0, std::memory_order_release);
    audio_av_split_started_ms_.store(0, std::memory_order_release);
    audio_av_split_forced_pause_.store(false, std::memory_order_release);
    resetVideoEmptyStallRecovery("stop");
    seek_resume_on_complete_.store(false, std::memory_order_release);
    resetSeekFlowState(true, true, false, false);
    consecutive_drop_count_ = 0;
    first_frame_wait_started_ms_ = 0;
    first_frame_rendered_.store(false, std::memory_order_release);
    severe_lag_start_ms_ = 0;
    severe_lag_audio_pause_start_ms_ = 0;
    secure_session_active_.store(false, std::memory_order_release);
    secure_forward_seek_bias_sec_.store(0.0, std::memory_order_release);
    secure_forward_seek_bias_hits_.store(0, std::memory_order_release);
    secure_forward_seek_bias_last_update_ms_.store(0, std::memory_order_release);
    has_pending_playback_completed_.store(false, std::memory_order_release);
    playback_completed_latched_.store(false, std::memory_order_release);
    open_start_position_sec_.store(-1.0, std::memory_order_release);
    open_requested_at_ms_.store(0, std::memory_order_release);

    // Stop OpenSL ES first to prevent new callbacks from being queued,
    // then acquire audio_mutex_ to wait for any running callback to finish,
    // then stop the core (which resets the SwrContext used by swr_convert).
    if (playItf_) {
        setOpenSLESPlayState(SL_PLAYSTATE_STOPPED, false);
    }
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        // Holding the lock means no callback body is executing right now.
    }
    player_core_stop(player_core_);
}

void AndroidPlayer::setSeekPhase(int new_phase, const char* reason) {
    int old_phase = seek_phase_.exchange(new_phase, std::memory_order_acq_rel);
    if (old_phase == new_phase) {
        return;
    }
    uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
    SYNCI("evt=seek_phase_transition id=%" PRIu64 " from=%s to=%s reason=%s",
          sid,
          seek_phase_name(old_phase),
          seek_phase_name(new_phase),
          reason ? reason : "unknown");
}

void AndroidPlayer::resetSeekFlowState(bool clear_session_id,
                                       bool clear_paused_origin,
                                       bool reset_budgets,
                                       bool reset_fast_catchup) {
    seek_lower_bound_active_.store(false, std::memory_order_release);
    seek_lower_bound_deadline_ms_ = 0;
    seek_recovery_active_.store(false, std::memory_order_release);
    seek_recovery_deadline_ms_ = 0;
    seek_audio_wait_video_.store(false, std::memory_order_release);
    seek_audio_wait_deadline_ms_ = 0;
    seek_resume_stable_hits_.store(0, std::memory_order_release);
    seek_verify_hits_.store(0, std::memory_order_release);
    seek_force_resume_pending_.store(false, std::memory_order_release);
    seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
    seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
    seek_force_resume_retry_count_.store(0, std::memory_order_release);
    seek_force_resume_nudged_.store(false, std::memory_order_release);
    setSeekPhase(SEEK_PHASE_IDLE, "seek_flow_reset");
    if (clear_session_id) {
        seek_session_active_id_.store(0, std::memory_order_release);
    }
    if (clear_paused_origin) {
        seek_started_while_paused_.store(false, std::memory_order_release);
    }
    if (reset_fast_catchup) {
        seek_fast_catchup_frames_.store(0, std::memory_order_release);
        seek_nonsecure_4k_reseek_count_.store(0, std::memory_order_release);
        seek_catchup_deadline_ms_ = 0;
    }
    if (reset_budgets) {
        seek_failover_budget_left_.store(2, std::memory_order_release);
        seek_soft_rebuild_budget_left_.store(1, std::memory_order_release);
    }
    seek_started_at_ms_ = 0;
    seek_lower_bound_drop_count_ = 0;
}

bool AndroidPlayer::resolveSeekResumeOnComplete(int seek_resume_override,
                                                bool core_paused_now,
                                                bool user_manual_pause_now) {
    bool should_resume_on_complete =
            player_core_is_playing(player_core_) ||
            player_core_get_play_when_ready(player_core_) != 0;
    if (seek_resume_override == 1) {
        should_resume_on_complete = true;
        user_manual_pause_.store(false, std::memory_order_release);
        user_manual_pause_block_until_ms_.store(0, std::memory_order_release);
        player_core_set_play_when_ready(player_core_, 1);
    } else if (seek_resume_override == 0) {
        should_resume_on_complete = false;
        player_core_set_play_when_ready(player_core_, 0);
    } else if (core_paused_now || user_manual_pause_now) {
        should_resume_on_complete = false;
        // Pause-origin seek must keep paused intent explicit to avoid timeout fallback
        // reading stale playWhenReady=1 and entering force-resume paths.
        player_core_set_play_when_ready(player_core_, 0);
    }
    return should_resume_on_complete;
}

void AndroidPlayer::seekToWithIntent(double position, bool resume_after_seek) {
    seek_resume_intent_override_.store(resume_after_seek ? 1 : 0, std::memory_order_release);
    seekTo(position);
}

void AndroidPlayer::seekTo(double position) {
    std::lock_guard<std::mutex> api_lock(api_mutex_);
    if (!player_core_) return;
    has_pending_playback_completed_.store(false, std::memory_order_release);
    playback_completed_latched_.store(false, std::memory_order_release);
    resetVideoEmptyStallRecovery("seek");
    // Explicit seek overrides "open-start near tail" intent.
    open_start_position_sec_.store(-1.0, std::memory_order_release);

    bool manual_pause_blocked = false;
    if (user_manual_pause_.load(std::memory_order_acquire)) {
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t block_until = user_manual_pause_block_until_ms_.load(std::memory_order_acquire);
        manual_pause_blocked = block_until <= 0 || now < block_until;
    }
    if (!manual_pause_blocked &&
        player_core_get_pipeline_state(player_core_) == PLAYER_PIPELINE_STATE_ENDED) {
        player_core_set_play_when_ready(player_core_, 1);
        player_core_play(player_core_);
        LOGI("[ctrl] seekTo: resume from ENDED pipeline before seek");
    }

    double duration_now = player_core_get_duration(player_core_);
    if (std::isfinite(duration_now) && duration_now > 0.0) {
        double max_seek_target = std::max(0.0, duration_now - 0.35);
        if (position > max_seek_target) {
            SYNCI("evt=seek_target_clamp_native req=%.3f clamped=%.3f duration=%.3f",
                  position, max_seek_target, duration_now);
            position = max_seek_target;
        }
    }
    double seek_from = player_core_get_position(player_core_);
    double seek_span = std::fabs(position - seek_from);
    bool very_large_seek = seek_span > 180.0;
    bool secure_session = secure_session_active_.load(std::memory_order_acquire);
    bool local_source = source_local_active_.load(std::memory_order_acquire);
    bool encrypted_source = source_encrypted_active_.load(std::memory_order_acquire) || secure_session;
    int policy_video_w = seek_policy_video_w_.load(std::memory_order_acquire);
    int policy_video_h = seek_policy_video_h_.load(std::memory_order_acquire);
    bool known_4k_for_seek = policy_video_w >= 3840 || policy_video_h >= 2160;
    bool non_secure_4k_large_seek = known_4k_for_seek && !encrypted_source && seek_span >= 25.0;
    int core_state_now = player_core_get_state(player_core_);
    bool core_paused_now = core_state_now == PLAYER_STATE_PAUSED;
    bool user_manual_pause_now = user_manual_pause_.load(std::memory_order_acquire);
    int seek_resume_override = seek_resume_intent_override_.exchange(-1, std::memory_order_acq_rel);
    bool should_resume_on_complete = resolveSeekResumeOnComplete(
            seek_resume_override,
            core_paused_now,
            user_manual_pause_now);
    seek_started_while_paused_.store(!should_resume_on_complete, std::memory_order_release);
    LOGI("[ctrl] seekTo: %.3fs (current pos=%.3f state=%d autoplay_after_seek=%d)",
         position, seek_from, core_state_now, should_resume_on_complete ? 1 : 0);
    constexpr double kSeekNoopEpsilonSec = 0.12;
    if (seek_span <= kSeekNoopEpsilonSec) {
        // No-op seek: avoid creating a new seek session that can be stuck in timeout/failover.
        // Align playWhenReady with current intent and clear stale seek gates/session markers.
        resetSeekFlowState(true, true, false, false);
        seek_resume_on_complete_.store(should_resume_on_complete, std::memory_order_release);
        if (player_core_) {
            player_core_set_play_when_ready(player_core_, should_resume_on_complete ? 1 : 0);
            if (should_resume_on_complete) {
                if (!player_core_is_playing(player_core_)) {
                    player_core_play(player_core_);
                }
            } else {
                player_core_pause(player_core_);
            }
        }
        SYNCI("evt=seek_noop_settle from=%.3f target=%.3f span=%.3f autoplay_after_seek=%d",
              seek_from, position, seek_span, should_resume_on_complete ? 1 : 0);
        return;
    }
    seek_just_happened_.store(true, std::memory_order_release);
    sync_warmup_frames_.store(40, std::memory_order_release); // wider warmup after seek
    seek_from_sec_.store(seek_from, std::memory_order_release);
    seek_target_sec_.store(position, std::memory_order_release);
    seek_resume_on_complete_.store(should_resume_on_complete, std::memory_order_release);
    seek_force_resume_pending_.store(false, std::memory_order_release);
    seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
    seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
    seek_force_resume_retry_count_.store(0, std::memory_order_release);
    seek_force_resume_nudged_.store(false, std::memory_order_release);
    // Seek 后短窗口内优先快速丢弃明显早于目标的帧。SecureHLS 长 seek 需要更强 catch-up，
    // 否则 target-preroll 会变成多秒 loading。
    int fast_catchup_frames = very_large_seek ? 128 : 96;
    if (secure_session && position > seek_from + 0.5 && seek_span >= 120.0) {
        fast_catchup_frames = very_large_seek ? 260 : 180;
    }
    if (non_secure_4k_large_seek) {
        // Mature-player style: for 4K, keep video catch-up inside the native gate
        // instead of resuming audio early and letting video chase a moving clock.
        fast_catchup_frames = very_large_seek ? 320 : 220;
    }
    seek_fast_catchup_frames_.store(fast_catchup_frames, std::memory_order_release);
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    uint64_t prev_seek_sid = seek_session_active_id_.load(std::memory_order_acquire);
    int prev_seek_phase = seek_phase_.load(std::memory_order_acquire);
    if (prev_seek_sid != 0) {
        // Single-session controller: hard-stop previous seek context before starting a new one.
        resetSeekFlowState(true, false, true, false);
        secure_seek_precise_reseek_count_.store(0, std::memory_order_release);
        secure_seek_precise_reseek_cooldown_until_ms_ = 0;
        SYNCW("evt=seek_session_abort_old old_id=%" PRIu64 " old_phase=%s new_target=%.3f",
              prev_seek_sid, seek_phase_name(prev_seek_phase), position);
    }
    uint64_t seek_session_id = seek_session_seq_.fetch_add(1, std::memory_order_acq_rel) + 1;
    seek_session_active_id_.store(seek_session_id, std::memory_order_release);
    setSeekPhase(SEEK_PHASE_PRIME, "seek_begin_prime");
    seek_verify_hits_.store(0, std::memory_order_release);
    seek_started_at_ms_ = now;
    seek_lower_bound_drop_count_ = 0;
    secure_seek_precise_reseek_count_.store(0, std::memory_order_release);
    seek_nonsecure_4k_reseek_count_.store(0, std::memory_order_release);
    secure_seek_precise_reseek_cooldown_until_ms_ = 0;
    int failover_budget = local_source ? 1 : 2;
    if (encrypted_source) {
        // Encrypted seek: prefer quick settle/recover over long repeated retries.
        failover_budget = 1;
    } else if (non_secure_4k_large_seek) {
        failover_budget = 1;
    }
    seek_failover_budget_left_.store(failover_budget, std::memory_order_release);
    seek_soft_rebuild_budget_left_.store(1, std::memory_order_release);
    int64_t catchup_window_ms = very_large_seek ? 1200 : 900;
    if (secure_session && position > seek_from + 0.5 && seek_span >= 120.0) {
        catchup_window_ms = very_large_seek ? 2400 : 1800;
    }
    if (non_secure_4k_large_seek) {
        catchup_window_ms = very_large_seek ? 3600 : 2600;
    }
    seek_catchup_deadline_ms_ = now + catchup_window_ms;
    seek_lower_bound_active_.store(true, std::memory_order_release);
    int64_t lower_deadline_ms = (secure_session
                                 ? (very_large_seek ? secure_lower_bound_deadline_large_ms_
                                                    : secure_lower_bound_deadline_normal_ms_)
                                 : (local_source
                                    ? (very_large_seek ? 1400 : 1100)
                                    : (very_large_seek ? 1700 : 2200)));
    int64_t recovery_deadline_ms = (secure_session
                                    ? (very_large_seek ? secure_recovery_deadline_large_ms_
                                                       : secure_recovery_deadline_normal_ms_)
                                    : (local_source
                                       ? (very_large_seek ? 3200 : 2400)
                                       : (very_large_seek ? 3600 : 4200)));
    int64_t audio_wait_deadline_ms = (secure_session
                                      ? (very_large_seek ? secure_audio_wait_deadline_large_ms_
                                                         : secure_audio_wait_deadline_normal_ms_)
                                      : (local_source
                                         ? (very_large_seek ? 2600 : 1900)
                                         : (very_large_seek ? 3400 : 3800)));
    if (non_secure_4k_large_seek) {
        lower_deadline_ms = very_large_seek ? 2600 : 2000;
        recovery_deadline_ms = very_large_seek ? 5200 : 4200;
        audio_wait_deadline_ms = (position > seek_from + 0.5)
                                 ? (very_large_seek ? 6200 : 4800)
                                 : (very_large_seek ? 4800 : 3600);
    }
    // Key point for encrypted large forward seek:
    // cap converge deadlines to avoid long loading loops before failover.
    if (secure_session && (position > seek_from + 0.5) && very_large_seek) {
        lower_deadline_ms = std::min<int64_t>(lower_deadline_ms, 2600);
        recovery_deadline_ms = std::min<int64_t>(recovery_deadline_ms, 4600);
        audio_wait_deadline_ms = std::min<int64_t>(audio_wait_deadline_ms, 3600);
    }
    seek_lower_bound_deadline_ms_ = now + lower_deadline_ms;
    seek_recovery_active_.store(true, std::memory_order_release);
    seek_recovery_deadline_ms_ = now + recovery_deadline_ms;
    seek_audio_wait_video_.store(true, std::memory_order_release);
    seek_audio_wait_deadline_ms_ = now + audio_wait_deadline_ms;
    seek_resume_stable_hits_.store(0, std::memory_order_release);
    audio_rebuffer_pending_.store(false, std::memory_order_release);
    audio_rebuffer_deadline_ms_ = 0;
    audio_rebuffer_paused_at_ms_ = 0;
    audio_rebuffer_min_resume_at_ms_ = 0;
    audio_rebuffer_cooldown_until_ms_ = 0;
    if (playItf_ && isAudioOutputEnabled()) {
        SLresult r = setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, true);
        LOGI("[sync] seek pause audio waiting first target frame: result=%d", r);
    }
    consecutive_drop_count_ = 0;
    severe_lag_start_ms_ = 0;
    severe_lag_audio_pause_start_ms_ = 0;
    last_sync_video_pts_ = std::numeric_limits<double>::quiet_NaN();
    bool is_forward_seek = position > seek_from + 0.5;
    bool apply_secure_forward_preroll = secure_session && is_forward_seek && seek_span >= 120.0;
    double seek_dispatch_position = position;
    if (apply_secure_forward_preroll) {
        int secure_stall_streak = secure_seek_stall_streak_.load(std::memory_order_acquire);
        int64_t secure_stall_last_ms = secure_seek_stall_last_ms_.load(std::memory_order_acquire);
        bool recent_secure_stall = secure_stall_streak > 0
                && secure_stall_last_ms > 0
                && (now - secure_stall_last_ms) <= 25000;
        int64_t learned_last_ms = secure_forward_seek_bias_last_update_ms_.load(std::memory_order_acquire);
        double learned_bias_sec = secure_forward_seek_bias_sec_.load(std::memory_order_acquire);
        int learned_hits = secure_forward_seek_bias_hits_.load(std::memory_order_acquire);
        bool learned_stale = learned_last_ms <= 0 || (now - learned_last_ms) > secure_forward_preroll_bias_expire_ms_;
        if (learned_stale || !std::isfinite(learned_bias_sec) || learned_bias_sec < 0.0) {
            learned_bias_sec = 0.0;
            learned_hits = 0;
        }
        double span_bias_sec = std::max(0.0, seek_span) * secure_forward_preroll_span_gain_;
        double learned_component_sec = std::min(2.0,
                                                learned_bias_sec * (secure_forward_preroll_learned_gain_ * 0.25));
        double preroll_sec = secure_forward_preroll_base_sec_ + span_bias_sec + learned_component_sec;
        if (very_large_seek) {
            preroll_sec += 0.6;
        }
        // Keep forward seek close to target. iOS seeks directly to target and drops
        // off-target warmup frames; Android keeps a small preroll for HLS GOP variance,
        // but caps it tightly to avoid multi-second catch-up loading.
        double dynamic_preroll_cap = std::min(6.8, std::max(3.5, 3.5 + seek_span * 0.0011));
        preroll_sec = std::min(std::min(secure_forward_preroll_max_sec_, dynamic_preroll_cap),
                               std::max(3.0, preroll_sec));
        if (recent_secure_stall) {
            // Recent secure seek stalls imply over-aggressive preroll is hurting convergence.
            // Downscale preroll for a short window to avoid repeated long loading.
            double fallback_cap = secure_stall_streak >= 2 ? 4.5 : 5.5;
            preroll_sec = std::min(preroll_sec * 0.45, fallback_cap);
            preroll_sec = std::max(0.0, preroll_sec);
            SYNCI("evt=seek_secure_preroll_degrade streak=%d span=%.3f preroll=%.3f",
                  secure_stall_streak, seek_span, preroll_sec);
        }
        seek_dispatch_position = std::max(0.0, position - preroll_sec);
        SYNCI("evt=seek_secure_forward_preroll from=%.3f target=%.3f dispatch=%.3f span=%.3f preroll=%.3f learned=%.3f hits=%d stale=%d",
              seek_from, position, seek_dispatch_position, seek_span, preroll_sec, learned_bias_sec,
              learned_hits, learned_stale ? 1 : 0);
    }
    player_core_seek(player_core_, seek_dispatch_position);
    setSeekPhase(SEEK_PHASE_CONVERGE, "seek_dispatched_to_core");
    SYNCI("evt=seek_phase id=%" PRIu64 " phase=%s from=%.3f target=%.3f",
          seek_session_id,
          seek_phase_name(seek_phase_.load(std::memory_order_acquire)),
          seek_from,
          position);
    SYNCI("evt=seek_policy_init secure=%d local=%d encrypted=%d from=%.3f target=%.3f span=%.3f lower_deadline_ms=%" PRId64 " recovery_deadline_ms=%" PRId64 " audio_wait_deadline_ms=%" PRId64 " failover_budget=%d",
          secure_session ? 1 : 0,
          local_source ? 1 : 0,
          encrypted_source ? 1 : 0,
          seek_from,
          position,
          seek_span,
          seek_lower_bound_deadline_ms_ - now,
          seek_recovery_deadline_ms_ - now,
          seek_audio_wait_deadline_ms_ - now,
          seek_failover_budget_left_.load(std::memory_order_acquire));
    if (non_secure_4k_large_seek) {
        SYNCI("evt=seek_nonsecure_4k_policy from=%.3f target=%.3f span=%.3f local=%d known_w=%d known_h=%d fast_frames=%d catchup_ms=%" PRId64,
              seek_from, position, seek_span, local_source ? 1 : 0,
              policy_video_w,
              policy_video_h,
              fast_catchup_frames, catchup_window_ms);
    }
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
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    bool was_enabled = isAudioOutputEnabled();
    current_volume_.store(volume, std::memory_order_relaxed);
    bool muted = muted_.load(std::memory_order_relaxed);
    float effective_volume = muted ? 0.0f : volume;
    LOGD("Set volume: %f effective=%f muted=%d (core only)", volume, effective_volume, muted ? 1 : 0);
    // Volume is applied inside the core; we do not use OpenSL ES VolumeItf
    // to avoid triggering Android AppOps CONTROL_AUDIO permission checks.
    player_core_set_volume(player_core_, effective_volume);

    // If output is restored while the player is running,
    // ensure the OpenSL ES audio output is actually playing (it may have been
    // skipped during the deferred-start path when the player was muted).
    // Guard with core playing state to avoid resuming OpenSL while app is paused.
    if (!was_enabled
        && isAudioOutputEnabled()
        && audio_active_
        && player_core_is_playing(player_core_)) {
        SLresult r = setOpenSLESPlayState(SL_PLAYSTATE_PLAYING, true);
        if (r == SL_RESULT_SUCCESS) {
            LOGI("[ctrl] audio resumed on volume restore");
        }
    }
}

void AndroidPlayer::setMuted(bool muted) {
    if (!player_core_) return;
    bool was_enabled = isAudioOutputEnabled();
    bool prev_muted = muted_.exchange(muted, std::memory_order_relaxed);
    float base_volume = current_volume_.load(std::memory_order_relaxed);
    float effective_volume = muted ? 0.0f : base_volume;
    LOGD("Set muted: %d prev=%d base_volume=%f effective=%f",
         muted ? 1 : 0, prev_muted ? 1 : 0, base_volume, effective_volume);
    player_core_set_volume(player_core_, effective_volume);

    if (!was_enabled
        && isAudioOutputEnabled()
        && audio_active_
        && player_core_is_playing(player_core_)) {
        SLresult r = setOpenSLESPlayState(SL_PLAYSTATE_PLAYING, true);
        if (r == SL_RESULT_SUCCESS) {
            LOGI("[ctrl] audio resumed on mute off");
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
        player_core_set_decode_mode(player_core_,
                                    decode_mode_ == 1 ? PLAYER_DECODE_MODE_HARDWARE
                                                      : PLAYER_DECODE_MODE_SOFTWARE);
    }
}

void AndroidPlayer::setSecureSeekTuning(double drop_only_window_backward_sec,
                                        double drop_only_window_forward_sec,
                                        double accept_future_backward_early_sec,
                                        double accept_future_forward_early_sec,
                                        double accept_future_backward_mid_sec,
                                        double accept_future_forward_mid_sec,
                                        double accept_future_backward_late_sec,
                                        double accept_future_forward_late_sec,
                                        int lower_bound_deadline_normal_ms,
                                        int lower_bound_deadline_large_ms,
                                        int recovery_deadline_normal_ms,
                                        int recovery_deadline_large_ms,
                                        int audio_wait_deadline_normal_ms,
                                        int audio_wait_deadline_large_ms) {
    secure_drop_only_window_backward_sec_ = clampd(drop_only_window_backward_sec, 1.0, 20.0);
    secure_drop_only_window_forward_sec_ = clampd(drop_only_window_forward_sec, 1.0, 30.0);
    secure_accept_future_backward_early_sec_ = clampd(accept_future_backward_early_sec, 0.5, 12.0);
    secure_accept_future_forward_early_sec_ = clampd(accept_future_forward_early_sec, 0.5, 18.0);
    secure_accept_future_backward_mid_sec_ = clampd(accept_future_backward_mid_sec, 1.0, 20.0);
    secure_accept_future_forward_mid_sec_ = clampd(accept_future_forward_mid_sec, 1.0, 24.0);
    secure_accept_future_backward_late_sec_ = clampd(accept_future_backward_late_sec, 1.0, 30.0);
    secure_accept_future_forward_late_sec_ = clampd(accept_future_forward_late_sec, 1.0, 40.0);

    secure_lower_bound_deadline_normal_ms_ = clampi64(lower_bound_deadline_normal_ms, 800, 12000);
    secure_lower_bound_deadline_large_ms_ = clampi64(lower_bound_deadline_large_ms, 1000, 14000);
    secure_recovery_deadline_normal_ms_ = clampi64(recovery_deadline_normal_ms, 1200, 18000);
    secure_recovery_deadline_large_ms_ = clampi64(recovery_deadline_large_ms, 1500, 22000);
    secure_audio_wait_deadline_normal_ms_ = clampi64(audio_wait_deadline_normal_ms, 1000, 18000);
    secure_audio_wait_deadline_large_ms_ = clampi64(audio_wait_deadline_large_ms, 1200, 22000);

    SYNCI("evt=secure_seek_tuning_apply drop_bw=%.3f drop_fw=%.3f "
          "accept_bw=(%.3f/%.3f/%.3f) accept_fw=(%.3f/%.3f/%.3f) "
          "deadline_lower=(%" PRId64 "/%" PRId64 ") deadline_recovery=(%" PRId64 "/%" PRId64 ") "
          "deadline_audio=(%" PRId64 "/%" PRId64 ")",
          secure_drop_only_window_backward_sec_,
          secure_drop_only_window_forward_sec_,
          secure_accept_future_backward_early_sec_,
          secure_accept_future_backward_mid_sec_,
          secure_accept_future_backward_late_sec_,
          secure_accept_future_forward_early_sec_,
          secure_accept_future_forward_mid_sec_,
          secure_accept_future_forward_late_sec_,
          secure_lower_bound_deadline_normal_ms_,
          secure_lower_bound_deadline_large_ms_,
          secure_recovery_deadline_normal_ms_,
          secure_recovery_deadline_large_ms_,
          secure_audio_wait_deadline_normal_ms_,
          secure_audio_wait_deadline_large_ms_);
}

void AndroidPlayer::resetSecureSeekTuning() {
    secure_drop_only_window_backward_sec_ = 5.0;
    secure_drop_only_window_forward_sec_ = 8.0;
    secure_drop_only_window_large_seek_bonus_sec_ = 2.0;
    secure_drop_only_window_elapsed_bonus_sec_ = 1.5;
    secure_drop_only_window_elapsed_threshold_ms_ = 1800;
    secure_accept_future_backward_early_sec_ = 3.0;
    secure_accept_future_forward_early_sec_ = 5.0;
    secure_accept_future_backward_mid_sec_ = 7.0;
    secure_accept_future_forward_mid_sec_ = 10.0;
    secure_accept_future_backward_late_sec_ = 11.0;
    secure_accept_future_forward_late_sec_ = 16.0;
    secure_accept_mid_elapsed_ms_ = 2600;
    secure_accept_late_elapsed_ms_ = 4200;
    secure_lower_bound_deadline_normal_ms_ = 2400;
    secure_lower_bound_deadline_large_ms_ = 2900;
    secure_recovery_deadline_normal_ms_ = 4600;
    secure_recovery_deadline_large_ms_ = 5800;
    secure_audio_wait_deadline_normal_ms_ = 3600;
    secure_audio_wait_deadline_large_ms_ = 4700;
    secure_forward_preroll_base_sec_ = 3.5;
    secure_forward_preroll_span_gain_ = 0.0011;
    secure_forward_preroll_learned_gain_ = 0.50;
    secure_forward_preroll_max_sec_ = 6.8;
    secure_forward_preroll_bias_expire_ms_ = 45000;
    secure_forward_seek_bias_sec_.store(0.0, std::memory_order_release);
    secure_forward_seek_bias_hits_.store(0, std::memory_order_release);
    secure_forward_seek_bias_last_update_ms_.store(0, std::memory_order_release);
    SYNCI("evt=secure_seek_tuning_reset");
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

int AndroidPlayer::getVideoWidth() const {
    if (!player_core_) return 0;
    return player_core_get_video_width(player_core_);
}

int AndroidPlayer::getVideoHeight() const {
    if (!player_core_) return 0;
    return player_core_get_video_height(player_core_);
}

bool AndroidPlayer::hasRenderedFirstFrame() const {
    return first_frame_rendered_.load(std::memory_order_acquire);
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
    if (player_core_is_playing(player_core_) != 0) {
        return true;
    }
    // Semantic fix for secure HLS edge cases:
    // Some streams keep core state in PAUSED/BUFFERING while frames still advance.
    // Treat it as effective playing when:
    // - autoplay intent is on (playWhenReady=true)
    // - no active seek-gates
    // - recent forward progress is observed
    bool play_when_ready_now = player_core_get_play_when_ready(player_core_) != 0;
    if (!play_when_ready_now) {
        return false;
    }
    bool seek_gates_active =
            seek_audio_wait_video_.load(std::memory_order_acquire) ||
            seek_recovery_active_.load(std::memory_order_acquire) ||
            seek_lower_bound_active_.load(std::memory_order_acquire) ||
            seek_force_resume_pending_.load(std::memory_order_acquire);
    if (seek_gates_active) {
        return false;
    }
    int64_t now_ms_now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t last_progress_ms = loading_progress_last_advance_ms_.load(std::memory_order_acquire);
    bool has_recent_progress = last_progress_ms > 0 && (now_ms_now - last_progress_ms) <= 1500;
    if (has_recent_progress) {
        LOGI_RATE(40, "[state] effective playing by progress (core_playing=0 pwr=1)");
        return true;
    }
    return false;
}

void AndroidPlayer::setPlayWhenReady(bool play_when_ready) {
    std::lock_guard<std::mutex> api_lock(api_mutex_);
    if (!player_core_) return;
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!play_when_ready) {
        pending_play_after_open_.store(false, std::memory_order_release);
        user_manual_pause_.store(true, std::memory_order_release);
        user_manual_pause_block_until_ms_.store(now + 5000, std::memory_order_release);
        seek_resume_on_complete_.store(false, std::memory_order_release);
        player_core_pause(player_core_);
        if (playItf_) {
            setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, false);
        }
    } else {
        user_manual_pause_.store(false, std::memory_order_release);
        user_manual_pause_block_until_ms_.store(0, std::memory_order_release);
    }
    player_core_set_play_when_ready(player_core_, play_when_ready ? 1 : 0);
}

bool AndroidPlayer::isLoading() const {
    if (playback_completed_latched_.load(std::memory_order_acquire)) {
        return false;
    }
    bool core_loading = is_loading_.load(std::memory_order_acquire);
    bool seek_loading =
        seek_audio_wait_video_.load(std::memory_order_acquire) ||
        seek_recovery_active_.load(std::memory_order_acquire) ||
        seek_lower_bound_active_.load(std::memory_order_acquire);
    bool play_when_ready_now = player_core_ && player_core_get_play_when_ready(player_core_) != 0;
    bool core_playing_now = player_core_ && player_core_is_playing(player_core_) != 0;
    bool video_stream_opened = player_core_ && player_core_is_video_stream_opened(player_core_) != 0;
    bool sw_decode_secure =
        secure_session_active_.load(std::memory_order_acquire) &&
        player_core_ &&
        player_core_is_video_hardware_decoding(player_core_) == 0;
    bool waiting_open_first_frame =
        video_stream_opened &&
        !first_frame_rendered_.load(std::memory_order_acquire) &&
        player_core_ &&
        play_when_ready_now;
    if (audio_av_split_forced_pause_.load(std::memory_order_acquire) && core_loading) {
        return true;
    }
    if (video_empty_stall_forced_pause_.load(std::memory_order_acquire)) {
        return true;
    }
    // Paused semantics: if caller explicitly does not want autoplay and no seek-gate is active,
    // stale core_loading=true should not pin UI in loading forever.
    if (!play_when_ready_now && !seek_loading && !waiting_open_first_frame) {
        return false;
    }
    if (core_loading && !seek_loading && !waiting_open_first_frame) {
        int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t suppress_until = suppress_stale_loading_true_until_ms_.load(std::memory_order_acquire);
        int64_t last_progress_ms = loading_progress_last_advance_ms_.load(std::memory_order_acquire);
        int64_t last_audio_ms = last_effective_audio_output_ms_.load(std::memory_order_acquire);
        int64_t underrun_started_ms = audio_underrun_streak_started_ms_.load(std::memory_order_acquire);
        int64_t progress_hold_ms = core_playing_now ? 2600 : (sw_decode_secure ? 3000 : 1600);
        bool has_recent_progress = last_progress_ms > 0 && (now - last_progress_ms) <= progress_hold_ms;
        bool secure_audio_starving =
            sw_decode_secure &&
            ((last_audio_ms > 0 && (now - last_audio_ms) >= 1800) ||
             (underrun_started_ms > 0 && (now - underrun_started_ms) >= 1200));
        if (secure_audio_starving) {
            return true;
        }
        if (now < suppress_until || has_recent_progress) {
            return false;
        }
    }
    return core_loading || seek_loading || waiting_open_first_frame;
}

bool AndroidPlayer::isHardwareDecodingActive() const {
    if (!player_core_) return false;
    return player_core_is_video_hardware_decoding(player_core_) != 0;
}

bool AndroidPlayer::isSeekSessionActive() const {
    bool active_sid = seek_session_active_id_.load(std::memory_order_acquire) != 0;
    bool seek_gates_active =
            seek_lower_bound_active_.load(std::memory_order_acquire) ||
            seek_recovery_active_.load(std::memory_order_acquire) ||
            seek_audio_wait_video_.load(std::memory_order_acquire) ||
            seek_force_resume_pending_.load(std::memory_order_acquire);
    // Root fix:
    // "seek session active" should track real gate activity, not stale phase token.
    // We observed cases where gates were already released but phase stayed CONVERGE,
    // causing Java watchdog/loading to wait extra seconds despite playback progress.
    // Session id and phase are correlation metadata; gates are the real source of truth.
    if (!seek_gates_active) {
        return false;
    }
    return active_sid || seek_gates_active;
}

void AndroidPlayer::loadingStateCallback(bool is_loading, void* user_data) {
    auto* player = static_cast<AndroidPlayer*>(user_data);
    if (!player) {
        return;
    }
    int64_t now_ms_now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    if (player->suppress_transient_loading_false_.load(std::memory_order_acquire)) {
        int64_t suppress_until = player->suppress_transient_loading_false_until_ms_.load(std::memory_order_acquire);
        if (is_loading || now_ms_now > suppress_until) {
            player->suppress_transient_loading_false_.store(false, std::memory_order_release);
            player->suppress_transient_loading_false_until_ms_.store(0, std::memory_order_release);
        } else {
            LOGI_RATE(60, "[state] ignore transient loading=false during open switch");
            return;
        }
    }
    if (is_loading && player->playback_completed_latched_.load(std::memory_order_acquire)) {
        LOGI_RATE(20, "[state] ignore loading=true after completed latched");
        return;
    }
    if (is_loading) {
        bool seek_loading =
                player->seek_audio_wait_video_.load(std::memory_order_acquire) ||
                player->seek_recovery_active_.load(std::memory_order_acquire) ||
                player->seek_lower_bound_active_.load(std::memory_order_acquire);
        bool play_when_ready_now = player->player_core_ && player_core_get_play_when_ready(player->player_core_) != 0;
        bool core_playing_now = player->player_core_ && player_core_is_playing(player->player_core_) != 0;
        int64_t last_progress_ms = player->loading_progress_last_advance_ms_.load(std::memory_order_acquire);
        bool sw_decode_secure =
                player->secure_session_active_.load(std::memory_order_acquire) &&
                player->player_core_ &&
                player_core_is_video_hardware_decoding(player->player_core_) == 0;
        int64_t progress_window_ms = sw_decode_secure ? 3000 : 1500;
        bool has_recent_progress = last_progress_ms > 0 && (now_ms_now - last_progress_ms) <= progress_window_ms;
        if (!seek_loading && !play_when_ready_now) {
            // Explicit pause is a stronger semantic than stale core loading edges.
            player->suppress_stale_loading_true_until_ms_.store(now_ms_now + 5000, std::memory_order_release);
            LOGI_RATE(30, "[state] ignore loading=true while playWhenReady=false");
            return;
        }
        if (!seek_loading && play_when_ready_now && (core_playing_now || has_recent_progress) && has_recent_progress) {
            // During steady playback some pipelines spike loading=true for a short moment.
            // Ignore this edge to avoid UI loading flash.
            player->suppress_stale_loading_true_until_ms_.store(now_ms_now + (sw_decode_secure ? 3200 : 2200), std::memory_order_release);
            LOGI_RATE(30, "[state] ignore transient loading=true while effective progress");
            return;
        }
    }
    if (is_loading) {
        // Real loading edge starts; cancel stale-true suppression window immediately.
        player->suppress_stale_loading_true_until_ms_.store(0, std::memory_order_release);
    }
    player->is_loading_.store(is_loading, std::memory_order_release);
    double pos_now = player->player_core_ ? player_core_get_position(player->player_core_) : 0.0;
    double prev_pos = player->state_last_reported_pos_.load(std::memory_order_acquire);
    if (std::isfinite(prev_pos) && std::isfinite(pos_now)) {
        double backward_delta = prev_pos - pos_now;
        if (backward_delta >= 2.0) {
            int64_t last_jump_log_ms = player->state_last_backward_jump_log_ms_.load(std::memory_order_acquire);
            if (now_ms_now - last_jump_log_ms >= 2500) {
                player->state_last_backward_jump_log_ms_.store(now_ms_now, std::memory_order_release);
                bool seek_active = player->isSeekSessionActive();
                LOGW("[state] position backward jump: delta=%.3f from=%.3f to=%.3f loading=%d seek_active=%d",
                     backward_delta, prev_pos, pos_now, is_loading ? 1 : 0, seek_active ? 1 : 0);
            }
        }
    }
    player->state_last_reported_pos_.store(pos_now, std::memory_order_release);
    LOGI("[state] loading=%s pos=%.3f", is_loading ? "true" : "false", pos_now);
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

    bool suppress_secure_hw_probe =
            player->suppress_secure_hw_probe_errors_.load(std::memory_order_acquire) &&
            (error_code == PLAYER_ERROR_CODEC_NOT_FOUND ||
             error_code == PLAYER_ERROR_NO_VIDEO_STREAM ||
             error_code == PLAYER_ERROR_CODEC_OPEN_FAILED);
    if (suppress_secure_hw_probe) {
        DECODEI("evt=secure_hw_probe_error_callback_suppressed code=%d msg=%s",
                error_code,
                error_msg ? error_msg : "");
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

static inline bool should_block_completed_in_opening_window(bool opening_now,
                                                            bool first_frame_ready,
                                                            bool play_when_ready_now) {
    // Mature player rule: completion is a terminal state, never emitted while opening
    // or before first frame under autoplay intent.
    return opening_now || (play_when_ready_now && !first_frame_ready);
}

static inline bool is_open_ready_for_tail_complete(bool opening_now, bool first_frame_ready) {
    // Tail force-complete is only valid after open lifecycle is stabilized.
    return !opening_now && first_frame_ready;
}

void AndroidPlayer::playbackCompletedCallback(void* user_data) {
    auto* player = static_cast<AndroidPlayer*>(user_data);
    if (!player) {
        LOGW("[playbackCompleted] player is null");
        return;
    }
    bool opening_now = player->open_in_progress_.load(std::memory_order_acquire);
    bool first_frame_ready = player->first_frame_rendered_.load(std::memory_order_acquire);
    bool play_when_ready_now = player->player_core_ && player_core_get_play_when_ready(player->player_core_) != 0;
    if (should_block_completed_in_opening_window(opening_now, first_frame_ready, play_when_ready_now)) {
        LOGI_RATE(20,
                  "[playbackCompleted] ignored during opening window: opening=%d first_frame_ready=%d pwr=%d pos=%.3f dur=%.3f state=%d",
                  opening_now ? 1 : 0,
                  first_frame_ready ? 1 : 0,
                  play_when_ready_now ? 1 : 0,
                  player->getPosition(),
                  player->getDuration(),
                  player->getState());
        return;
    }
    LOGI("[playbackCompleted] pos=%.3f dur=%.3f state=%d",
         player->getPosition(), player->getDuration(), player->getState());
    player->playback_completed_latched_.store(true, std::memory_order_release);
    player->is_loading_.store(false, std::memory_order_release);
    player->suppress_stale_loading_true_until_ms_.store(0, std::memory_order_release);
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

void AndroidPlayer::settleSeekSessionFromApp(bool by_timeout) {
    if (!player_core_) return;

    uint64_t settle_sid = seek_session_active_id_.load(std::memory_order_acquire);
    int settle_phase = seek_phase_.load(std::memory_order_acquire);
    bool settle_gate_lower = seek_lower_bound_active_.load(std::memory_order_acquire);
    bool settle_gate_recovery = seek_recovery_active_.load(std::memory_order_acquire);
    bool settle_gate_audio_wait = seek_audio_wait_video_.load(std::memory_order_acquire);
    bool settle_gate_force_resume = seek_force_resume_pending_.load(std::memory_order_acquire);
    int settle_retry = seek_force_resume_retry_count_.load(std::memory_order_acquire);
    int64_t settle_deadline_ms = seek_force_resume_deadline_ms_.load(std::memory_order_acquire);
    int64_t settle_now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t settle_deadline_left_ms = settle_deadline_ms > 0 ? (settle_deadline_ms - settle_now) : -1;
    double settle_target = seek_target_sec_.load(std::memory_order_acquire);
    double settle_from = seek_from_sec_.load(std::memory_order_acquire);
    double settle_pos_before = player_core_get_position(player_core_);
    int settle_state_before = player_core_get_state(player_core_);
    int settle_pwr_before = player_core_get_play_when_ready(player_core_);
    bool settle_playing_before = player_core_is_playing(player_core_) != 0;
    double settle_progress_ref_pos = loading_progress_last_pos_.load(std::memory_order_acquire);
    int64_t settle_progress_last_advance_ms = loading_progress_last_advance_ms_.load(std::memory_order_acquire);
    int64_t settle_progress_age_ms = settle_progress_last_advance_ms > 0
                                     ? (settle_now - settle_progress_last_advance_ms)
                                     : -1;
    double settle_pos_to_target = (std::isfinite(settle_pos_before) && std::isfinite(settle_target))
                                  ? std::fabs(settle_pos_before - settle_target)
                                  : -1.0;
    double settle_pos_from_progress_ref = (std::isfinite(settle_pos_before) && std::isfinite(settle_progress_ref_pos))
                                          ? (settle_pos_before - settle_progress_ref_pos)
                                          : 0.0;
    SYNCI("evt=seek_settle_from_app_entry sid=%" PRIu64 " phase=%s by_timeout=%d target=%.3f from=%.3f gate_lower=%d gate_recovery=%d gate_audio_wait=%d gate_force_resume=%d retry=%d deadline_left_ms=%" PRId64,
          settle_sid,
          seek_phase_name(settle_phase),
          by_timeout ? 1 : 0,
          settle_target,
          settle_from,
          settle_gate_lower ? 1 : 0,
          settle_gate_recovery ? 1 : 0,
          settle_gate_audio_wait ? 1 : 0,
          settle_gate_force_resume ? 1 : 0,
          settle_retry,
          settle_deadline_left_ms);
    SYNCI("evt=seek_settle_diag_pre sid=%" PRIu64 " by_timeout=%d pos=%.3f target=%.3f pos_to_target=%.3f state=%d pwr=%d playing=%d progress_ref_pos=%.3f pos_delta_ref=%.3f progress_age_ms=%" PRId64,
          settle_sid,
          by_timeout ? 1 : 0,
          settle_pos_before,
          settle_target,
          settle_pos_to_target,
          settle_state_before,
          settle_pwr_before,
          settle_playing_before ? 1 : 0,
          settle_progress_ref_pos,
          settle_pos_from_progress_ref,
          settle_progress_age_ms);

    resetSeekFlowState(true, false, true, true);

    bool manual_pause_blocked = false;
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (user_manual_pause_.load(std::memory_order_acquire)) {
        int64_t block_until = user_manual_pause_block_until_ms_.load(std::memory_order_acquire);
        manual_pause_blocked = block_until <= 0 || now < block_until;
    }
    bool paused_origin = seek_started_while_paused_.load(std::memory_order_acquire);
    if (manual_pause_blocked || paused_origin) {
        player_core_set_play_when_ready(player_core_, 0);
        player_core_pause(player_core_);
        // Pause-origin settle should suppress stale loading=true for a short window,
        // otherwise UI can stay in loading despite explicit paused intent.
        suppress_stale_loading_true_until_ms_.store(now + 1200, std::memory_order_release);
    } else if (by_timeout) {
        // Timeout settle should still preserve "resume intent" unless this seek originated from pause.
        // Otherwise player can be left in PAUSED + playWhenReady=1 and stay frozen indefinitely.
        player_core_set_play_when_ready(player_core_, 1);
        player_core_play(player_core_);
        bool core_playing_after = player_core_is_playing(player_core_);
        int state_after = player_core_get_state(player_core_);
        if (!core_playing_after && state_after == PLAYER_STATE_PAUSED) {
            play();
            core_playing_after = player_core_is_playing(player_core_);
            state_after = player_core_get_state(player_core_);
        }
        SYNCI("evt=seek_settle_timeout_force_play playing=%d state=%d pwr=%d",
              core_playing_after ? 1 : 0,
              state_after,
              player_core_get_play_when_ready(player_core_));
    } else if (!by_timeout) {
        int pwr_now = player_core_get_play_when_ready(player_core_);
        bool wants_play = pwr_now != 0;
        bool playing_now = player_core_is_playing(player_core_) != 0;
        int state_now = player_core_get_state(player_core_);
        bool force_play_attempted = false;
        bool force_play_succeeded = false;
        if (wants_play && !playing_now) {
            force_play_attempted = true;
            player_core_play(player_core_);
            playing_now = player_core_is_playing(player_core_) != 0;
            state_now = player_core_get_state(player_core_);
            if (!playing_now && state_now == PLAYER_STATE_PAUSED) {
                play();
                playing_now = player_core_is_playing(player_core_) != 0;
                state_now = player_core_get_state(player_core_);
            }
            force_play_succeeded = playing_now;
        }
        // Seek 已收敛后，给短窗口屏蔽偶发 stale loading=true。
        suppress_stale_loading_true_until_ms_.store(now + (force_play_succeeded ? 2200 : 1400), std::memory_order_release);
        SYNCI("evt=seek_settle_non_timeout_resume_check wants_play=%d attempted=%d success=%d state=%d pwr=%d playing=%d",
              wants_play ? 1 : 0,
              force_play_attempted ? 1 : 0,
              force_play_succeeded ? 1 : 0,
              state_now,
              player_core_get_play_when_ready(player_core_),
              player_core_is_playing(player_core_) ? 1 : 0);
        if (wants_play && !force_play_succeeded) {
            int64_t settle_last_progress_ms = loading_progress_last_advance_ms_.load(std::memory_order_acquire);
            bool has_recent_progress = settle_last_progress_ms > 0 && (now - settle_last_progress_ms) <= 1200;
            if (has_recent_progress || state_now == PLAYER_STATE_PLAYING) {
                // Deterministic convergence path:
                // playback intent is active and timeline is moving, treat as effective resume.
                force_play_succeeded = true;
                suppress_stale_loading_true_until_ms_.store(now + 2200, std::memory_order_release);
                SYNCI("evt=seek_settle_non_timeout_resume_effective_by_progress target=%.3f from=%.3f pos=%.3f state=%d pwr=%d",
                      settle_target,
                      settle_from,
                      player_core_get_position(player_core_),
                      player_core_get_state(player_core_),
                      player_core_get_play_when_ready(player_core_));
            } else {
                // Keep autoplay intent on unresolved resume path.
                // Do NOT force paused fallback here; otherwise pause->seek(resume=1)
                // may be downgraded to pwr=0 and become non-autoplay.
                seek_resume_on_complete_.store(true, std::memory_order_release);
                suppress_stale_loading_true_until_ms_.store(now + 1800, std::memory_order_release);
                SYNCW("evt=seek_settle_non_timeout_resume_hold_intent target=%.3f from=%.3f pos=%.3f state=%d pwr=%d",
                      settle_target,
                      settle_from,
                      player_core_get_position(player_core_),
                      player_core_get_state(player_core_),
                      player_core_get_play_when_ready(player_core_));
                SYNCW("evt=seek_settle_non_timeout_resume_unresolved target=%.3f from=%.3f pos=%.3f state=%d pwr=%d",
                      settle_target,
                      settle_from,
                      player_core_get_position(player_core_),
                      player_core_get_state(player_core_),
                      player_core_get_play_when_ready(player_core_));
            }
        }
    }
    seek_started_while_paused_.store(false, std::memory_order_release);
    if (source_encrypted_active_.load(std::memory_order_acquire)) {
        if (by_timeout) {
            int prev = secure_seek_stall_streak_.load(std::memory_order_acquire);
            int next = std::min(prev + 1, 6);
            secure_seek_stall_streak_.store(next, std::memory_order_release);
            secure_seek_stall_last_ms_.store(now, std::memory_order_release);
            SYNCI("evt=secure_seek_stall_streak_update by_timeout=1 streak=%d", next);
        } else {
            int prev = secure_seek_stall_streak_.load(std::memory_order_acquire);
            bool healthy_settle = false;
            int settle_state_now = player_core_get_state(player_core_);
            int settle_pwr_now = player_core_get_play_when_ready(player_core_);
            bool settle_playing_now = player_core_is_playing(player_core_) != 0;
            int64_t settle_last_progress_ms = loading_progress_last_advance_ms_.load(std::memory_order_acquire);
            bool has_recent_progress = settle_last_progress_ms > 0 && (now - settle_last_progress_ms) <= 1200;
            if (settle_pwr_now == 0) {
                healthy_settle = true;
            } else if (settle_playing_now || has_recent_progress) {
                healthy_settle = true;
            }
            if (prev > 0 && healthy_settle) {
                int next = std::max(0, prev - 1);
                secure_seek_stall_streak_.store(next, std::memory_order_release);
                secure_seek_stall_last_ms_.store(now, std::memory_order_release);
                SYNCI("evt=secure_seek_stall_streak_update by_timeout=0 streak=%d", next);
            } else if (prev > 0) {
                SYNCW("evt=secure_seek_stall_streak_hold_unhealthy streak=%d state=%d pwr=%d playing=%d recent_progress=%d",
                      prev,
                      settle_state_now,
                      settle_pwr_now,
                      settle_playing_now ? 1 : 0,
                      has_recent_progress ? 1 : 0);
            }
        }
    }

    SYNCI("evt=seek_session_settled_by_app by_timeout=%d paused_origin=%d manual_pause_blocked=%d pos=%.3f state=%d pwr=%d playing=%d",
          by_timeout ? 1 : 0,
          paused_origin ? 1 : 0,
          manual_pause_blocked ? 1 : 0,
          player_core_get_position(player_core_),
          player_core_get_state(player_core_),
          player_core_get_play_when_ready(player_core_),
          player_core_is_playing(player_core_) ? 1 : 0);
    double settle_pos_after = player_core_get_position(player_core_);
    int settle_state_after = player_core_get_state(player_core_);
    int settle_pwr_after = player_core_get_play_when_ready(player_core_);
    bool settle_playing_after = player_core_is_playing(player_core_) != 0;
    double settle_pos_delta = (std::isfinite(settle_pos_after) && std::isfinite(settle_pos_before))
                              ? (settle_pos_after - settle_pos_before)
                              : 0.0;
    SYNCI("evt=seek_settle_diag_post sid=%" PRIu64 " by_timeout=%d pos_before=%.3f pos_after=%.3f pos_delta=%.3f target=%.3f state_before=%d state_after=%d pwr_before=%d pwr_after=%d playing_before=%d playing_after=%d",
          settle_sid,
          by_timeout ? 1 : 0,
          settle_pos_before,
          settle_pos_after,
          settle_pos_delta,
          settle_target,
          settle_state_before,
          settle_state_after,
          settle_pwr_before,
          settle_pwr_after,
          settle_playing_before ? 1 : 0,
          settle_playing_after ? 1 : 0);
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

void AndroidPlayer::resetRenderStateForStreamSwitch() {
    std::lock_guard<std::mutex> lock(render_mutex_);
    // Clear cached frame to avoid carrying stale chroma into next source.
    last_frame_y_.clear();
    last_frame_u_.clear();
    last_frame_v_.clear();
    last_frame_width_ = 0;
    last_frame_height_ = 0;
    last_frame_y_stride_ = 0;
    last_frame_u_stride_ = 0;
    last_frame_v_stride_ = 0;
    last_frame_cache_ms_ = 0;

    // Re-probe UV order for every newly opened stream.
    gl_uv_swap_decided_ = false;
    gl_uv_swap_selected_ = false;
    gl_uv_swap_votes_ = 0;
    gl_uv_swap_probe_budget_ = 24;

    // Force render layout markers to be rebuilt from fresh frames.
    gl_last_video_w_ = 0;
    gl_last_video_h_ = 0;
    seek_policy_video_w_.store(0, std::memory_order_release);
    seek_policy_video_h_.store(0, std::memory_order_release);
    gl_last_uv_interleaved_ = false;
    gl_last_uv_swap_ = false;
    gl_last_uv_tex_w_ = 0;
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
    seek_policy_video_w_.store(0, std::memory_order_release);
    seek_policy_video_h_.store(0, std::memory_order_release);
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


void AndroidPlayer::trySeekAudioWaitDeadlineFallback(int64_t now,
                                                   double pts,
                                                   bool likely_4k,
                                                   int64_t& post_seek_ahead_bypass_until_ms) {
    if (!seek_audio_wait_video_.load(std::memory_order_acquire)) {
        return;
    }
    if (seek_audio_wait_deadline_ms_ <= 0 || now < seek_audio_wait_deadline_ms_) {
        return;
    }
    // Audio resume is decoupled from video recovery gates: deadline reached => force resume
    // to avoid "picture plays but silent until user re-enters".
    setSeekPhase(SEEK_PHASE_FAILOVER, "seek_wait_video_deadline");
    if (!seek_audio_wait_video_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    seek_resume_stable_hits_.store(0, std::memory_order_release);
    seek_audio_wait_deadline_ms_ = 0;
    double seek_target_now = seek_target_sec_.load(std::memory_order_acquire);
    double anchor_pts = pts;
    if (!std::isfinite(anchor_pts) || anchor_pts < 0.0) {
        anchor_pts = seek_target_now;
    }
    bool manual_pause_blocked = false;
    if (user_manual_pause_.load(std::memory_order_acquire)) {
        int64_t block_until = user_manual_pause_block_until_ms_.load(std::memory_order_acquire);
        manual_pause_blocked = block_until <= 0 || now < block_until;
    }
    bool should_resume_on_complete = seek_resume_on_complete_.load(std::memory_order_acquire);
    if (manual_pause_blocked || seek_started_while_paused_.load(std::memory_order_acquire)) {
        should_resume_on_complete = false;
        SYNCI_RATE(10, "evt=seek_wait_video_deadline_skip_autoplay reason=manual_pause_or_paused_origin");
    }
    if (player_core_ && should_resume_on_complete) {
        player_core_set_play_when_ready(player_core_, 1);
        if (!player_core_is_playing(player_core_)) {
            player_core_play(player_core_);
        }
    }
    sync_warmup_frames_.store(likely_4k ? 28 : 20, std::memory_order_release);
    post_seek_ahead_bypass_until_ms = now + (likely_4k ? 3200 : 1800);
    uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
    setSeekPhase(SEEK_PHASE_RESUME, "seek_wait_video_deadline_resume");
    forceResumeAudioOutput(now, anchor_pts, "seek_wait_video_deadline");
    SYNCI("evt=seek_wait_video_deadline_resume id=%" PRIu64 " phase=%s anchor=%.3f target=%.3f abs_err=%.3f",
          sid, seek_phase_name(seek_phase_.load(std::memory_order_acquire)),
          anchor_pts, seek_target_now, std::fabs(anchor_pts - seek_target_now));
    setSeekPhase(SEEK_PHASE_IDLE, "seek_wait_video_deadline_settled");
}

void AndroidPlayer::resumeSeekAudioAfterKeyframeAhead(int64_t now,
                                                      double pts,
                                                      double& clock,
                                                      double& delay,
                                                      double seek_target_now,
                                                      bool is_backward_seek,
                                                      bool large_forward_seek,
                                                      bool likely_4k,
                                                      int64_t& post_seek_ahead_bypass_until_ms) {
    bool secure_session = secure_session_active_.load(std::memory_order_acquire);
    bool keyframe_ahead_land = !is_backward_seek && delay > 1.0;
    if (!secure_session && !keyframe_ahead_land && !large_forward_seek) {
        return;
    }

    if (player_core_ && std::isfinite(pts) && pts >= 0.0) {
        player_core_anchor_clock(player_core_, pts);
        clock = player_core_get_position(player_core_);
        if (audio_output_latency_sec_ > 0.0) {
            clock -= audio_output_latency_sec_;
            if (clock < 0.0) clock = 0.0;
        }
        delay = pts - clock;
    }

    int64_t bypass_ms = is_backward_seek ? (likely_4k ? 1800 : 1200) : 450;
    if (likely_4k) {
        bypass_ms = is_backward_seek ? 1800 : 1200;
    }
    if (keyframe_ahead_land || secure_session) {
        int64_t ahead_ms = (int64_t)std::min(8000.0, std::max(1800.0, delay * 1000.0 + 1200.0));
        bypass_ms = std::max(bypass_ms, ahead_ms);
        if (secure_session) {
            bypass_ms = std::max<int64_t>(bypass_ms, 5000);
        }
    }
    post_seek_ahead_bypass_until_ms = now + bypass_ms;

    bool manual_pause_blocked = false;
    if (user_manual_pause_.load(std::memory_order_acquire)) {
        int64_t block_until = user_manual_pause_block_until_ms_.load(std::memory_order_acquire);
        manual_pause_blocked = block_until <= 0 || now < block_until;
    }
    bool should_resume_on_complete = seek_resume_on_complete_.load(std::memory_order_acquire);
    if (manual_pause_blocked || seek_started_while_paused_.load(std::memory_order_acquire)) {
        should_resume_on_complete = false;
    }
    if (seek_audio_wait_video_.exchange(false, std::memory_order_acq_rel)) {
        setSeekPhase(SEEK_PHASE_RESUME, "seek_keyframe_ahead_resume");
        seek_resume_stable_hits_.store(0, std::memory_order_release);
        seek_audio_wait_deadline_ms_ = 0;
        if (player_core_ && should_resume_on_complete) {
            player_core_set_play_when_ready(player_core_, 1);
            if (!player_core_is_playing(player_core_)) {
                player_core_play(player_core_);
            }
        }
        if (playItf_ && isAudioOutputEnabled()) {
            SLresult r = setOpenSLESPlayState(SL_PLAYSTATE_PLAYING, true);
            LOGI("[sync] seek keyframe-ahead resume audio: result=%d delay=%.3f secure=%d target=%.3f",
                 r, delay, secure_session ? 1 : 0, seek_target_now);
            uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
            SYNCI("evt=seek_keyframe_ahead_resume_audio id=%" PRIu64 " phase=%s delay=%.3f secure=%d pts=%.3f target=%.3f abs_err=%.3f bypass_ms=%" PRId64,
                  sid, seek_phase_name(seek_phase_.load(std::memory_order_acquire)),
                  delay, secure_session ? 1 : 0, pts, seek_target_now, std::fabs(pts - seek_target_now), bypass_ms);
        }
        // Keyframe-ahead edge has finished all native seek gates.
        // Keep sid for correlation, but mark phase idle so seek can settle promptly.
        setSeekPhase(SEEK_PHASE_IDLE, "seek_keyframe_ahead_settled");
    }
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
    // Sync-stall watchdog: if pts/clock stay almost unchanged for too long while
    // playback is active, force one consume+display to break potential deadlock.
    double stall_watchdog_last_pts = std::numeric_limits<double>::quiet_NaN();
    double stall_watchdog_last_clk = std::numeric_limits<double>::quiet_NaN();
    int64_t stall_watchdog_since_ms = 0;
    int64_t stall_watchdog_last_break_ms = 0;
    const int64_t kSyncStallWatchdogMs = 1200;
    const int64_t kSyncStallWatchdogCooldownMs = 450;
    const double kSyncStallWatchdogEps = 0.002;
    int secure_bypass_extend_count = 0;
    int64_t secure_bypass_extend_window_start_ms = 0;
    const int64_t kSecureBypassExtendWindowMs = 8000;
    const int kSecureBypassExtendMaxCount = 3;
    // Empty-queue soft-recovery watchdog (stability-first):
    // if playback appears "running" but frame queue stays empty for too long,
    // issue a conservative core/audio wake-up once per cooldown window.
    int64_t empty_stall_recover_cooldown_until_ms = 0;
    const int64_t kEmptyStallRecoverTriggerMs = 2800;
    const int64_t kEmptyStallRecoverCooldownMs = 12000;
    int64_t tail_stall_diag_last_log_ms = 0;
    const int64_t kTailStallDiagIntervalMs = 2000;
    const int64_t kTailStallForceCompleteMs = 12000;
    const int64_t kTailStallForceCompleteFastMs = 3200;
    // Force-resume soft success detector:
    // some secure seek sessions keep core state at PAUSED while clock/frames
    // still progress toward target. Avoid retry storms in that situation.
    // Seek convergence progress tracker:
    // when abs(target-pts) keeps shrinking, avoid triggering timeout fallback
    // too early (especially large secure forward seeks).
    uint64_t seek_progress_sid = 0;
    double seek_progress_best_abs_err = std::numeric_limits<double>::infinity();
    int64_t seek_progress_last_update_ms = 0;
    int64_t seek_progress_last_improve_ms = 0;

    int64_t total_render_ms = 0, total_upload_ms = 0;
    int64_t max_render_ms   = 0, max_upload_ms   = 0;
    const int kDiagInterval = 180;

    const double kSyncThreshold = 0.050; // 50 ms: drop if video is behind
    const double kMaxAhead      = 2.000; // 2 s:  hold if video is too far ahead

    // Adaptive wait: shorten when we know there's work to do soon.
    // Default 16ms (~60fps). Lengthened when queue is empty (decoder filling).
    int wait_ms = 16;

    auto now_ms = []() -> int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    };
    PERFI("evt=sdk_tail_stall_diag_enabled version=20260605_tail_intent_fastcomplete");

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

        {
            int64_t health_now = now_ms();
            if (health_now - last_audio_health_check_ms_.load(std::memory_order_acquire) >= 500) {
                last_audio_health_check_ms_.store(health_now, std::memory_order_release);
                enforceAudioPauseDeadlines(health_now, "render_health_tick");
                checkAndRecoverAudioHealth(health_now);
            }
        }

        // Mid-term ANR optimization:
        // If UI requested play during long open(), execute once after open settles.
        if (!open_in_progress_.load(std::memory_order_acquire) &&
            pending_play_after_open_.load(std::memory_order_acquire)) {
            LOGI("[ctrl] consume deferred play after open");
            play();
            continue;
        }

        // Progress tracking is updated on actual rendered video frames (PTS-based),
        // not by core clock position. Clock-only advance can be misleading on
        // problematic streams and cause false "progress" in seek/loading recovery.

        auto trySeekSoftRebuild = [&](int64_t now_ts, const char* reason) -> bool {
            if (!player_core_) return false;
            bool manual_pause_blocked = false;
            if (user_manual_pause_.load(std::memory_order_acquire)) {
                int64_t block_until = user_manual_pause_block_until_ms_.load(std::memory_order_acquire);
                manual_pause_blocked = block_until <= 0 || now_ts < block_until;
            }
            bool seek_started_while_paused = seek_started_while_paused_.load(std::memory_order_acquire);
            if (manual_pause_blocked || seek_started_while_paused) {
                SYNCW_RATE(8, "evt=seek_soft_rebuild_skip reason=%s manual_pause=1", reason);
                return false;
            }
            double target = seek_target_sec_.load(std::memory_order_acquire);
            if (!std::isfinite(target) || target < 0.0) {
                return false;
            }
            double from = seek_from_sec_.load(std::memory_order_acquire);
            if (!std::isfinite(from) || from < 0.0) {
                from = player_core_get_position(player_core_);
                seek_from_sec_.store(from, std::memory_order_release);
            }
            int budget_before = seek_soft_rebuild_budget_left_.fetch_sub(1, std::memory_order_acq_rel);
            if (budget_before <= 0) {
                seek_soft_rebuild_budget_left_.store(0, std::memory_order_release);
                uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
                SYNCW("evt=seek_soft_rebuild_budget_exhausted id=%" PRIu64 " reason=%s",
                      sid, reason);
                return false;
            }
            int budget_left = budget_before - 1;
            bool is_backward_seek = (from - target) > 0.5;
            uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
            if (sid == 0) {
                // Internal rebuild must stay inside an existing seek session.
                // Creating a new sid here breaks app/native session mapping.
                SYNCW("evt=seek_soft_rebuild_skip reason=%s no_active_session=1", reason);
                return false;
            }
            setSeekPhase(SEEK_PHASE_CONVERGE, "seek_soft_rebuild_dispatch");
            seek_verify_hits_.store(0, std::memory_order_release);
            seek_resume_stable_hits_.store(0, std::memory_order_release);
            seek_started_at_ms_ = now_ts;
            seek_lower_bound_drop_count_ = 0;
            secure_seek_precise_reseek_count_.store(0, std::memory_order_release);
            secure_seek_precise_reseek_cooldown_until_ms_ = 0;
            seek_lower_bound_active_.store(true, std::memory_order_release);
            seek_lower_bound_deadline_ms_ = now_ts + (is_backward_seek ? 4200 : 3000);
            seek_recovery_active_.store(true, std::memory_order_release);
            seek_recovery_deadline_ms_ = now_ts + (is_backward_seek ? 8600 : 6200);
            seek_audio_wait_video_.store(true, std::memory_order_release);
            seek_audio_wait_deadline_ms_ = now_ts + (is_backward_seek ? 7600 : 5200);
            sync_warmup_frames_.store(44, std::memory_order_release);

            double dispatch_target = target;
            if (is_backward_seek) {
                // Backward seek软重建时给一个很小的回退，避免二次落点过早。
                double backward_backoff_sec = 2.0;
                double duration_now = player_core_get_duration(player_core_);
                // 临近结尾（最后 5 分钟）时，减小软重建回退幅度，避免 seek 在尾段来回拉扯。
                if (std::isfinite(duration_now) && duration_now > 0.0) {
                    double remain = duration_now - target;
                    if (remain <= 180.0) {
                        backward_backoff_sec = 0.4;
                    } else if (remain <= 300.0) {
                        backward_backoff_sec = 0.8;
                    }
                }
                dispatch_target = std::max(0.0, target - backward_backoff_sec);
            }
            seek_target_sec_.store(target, std::memory_order_release);

            if (playItf_ && isAudioOutputEnabled()) {
                setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, true);
            }
            player_core_seek(player_core_, dispatch_target);
            player_core_set_play_when_ready(player_core_, 1);
            player_core_play(player_core_);
            play();
            render_cv_.notify_one();
            bool playing = player_core_is_playing(player_core_);
            SYNCW("evt=seek_soft_rebuild reason=%s id=%" PRIu64 " backward=%d target=%.3f dispatch=%.3f playing=%d budget_left=%d",
                  reason, sid, is_backward_seek ? 1 : 0, target, dispatch_target, playing ? 1 : 0, budget_left);
            return playing;
        };

        auto armSeekForceResumePending = [&](int64_t now_ts,
                                             double target,
                                             double from,
                                             int64_t deadline_ms,
                                             const char* event_tag) -> bool {
            bool manual_pause_blocked = false;
            if (user_manual_pause_.load(std::memory_order_acquire)) {
                int64_t block_until = user_manual_pause_block_until_ms_.load(std::memory_order_acquire);
                manual_pause_blocked = block_until <= 0 || now_ts < block_until;
            }
            bool seek_started_while_paused = seek_started_while_paused_.load(std::memory_order_acquire);
            if (manual_pause_blocked || seek_started_while_paused) {
                SYNCW("evt=seek_force_resume_skip_manual_pause target=%.3f from=%.3f", target, from);
                return false;
            }
            int budget_before = seek_failover_budget_left_.fetch_sub(1, std::memory_order_acq_rel);
            if (budget_before <= 0) {
                seek_failover_budget_left_.store(0, std::memory_order_release);
                seek_force_resume_pending_.store(false, std::memory_order_release);
                seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
                seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
                seek_force_resume_retry_count_.store(0, std::memory_order_release);
                seek_force_resume_nudged_.store(false, std::memory_order_release);
                setSeekPhase(SEEK_PHASE_IDLE, "seek_force_resume_budget_exhausted");
                seek_session_active_id_.store(0, std::memory_order_release);
                SYNCW("evt=seek_force_resume_budget_exhausted target=%.3f from=%.3f",
                      target, from);
                return false;
            }
            int budget_left = budget_before - 1;
            seek_force_resume_pending_.store(true, std::memory_order_release);
            seek_force_resume_deadline_ms_.store(now_ts + deadline_ms, std::memory_order_release);
            seek_force_resume_next_try_ms_.store(now_ts + 220, std::memory_order_release);
            seek_force_resume_retry_count_.store(0, std::memory_order_release);
            seek_force_resume_nudged_.store(false, std::memory_order_release);
            SYNCW("%s target=%.3f from=%.3f budget_left=%d",
                  event_tag, target, from, budget_left);
            return true;
        };

        auto trySeekForceResume = [&](int64_t now_ts) {
            if (!seek_force_resume_pending_.load(std::memory_order_acquire)) return;
            bool manual_pause_blocked = false;
            if (user_manual_pause_.load(std::memory_order_acquire)) {
                int64_t block_until = user_manual_pause_block_until_ms_.load(std::memory_order_acquire);
                manual_pause_blocked = block_until <= 0 || now_ts < block_until;
            }
            bool seek_started_while_paused = seek_started_while_paused_.load(std::memory_order_acquire);
            if (manual_pause_blocked || seek_started_while_paused) {
                seek_force_resume_pending_.store(false, std::memory_order_release);
                seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
                seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
                seek_force_resume_retry_count_.store(0, std::memory_order_release);
                seek_force_resume_nudged_.store(false, std::memory_order_release);
                SYNCW_RATE(6, "evt=seek_force_resume_cancel_manual_pause");
                return;
            }
            int64_t deadline_ms = seek_force_resume_deadline_ms_.load(std::memory_order_acquire);
            int64_t next_try_ms = seek_force_resume_next_try_ms_.load(std::memory_order_acquire);
            if (deadline_ms <= 0 || now_ts >= deadline_ms) {
                uint64_t sid_before_expire = seek_session_active_id_.load(std::memory_order_acquire);
                int phase_before_expire = seek_phase_.load(std::memory_order_acquire);
                int retry_before_expire = seek_force_resume_retry_count_.load(std::memory_order_acquire);
                int pwr_before_expire = player_core_ ? player_core_get_play_when_ready(player_core_) : 0;
                int state_before_expire = player_core_ ? player_core_get_state(player_core_) : 0;
                double pos_before_expire = player_core_ ? player_core_get_position(player_core_) : -1.0;
                double target_before_expire = seek_target_sec_.load(std::memory_order_acquire);
                double from_before_expire = seek_from_sec_.load(std::memory_order_acquire);
                bool gate_lower = seek_lower_bound_active_.load(std::memory_order_acquire);
                bool gate_recovery = seek_recovery_active_.load(std::memory_order_acquire);
                bool gate_audio_wait = seek_audio_wait_video_.load(std::memory_order_acquire);
                // Root fix: force-resume expiry must also clear seek gates, otherwise
                // native isLoading() can stay true forever (gate-based loading latch).
                seek_lower_bound_active_.store(false, std::memory_order_release);
                seek_lower_bound_deadline_ms_ = 0;
                seek_recovery_active_.store(false, std::memory_order_release);
                seek_recovery_deadline_ms_ = 0;
                seek_audio_wait_video_.store(false, std::memory_order_release);
                seek_audio_wait_deadline_ms_ = 0;
                seek_started_at_ms_ = 0;
                seek_lower_bound_drop_count_ = 0;
                seek_verify_hits_.store(0, std::memory_order_release);
                seek_resume_stable_hits_.store(0, std::memory_order_release);
                seek_force_resume_pending_.store(false, std::memory_order_release);
                seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
                seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
                seek_force_resume_retry_count_.store(0, std::memory_order_release);
                seek_force_resume_nudged_.store(false, std::memory_order_release);
                setSeekPhase(SEEK_PHASE_IDLE, "seek_force_resume_expired");
                seek_session_active_id_.store(0, std::memory_order_release);
                SYNCW("evt=seek_force_resume_expired sid=%" PRIu64 " phase=%s retry=%d pwr=%d state=%d pos=%.3f target=%.3f from=%.3f gate_lower=%d gate_recovery=%d gate_audio_wait=%d",
                      sid_before_expire,
                      seek_phase_name(phase_before_expire),
                      retry_before_expire,
                      pwr_before_expire,
                      state_before_expire,
                      pos_before_expire,
                      target_before_expire,
                      from_before_expire,
                      gate_lower ? 1 : 0,
                      gate_recovery ? 1 : 0,
                      gate_audio_wait ? 1 : 0);
                return;
            }
            if (now_ts < next_try_ms || !player_core_ ||
                !seek_resume_on_complete_.load(std::memory_order_acquire)) {
                return;
            }
            player_core_set_play_when_ready(player_core_, 1);
            if (!player_core_is_playing(player_core_)) {
                player_core_play(player_core_);
            }
            bool core_playing_retry = player_core_is_playing(player_core_);
            int pwr_retry = player_core_get_play_when_ready(player_core_);
            int state_retry = player_core_get_state(player_core_);
            double pos_retry = player_core_get_position(player_core_);
            double target_retry = seek_target_sec_.load(std::memory_order_acquire);
            double from_retry = seek_from_sec_.load(std::memory_order_acquire);
            bool block_secure_forward_far_resume =
                    secure_session_active_.load(std::memory_order_acquire) &&
                    std::isfinite(target_retry) &&
                    std::isfinite(from_retry) &&
                    std::isfinite(pos_retry) &&
                    target_retry > from_retry + 0.5 &&
                    (target_retry - pos_retry) > 1.5;
            if (block_secure_forward_far_resume) {
                SYNCW_RATE(10,
                           "evt=seek_force_resume_blocked_secure_forward_far target=%.3f from=%.3f pos=%.3f behind=%.3f state=%d pwr=%d",
                           target_retry, from_retry, pos_retry, target_retry - pos_retry, state_retry, pwr_retry);
            }
            int64_t rendered_progress_age_ms_retry = -1;
            int64_t rendered_progress_last_ms_retry = loading_progress_last_advance_ms_.load(std::memory_order_acquire);
            if (rendered_progress_last_ms_retry > 0) {
                rendered_progress_age_ms_retry = now_ts - rendered_progress_last_ms_retry;
            }
            bool has_rendered_progress_retry =
                    rendered_progress_age_ms_retry >= 0 &&
                    rendered_progress_age_ms_retry <= 1200;
            if (!block_secure_forward_far_resume &&
                pwr_retry != 0 && state_retry == PLAYER_STATE_PLAYING && has_rendered_progress_retry) {
                setSeekPhase(SEEK_PHASE_RESUME, "seek_force_resume_soft_success_state_playing");
                seek_force_resume_pending_.store(false, std::memory_order_release);
                seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
                seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
                seek_force_resume_retry_count_.store(0, std::memory_order_release);
                seek_force_resume_nudged_.store(false, std::memory_order_release);
                uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
                SYNCI("evt=seek_force_resume_soft_success id=%" PRIu64 " phase=%s pwr=%d state=%d pos=%.3f by=state_playing_with_progress",
                      sid, seek_phase_name(seek_phase_.load(std::memory_order_acquire)),
                      pwr_retry, state_retry, pos_retry);
                setSeekPhase(SEEK_PHASE_IDLE, "seek_force_resume_soft_success_settled");
                seek_session_active_id_.store(0, std::memory_order_release);
                return;
            } else if (pwr_retry != 0 && state_retry == PLAYER_STATE_PLAYING) {
                SYNCW_RATE(10,
                           "evt=seek_force_resume_soft_reject reason=state_playing_without_rendered_progress pwr=%d state=%d pos=%.3f progress_age_ms=%" PRId64,
                           pwr_retry, state_retry, pos_retry, rendered_progress_age_ms_retry);
            }
            bool pos_progressing = has_rendered_progress_retry;
            bool seek_gates_cleared =
                    !seek_lower_bound_active_.load(std::memory_order_acquire) &&
                    !seek_recovery_active_.load(std::memory_order_acquire) &&
                    !seek_audio_wait_video_.load(std::memory_order_acquire);
            bool soft_resume_healthy =
                    !core_playing_retry &&
                    pwr_retry != 0 &&
                    state_retry == PLAYER_STATE_PAUSED &&
                    seek_gates_cleared &&
                    pos_progressing;
            if (!block_secure_forward_far_resume && soft_resume_healthy) {
                setSeekPhase(SEEK_PHASE_RESUME, "seek_force_resume_soft_success_pos_progress");
                seek_force_resume_pending_.store(false, std::memory_order_release);
                seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
                seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
                seek_force_resume_retry_count_.store(0, std::memory_order_release);
                seek_force_resume_nudged_.store(false, std::memory_order_release);
                uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
                SYNCI("evt=seek_force_resume_soft_success id=%" PRIu64 " phase=%s pwr=%d state=%d pos=%.3f",
                      sid, seek_phase_name(seek_phase_.load(std::memory_order_acquire)),
                      pwr_retry, state_retry, pos_retry);
                setSeekPhase(SEEK_PHASE_IDLE, "seek_force_resume_pos_progress_settled");
                seek_session_active_id_.store(0, std::memory_order_release);
                return;
            }
            if (!block_secure_forward_far_resume && core_playing_retry) {
                setSeekPhase(SEEK_PHASE_RESUME, "seek_force_resume_core_playing");
                seek_force_resume_pending_.store(false, std::memory_order_release);
                seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
                seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
                seek_force_resume_retry_count_.store(0, std::memory_order_release);
                seek_force_resume_nudged_.store(false, std::memory_order_release);
                if (playItf_ && isAudioOutputEnabled()) {
                    SLresult rr = setOpenSLESPlayState(SL_PLAYSTATE_PLAYING, true);
                    LOGI("[sync] seek force-resume retry audio: result=%d", rr);
                }
                uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
                SYNCI("evt=seek_force_resume_success id=%" PRIu64 " phase=%s playing=1 pwr=%d",
                      sid, seek_phase_name(seek_phase_.load(std::memory_order_acquire)), pwr_retry);
                setSeekPhase(SEEK_PHASE_IDLE, "seek_force_resume_core_playing_settled");
                seek_session_active_id_.store(0, std::memory_order_release);
                return;
            }
            int retry_count = seek_force_resume_retry_count_.fetch_add(1, std::memory_order_acq_rel) + 1;
            bool seek_gates_still_active =
                    seek_lower_bound_active_.load(std::memory_order_acquire) ||
                    seek_recovery_active_.load(std::memory_order_acquire) ||
                    seek_audio_wait_video_.load(std::memory_order_acquire);
            if (!core_playing_retry &&
                pwr_retry != 0 &&
                state_retry == PLAYER_STATE_PAUSED &&
                retry_count >= 4) {
                // Hard-stop repeated paused-state retries.
                // Keep this bounded, but do a full seek-flow reset so upper layer can recover
                // instead of being stuck in CONVERGE + loading=true indefinitely.
                seek_force_resume_pending_.store(false, std::memory_order_release);
                seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
                seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
                seek_force_resume_retry_count_.store(0, std::memory_order_release);
                seek_force_resume_nudged_.store(false, std::memory_order_release);
                bool had_active_gates = seek_gates_still_active;
                if (had_active_gates) {
                    seek_lower_bound_active_.store(false, std::memory_order_release);
                    seek_lower_bound_deadline_ms_ = 0;
                    seek_recovery_active_.store(false, std::memory_order_release);
                    seek_recovery_deadline_ms_ = 0;
                    seek_audio_wait_video_.store(false, std::memory_order_release);
                    seek_audio_wait_deadline_ms_ = 0;
                    seek_started_at_ms_ = 0;
                }
                setSeekPhase(SEEK_PHASE_IDLE, "seek_force_resume_abort_paused_state");
                seek_session_active_id_.store(0, std::memory_order_release);
                SYNCW("evt=seek_force_resume_abort_paused_state retry=%d state=%d pwr=%d gates_active=%d",
                      retry_count, state_retry, pwr_retry, seek_gates_still_active ? 1 : 0);
                if (had_active_gates) {
                    SYNCW("evt=seek_force_resume_abort_reset_gates retry=%d", retry_count);
                }
                if (source_encrypted_active_.load(std::memory_order_acquire)) {
                    int prev = secure_seek_stall_streak_.load(std::memory_order_acquire);
                    int next = std::min(prev + 1, 6);
                    secure_seek_stall_streak_.store(next, std::memory_order_release);
                    secure_seek_stall_last_ms_.store(now_ts, std::memory_order_release);
                    SYNCW("evt=secure_seek_stall_streak_update reason=force_resume_abort streak=%d", next);
                }
                return;
            }
            bool can_force_full_play_path =
                    !core_playing_retry &&
                    pwr_retry != 0 &&
                    state_retry == PLAYER_STATE_PAUSED &&
                    !seek_gates_still_active &&
                    retry_count == 4;
            bool can_force_unstick_seek_gate =
                    !core_playing_retry &&
                    pwr_retry != 0 &&
                    state_retry == PLAYER_STATE_PAUSED &&
                    retry_count >= 3;
            if (can_force_unstick_seek_gate) {
                setSeekPhase(SEEK_PHASE_RESUME, "seek_force_resume_unstick_gate");
                seek_lower_bound_active_.store(false, std::memory_order_release);
                seek_lower_bound_deadline_ms_ = 0;
                seek_recovery_active_.store(false, std::memory_order_release);
                seek_recovery_deadline_ms_ = 0;
                seek_audio_wait_video_.store(false, std::memory_order_release);
                seek_audio_wait_deadline_ms_ = 0;
                seek_started_at_ms_ = 0;
                seek_lower_bound_drop_count_ = 0;
                seek_verify_hits_.store(0, std::memory_order_release);
                sync_warmup_frames_.store(16, std::memory_order_release);
                if (playItf_ && isAudioOutputEnabled()) {
                    setOpenSLESPlayState(SL_PLAYSTATE_PLAYING, true);
                }
                double nudge_target = seek_target_sec_.load(std::memory_order_acquire);
                if (player_core_ && std::isfinite(nudge_target) && nudge_target >= 0.0) {
                    player_core_seek(player_core_, std::max(0.0, nudge_target - 0.10));
                    render_cv_.notify_one();
                }
                SYNCW_RATE(8, "evt=seek_force_resume_unstick retry=%d state=%d pwr=%d",
                           retry_count, state_retry, pwr_retry);
            }
            if (can_force_full_play_path) {
                SYNCW("evt=seek_force_resume_try_full_play_path retry=%d state=%d pwr=%d",
                      retry_count, state_retry, pwr_retry);
                play();
                core_playing_retry = player_core_is_playing(player_core_);
                pwr_retry = player_core_get_play_when_ready(player_core_);
                state_retry = player_core_get_state(player_core_);
                if (core_playing_retry) {
                    setSeekPhase(SEEK_PHASE_RESUME, "seek_force_resume_full_play_success");
                    seek_force_resume_pending_.store(false, std::memory_order_release);
                    seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
                    seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
                    seek_force_resume_retry_count_.store(0, std::memory_order_release);
                    seek_force_resume_nudged_.store(false, std::memory_order_release);
                    uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
                    SYNCI("evt=seek_force_resume_success id=%" PRIu64 " phase=%s playing=1 pwr=%d by=full_play_path",
                          sid, seek_phase_name(seek_phase_.load(std::memory_order_acquire)), pwr_retry);
                    setSeekPhase(SEEK_PHASE_IDLE, "seek_force_resume_full_play_settled");
                    seek_session_active_id_.store(0, std::memory_order_release);
                    return;
                }
            }
            if (!core_playing_retry && retry_count == 4) {
                if (trySeekSoftRebuild(now_ts, "force_resume_retry")) {
                    setSeekPhase(SEEK_PHASE_RESUME, "seek_force_resume_soft_rebuild_success");
                    seek_force_resume_pending_.store(false, std::memory_order_release);
                    seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
                    seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
                    seek_force_resume_retry_count_.store(0, std::memory_order_release);
                    seek_force_resume_nudged_.store(false, std::memory_order_release);
                    uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
                    SYNCI("evt=seek_force_resume_success id=%" PRIu64 " phase=%s playing=1 by=soft_rebuild",
                          sid, seek_phase_name(seek_phase_.load(std::memory_order_acquire)));
                    setSeekPhase(SEEK_PHASE_IDLE, "seek_force_resume_soft_rebuild_settled");
                    seek_session_active_id_.store(0, std::memory_order_release);
                    return;
                }
            }
            bool can_nudge_seek = !seek_force_resume_nudged_.load(std::memory_order_acquire) && retry_count >= 3;
            if (can_nudge_seek && player_core_) {
                double nudge_target = seek_target_sec_.load(std::memory_order_acquire);
                if (std::isfinite(nudge_target) && nudge_target >= 0.0) {
                    player_core_seek(player_core_, nudge_target);
                    render_cv_.notify_one();
                    seek_force_resume_nudged_.store(true, std::memory_order_release);
                    SYNCW("evt=seek_force_resume_nudge_seek target=%.3f retry=%d state=%d",
                          nudge_target, retry_count, player_core_get_state(player_core_));
                }
            }
            int64_t retry_interval_ms = seek_gates_still_active ? 420 : 260;
            seek_force_resume_next_try_ms_.store(now_ts + retry_interval_ms, std::memory_order_release);
            SYNCW_RATE(10, "evt=seek_force_resume_retry playing=0 pwr=%d state=%d retry=%d gates_active=%d interval_ms=%" PRId64,
                       pwr_retry, state_retry, retry_count, seek_gates_still_active ? 1 : 0, retry_interval_ms);
        };

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
                if (video_empty_stall_forced_pause_.load(std::memory_order_acquire)) {
                    is_loading_.store(false, std::memory_order_release);
                    if (player_core_) {
                        player_core_set_play_when_ready(player_core_, 1);
                        player_core_play(player_core_);
                    }
                    if (playItf_ && isAudioOutputEnabled()) {
                        setOpenSLESPlayStateWithRetry(SL_PLAYSTATE_PLAYING, true);
                    }
                    SYNCI("evt=video_empty_stall_refill_resume frame=%dx%d", frame_data.width, frame_data.height);
                }
                resetVideoEmptyStallRecovery("frame_refilled");
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
            double delay = pts - clock;
            int64_t now = now_ms();
            trySeekForceResume(now);
            bool likely_4k_early = (frame_data.width >= 3840 || frame_data.height >= 2160);
            trySeekAudioWaitDeadlineFallback(now, pts, likely_4k_early, post_seek_ahead_bypass_until_ms);
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

            // Track convergence progress for active seek sessions.
            // This is intentionally lightweight and only uses frame pts snapshots.
            uint64_t active_seek_sid = seek_session_active_id_.load(std::memory_order_acquire);
            bool seek_flow_active_now =
                    seek_lower_bound_active_.load(std::memory_order_acquire) ||
                    seek_recovery_active_.load(std::memory_order_acquire) ||
                    seek_audio_wait_video_.load(std::memory_order_acquire);
            double seek_target_progress = seek_target_sec_.load(std::memory_order_acquire);
            if (seek_flow_active_now &&
                active_seek_sid != 0 &&
                std::isfinite(pts) &&
                std::isfinite(seek_target_progress) &&
                seek_target_progress >= 0.0) {
                if (seek_progress_sid != active_seek_sid) {
                    seek_progress_sid = active_seek_sid;
                    seek_progress_best_abs_err = std::numeric_limits<double>::infinity();
                    seek_progress_last_update_ms = 0;
                    seek_progress_last_improve_ms = 0;
                }
                double abs_err = std::fabs(pts - seek_target_progress);
                seek_progress_last_update_ms = now;
                if (abs_err + 0.08 < seek_progress_best_abs_err) {
                    seek_progress_best_abs_err = abs_err;
                    seek_progress_last_improve_ms = now;
                    SYNCI_RATE(25,
                               "evt=seek_progress_improve id=%" PRIu64 " abs_err=%.3f target=%.3f pts=%.3f",
                               active_seek_sid, abs_err, seek_target_progress, pts);
                }
            } else if (!seek_flow_active_now) {
                seek_progress_sid = 0;
                seek_progress_best_abs_err = std::numeric_limits<double>::infinity();
                seek_progress_last_update_ms = 0;
                seek_progress_last_improve_ms = 0;
            }
            bool likely_4k = (frame_data.width >= 3840 || gl_last_video_w_ >= 3840 || gl_last_video_h_ >= 2160);
            bool likely_8k = (frame_data.width >= 7680 || gl_last_video_w_ >= 7680 ||
                              frame_data.height >= 4320 || gl_last_video_h_ >= 4320);
            bool hw_decode_active = player_core_is_video_hardware_decoding(player_core_) != 0;
            bool sw_decode_4k = likely_4k && !hw_decode_active;
            bool sw_decode_8k = likely_8k && !hw_decode_active;
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
            SYNCI_RATE(120, "evt=sync_snapshot pts=%.3f clk=%.3f delay=%.3f rate=%.2f video_w=%d video_h=%d is_4k=%d is_8k=%d is_high_rate_4k=%d is_ultra_high_rate_4k=%d hw_video=%d",
                       pts, clock, delay, playback_rate, frame_data.width, frame_data.height,
                       likely_4k ? 1 : 0, likely_8k ? 1 : 0,
                       high_rate_4k ? 1 : 0, ultra_high_rate_4k ? 1 : 0,
                       hw_decode_active ? 1 : 0);

            // Soft re-anchor safeguard: if we stay severely behind for too long under
            // 4K high/mid-rate playback, perform one bounded seek near audio clock to
            // break out of endless drop loops (especially after rate changes).
            bool reanchor_candidate_4k = (likely_4k && playback_rate >= 2.0f) || sw_decode_8k;
            double reanchor_delay_threshold = -3.2;
            int64_t reanchor_lag_persistent_ms = 3000;
            int64_t reanchor_cooldown_ms = 6000;
            int reanchor_budget = 3;
            if (sw_decode_8k) {
                // 8K software decode can fall behind even at 1.0x. If the device cannot
                // sustain decode, bounded re-anchor prevents endless old-frame drops.
                reanchor_delay_threshold = -2.8;
                reanchor_lag_persistent_ms = 2200;
                reanchor_cooldown_ms = 9000;
                reanchor_budget = 2;
            } else if (very_high_rate_4k) {
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

            double duration_for_reanchor = player_core_get_duration(player_core_);
            bool near_tail_for_reanchor = std::isfinite(duration_for_reanchor) &&
                                          duration_for_reanchor > 0.0 &&
                                          (duration_for_reanchor - clock) <= 2.0;
            if (!in_seek_recovery && reanchor_candidate_4k && delay < reanchor_delay_threshold &&
                player_core_is_playing(player_core_) && !near_tail_for_reanchor) {
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
                    if (playItf_ && isAudioOutputEnabled()) {
                        setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, true);
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
                player_core_is_playing(player_core_) && (high_rate_4k || sw_decode_8k)) {
                double lag_pause_threshold = sw_decode_8k ? -1.8 : (very_high_rate_4k ? -2.2 : ((playback_rate >= 2.5) ? -2.5 : ((playback_rate >= 2.0) ? -2.8 : -2.2)));
                int64_t lag_pause_trigger_ms = sw_decode_8k ? 360 : (very_high_rate_4k ? 220 : ((playback_rate >= 2.5) ? 320 : ((playback_rate >= 2.0) ? 420 : 700)));
                int64_t lag_pause_fallback_ms = sw_decode_8k ? 2400 : (very_high_rate_4k ? 1700 : ((playback_rate >= 2.0) ? 1400 : 1800));
                int64_t lag_pause_min_hold_ms = sw_decode_8k ? 1300 : (very_high_rate_4k ? 1100 : ((playback_rate >= 2.5) ? 900 : ((playback_rate >= 2.0) ? 800 : 600)));
                if (delay <= lag_pause_threshold) {
                    if (severe_lag_audio_pause_start_ms_ == 0) severe_lag_audio_pause_start_ms_ = now;
                    int64_t severe_lag_ms = now - severe_lag_audio_pause_start_ms_;
                    if (severe_lag_ms >= lag_pause_trigger_ms &&
                        playItf_ && isAudioOutputEnabled()) {
                        SLresult r = setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, true);
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
                bool secure_session = secure_session_active_.load(std::memory_order_acquire);
                bool encrypted_source = source_encrypted_active_.load(std::memory_order_acquire) || secure_session;
                bool nonsecure_4k_large_seek = likely_4k && !encrypted_source && large_seek_any_direction;
                double stale_future_margin_sec = ultra_high_rate_4k ? 5.0 : 3.5;
                if (is_backward_seek && large_seek_any_direction) {
                    // Backward large seek often lands a few seconds ahead of target keyframe.
                    // Keep stale-future guard, but avoid overly tight rejection that causes long loops.
                    stale_future_margin_sec = std::max(stale_future_margin_sec, 9.0);
                    if (seek_elapsed_ms >= 1200) {
                        stale_future_margin_sec = std::max(stale_future_margin_sec, 11.0);
                    }
                }
                if (nonsecure_4k_large_seek && is_backward_seek) {
                    // Local/online 4K can land many seconds before target on BACKWARD seek.
                    // Accept a slightly wider visual window after the first second, and use
                    // a single compensated reseek if the keyframe is still too far behind.
                    if (seek_elapsed_ms >= 900) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, very_large_seek ? 2.20 : 1.80);
                    }
                    if (seek_elapsed_ms >= 1800) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, very_large_seek ? 3.20 : 2.50);
                    }
                    if (seek_elapsed_ms >= 3000) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, very_large_seek ? 4.20 : 3.20);
                    }
                }
                if (large_forward_seek) {
                    // Tencent/Exo-like seek UX: on large forward jumps, prioritize
                    // "show something close quickly" over strict exact-target hit, but
                    // keep the lower-bound window tight enough for precise UI seeking.
                    seek_epsilon_sec = std::max(seek_epsilon_sec, 0.45);
                    if (seek_span_sec > 120.0) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, 0.75);
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
                        // After deadline, prioritize quick visual recovery over exact seek hit.
                        seek_epsilon_sec = std::max(seek_epsilon_sec, 1.80);
                    }
                    if (large_forward_seek) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, 0.90);
                    }
                }
                // Mature players tend to gradually relax seek gate when a large seek
                // cannot immediately land near target keyframe. This avoids prolonged
                // frozen frames and prioritizes visible recovery.
                if (seek_elapsed_ms >= 1200) {
                    // Tencent-like accurate policy:
                    // backward seek keeps tighter epsilon to avoid early relaxed hits.
                    seek_epsilon_sec = std::max(seek_epsilon_sec,
                                                is_backward_seek
                                                        ? (very_large_seek ? 1.40 : (large_seek_any_direction ? 1.20 : 0.80))
                                                        : (large_seek_any_direction ? 0.90 : 0.60));
                }
                if (seek_elapsed_ms >= 2200) {
                    if (is_backward_seek) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, very_large_seek ? 2.00 : 1.70);
                    } else {
                        double forward_relax_eps = very_large_seek ? 1.40 : (large_seek_any_direction ? 1.20 : 1.00);
                        if (playback_rate >= 2.0) {
                            forward_relax_eps = very_large_seek ? 1.30 : (large_seek_any_direction ? 1.20 : 1.00);
                        }
                        seek_epsilon_sec = std::max(seek_epsilon_sec, forward_relax_eps);
                    }
                }
                if (seek_elapsed_ms >= 3200) {
                    if (is_backward_seek) {
                        seek_epsilon_sec = std::max(seek_epsilon_sec, very_large_seek ? 2.60 : 2.10);
                    } else {
                        double forward_relax_eps = very_large_seek ? 1.90 : (large_seek_any_direction ? 1.60 : 1.40);
                        if (playback_rate >= 2.0) {
                            forward_relax_eps = very_large_seek ? 1.70 : (large_seek_any_direction ? 1.60 : 1.40);
                        }
                        seek_epsilon_sec = std::max(seek_epsilon_sec, forward_relax_eps);
                    }
                }
                bool lower_bound_force_relax = seek_lower_bound_drop_count_ >= (very_large_seek ? 180 : 120);
                if (nonsecure_4k_large_seek) {
                    lower_bound_force_relax = seek_lower_bound_drop_count_ >= (is_backward_seek ? 72 : 90);
                }
                if (lower_bound_force_relax) {
                    double forced_eps = is_backward_seek ? 1.40 : (very_large_seek ? 2.20 : 1.80);
                    if (nonsecure_4k_large_seek) {
                        forced_eps = is_backward_seek ? (very_large_seek ? 4.80 : 3.60)
                                                      : (very_large_seek ? 2.60 : 2.00);
                    }
                    if (!is_backward_seek && playback_rate >= 2.0) {
                        forced_eps = very_large_seek ? 2.00 : 1.80;
                    }
                    seek_epsilon_sec = std::max(seek_epsilon_sec, forced_eps);
                }
                if (secure_session && !is_backward_seek && large_seek_any_direction) {
                    double secure_forward_max_eps = seek_elapsed_ms >= 5200 ? 1.00
                                                  : (seek_elapsed_ms >= 3200 ? 0.80 : 0.60);
                    seek_epsilon_sec = std::min(seek_epsilon_sec, secure_forward_max_eps);
                }
                double secure_future_guard_sec = is_backward_seek ? 10.0 : (very_large_seek ? 22.0 : 14.0);
                if (seek_elapsed_ms >= 2600) {
                    secure_future_guard_sec += is_backward_seek ? 6.0 : 4.0;
                }
                bool secure_far_future_hit = secure_session
                        && pts > seek_target_now + secure_future_guard_sec
                        && seek_elapsed_ms < 6500;
                if (secure_far_future_hit) {
                    should_consume = true;
                    seek_lower_bound_drop_count_++;
                    int reseek_count = secure_seek_precise_reseek_count_.load(std::memory_order_acquire);
                    double overshoot_sec = pts - seek_target_now;
                    bool forward_near_target_zone = !is_backward_seek &&
                            std::isfinite(overshoot_sec) &&
                            overshoot_sec > 0.0 &&
                            overshoot_sec <= 90.0 &&
                            seek_elapsed_ms >= 700;
                    if (!is_backward_seek &&
                        std::isfinite(overshoot_sec) &&
                        overshoot_sec > 0.0 &&
                        overshoot_sec <= 180.0 &&
                        seek_elapsed_ms <= 5200) {
                        double prev_bias = secure_forward_seek_bias_sec_.load(std::memory_order_acquire);
                        int prev_hits = secure_forward_seek_bias_hits_.load(std::memory_order_acquire);
                        if (!std::isfinite(prev_bias) || prev_bias < 0.0) {
                            prev_bias = 0.0;
                            prev_hits = 0;
                        }
                        double clamped_obs = std::min(260.0, overshoot_sec);
                        double blend_alpha = prev_hits >= 4 ? 0.45 : 0.60;
                        double next_bias = prev_bias * (1.0 - blend_alpha) + clamped_obs * blend_alpha;
                        next_bias = std::min(secure_forward_preroll_max_sec_, std::max(0.0, next_bias));
                        secure_forward_seek_bias_sec_.store(next_bias, std::memory_order_release);
                        secure_forward_seek_bias_hits_.store(std::min(prev_hits + 1, 32), std::memory_order_release);
                        secure_forward_seek_bias_last_update_ms_.store(now, std::memory_order_release);
                        SYNCW_RATE(20, "evt=seek_secure_forward_bias_update overshoot=%.3f prev=%.3f next=%.3f hits=%d",
                                   overshoot_sec, prev_bias, next_bias, std::min(prev_hits + 1, 32));
                    }
                    // Hybrid policy:
                    // - Moderate overshoot: prefer "lower-bound drop and roll-forward"
                    // - Severe overshoot: trigger bounded reseek
                    double drop_only_window_sec = is_backward_seek
                            ? secure_drop_only_window_backward_sec_
                            : secure_drop_only_window_forward_sec_;
                    if (large_seek_any_direction) {
                        drop_only_window_sec += secure_drop_only_window_large_seek_bonus_sec_;
                    }
                    if (seek_elapsed_ms >= secure_drop_only_window_elapsed_threshold_ms_) {
                        drop_only_window_sec += secure_drop_only_window_elapsed_bonus_sec_;
                    }
                    bool prefer_drop_only = overshoot_sec <= drop_only_window_sec;
                    bool likely_preseek_stale_frame = seek_elapsed_ms < 900 && overshoot_sec > 120.0;
                    int max_precise_reseek = is_backward_seek ? 3 : (large_seek_any_direction ? 5 : 3);
                    bool forward_reseek_budget_reached = !is_backward_seek &&
                                                         std::isfinite(seek_progress_best_abs_err) &&
                                                         seek_progress_best_abs_err <= 6.5 &&
                                                         reseek_count >= 4 &&
                                                         seek_elapsed_ms >= 1500;
                    bool can_reseek = !prefer_drop_only
                            && !likely_preseek_stale_frame
                            && reseek_count < max_precise_reseek
                            && !forward_reseek_budget_reached
                            && now >= secure_seek_precise_reseek_cooldown_until_ms_
                            && seek_elapsed_ms >= 260;
                    if (can_reseek && player_core_) {
                        double dispatch_seek_target = seek_target_now;
                        // Secure precise landing:
                        // - forward seek overshoot: fallback to earlier sync-point then converge
                        // - backward seek overshoot: also fallback earlier to avoid repeatedly landing on same future keyframe
                        if (overshoot_sec > (is_backward_seek ? 6.0 : 18.0)) {
                            double backoff_sec = is_backward_seek
                                    ? std::min(160.0, std::max(6.0, overshoot_sec * 1.05))
                                    : ([&]() {
                                        double progressive_factor = forward_near_target_zone
                                                                    ? (0.40 + 0.18 * reseek_count)
                                                                    : (0.55 + 0.35 * reseek_count);
                                        if (!forward_near_target_zone && reseek_count >= 3) {
                                            progressive_factor += 0.45;
                                        }
                                        double additive = forward_near_target_zone
                                                          ? (reseek_count * 3.0)
                                                          : (reseek_count * 8.0);
                                        double max_backoff = forward_near_target_zone ? 72.0 : 260.0;
                                        double min_backoff = forward_near_target_zone ? 6.0 : 10.0;
                                        return std::min(max_backoff,
                                                        std::max(min_backoff,
                                                                 overshoot_sec * progressive_factor + additive));
                                    })();
                            dispatch_seek_target = std::max(0.0, seek_target_now - backoff_sec);
                        }
                        secure_seek_precise_reseek_count_.store(reseek_count + 1, std::memory_order_release);
                        secure_seek_precise_reseek_cooldown_until_ms_ = now + 900;
                        // Keep seek_from baseline from the original request.
                        // Do not overwrite with current pts, otherwise forward seek may be misclassified as backward.
                        seek_target_sec_.store(seek_target_now, std::memory_order_release);
                        seek_started_at_ms_ = now;
                        setSeekPhase(SEEK_PHASE_CONVERGE, "seek_secure_precise_reseek");
                        seek_verify_hits_.store(0, std::memory_order_release);
                        seek_lower_bound_active_.store(true, std::memory_order_release);
                        seek_lower_bound_deadline_ms_ = now + (is_backward_seek ? 3600 : 2400);
                        seek_recovery_active_.store(true, std::memory_order_release);
                        seek_recovery_deadline_ms_ = now + (is_backward_seek ? 6200 : 4600);
                        seek_audio_wait_video_.store(true, std::memory_order_release);
                        seek_audio_wait_deadline_ms_ = now + (is_backward_seek ? 5600 : 4200);
                        seek_resume_stable_hits_.store(0, std::memory_order_release);
                        sync_warmup_frames_.store(44, std::memory_order_release);
                        if (playItf_ && isAudioOutputEnabled()) {
                            setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, true);
                        }
                        player_core_seek(player_core_, dispatch_seek_target);
                        render_cv_.notify_one();
                        SYNCW("evt=seek_secure_precise_reseek target=%.3f dispatch=%.3f pts=%.3f overshoot=%.3f elapsed_ms=%" PRId64 " count=%d guard=%.3f drop_window=%.3f",
                              seek_target_now, dispatch_seek_target, pts, overshoot_sec, seek_elapsed_ms, reseek_count + 1, secure_future_guard_sec, drop_only_window_sec);
                    } else {
                        SYNCW_RATE(20, "evt=seek_secure_far_future_drop pts=%.3f target=%.3f elapsed_ms=%" PRId64 " drop_count=%d guard=%.3f overshoot=%.3f drop_window=%.3f reseek_count=%d",
                                   pts, seek_target_now, seek_elapsed_ms, seek_lower_bound_drop_count_, secure_future_guard_sec, overshoot_sec, drop_only_window_sec, reseek_count);
                    }
                } else if (pts + seek_epsilon_sec < seek_target_now) {
                    if (nonsecure_4k_large_seek && is_backward_seek && player_core_) {
                        double behind_sec = seek_target_now - pts;
                        int reseek_count = seek_nonsecure_4k_reseek_count_.load(std::memory_order_acquire);
                        bool can_4k_compensate_reseek =
                                behind_sec >= (very_large_seek ? 7.0 : 5.5) &&
                                seek_elapsed_ms >= 1000 &&
                                seek_elapsed_ms <= 3200 &&
                                reseek_count < 1;
                        if (can_4k_compensate_reseek) {
                            double nudge_sec = std::min(10.0, std::max(3.0, behind_sec * 0.65));
                            double dispatch_target = seek_target_now + nudge_sec;
                            double duration_now = player_core_get_duration(player_core_);
                            if (std::isfinite(duration_now) && duration_now > 0.0) {
                                dispatch_target = std::min(dispatch_target, std::max(0.0, duration_now - 0.35));
                            }
                            seek_nonsecure_4k_reseek_count_.store(reseek_count + 1, std::memory_order_release);
                            seek_started_at_ms_ = now;
                            seek_lower_bound_drop_count_ = 0;
                            seek_verify_hits_.store(0, std::memory_order_release);
                            seek_lower_bound_active_.store(true, std::memory_order_release);
                            seek_lower_bound_deadline_ms_ = now + (very_large_seek ? 2600 : 2000);
                            seek_recovery_active_.store(true, std::memory_order_release);
                            seek_recovery_deadline_ms_ = now + (very_large_seek ? 5200 : 4200);
                            seek_audio_wait_video_.store(true, std::memory_order_release);
                            seek_audio_wait_deadline_ms_ = now + (very_large_seek ? 4800 : 3600);
                            seek_resume_stable_hits_.store(0, std::memory_order_release);
                            seek_fast_catchup_frames_.store(very_large_seek ? 260 : 180, std::memory_order_release);
                            seek_catchup_deadline_ms_ = now + (very_large_seek ? 3000 : 2200);
                            sync_warmup_frames_.store(36, std::memory_order_release);
                            if (playItf_ && isAudioOutputEnabled()) {
                                setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, true);
                            }
                            player_core_seek(player_core_, dispatch_target);
                            render_cv_.notify_one();
                            should_consume = true;
                            SYNCW("evt=seek_nonsecure_4k_compensate_reseek target=%.3f dispatch=%.3f pts=%.3f behind=%.3f elapsed_ms=%" PRId64 " count=%d",
                                  seek_target_now, dispatch_target, pts, behind_sec, seek_elapsed_ms, reseek_count + 1);
                        }
                    }
                    if (!is_backward_seek && secure_session && player_core_) {
                        double behind_sec = seek_target_now - pts;
                        int reseek_count = secure_seek_precise_reseek_count_.load(std::memory_order_acquire);
                        constexpr bool kForwardUseBehindReseek = false;
                        bool can_forward_behind_reseek = false;
                        if (kForwardUseBehindReseek) {
                            can_forward_behind_reseek =
                                    behind_sec > 8.0 &&
                                    seek_elapsed_ms >= 1100 &&
                                    seek_elapsed_ms <= 2600 &&
                                    reseek_count < 2 &&
                                    now >= secure_seek_precise_reseek_cooldown_until_ms_;
                        }
                        if (can_forward_behind_reseek) {
                            double nudge_factor = 0.36 + 0.08 * std::min(reseek_count, 4);
                            double forward_nudge_sec = std::min(12.0, std::max(2.5, behind_sec * nudge_factor));
                            double dispatch_forward_target = seek_target_now + forward_nudge_sec;
                            int next_reseek_count = std::min(reseek_count + 1, 2);
                            // Do not let "behind-reseek" inflate the counter too much,
                            // otherwise later overshoot branch may over-backoff.
                            secure_seek_precise_reseek_count_.store(next_reseek_count, std::memory_order_release);
                            secure_seek_precise_reseek_cooldown_until_ms_ = now + 900;
                            seek_target_sec_.store(seek_target_now, std::memory_order_release);
                            seek_started_at_ms_ = now;
                            setSeekPhase(SEEK_PHASE_CONVERGE, "seek_forward_behind_reseek");
                            seek_verify_hits_.store(0, std::memory_order_release);
                            seek_lower_bound_active_.store(true, std::memory_order_release);
                            seek_lower_bound_deadline_ms_ = now + secure_lower_bound_deadline_normal_ms_;
                            seek_recovery_active_.store(true, std::memory_order_release);
                            seek_recovery_deadline_ms_ = now + secure_recovery_deadline_normal_ms_;
                            seek_audio_wait_video_.store(true, std::memory_order_release);
                            seek_audio_wait_deadline_ms_ = now + secure_audio_wait_deadline_normal_ms_;
                            seek_resume_stable_hits_.store(0, std::memory_order_release);
                            sync_warmup_frames_.store(44, std::memory_order_release);
                            if (playItf_ && isAudioOutputEnabled()) {
                                setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, true);
                            }
                            player_core_seek(player_core_, dispatch_forward_target);
                            render_cv_.notify_one();
                            should_consume = true;
                            seek_lower_bound_drop_count_++;
                            SYNCW("evt=seek_forward_behind_reseek target=%.3f dispatch=%.3f pts=%.3f behind=%.3f elapsed_ms=%" PRId64 " count=%d",
                                  seek_target_now, dispatch_forward_target, pts, behind_sec, seek_elapsed_ms, next_reseek_count);
                        }
                    }
                    if (should_consume) {
                        // Forward-behind reseek path already handled above.
                    } else {
                    should_consume = true;
                    seek_lower_bound_drop_count_++;
                    LOGW_RATE(20, "[sync] seek lower-bound drop: pts=%.3f target=%.3f",
                              pts, seek_target_now);
                    SYNCW_RATE(20, "evt=seek_lower_bound_drop pts=%.3f target=%.3f elapsed_ms=%" PRId64 " drop_count=%d eps=%.3f",
                               pts, seek_target_now, seek_elapsed_ms, seek_lower_bound_drop_count_, seek_epsilon_sec);
                    }
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
                           delay < -3.2 &&
                           seek_elapsed_ms < (likely_4k ? 2600 : 5200)) {
                    // Avoid exiting seek gate too early when frame still trails
                    // audio clock by a lot; otherwise users see "loading gone but frozen".
                    should_consume = true;
                    seek_lower_bound_drop_count_++;
                    SYNCW_RATE(20, "evt=seek_lower_bound_delay_guard pts=%.3f target=%.3f delay=%.3f elapsed_ms=%" PRId64 " drop_count=%d",
                               pts, seek_target_now, delay, seek_elapsed_ms, seek_lower_bound_drop_count_);
                } else {
                    // SecureHLS strict acceptance gate:
                    // lower-bound hit should not accept a frame too far ahead of target too early.
                    // keep dropping during early window to pursue a closer landing point.
                    bool forward_soft_accept_active = false;
                    double forward_soft_accept_offset_sec = 0.0;
                    double secure_accept_gate_max_sec_for_diag = std::numeric_limits<double>::quiet_NaN();
                    if (secure_session) {
                        double future_offset_sec = pts - seek_target_now;
                        double secure_accept_future_sec = is_backward_seek
                                ? secure_accept_future_backward_early_sec_
                                : secure_accept_future_forward_early_sec_;
                        if (seek_elapsed_ms >= secure_accept_mid_elapsed_ms_) {
                            secure_accept_future_sec = is_backward_seek
                                    ? secure_accept_future_backward_mid_sec_
                                    : secure_accept_future_forward_mid_sec_;
                        }
                        if (seek_elapsed_ms >= secure_accept_late_elapsed_ms_) {
                            secure_accept_future_sec = is_backward_seek
                                    ? secure_accept_future_backward_late_sec_
                                    : secure_accept_future_forward_late_sec_;
                        }
                        secure_accept_gate_max_sec_for_diag = secure_accept_future_sec;
                        if (is_backward_seek && large_seek_any_direction) {
                            // UX-first backward seek: earlier relaxed accept window
                            // shortens long drop loops on encrypted streams.
                            secure_accept_future_sec = std::max(secure_accept_future_sec, 4.8);
                            if (seek_elapsed_ms >= 900) {
                                secure_accept_future_sec = std::max(secure_accept_future_sec, 6.5);
                            }
                            if (seek_elapsed_ms >= 1800) {
                                secure_accept_future_sec = std::max(secure_accept_future_sec, 8.0);
                            }
                        }
                        if (!is_backward_seek && large_seek_any_direction) {
                            // Large forward secure seeks are prone to keyframe cluster jitter.
                            // Slightly relax early acceptance to reduce reseek ping-pong.
                            if (seek_elapsed_ms < secure_accept_mid_elapsed_ms_) {
                                secure_accept_future_sec = std::max(secure_accept_future_sec, 8.8);
                            } else if (seek_elapsed_ms < secure_accept_late_elapsed_ms_) {
                                secure_accept_future_sec = std::max(secure_accept_future_sec, 9.8);
                            }
                            if (seek_elapsed_ms >= 2600) {
                                secure_accept_future_sec = std::max(secure_accept_future_sec, 11.0);
                            }
                            if (seek_elapsed_ms >= 4200) {
                                secure_accept_future_sec = std::max(secure_accept_future_sec, 12.5);
                            }
                        }
                        int reseek_count = secure_seek_precise_reseek_count_.load(std::memory_order_acquire);
                        bool has_seek_progress = std::isfinite(seek_progress_best_abs_err) &&
                                                 seek_progress_best_abs_err < 9.5;
                        double forward_abs_offset_sec = std::fabs(future_offset_sec);
                        double forward_soft_accept_max_sec = seek_elapsed_ms >= 6500 ? 0.80
                                                            : (seek_elapsed_ms >= 4500 ? 0.65
                                                            : (seek_elapsed_ms >= 3200 ? 0.55 : 0.45));
                        bool forward_ux_fast_accept = false;
                        bool backward_ux_fast_accept = false;
                        if (!is_backward_seek && large_seek_any_direction && seek_elapsed_ms >= 1800) {
                            double ux_fast_accept_sec = (seek_elapsed_ms >= 4200) ? 1.20 : 0.85;
                            if (reseek_count >= 3) {
                                ux_fast_accept_sec += 0.2;
                            }
                            forward_ux_fast_accept =
                                    future_offset_sec > secure_accept_future_sec &&
                                    future_offset_sec <= ux_fast_accept_sec &&
                                    has_seek_progress;
                        }
                        if (is_backward_seek && large_seek_any_direction && seek_elapsed_ms >= 300) {
                            double backward_fast_accept_sec = (seek_elapsed_ms >= 1800) ? 8.0 : 6.5;
                            backward_ux_fast_accept =
                                    future_offset_sec <= backward_fast_accept_sec &&
                                    (has_seek_progress || future_offset_sec <= 4.5);
                        }
                        bool forward_one_way_converge = !is_backward_seek &&
                                                        large_seek_any_direction &&
                                                        seek_elapsed_ms >= 1800 &&
                                                        has_seek_progress &&
                                                        forward_abs_offset_sec <= forward_soft_accept_max_sec;
                        if (!is_backward_seek &&
                            large_seek_any_direction &&
                            seek_elapsed_ms >= 4200 &&
                            has_seek_progress &&
                            forward_abs_offset_sec <= forward_soft_accept_max_sec) {
                            // Late-stage forward seek: avoid ping-pong between accept/reseek.
                            forward_one_way_converge = true;
                        }
                        if (forward_ux_fast_accept) {
                            SYNCI("evt=seek_secure_accept_gate_fast_accept pts=%.3f target=%.3f future_offset=%.3f accept_max=%.3f elapsed_ms=%" PRId64 " reseek_count=%d best_abs_err=%.3f",
                                  pts, seek_target_now, future_offset_sec, secure_accept_future_sec, seek_elapsed_ms,
                                  reseek_count, seek_progress_best_abs_err);
                        }
                        if (backward_ux_fast_accept) {
                            SYNCI_RATE(20,
                                       "evt=seek_secure_accept_gate_backward_fast_accept pts=%.3f target=%.3f future_offset=%.3f accept_max=%.3f elapsed_ms=%" PRId64 " reseek_count=%d best_abs_err=%.3f",
                                       pts, seek_target_now, future_offset_sec, secure_accept_future_sec, seek_elapsed_ms,
                                       reseek_count, seek_progress_best_abs_err);
                        }
                        if (forward_one_way_converge) {
                            forward_soft_accept_active = true;
                            forward_soft_accept_offset_sec = std::max(forward_soft_accept_offset_sec, std::fabs(future_offset_sec));
                            SYNCI_RATE(20,
                                       "evt=seek_secure_accept_gate_one_way_converge pts=%.3f target=%.3f future_offset=%.3f accept_max=%.3f elapsed_ms=%" PRId64 " reseek_count=%d best_abs_err=%.3f",
                                       pts, seek_target_now, future_offset_sec, secure_accept_future_sec, seek_elapsed_ms,
                                       reseek_count, seek_progress_best_abs_err);
                        }
                        bool forward_anti_stall_relax =
                                !is_backward_seek &&
                                large_seek_any_direction &&
                                (
                                        (seek_elapsed_ms >= 1800 &&
                                         seek_lower_bound_drop_count_ >= 18 &&
                                         has_seek_progress &&
                                         forward_abs_offset_sec <= forward_soft_accept_max_sec) ||
                                        (seek_elapsed_ms >= 4200 &&
                                         seek_lower_bound_drop_count_ >= 30 &&
                                         forward_abs_offset_sec <= forward_soft_accept_max_sec)
                                );
                    bool secure_forward_early_relax =
                            !is_backward_seek &&
                            large_seek_any_direction &&
                            secure_session_active_.load(std::memory_order_acquire) &&
                            seek_elapsed_ms >= 1200 &&
                            seek_lower_bound_drop_count_ >= 10 &&
                            has_seek_progress &&
                            forward_abs_offset_sec <= forward_soft_accept_max_sec;
                    if (secure_forward_early_relax) {
                        forward_anti_stall_relax = true;
                    }
                        if (forward_anti_stall_relax) {
                            forward_soft_accept_active = true;
                            forward_soft_accept_offset_sec = std::max(forward_soft_accept_offset_sec, std::fabs(future_offset_sec));
                            SYNCI_RATE(20,
                                   "evt=seek_forward_anti_stall_relax pts=%.3f target=%.3f future_offset=%.3f elapsed_ms=%" PRId64 " drop_count=%d best_abs_err=%.3f secure_early=%d",
                                       pts, seek_target_now, future_offset_sec, seek_elapsed_ms,
                                   seek_lower_bound_drop_count_, seek_progress_best_abs_err,
                                   secure_forward_early_relax ? 1 : 0);
                        }
                        if (!forward_ux_fast_accept &&
                            !backward_ux_fast_accept &&
                            !forward_one_way_converge &&
                            !forward_anti_stall_relax &&
                            future_offset_sec > secure_accept_future_sec &&
                            seek_elapsed_ms < 7000) {
                            should_consume = true;
                            seek_lower_bound_drop_count_++;
                            bool backward_gate_stuck = is_backward_seek
                                    && future_offset_sec > (secure_accept_future_sec + 2.8)
                                    && seek_elapsed_ms >= 700
                                    && seek_elapsed_ms <= 1700
                                    && player_core_;
                            bool can_gate_reseek = ((backward_gate_stuck && reseek_count < 1))
                                    && now >= secure_seek_precise_reseek_cooldown_until_ms_;
                            if (!is_backward_seek) {
                                // Forward only keeps precise_reseek channel.
                                can_gate_reseek = false;
                            }
                            if (!is_backward_seek && seek_elapsed_ms >= 1800 && reseek_count >= 3) {
                                can_gate_reseek = false;
                            }
                            if (can_gate_reseek) {
                                double gate_backoff_sec = is_backward_seek
                                        ? std::min(180.0, std::max(6.0, future_offset_sec * 1.15))
                                        : ([&]() {
                                            bool near_target_forward = future_offset_sec <= 16.0;
                                            double progressive_factor = near_target_forward
                                                                        ? (0.78 + 0.14 * reseek_count)
                                                                        : (1.10 + 0.30 * reseek_count);
                                            if (!near_target_forward && reseek_count >= 3) {
                                                progressive_factor += 0.40;
                                            }
                                            double additive = near_target_forward
                                                              ? (reseek_count * 2.5)
                                                              : (reseek_count * 6.0);
                                            double max_backoff = near_target_forward ? 48.0 : 240.0;
                                            double min_backoff = near_target_forward ? 6.0 : 10.0;
                                            return std::min(max_backoff,
                                                            std::max(min_backoff,
                                                                     future_offset_sec * progressive_factor + additive));
                                        })();
                                double gate_dispatch_target = std::max(0.0, seek_target_now - gate_backoff_sec);
                                secure_seek_precise_reseek_count_.store(reseek_count + 1, std::memory_order_release);
                                secure_seek_precise_reseek_cooldown_until_ms_ = now + 900;
                                seek_target_sec_.store(seek_target_now, std::memory_order_release);
                                seek_started_at_ms_ = now;
                                setSeekPhase(SEEK_PHASE_CONVERGE, "seek_secure_accept_gate_reseek");
                                seek_verify_hits_.store(0, std::memory_order_release);
                                seek_lower_bound_active_.store(true, std::memory_order_release);
                                seek_lower_bound_deadline_ms_ = now + (is_backward_seek
                                                                       ? secure_lower_bound_deadline_large_ms_
                                                                       : secure_lower_bound_deadline_normal_ms_);
                                seek_recovery_active_.store(true, std::memory_order_release);
                                seek_recovery_deadline_ms_ = now + (is_backward_seek
                                                                    ? secure_recovery_deadline_large_ms_
                                                                    : secure_recovery_deadline_normal_ms_);
                                seek_audio_wait_video_.store(true, std::memory_order_release);
                                seek_audio_wait_deadline_ms_ = now + (is_backward_seek
                                                                      ? secure_audio_wait_deadline_large_ms_
                                                                      : secure_audio_wait_deadline_normal_ms_);
                                seek_resume_stable_hits_.store(0, std::memory_order_release);
                                sync_warmup_frames_.store(44, std::memory_order_release);
                                if (playItf_ && isAudioOutputEnabled()) {
                                    setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, true);
                                }
                                player_core_seek(player_core_, gate_dispatch_target);
                                render_cv_.notify_one();
                                SYNCW("evt=seek_secure_accept_gate_reseek target=%.3f dispatch=%.3f pts=%.3f future_offset=%.3f accept_max=%.3f elapsed_ms=%" PRId64 " count=%d backward=%d",
                                      seek_target_now, gate_dispatch_target, pts, future_offset_sec,
                                      secure_accept_future_sec, seek_elapsed_ms, reseek_count + 1,
                                      is_backward_seek ? 1 : 0);
                            } else {
                                SYNCW_RATE(20,
                                           "evt=seek_secure_accept_gate_drop pts=%.3f target=%.3f future_offset=%.3f accept_max=%.3f elapsed_ms=%" PRId64 " drop_count=%d",
                                           pts, seek_target_now, future_offset_sec, secure_accept_future_sec,
                                           seek_elapsed_ms, seek_lower_bound_drop_count_);
                            }
                        }

                        // Tencent-like precise completion gate:
                        // even after lower-bound hit, avoid completing seek when still too far ahead.
                        // Prefer a bounded completion-reseek before accepting large offsets.
                        double secure_completion_tol_sec = is_backward_seek ? 4.2 : 1.8;
                        if (large_seek_any_direction) {
                            secure_completion_tol_sec += is_backward_seek ? 1.8 : 0.7;
                        }
                        if (seek_elapsed_ms >= 5000) {
                            secure_completion_tol_sec += is_backward_seek ? 0.6 : 1.2;
                        }
                        if (is_backward_seek && large_seek_any_direction) {
                            if (seek_elapsed_ms >= 900) {
                                secure_completion_tol_sec = std::max(secure_completion_tol_sec, 5.0);
                            }
                            if (seek_elapsed_ms >= 1800) {
                                secure_completion_tol_sec = std::max(secure_completion_tol_sec, 6.2);
                            }
                        }
                        if (!is_backward_seek && large_seek_any_direction) {
                            if (seek_elapsed_ms >= 2200) {
                                secure_completion_tol_sec = std::max(secure_completion_tol_sec, 1.20);
                            }
                            if (seek_elapsed_ms >= 4200) {
                                secure_completion_tol_sec = std::max(secure_completion_tol_sec, 1.40);
                            }
                            if (seek_elapsed_ms >= 3000) {
                                secure_completion_tol_sec = std::max(secure_completion_tol_sec, 1.30);
                            }
                            if (seek_elapsed_ms >= 5200) {
                                secure_completion_tol_sec = std::max(secure_completion_tol_sec, 1.60);
                            }
                        }
                        bool forward_terminal_accept = !is_backward_seek &&
                                                       large_seek_any_direction &&
                                                       seek_elapsed_ms >= 2100 &&
                                                       has_seek_progress &&
                                                       std::isfinite(seek_progress_best_abs_err) &&
                                                       seek_progress_best_abs_err <= 3.2 &&
                                                       forward_abs_offset_sec <= forward_soft_accept_max_sec;
                        if (forward_terminal_accept) {
                            forward_soft_accept_active = true;
                            forward_soft_accept_offset_sec = std::max(forward_soft_accept_offset_sec, std::fabs(future_offset_sec));
                            SYNCI_RATE(20,
                                       "evt=seek_secure_forward_terminal_accept pts=%.3f target=%.3f future_offset=%.3f elapsed_ms=%" PRId64 " best_abs_err=%.3f",
                                       pts, seek_target_now, future_offset_sec, seek_elapsed_ms, seek_progress_best_abs_err);
                        }
                        if (!should_consume &&
                            !forward_soft_accept_active &&
                            future_offset_sec > secure_completion_tol_sec &&
                            seek_elapsed_ms < 9500) {
                            int completion_reseek_count = secure_seek_precise_reseek_count_.load(std::memory_order_acquire);
                            bool can_completion_reseek = (is_backward_seek
                                                          ? (future_offset_sec <= 6.0 &&
                                                             completion_reseek_count < 1 &&
                                                             seek_elapsed_ms < 700)
                                                          : false)
                                    && now >= secure_seek_precise_reseek_cooldown_until_ms_
                                    && seek_elapsed_ms >= 650
                                    && player_core_;
                            if (!is_backward_seek &&
                                seek_elapsed_ms >= 1800 &&
                                completion_reseek_count >= 1) {
                                can_completion_reseek = false;
                            }
                            bool backward_fast_accept = is_backward_seek &&
                                                        large_seek_any_direction &&
                                                        seek_elapsed_ms >= 1200 &&
                                                        future_offset_sec <= 5.8;
                            if (can_completion_reseek) {
                                double completion_backoff_sec = is_backward_seek
                                        ? std::min(180.0, std::max(5.0, future_offset_sec * 1.20))
                                        : ([&]() {
                                            double progressive_factor = 0.95 + 0.25 * completion_reseek_count;
                                            return std::min(180.0,
                                                            std::max(8.0,
                                                                     future_offset_sec * progressive_factor +
                                                                     completion_reseek_count * 4.0));
                                        })();
                                double completion_dispatch_target = std::max(0.0, seek_target_now - completion_backoff_sec);
                                secure_seek_precise_reseek_count_.store(completion_reseek_count + 1, std::memory_order_release);
                                secure_seek_precise_reseek_cooldown_until_ms_ = now + 900;
                                seek_target_sec_.store(seek_target_now, std::memory_order_release);
                                seek_started_at_ms_ = now;
                                setSeekPhase(SEEK_PHASE_CONVERGE, "seek_secure_completion_reseek");
                                seek_verify_hits_.store(0, std::memory_order_release);
                                seek_lower_bound_active_.store(true, std::memory_order_release);
                                seek_lower_bound_deadline_ms_ = now + (is_backward_seek ? secure_lower_bound_deadline_large_ms_
                                                                                          : secure_lower_bound_deadline_normal_ms_);
                                seek_recovery_active_.store(true, std::memory_order_release);
                                seek_recovery_deadline_ms_ = now + (is_backward_seek ? secure_recovery_deadline_large_ms_
                                                                                      : secure_recovery_deadline_normal_ms_);
                                seek_audio_wait_video_.store(true, std::memory_order_release);
                                seek_audio_wait_deadline_ms_ = now + (is_backward_seek ? secure_audio_wait_deadline_large_ms_
                                                                                         : secure_audio_wait_deadline_normal_ms_);
                                seek_resume_stable_hits_.store(0, std::memory_order_release);
                                sync_warmup_frames_.store(44, std::memory_order_release);
                                if (playItf_ && isAudioOutputEnabled()) {
                                    setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, true);
                                }
                                player_core_seek(player_core_, completion_dispatch_target);
                                render_cv_.notify_one();
                                should_consume = true;
                                seek_lower_bound_drop_count_++;
                                SYNCW("evt=seek_secure_completion_reseek target=%.3f dispatch=%.3f pts=%.3f future_offset=%.3f tol=%.3f elapsed_ms=%" PRId64 " count=%d",
                                      seek_target_now, completion_dispatch_target, pts, future_offset_sec, secure_completion_tol_sec,
                                      seek_elapsed_ms, completion_reseek_count + 1);
                            } else if (backward_fast_accept) {
                                SYNCI_RATE(20,
                                           "evt=seek_secure_completion_backward_fast_accept pts=%.3f target=%.3f future_offset=%.3f tol=%.3f elapsed_ms=%" PRId64,
                                           pts, seek_target_now, future_offset_sec, secure_completion_tol_sec, seek_elapsed_ms);
                            } else if (seek_elapsed_ms < 7000) {
                                should_consume = true;
                                seek_lower_bound_drop_count_++;
                                SYNCW_RATE(20,
                                           "evt=seek_secure_completion_drop pts=%.3f target=%.3f future_offset=%.3f tol=%.3f elapsed_ms=%" PRId64 " drop_count=%d",
                                           pts, seek_target_now, future_offset_sec, secure_completion_tol_sec,
                                           seek_elapsed_ms, seek_lower_bound_drop_count_);
                            }
                        }
                    }
                    if (!should_consume && secure_session) {
                        setSeekPhase(SEEK_PHASE_VERIFY, "seek_secure_verify_window");
                        double duration_now = player_core_get_duration(player_core_);
                        bool near_end_seek = false;
                        if (std::isfinite(duration_now) && duration_now > 0.0 && std::isfinite(seek_target_now)) {
                            near_end_seek = (duration_now - seek_target_now) <= 300.0;
                        }
                        double verify_max_offset_sec = is_backward_seek ? 3.8 : 0.65;
                        if (large_seek_any_direction) {
                            verify_max_offset_sec += is_backward_seek ? 0.6 : 0.0;
                        }
                        if (is_backward_seek && near_end_seek) {
                            // 尾段 backward seek 使用更严格的收敛容忍，减少“看似完成但仍偏离目标”的假收敛。
                            verify_max_offset_sec = std::min(verify_max_offset_sec, 2.4);
                        }
                        if (is_backward_seek &&
                            std::isfinite(secure_accept_gate_max_sec_for_diag) &&
                            secure_accept_gate_max_sec_for_diag > 0.0) {
                            // Keep VERIFY tolerance aligned with ACCEPT gate on backward secure seek,
                            // otherwise we can loop on "accept says ok but verify keeps dropping".
                            double backward_verify_floor = std::min(6.2, std::max(2.8, secure_accept_gate_max_sec_for_diag - 0.2));
                            verify_max_offset_sec = std::max(verify_max_offset_sec, backward_verify_floor);
                        }
                        if (seek_elapsed_ms >= 5200 && !(is_backward_seek && near_end_seek)) {
                            verify_max_offset_sec = std::max(verify_max_offset_sec, is_backward_seek ? 4.8 : 1.0);
                        }
                        if (!is_backward_seek && large_seek_any_direction) {
                            if (seek_elapsed_ms >= 4500) {
                                verify_max_offset_sec = std::max(verify_max_offset_sec, 0.80);
                            }
                            if (seek_elapsed_ms >= 6500) {
                                verify_max_offset_sec = std::max(verify_max_offset_sec, 1.00);
                            }
                        }
                        if (!is_backward_seek && forward_soft_accept_active) {
                            verify_max_offset_sec = std::max(verify_max_offset_sec,
                                                            forward_soft_accept_offset_sec + 0.12);
                        }
                        double verify_offset_sec = std::fabs(pts - seek_target_now);
                        int verify_need_hits = is_backward_seek ? 2 : 3;
                        if (is_backward_seek && near_end_seek) {
                            verify_need_hits = 3;
                        }
                        if (is_backward_seek && !near_end_seek && seek_elapsed_ms >= 900 && verify_offset_sec <= 3.0) {
                            verify_need_hits = 1;
                        }
                        if (is_backward_seek && !near_end_seek && seek_elapsed_ms >= 1800 && verify_offset_sec <= 2.4) {
                            verify_need_hits = 1;
                        }
                        if (is_backward_seek && !near_end_seek && seek_elapsed_ms >= 3200) {
                            verify_need_hits = 1;
                        }
                        if (!is_backward_seek && seek_elapsed_ms >= 2600 && verify_offset_sec <= 0.50) {
                            verify_need_hits = 1;
                        }
                        if (!is_backward_seek && large_seek_any_direction && seek_elapsed_ms >= 2800 && verify_offset_sec <= 0.55) {
                            verify_need_hits = 1;
                        }
                        if (!is_backward_seek && large_seek_any_direction && seek_elapsed_ms >= 4200 && verify_offset_sec <= 0.65) {
                            verify_need_hits = 1;
                        }
                        if (seek_elapsed_ms >= 5600 && !(is_backward_seek && near_end_seek)) {
                            verify_need_hits = 1;
                        }
                        if (is_backward_seek && near_end_seek) {
                            // 尾段下避免过早降为单次命中放行，优先保证落点稳定。
                            if (seek_elapsed_ms < 5000 || verify_offset_sec > 1.8) {
                                verify_need_hits = std::max(verify_need_hits, 2);
                            }
                            if (seek_elapsed_ms < 9000 && verify_offset_sec > 2.2) {
                                should_consume = true;
                                seek_verify_hits_.store(0, std::memory_order_release);
                                seek_lower_bound_drop_count_++;
                                SYNCW_RATE(20,
                                           "evt=seek_verify_tail_backward_hold pts=%.3f target=%.3f offset=%.3f elapsed_ms=%" PRId64 " drop_count=%d",
                                           pts, seek_target_now, verify_offset_sec, seek_elapsed_ms, seek_lower_bound_drop_count_);
                            }
                        }
                        if (!is_backward_seek && forward_soft_accept_active) {
                            verify_need_hits = 1;
                        }
                        if (!is_backward_seek &&
                            large_seek_any_direction &&
                            seek_elapsed_ms >= 2600 &&
                            std::isfinite(seek_progress_best_abs_err) &&
                            seek_progress_best_abs_err < 0.80) {
                            verify_max_offset_sec = std::max(verify_max_offset_sec, 0.65);
                            verify_need_hits = 1;
                        }
                        if (!is_backward_seek &&
                            large_seek_any_direction &&
                            seek_elapsed_ms >= 2200 &&
                            seek_lower_bound_drop_count_ >= 18) {
                            double forward_verify_relax = seek_elapsed_ms >= 6500 ? 1.00
                                                          : (seek_elapsed_ms >= 4500 ? 0.80 : 0.65);
                            verify_max_offset_sec = std::max(verify_max_offset_sec, forward_verify_relax);
                            if (verify_offset_sec <= forward_verify_relax) {
                                verify_need_hits = 1;
                            }
                            SYNCI_RATE(20,
                                       "evt=seek_forward_verify_relax verify_max=%.3f elapsed_ms=%" PRId64 " drop_count=%d offset=%.3f",
                                       verify_max_offset_sec, seek_elapsed_ms, seek_lower_bound_drop_count_, verify_offset_sec);
                        }
                        if (verify_offset_sec > verify_max_offset_sec && seek_elapsed_ms < 9000) {
                            should_consume = true;
                            seek_verify_hits_.store(0, std::memory_order_release);
                            seek_lower_bound_drop_count_++;
                            bool accept_verify_conflict =
                                    std::isfinite(secure_accept_gate_max_sec_for_diag) &&
                                    verify_offset_sec <= secure_accept_gate_max_sec_for_diag &&
                                    verify_offset_sec > verify_max_offset_sec;
                            if (accept_verify_conflict) {
                                SYNCW_RATE(20,
                                           "evt=seek_verify_accept_conflict pts=%.3f target=%.3f offset=%.3f accept_max=%.3f verify_max=%.3f elapsed_ms=%" PRId64 " backward=%d drop_count=%d",
                                           pts, seek_target_now, verify_offset_sec,
                                           secure_accept_gate_max_sec_for_diag, verify_max_offset_sec,
                                           seek_elapsed_ms, is_backward_seek ? 1 : 0, seek_lower_bound_drop_count_);
                            }
                            SYNCW_RATE(20,
                                       "evt=seek_verify_drop pts=%.3f target=%.3f offset=%.3f verify_max=%.3f accept_max=%.3f conflict=%d elapsed_ms=%" PRId64 " drop_count=%d",
                                       pts, seek_target_now, verify_offset_sec, verify_max_offset_sec,
                                       secure_accept_gate_max_sec_for_diag,
                                       accept_verify_conflict ? 1 : 0,
                                       seek_elapsed_ms, seek_lower_bound_drop_count_);
                        } else {
                            int verify_hits = seek_verify_hits_.fetch_add(1, std::memory_order_acq_rel) + 1;
                            if (verify_hits < verify_need_hits && seek_elapsed_ms < 8000) {
                                should_consume = true;
                                seek_lower_bound_drop_count_++;
                                SYNCW_RATE(20,
                                           "evt=seek_verify_wait pts=%.3f target=%.3f offset=%.3f hits=%d/%d elapsed_ms=%" PRId64 " drop_count=%d",
                                           pts, seek_target_now, verify_offset_sec, verify_hits, verify_need_hits,
                                           seek_elapsed_ms, seek_lower_bound_drop_count_);
                            } else {
                                seek_verify_hits_.store(0, std::memory_order_release);
                            }
                        }
                    }
                    if (!should_consume) {
                        seek_lower_bound_active_.store(false, std::memory_order_release);
                        seek_fast_catchup_frames_.store(0, std::memory_order_release);
                        seek_catchup_deadline_ms_ = 0;
                        seek_recovery_active_.store(false, std::memory_order_release);
                        seek_recovery_deadline_ms_ = 0;
                        in_seek_recovery = false;
                        seek_started_at_ms_ = 0;
                        seek_lower_bound_drop_count_ = 0;
                        sync_warmup_frames_.store(24, std::memory_order_release);
                        int64_t bypass_ms = is_backward_seek ? 700 : 450;
                        if (likely_4k) {
                            // 4K keeps a slightly longer anti-freeze window, but still
                            // much shorter than before to avoid multi-second A/V drift tails.
                            bypass_ms = is_backward_seek ? 1800 : 1200;
                        }
                        double duration_now = player_core_get_duration(player_core_);
                        bool near_end_seek = false;
                        if (std::isfinite(duration_now) && duration_now > 0.0 && std::isfinite(seek_target_now)) {
                            near_end_seek = (duration_now - seek_target_now) <= 300.0;
                        }
                        if (is_backward_seek && near_end_seek) {
                            bypass_ms = std::max<int64_t>(bypass_ms, likely_4k ? 4200 : 3200);
                        }
                        post_seek_ahead_bypass_until_ms = now + bypass_ms;
                        should_display = should_consume = true;
                        LOGI("[sync] seek lower-bound hit: pts=%.3f target=%.3f delay=%.3f backward=%d bypass_ms=%" PRId64 " elapsed_ms=%" PRId64,
                             pts, seek_target_now, delay, is_backward_seek ? 1 : 0, bypass_ms, seek_elapsed_ms);
                        SYNCI("evt=seek_lower_bound_hit pts=%.3f target=%.3f delay=%.3f backward=%d bypass_ms=%" PRId64 " elapsed_ms=%" PRId64,
                              pts, seek_target_now, delay, is_backward_seek ? 1 : 0, bypass_ms, seek_elapsed_ms);
                        if (!is_backward_seek) {
                            double abs_err = std::fabs(pts - seek_target_now);
                            if (std::isfinite(abs_err)) {
                                double prev_bias = secure_forward_seek_bias_sec_.load(std::memory_order_acquire);
                                int prev_hits = secure_forward_seek_bias_hits_.load(std::memory_order_acquire);
                                if (!std::isfinite(prev_bias) || prev_bias < 0.0) {
                                    prev_bias = 0.0;
                                    prev_hits = 0;
                                }
                                if (abs_err <= 2.5) {
                                    double next_bias = std::max(0.0, prev_bias * 0.78 - 1.2);
                                    int next_hits = prev_hits > 0 ? prev_hits - 1 : 0;
                                    secure_forward_seek_bias_sec_.store(next_bias, std::memory_order_release);
                                    secure_forward_seek_bias_hits_.store(next_hits, std::memory_order_release);
                                    secure_forward_seek_bias_last_update_ms_.store(now, std::memory_order_release);
                                    SYNCI("evt=seek_secure_forward_bias_decay abs_err=%.3f prev=%.3f next=%.3f hits=%d",
                                          abs_err, prev_bias, next_bias, next_hits);
                                }
                            }
                        }
                        resumeSeekAudioAfterKeyframeAhead(now,
                                                          pts,
                                                          clock,
                                                          delay,
                                                          seek_target_now,
                                                          is_backward_seek,
                                                          large_forward_seek,
                                                          likely_4k,
                                                          post_seek_ahead_bypass_until_ms);
                    }
                }
            }

            // During seek recovery, disable normal sync branches entirely.
            if (in_seek_recovery && !should_consume && !should_display) {
                if (now >= seek_recovery_deadline_ms_) {
                    setSeekPhase(SEEK_PHASE_FAILOVER, "seek_recovery_deadline");
                    // Timeout fallback: stop recovery loop and render latest frame.
                    seek_recovery_active_.store(false, std::memory_order_release);
                    seek_recovery_deadline_ms_ = 0;
                    seek_lower_bound_active_.store(false, std::memory_order_release);
                    seek_lower_bound_deadline_ms_ = 0;
                    seek_started_at_ms_ = 0;
                    seek_lower_bound_drop_count_ = 0;
                    seek_verify_hits_.store(0, std::memory_order_release);
                    seek_fast_catchup_frames_.store(0, std::memory_order_release);
                    seek_catchup_deadline_ms_ = 0;
                    double anchor_pts = (std::isfinite(pts) && pts >= 0.0) ? pts : seek_target_now;
                    if (player_core_ && std::isfinite(anchor_pts) && anchor_pts >= 0.0) {
                        player_core_anchor_clock(player_core_, anchor_pts);
                        clock = player_core_get_position(player_core_);
                        if (audio_output_latency_sec_ > 0.0) {
                            clock -= audio_output_latency_sec_;
                            if (clock < 0.0) clock = 0.0;
                        }
                        delay = pts - clock;
                    }
                    sync_warmup_frames_.store(likely_4k ? 28 : 20, std::memory_order_release);
                    post_seek_ahead_bypass_until_ms = now + (likely_4k ? 3200 : 1800);
                    seek_audio_wait_video_.store(false, std::memory_order_release);
                    seek_audio_wait_deadline_ms_ = 0;
                    seek_resume_stable_hits_.store(0, std::memory_order_release);
                    bool should_resume_on_complete = seek_resume_on_complete_.load(std::memory_order_acquire);
                    bool manual_pause_blocked = false;
                    if (user_manual_pause_.load(std::memory_order_acquire)) {
                        int64_t block_until = user_manual_pause_block_until_ms_.load(std::memory_order_acquire);
                        manual_pause_blocked = block_until <= 0 || now < block_until;
                    }
                    if (manual_pause_blocked) {
                        should_resume_on_complete = false;
                    }
                    if (should_resume_on_complete) {
                        player_core_set_play_when_ready(player_core_, 1);
                        if (!player_core_is_playing(player_core_)) {
                            player_core_play(player_core_);
                        }
                        if (!player_core_is_playing(player_core_)) {
                            double seek_from_now = seek_from_sec_.load(std::memory_order_acquire);
                            armSeekForceResumePending(now,
                                                      seek_target_now,
                                                      seek_from_now,
                                                      2600,
                                                      "evt=seek_recovery_timeout_force_resume_pending");
                        }
                    }
                    if (playItf_ && isAudioOutputEnabled()) {
                        SLresult r = setOpenSLESPlayState(SL_PLAYSTATE_PLAYING, true);
                        LOGW("[sync] seek recovery timeout resume audio: result=%d anchor=%.3f",
                             r, anchor_pts);
                    }
                    should_display = should_consume = true;
                    LOGW("[sync] seek recovery timeout fallback: pts=%.3f target=%.3f", pts, seek_target_now);
                    uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
                    SYNCW("evt=seek_recovery_timeout_fallback id=%" PRIu64 " phase=%s pts=%.3f target=%.3f anchor=%.3f abs_err=%.3f",
                          sid, seek_phase_name(seek_phase_.load(std::memory_order_acquire)),
                          pts, seek_target_now, anchor_pts, std::fabs(anchor_pts - seek_target_now));
                    in_seek_recovery = false;
                } else {
                    should_consume = true;
                }
            }

            // --- First/post-seek fast-path ---
            bool open_first_frame_pending = !first_frame_rendered_.load(std::memory_order_acquire);
            bool is_first_or_seek = open_first_frame_pending ||
                                     seek_just_happened_.exchange(false, std::memory_order_acq_rel);
            int seek_catchup_left = seek_fast_catchup_frames_.load(std::memory_order_acquire);
            double seek_target = seek_target_sec_.load(std::memory_order_acquire);
            double seek_from_for_catchup = seek_from_sec_.load(std::memory_order_acquire);
            bool secure_forward_catchup =
                    secure_session_active_.load(std::memory_order_acquire) &&
                    seek_target >= 0.0 &&
                    seek_from_for_catchup >= 0.0 &&
                    seek_target > seek_from_for_catchup + 120.0;
            bool seek_catchup_deadline_ok = seek_catchup_deadline_ms_ <= 0 || now < seek_catchup_deadline_ms_;
            bool seek_catchup_enabled = seek_catchup_left > 0
                    && seek_target >= 0.0
                    && (secure_forward_catchup || playback_rate >= 1.75 || frame_data.width >= 3840)
                    && seek_catchup_deadline_ok;
            if (!in_seek_recovery && !should_consume && seek_catchup_enabled && !std::isnan(pts) && !std::isinf(pts)) {
                double behind_seek_target = seek_target - pts;
                // 高倍速下阈值更紧一些，减少“先播旧画面再追上”的时间。
                double seek_drop_threshold = (playback_rate >= 2.25) ? 0.45 : 0.60;
                if (frame_data.width >= 3840) {
                    seek_drop_threshold = std::min(seek_drop_threshold, 0.50);
                }
                if (behind_seek_target > seek_drop_threshold) {
                    // 关键借鉴：seek 后先快速丢弃“明显早于目标位点”的旧帧，
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
                double first_frame_force_threshold = likely_4k ? -0.65 : -0.45;
                bool is_open_first_frame = open_first_frame_pending;
                int64_t first_wait_ms = (is_open_first_frame && first_frame_wait_started_ms_ > 0)
                                        ? (now - first_frame_wait_started_ms_) : 0;
                int64_t first_frame_max_wait_ms = likely_4k ? 3200 : 1800;
                bool first_frame_wait_timeout = is_open_first_frame &&
                                                first_wait_ms >= first_frame_max_wait_ms;
                bool audio_deferred_now = is_open_first_frame &&
                                          audio_start_pending_.load(std::memory_order_acquire);
                // Root fix:
                // During open-first-frame, audio is intentionally deferred. In this stage,
                // the running clock can drift ahead while decoder is still filling queue,
                // making delay look "too behind" and causing long catch-up drop loops.
                // If this lasts for a short grace window, prefer showing first frame and
                // re-anchor clock to current PTS to break freeze/redraw loops.
                bool force_by_deferred_audio_drift =
                        audio_deferred_now &&
                        delay < -0.80 &&
                        first_wait_ms >= 450;
                if (delay < first_frame_force_threshold &&
                    !first_frame_wait_timeout &&
                    !force_by_deferred_audio_drift) {
                    // Still too far behind -- drop silently and keep catching up.
                    should_consume = true;
                    LOGI_RATE(30, "[sync] %s catching up: pts=%.3f clk=%.3f delay=%.3f thr=%.3f",
                              is_open_first_frame ? "first frame" : "post-seek frame",
                              pts, clock, delay, first_frame_force_threshold);
                } else {
                    if (force_by_deferred_audio_drift && player_core_ &&
                        std::isfinite(pts) && pts >= 0.0) {
                        player_core_anchor_clock(player_core_, pts);
                        clock = player_core_get_position(player_core_);
                        if (audio_output_latency_sec_ > 0.0) {
                            clock -= audio_output_latency_sec_;
                            if (clock < 0.0) clock = 0.0;
                        }
                        delay = pts - clock;
                        LOGI("[sync] first frame force by deferred-audio drift: pts=%.3f clk=%.3f delay=%.3f wait_ms=%" PRId64,
                             pts, clock, delay, first_wait_ms);
                        SYNCI("evt=open_first_frame_drift_compat pts=%.3f clk=%.3f delay=%.3f wait_ms=%" PRId64 " thr=%.3f audio_pending=%d",
                              pts, clock, delay, first_wait_ms, first_frame_force_threshold,
                              audio_deferred_now ? 1 : 0);
                    }
                    if (first_frame_wait_timeout) {
                        // Open-first-frame hard timeout: prefer visible recovery over
                        // extended black screen, then re-anchor to current video PTS.
                        if (player_core_ && std::isfinite(pts) && pts >= 0.0) {
                            player_core_anchor_clock(player_core_, pts);
                        }
                        LOGI("[sync] first frame force by timeout: pts=%.3f clk=%.3f delay=%.3f wait_ms=%" PRId64 " max_wait_ms=%" PRId64,
                             pts, clock, delay, first_wait_ms, first_frame_max_wait_ms);
                    }
                    LOGI("[sync] %s forced: pts=%.3f clk=%.3f delay=%.3f",
                         is_open_first_frame ? "first frame" : "post-seek frame",
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
                           post_seek_ahead_bypass_until_ms > now &&
                           delay <= (likely_4k ? 2.6 : 1.4)) {
                    // Seek just recovered, but clock may temporarily roll back or lag.
                    // Bypass "ahead hold" briefly so users don't see frozen picture
                    // (not only for high-rate; 1.0x backward seek can also hit this).
                    should_display = should_consume = true;
                    SYNCI_RATE(30, "evt=post_seek_ahead_bypass pts=%.3f clk=%.3f delay=%.3f rate=%.2f bypass_left_ms=%" PRId64,
                               pts, clock, delay, playback_rate,
                               (int64_t)std::max<int64_t>(0, post_seek_ahead_bypass_until_ms - now));
                } else if (!in_seek_recovery &&
                           post_seek_ahead_bypass_until_ms > now &&
                           delay > (likely_4k ? 2.6 : 1.4)) {
                    bool secure_session = secure_session_active_.load(std::memory_order_acquire);
                    if (secure_session && delay > 3.0 && player_core_ && std::isfinite(pts) && pts >= 0.0) {
                        // Encrypted HLS often lands on a keyframe far ahead of seek target.
                        // Re-anchor and keep bypass instead of cancelling into ahead-hold deadlock.
                        if (secure_bypass_extend_window_start_ms == 0 ||
                            (now - secure_bypass_extend_window_start_ms) > kSecureBypassExtendWindowMs) {
                            secure_bypass_extend_window_start_ms = now;
                            secure_bypass_extend_count = 0;
                        }
                        player_core_anchor_clock(player_core_, pts);
                        clock = player_core_get_position(player_core_);
                        if (audio_output_latency_sec_ > 0.0) {
                            clock -= audio_output_latency_sec_;
                            if (clock < 0.0) clock = 0.0;
                        }
                        delay = pts - clock;
                        if (secure_bypass_extend_count < kSecureBypassExtendMaxCount) {
                            int64_t extend_ms = (int64_t)std::min(4500.0, std::max(1800.0, delay * 700.0 + 900.0));
                            post_seek_ahead_bypass_until_ms = now + extend_ms;
                            secure_bypass_extend_count++;
                            SYNCI_RATE(30, "evt=post_seek_ahead_bypass_secure_extend delay=%.3f extend_ms=%" PRId64 " count=%d",
                                       delay, extend_ms, secure_bypass_extend_count);
                        } else {
                            // Stop repeatedly extending bypass forever; return to normal sync path.
                            post_seek_ahead_bypass_until_ms = 0;
                            SYNCI_RATE(30, "evt=post_seek_ahead_bypass_secure_extend_limit delay=%.3f count=%d",
                                       delay, secure_bypass_extend_count);
                        }
                        should_display = should_consume = true;
                    } else {
                        // Video has already become too far ahead; stop bypass immediately
                        // and return to normal sync path for faster A/V re-alignment.
                        post_seek_ahead_bypass_until_ms = 0;
                        SYNCI_RATE(30, "evt=post_seek_ahead_bypass_skip delay=%.3f max_ahead=%.3f is_4k=%d",
                                   delay, (likely_4k ? 2.6 : 1.4), likely_4k ? 1 : 0);
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
                bool is_displaying_first_frame = open_first_frame_pending;
                consecutive_drop_count_ = 0;
                if (seek_audio_wait_video_.load(std::memory_order_acquire)) {
                    // Stability-first triple gate:
                    // 1) 视频已到 seek 目标窗口
                    // 2) A/V 差值进入可接受区间（不允许视频过度领先）
                    // 3) 连续 N 帧稳定命中
                    // 同时保留 deadline 兜底，避免异常设备/流导致长时间无声卡住。
                    double seek_target_now = seek_target_sec_.load(std::memory_order_acquire);
                    double seek_target_back_tol = likely_4k ? 0.35 : 0.25;
                    bool seek_target_ready = (seek_target_now < 0.0) ||
                                             std::isnan(pts) || std::isinf(pts) ||
                                             (pts + seek_target_back_tol >= seek_target_now);

                    double seek_resume_delay_threshold = likely_4k ? -0.40 : -0.65;
                    double seek_resume_max_ahead = likely_4k ? 0.45 : 0.30;
                    double seek_abs_err = (seek_target_now >= 0.0 && std::isfinite(pts))
                                          ? std::fabs(pts - seek_target_now)
                                          : std::numeric_limits<double>::infinity();
                    // 非加密流在 seek 命中后，时钟可能短时间落后导致 delay 偏大；
                    // 若已接近目标点，不必等待严格 0.3s ahead 门槛，避免无意义长 loading。
                    if (!secure_session_active_.load(std::memory_order_acquire)
                        && seek_target_now >= 0.0
                        && seek_abs_err <= 2.20) {
                        seek_resume_max_ahead = std::max(seek_resume_max_ahead, 1.90);
                    }
                    if (secure_session_active_.load(std::memory_order_acquire) && delay > seek_resume_max_ahead) {
                        // Secure HLS keyframe-ahead landing can legitimately exceed 0.3~0.45s
                        // until clock is re-anchored; allow bounded ahead window.
                        seek_resume_max_ahead = std::min(60.0, delay + 0.8);
                    }
                    bool seek_resume_sync_ready = std::isnan(delay) || std::isinf(delay) ||
                                                  (delay >= seek_resume_delay_threshold &&
                                                   delay <= seek_resume_max_ahead);
                    int stable_need = likely_4k ? 3 : 2;
                    int stable_hits = seek_resume_stable_hits_.load(std::memory_order_acquire);
                    if (seek_target_ready && seek_resume_sync_ready) {
                        stable_hits = seek_resume_stable_hits_.fetch_add(1, std::memory_order_acq_rel) + 1;
                    } else {
                        seek_resume_stable_hits_.store(0, std::memory_order_release);
                        stable_hits = 0;
                    }

                    bool seek_resume_by_stable_gate = seek_target_ready &&
                                                      seek_resume_sync_ready &&
                                                      stable_hits >= stable_need;
                    bool seek_resume_by_deadline = seek_audio_wait_deadline_ms_ > 0 && now >= seek_audio_wait_deadline_ms_;
                    if (seek_resume_by_stable_gate || seek_resume_by_deadline) {
                        if (seek_audio_wait_video_.exchange(false, std::memory_order_acq_rel)) {
                            seek_resume_stable_hits_.store(0, std::memory_order_release);
                            if (player_core_ && std::isfinite(pts) && pts >= 0.0) {
                                // Seek 恢复时做一次主时钟重锚，减少恢复初期延迟抖动。
                                player_core_anchor_clock(player_core_, pts);
                            }
                            if (playItf_ && isAudioOutputEnabled()) {
                                SLresult r = setOpenSLESPlayState(SL_PLAYSTATE_PLAYING, true);
                                LOGI("[sync] seek first-frame resume audio: result=%d", r);
                            }
                            seek_audio_wait_deadline_ms_ = 0;
                        }
                    } else {
                        SYNCI_RATE(30, "evt=seek_audio_resume_wait delay=%.3f low=%.3f high=%.3f target=%.3f target_ready=%d stable=%d/%d deadline_left_ms=%" PRId64 " is_4k=%d",
                                   delay, seek_resume_delay_threshold, seek_resume_max_ahead,
                                   seek_target_now, seek_target_ready ? 1 : 0,
                                   stable_hits, stable_need,
                                   (int64_t)std::max<int64_t>(0, seek_audio_wait_deadline_ms_ - now),
                                   likely_4k ? 1 : 0);
                    }
                }
                // If we previously paused audio due to prolonged video starvation,
                // resume audio as soon as video starts presenting again.
                if (!seek_audio_wait_video_.load(std::memory_order_acquire) &&
                    audio_rebuffer_pending_.load(std::memory_order_acquire)) {
                    if (now >= audio_rebuffer_min_resume_at_ms_) {
                        bool sw_decode_secure = secure_session_active_.load(std::memory_order_acquire) &&
                                player_core_ && !player_core_is_video_hardware_decoding(player_core_);
                        bool recover_enough = delay >= (very_high_rate_4k ? -0.55 : ((playback_rate >= 2.5) ? -0.75 : ((playback_rate >= 2.0) ? -1.0 : -0.75)));
                        bool resume_by_fallback = now >= audio_rebuffer_deadline_ms_;
                        if (sw_decode_secure && now >= audio_rebuffer_min_resume_at_ms_) {
                            resume_by_fallback = true;
                        }
                        if (!recover_enough && !resume_by_fallback) {
                            SYNCI_RATE(60, "evt=audio_rebuffer_resume_wait delay=%.3f rate=%.2f min_resume_in_ms=%" PRId64 " fallback_in_ms=%" PRId64,
                                       delay, playback_rate,
                                       (int64_t)std::max<int64_t>(0, audio_rebuffer_min_resume_at_ms_ - now),
                                       (int64_t)std::max<int64_t>(0, audio_rebuffer_deadline_ms_ - now));
                        } else if (audio_rebuffer_pending_.exchange(false, std::memory_order_acq_rel)) {
                            audio_rebuffer_paused_at_ms_ = 0;
                            audio_rebuffer_min_resume_at_ms_ = 0;
                            // Add a short cool-down after resume to avoid rapid pause/resume loops.
                            int64_t cooldown_ms = (playback_rate >= 2.8 && !likely_4k) ? 1300 :
                                                  ((playback_rate >= 2.5 && !likely_4k) ? 1000 :
                                                   ((playback_rate >= 2.0f) ? 700 : 450));
                            audio_rebuffer_cooldown_until_ms_ = now + cooldown_ms;
                            if (playItf_ && isAudioOutputEnabled()) {
                                SLresult r = setOpenSLESPlayStateWithRetry(SL_PLAYSTATE_PLAYING, true);
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
                // Skip if app muted or player gain is disabled.
                if (is_displaying_first_frame && audio_start_pending_.exchange(false, std::memory_order_acq_rel)) {
                    if (playItf_ && isAudioOutputEnabled()) {
                        SLresult r = setOpenSLESPlayState(SL_PLAYSTATE_PLAYING, true);
                        LOGI("[sync] audio started on first frame: result=%d", r);
                    } else {
                        LOGI("[sync] audio start skipped: output disabled");
                    }
                }
                frame_count++;
                last_sync_video_pts_ = pts; // track for dynamic frame-interval estimation
                if (std::isfinite(pts) && pts >= 0.0) {
                    double prev_progress_pts = loading_progress_last_pos_.load(std::memory_order_acquire);
                    if (std::isfinite(prev_progress_pts) && prev_progress_pts >= 0.0) {
                        double rendered_step = pts - prev_progress_pts;
                        if (rendered_step >= 0.02 && rendered_step <= 3.20) {
                            loading_progress_last_advance_ms_.store(now_ms(), std::memory_order_release);
                        }
                    } else {
                        // First valid rendered frame: initialize progress anchor.
                        loading_progress_last_advance_ms_.store(now_ms(), std::memory_order_release);
                    }
                    loading_progress_last_pos_.store(pts, std::memory_order_release);
                }
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
                if (is_displaying_first_frame) {
                    first_frame_rendered_.store(true, std::memory_order_release);
                    first_frame_wait_started_ms_ = 0;
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
                    LOGW_RATE(20, "[perf] slow frame %" PRId64 "ms %dx%d surf=%dx%d",
                              render_ms, frame_data.width, frame_data.height,
                              surface_width_, surface_height_);
                if (render_ms > 33) {
                    PERFW_RATE(20, "evt=slow_frame render_ms=%" PRId64 " video_w=%d video_h=%d surface_w=%d surface_h=%d rate=%.2f delay=%.3f",
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
                    (delay < -0.20 || playback_rate >= 1.75)) {
                    wait_ms = 0;
                } else {
                    wait_ms = 8;
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
                int64_t now_fallback = now_ms();
                bool open_first_hard_timeout = first_frame_wait_started_ms_ > 0 &&
                                               (now_fallback - first_frame_wait_started_ms_) >= 5000;
                // Avoid audio running ahead in open-first-frame phase; only allow
                // fallback after a much longer timeout.
                if (open_first_hard_timeout && audio_start_pending_.exchange(false, std::memory_order_acq_rel)) {
                    if (playItf_ && isAudioOutputEnabled()) {
                        SLresult r = setOpenSLESPlayState(SL_PLAYSTATE_PLAYING, true);
                        LOGI("[sync] audio deadline fallback (open hard-timeout): result=%d wait_ms=%" PRId64,
                             r, (int64_t)(now_fallback - first_frame_wait_started_ms_));
                    }
                }
            }
            // High-rate/4K starvation path:
            // if video queue stays empty for a while, pause OpenSL audio so user
            // doesn't hear a long "audio-only" segment and then permanent A/V drift.
            int64_t now = now_ms();
            trySeekForceResume(now);
            // Seek watchdog in empty-frame path:
            // seek recovery timeout fallback previously only ran in the "frame available"
            // branch. If decoder keeps returning no frames, the player can stay in
            // LOADING forever. Add an empty-queue timeout to break the deadlock.
            bool seek_lower_active = seek_lower_bound_active_.load(std::memory_order_acquire);
            bool seek_recovery_active = seek_recovery_active_.load(std::memory_order_acquire);
            bool seek_wait_video = seek_audio_wait_video_.load(std::memory_order_acquire);
            if ((seek_lower_active || seek_recovery_active || seek_wait_video) && seek_started_at_ms_ > 0) {
                int64_t seek_elapsed_ms = now - seek_started_at_ms_;
                double seek_target_now = seek_target_sec_.load(std::memory_order_acquire);
                double seek_from_now = seek_from_sec_.load(std::memory_order_acquire);
                int policy_video_w = seek_policy_video_w_.load(std::memory_order_acquire);
                int policy_video_h = seek_policy_video_h_.load(std::memory_order_acquire);
                bool likely_4k_empty = policy_video_w >= 3840 || policy_video_h >= 2160;
                bool is_backward_seek = seek_from_now >= 0.0 && seek_target_now >= 0.0 &&
                                        (seek_from_now - seek_target_now) > 0.5;
                bool secure_session_now = secure_session_active_.load(std::memory_order_acquire);
                bool local_source_now = source_local_active_.load(std::memory_order_acquire);
                bool encrypted_source_now = source_encrypted_active_.load(std::memory_order_acquire) || secure_session_now;
                double span_now = (std::isfinite(seek_from_now) && std::isfinite(seek_target_now))
                                  ? std::fabs(seek_from_now - seek_target_now)
                                  : 0.0;
                bool nonsecure_4k_large_empty = likely_4k_empty && !encrypted_source_now && span_now >= 25.0;
                int64_t seek_empty_timeout_ms = is_backward_seek ? 7200 : 6200;
                if (local_source_now) {
                    seek_empty_timeout_ms = is_backward_seek ? 4200 : 3600;
                    if (span_now <= 0.8) {
                        seek_empty_timeout_ms = std::min<int64_t>(seek_empty_timeout_ms, 2600);
                    }
                }
                if (nonsecure_4k_large_empty) {
                    // Give 4K decoder/render pipeline enough time to produce a target-near frame.
                    // Otherwise audio resumes early and video keeps chasing a moving clock.
                    seek_empty_timeout_ms = is_backward_seek ? 5600 : 4800;
                }
                uint64_t sid_now = seek_session_active_id_.load(std::memory_order_acquire);
                bool recent_seek_progress =
                        sid_now != 0 &&
                        sid_now == seek_progress_sid &&
                        std::isfinite(seek_progress_best_abs_err) &&
                        seek_progress_best_abs_err < 24.0 &&
                        seek_progress_last_update_ms > 0 &&
                        seek_progress_last_improve_ms > 0 &&
                        (now - seek_progress_last_update_ms) <= 1000 &&
                        (now - seek_progress_last_improve_ms) <= 2600;
                if (secure_session_now &&
                    !is_backward_seek &&
                    recent_seek_progress &&
                    seek_elapsed_ms >= 7000 &&
                    seek_progress_best_abs_err > 8.0) {
                    // Forward seek should not keep extending loading window if still far away.
                    // Let timeout fallback trigger active recovery sooner.
                    recent_seek_progress = false;
                }
                if (!nonsecure_4k_large_empty && recent_seek_progress && seek_elapsed_ms < 2600) {
                    int64_t progress_timeout_ms = is_backward_seek ? 2800 : 2400;
                    seek_empty_timeout_ms = std::min(seek_empty_timeout_ms, progress_timeout_ms);
                    SYNCW_RATE(15,
                               "evt=seek_empty_timeout_hold_by_progress id=%" PRIu64 " elapsed_ms=%" PRId64 " timeout_ms=%" PRId64 " best_abs_err=%.3f backward=%d local=%d encrypted=%d",
                               sid_now, seek_elapsed_ms, seek_empty_timeout_ms, seek_progress_best_abs_err,
                               is_backward_seek ? 1 : 0, local_source_now ? 1 : 0, encrypted_source_now ? 1 : 0);
                }
                if (secure_session_now && !is_backward_seek) {
                    bool very_large_forward_span = span_now >= 120.0;
                    bool far_from_target = std::isfinite(seek_progress_best_abs_err) && seek_progress_best_abs_err >= 10.0;
                    bool progress_stalled =
                            seek_progress_last_improve_ms > 0 &&
                            (now - seek_progress_last_improve_ms) >= 1000;
                    // For encrypted large forward seek, prolonged "slightly improving but far" should
                    // switch to failover earlier instead of holding loading for many seconds.
                    if (very_large_forward_span && far_from_target && seek_elapsed_ms >= 2200) {
                        int64_t secure_large_timeout_ms = progress_stalled ? 2600 : 3200;
                        seek_empty_timeout_ms = std::min(seek_empty_timeout_ms, secure_large_timeout_ms);
                        SYNCW_RATE(15,
                                   "evt=seek_empty_timeout_secure_large_forward_guard id=%" PRIu64 " elapsed_ms=%" PRId64 " timeout_ms=%" PRId64 " span=%.3f best_abs_err=%.3f stalled=%d",
                                   sid_now, seek_elapsed_ms, seek_empty_timeout_ms, span_now, seek_progress_best_abs_err,
                                   progress_stalled ? 1 : 0);
                    }
                    if (very_large_forward_span &&
                        !far_from_target &&
                        recent_seek_progress &&
                        !progress_stalled &&
                        seek_elapsed_ms < 4200) {
                        seek_empty_timeout_ms = std::max<int64_t>(seek_empty_timeout_ms, 4200);
                        SYNCW_RATE(15,
                                   "evt=seek_empty_timeout_extend_secure_forward_close id=%" PRIu64 " elapsed_ms=%" PRId64 " timeout_ms=%" PRId64 " span=%.3f best_abs_err=%.3f",
                                   sid_now, seek_elapsed_ms, seek_empty_timeout_ms, span_now, seek_progress_best_abs_err);
                    }
                }
                int64_t rendered_progress_age_ms_now = -1;
                int64_t rendered_progress_last_ms_now = loading_progress_last_advance_ms_.load(std::memory_order_acquire);
                if (rendered_progress_last_ms_now > 0) {
                    rendered_progress_age_ms_now = now - rendered_progress_last_ms_now;
                }
                bool has_rendered_progress_now =
                        rendered_progress_age_ms_now >= 0 &&
                        rendered_progress_age_ms_now <= 1200;
                if (nonsecure_4k_large_empty && !is_backward_seek && !has_rendered_progress_now) {
                    int64_t no_render_timeout_ms = span_now >= 180.0 ? 6800 : 5600;
                    if (seek_elapsed_ms < no_render_timeout_ms) {
                        seek_empty_timeout_ms = std::max<int64_t>(seek_empty_timeout_ms, no_render_timeout_ms);
                        SYNCW_RATE(15,
                                   "evt=seek_empty_timeout_extend_nonsecure_4k_forward_no_render elapsed_ms=%" PRId64 " timeout_ms=%" PRId64 " span=%.3f rendered_age_ms=%" PRId64 " target=%.3f from=%.3f",
                                   seek_elapsed_ms, seek_empty_timeout_ms, span_now,
                                   rendered_progress_age_ms_now, seek_target_now, seek_from_now);
                    }
                }
                if (seek_elapsed_ms >= seek_empty_timeout_ms) {
                    setSeekPhase(SEEK_PHASE_FAILOVER, "seek_empty_timeout");
                    seek_lower_bound_active_.store(false, std::memory_order_release);
                    seek_lower_bound_deadline_ms_ = 0;
                    seek_recovery_active_.store(false, std::memory_order_release);
                    seek_recovery_deadline_ms_ = 0;
                    seek_fast_catchup_frames_.store(0, std::memory_order_release);
                    seek_catchup_deadline_ms_ = 0;
                    seek_audio_wait_video_.store(false, std::memory_order_release);
                    seek_audio_wait_deadline_ms_ = 0;
                    seek_started_at_ms_ = 0;
                    seek_lower_bound_drop_count_ = 0;
                    seek_verify_hits_.store(0, std::memory_order_release);
                    seek_resume_stable_hits_.store(0, std::memory_order_release);
                    sync_warmup_frames_.store(28, std::memory_order_release);
                    int64_t timeout_bypass_ms = likely_4k_empty ? 3200 : 1800;
                    post_seek_ahead_bypass_until_ms = now + timeout_bypass_ms;
                    // Reset stall-watchdog baseline so timeout recovery starts from a clean edge.
                    stall_watchdog_since_ms = 0;
                    stall_watchdog_last_break_ms = now;
                    bool should_resume_on_complete = seek_resume_on_complete_.load(std::memory_order_acquire);
                    bool manual_pause_blocked = false;
                    if (user_manual_pause_.load(std::memory_order_acquire)) {
                        int64_t block_until = user_manual_pause_block_until_ms_.load(std::memory_order_acquire);
                        manual_pause_blocked = block_until <= 0 || now < block_until;
                    }
                    if (manual_pause_blocked || seek_started_while_paused_.load(std::memory_order_acquire)) {
                        should_resume_on_complete = false;
                        SYNCW("evt=seek_empty_timeout_skip_autoplay target=%.3f from=%.3f reason=paused_origin_or_manual_pause",
                              seek_target_now, seek_from_now);
                    }
                    if (!should_resume_on_complete) {
                        // 暂停态发起的 seek：timeout 后直接收敛，不进入 force_play / soft_rebuild 链路。
                        if (player_core_) {
                            player_core_set_play_when_ready(player_core_, 0);
                            player_core_pause(player_core_);
                        }
                        seek_force_resume_pending_.store(false, std::memory_order_release);
                        seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
                        seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
                        seek_force_resume_retry_count_.store(0, std::memory_order_release);
                        seek_force_resume_nudged_.store(false, std::memory_order_release);
                        setSeekPhase(SEEK_PHASE_IDLE, "seek_empty_timeout_paused_origin_settle");
                        seek_session_active_id_.store(0, std::memory_order_release);
                        seek_started_while_paused_.store(false, std::memory_order_release);
                        SYNCW("evt=seek_empty_timeout_settle_paused_origin target=%.3f from=%.3f elapsed_ms=%" PRId64,
                              seek_target_now, seek_from_now, seek_elapsed_ms);
                    }
                    if (player_core_ && should_resume_on_complete) {
                        player_core_set_play_when_ready(player_core_, 1);
                    }
                    bool core_playing_now = player_core_ && player_core_is_playing(player_core_);
                    if (!should_resume_on_complete) {
                        core_playing_now = false;
                    }
                    int pwr_now = player_core_ ? player_core_get_play_when_ready(player_core_) : 0;
                    int state_now = player_core_ ? player_core_get_state(player_core_) : 0;
                    double pos_now = player_core_ ? player_core_get_position(player_core_) : -1.0;
                    bool soft_resume_healthy_now = false;
                    bool seek_gates_cleared_now =
                            !seek_lower_bound_active_.load(std::memory_order_acquire) &&
                            !seek_recovery_active_.load(std::memory_order_acquire) &&
                            !seek_audio_wait_video_.load(std::memory_order_acquire);
                    SYNCW_RATE(15,
                               "evt=seek_empty_timeout_diag target=%.3f from=%.3f pwr=%d state=%d pos=%.3f seek_progress_best_abs_err=%.3f recent_seek_progress=%d rendered_progress=%d rendered_progress_age_ms=%" PRId64 " gates_cleared=%d local=%d encrypted=%d timeout_ms=%" PRId64,
                               seek_target_now, seek_from_now, pwr_now, state_now, pos_now,
                               seek_progress_best_abs_err,
                               recent_seek_progress ? 1 : 0,
                               has_rendered_progress_now ? 1 : 0,
                               rendered_progress_age_ms_now,
                               seek_gates_cleared_now ? 1 : 0,
                               local_source_now ? 1 : 0,
                               encrypted_source_now ? 1 : 0,
                               seek_empty_timeout_ms);
                    if (pwr_now != 0 && state_now == PLAYER_STATE_PLAYING && has_rendered_progress_now) {
                        core_playing_now = true;
                        SYNCI("evt=seek_empty_timeout_soft_resume_ok target=%.3f from=%.3f pwr=%d state=%d pos=%.3f by=state_playing_with_rendered_progress",
                              seek_target_now, seek_from_now, pwr_now, state_now, pos_now);
                    } else if (pwr_now != 0 && state_now == PLAYER_STATE_PLAYING) {
                        // Avoid false "soft resume ok": some devices report PLAYING while position is frozen.
                        core_playing_now = false;
                        SYNCW_RATE(15,
                                   "evt=seek_empty_timeout_soft_resume_reject reason=state_playing_without_rendered_progress target=%.3f from=%.3f pos=%.3f",
                                   seek_target_now, seek_from_now, pos_now);
                    }
                    if (pwr_now != 0 &&
                        state_now == PLAYER_STATE_PAUSED &&
                        seek_gates_cleared_now &&
                        has_rendered_progress_now) {
                        soft_resume_healthy_now = true;
                    }
                    if (soft_resume_healthy_now) {
                        core_playing_now = true;
                        SYNCI("evt=seek_empty_timeout_soft_resume_ok target=%.3f from=%.3f pwr=%d state=%d pos=%.3f",
                              seek_target_now, seek_from_now, pwr_now, state_now, pos_now);
                    }
                    if (!core_playing_now && player_core_ && should_resume_on_complete) {
                        if (is_backward_seek) {
                            player_core_play(player_core_);
                            core_playing_now = player_core_is_playing(player_core_);
                            SEEKW("seek_empty_timeout_fallback force_core_play playing=%d",
                                  core_playing_now ? 1 : 0);
                            int state_after_core_play = player_core_get_state(player_core_);
                            int pwr_after_core_play = player_core_get_play_when_ready(player_core_);
                            if (!core_playing_now &&
                                pwr_after_core_play != 0 &&
                                state_after_core_play == PLAYER_STATE_PAUSED) {
                                SYNCW("evt=seek_empty_timeout_try_full_play_path state=%d pwr=%d",
                                      state_after_core_play, pwr_after_core_play);
                                play();
                                core_playing_now = player_core_is_playing(player_core_);
                                SEEKW("seek_empty_timeout_fallback force_full_play_path playing=%d",
                                      core_playing_now ? 1 : 0);
                            }
                            if (!core_playing_now && trySeekSoftRebuild(now, "empty_timeout")) {
                                core_playing_now = true;
                            }
                            if (!core_playing_now) {
                                armSeekForceResumePending(now,
                                                          seek_target_now,
                                                          seek_from_now,
                                                          local_source_now ? 800 : 1800,
                                                          "evt=seek_empty_timeout_force_resume_pending");
                            }
                        } else {
                            // Forward seek timeout: avoid infinite loop, but do not give up too early.
                            // Try core play first, then full play path once, and finally arm short bounded retries.
                            player_core_play(player_core_);
                            core_playing_now = player_core_is_playing(player_core_);
                            SEEKW("seek_empty_timeout_forward_single_play playing=%d",
                                  core_playing_now ? 1 : 0);
                            int state_after_forward_play = player_core_get_state(player_core_);
                            int pwr_after_forward_play = player_core_get_play_when_ready(player_core_);
                            if (!core_playing_now &&
                                pwr_after_forward_play != 0 &&
                                state_after_forward_play == PLAYER_STATE_PAUSED) {
                                SYNCW("evt=seek_empty_timeout_forward_try_full_play_path state=%d pwr=%d",
                                      state_after_forward_play, pwr_after_forward_play);
                                play();
                                core_playing_now = player_core_is_playing(player_core_);
                                SEEKW("seek_empty_timeout_forward_full_play_path playing=%d",
                                      core_playing_now ? 1 : 0);
                            }
                            if (!core_playing_now) {
                                armSeekForceResumePending(now,
                                                          seek_target_now,
                                                          seek_from_now,
                                                          local_source_now ? 700 : 1400,
                                                          "evt=seek_empty_timeout_forward_force_resume_pending");
                            } else {
                                seek_force_resume_pending_.store(false, std::memory_order_release);
                                seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
                                seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
                                seek_force_resume_retry_count_.store(0, std::memory_order_release);
                                seek_force_resume_nudged_.store(false, std::memory_order_release);
                                setSeekPhase(SEEK_PHASE_IDLE, "seek_empty_timeout_forward_stop_retry");
                                seek_session_active_id_.store(0, std::memory_order_release);
                                SYNCW("evt=seek_empty_timeout_forward_stop_retry target=%.3f from=%.3f",
                                      seek_target_now, seek_from_now);
                            }
                        }
                    }
                    if (core_playing_now && playItf_ && isAudioOutputEnabled()) {
                        SLresult r = setOpenSLESPlayState(SL_PLAYSTATE_PLAYING, true);
                        LOGW("[sync] seek empty fallback resume audio: result=%d", r);
                    }
                    if (core_playing_now) {
                        uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
                        seek_force_resume_pending_.store(false, std::memory_order_release);
                        seek_force_resume_deadline_ms_.store(0, std::memory_order_release);
                        seek_force_resume_next_try_ms_.store(0, std::memory_order_release);
                        seek_force_resume_retry_count_.store(0, std::memory_order_release);
                        seek_force_resume_nudged_.store(false, std::memory_order_release);
                        setSeekPhase(SEEK_PHASE_IDLE, "seek_timeout_settle_success");
                        seek_session_active_id_.store(0, std::memory_order_release);
                        SYNCI("evt=seek_timeout_settle_success id=%" PRIu64 " target=%.3f from=%.3f",
                              sid, seek_target_now, seek_from_now);
                    }
                    if (should_resume_on_complete || core_playing_now) {
                        SEEKW("seek_empty_timeout_fallback elapsed_ms=%" PRId64 " backward=%d target=%.3f from=%.3f",
                              seek_elapsed_ms, is_backward_seek ? 1 : 0, seek_target_now, seek_from_now);
                        uint64_t sid = seek_session_active_id_.load(std::memory_order_acquire);
                        SYNCW("evt=seek_empty_timeout_fallback id=%" PRIu64 " phase=%s elapsed_ms=%" PRId64 " backward=%d target=%.3f from=%.3f",
                              sid, seek_phase_name(seek_phase_.load(std::memory_order_acquire)),
                              seek_elapsed_ms, is_backward_seek ? 1 : 0, seek_target_now, seek_from_now);
                    }
                }
            }
            {
                bool likely_4k_empty_for_seek = gl_last_video_w_ >= 3840 || gl_last_video_h_ >= 2160;
                trySeekAudioWaitDeadlineFallback(now,
                                                 std::numeric_limits<double>::quiet_NaN(),
                                                 likely_4k_empty_for_seek,
                                                 post_seek_ahead_bypass_until_ms);
            }
            double playback_rate = player_core_get_playback_rate(player_core_);
            if (playback_rate <= 0.0) playback_rate = 1.0;
            bool core_playing = player_core_is_playing(player_core_);
            bool high_rate = playback_rate >= 2.0;
            bool likely_4k = gl_last_video_w_ >= 3840 || gl_last_video_h_ >= 2160;
            bool hw_decode_active = player_core_ && player_core_is_video_hardware_decoding(player_core_) != 0;
            bool sw_decode_secure = secure_session_active_.load(std::memory_order_acquire) &&
                                    player_core_ && !hw_decode_active;
            bool in_loading = is_loading_.load(std::memory_order_acquire);
            bool in_sync_warmup = sync_warmup_frames_.load(std::memory_order_acquire) > 0;
            bool play_when_ready_for_empty = player_core_get_play_when_ready(player_core_) != 0;
            if (!play_when_ready_for_empty) {
                // Do not let a user-paused empty queue age into an immediate soft recover on play().
                empty_start_ms = now;
            }
            // Non-seek soft recovery:
            // Prevent long "frozen picture + audio underrun" when we are not in
            // seek-recovery but decoder queue keeps empty unexpectedly.
            if (!seek_lower_active &&
                !seek_recovery_active &&
                !seek_wait_video &&
                !in_loading &&
                !in_sync_warmup &&
                core_playing &&
                !high_rate &&
                now >= empty_stall_recover_cooldown_until_ms &&
                in_empty_streak) {
                int64_t empty_ms = now - empty_start_ms;
                if (empty_ms >= kEmptyStallRecoverTriggerMs) {
                    double recover_anchor = player_core_get_position(player_core_);
                    if (player_core_) {
                        player_core_set_play_when_ready(player_core_, 1);
                        if (!player_core_is_playing(player_core_)) {
                            player_core_play(player_core_);
                        }
                        if (std::isfinite(recover_anchor) && recover_anchor >= 0.0) {
                            player_core_anchor_clock(player_core_, recover_anchor);
                        }
                    }
                    audio_rebuffer_pending_.store(false, std::memory_order_release);
                    audio_rebuffer_deadline_ms_ = 0;
                    audio_rebuffer_paused_at_ms_ = 0;
                    audio_rebuffer_min_resume_at_ms_ = 0;
                    if (playItf_ && isAudioOutputEnabled()) {
                        SLresult r = setOpenSLESPlayState(SL_PLAYSTATE_PLAYING, true);
                        LOGW("[sync] empty-stall soft recover resume audio: result=%d empty_ms=%" PRId64,
                             r, empty_ms);
                    }
                    // Grant a short anti-hold window after recovery to avoid
                    // immediate re-entry into ahead-hold when frames come back.
                    post_seek_ahead_bypass_until_ms = now + 1200;
                    stall_watchdog_since_ms = 0;
                    stall_watchdog_last_break_ms = now;
                    empty_stall_recover_cooldown_until_ms = now + kEmptyStallRecoverCooldownMs;
                    SYNCW("evt=empty_stall_soft_recover empty_ms=%" PRId64 " anchor=%.3f cooldown_ms=%" PRId64,
                          empty_ms, recover_anchor, kEmptyStallRecoverCooldownMs);
                }
            }
            int64_t rebuffer_trigger_ms = high_rate ? 220 : 260;
            if (sw_decode_secure && !high_rate) {
                rebuffer_trigger_ms = 3000;
            }
            if (!likely_4k && playback_rate >= 2.5) {
                rebuffer_trigger_ms = 260;
            }
            if (!likely_4k && playback_rate >= 2.8) {
                rebuffer_trigger_ms = 320;
            }
            if (likely_4k) rebuffer_trigger_ms = std::max<int64_t>(160, rebuffer_trigger_ms - 20);
            int64_t rebuffer_fallback_ms = high_rate ? 850 : (sw_decode_secure ? 2000 : 700);
            if (likely_4k) rebuffer_fallback_ms += 150;
            int64_t rebuffer_min_hold_ms = high_rate ? 650 : (sw_decode_secure ? 300 : 450);
            if (likely_4k) rebuffer_min_hold_ms += 150;
            bool rebuffer_cooldown_active = now < audio_rebuffer_cooldown_until_ms_;
            bool should_pause_audio_on_starvation = high_rate || sw_decode_secure;
            if (!seek_audio_wait_video_.load(std::memory_order_acquire) &&
                should_pause_audio_on_starvation && in_empty_streak &&
                !in_loading &&
                !in_sync_warmup &&
                core_playing &&
                !rebuffer_cooldown_active &&
                !audio_rebuffer_pending_.load(std::memory_order_acquire)) {
                int64_t empty_ms = now - empty_start_ms;
                if (empty_ms >= rebuffer_trigger_ms && playItf_ && isAudioOutputEnabled()) {
                    SLresult r = setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, true);
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
                        const char* core_diag = player_core_get_runtime_diagnostic(player_core_);
                        SYNCW("evt=starvation_core_diag source=video_starvation_pause_audio empty_ms=%" PRId64 " diag=%s",
                              empty_ms, core_diag ? core_diag : "");
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
                    int64_t cooldown_ms = (playback_rate >= 2.8 && !likely_4k) ? 1300 :
                                          ((playback_rate >= 2.5 && !likely_4k) ? 1000 :
                                           (high_rate ? 700 : 450));
                    audio_rebuffer_cooldown_until_ms_ = now + cooldown_ms;
                    if (core_playing && playItf_ && isAudioOutputEnabled()) {
                        SLresult r = setOpenSLESPlayStateWithRetry(SL_PLAYSTATE_PLAYING, true);
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
            int64_t now_empty = now_ms();
            int64_t empty_ms = now_empty - empty_start_ms;
            if (empty_ms >= 2000) {
                double pos_now = player_core_get_position(player_core_);
                double dur_now = player_core_get_duration(player_core_);
                double remain_now = (std::isfinite(dur_now) && std::isfinite(pos_now)) ? (dur_now - pos_now) : std::numeric_limits<double>::quiet_NaN();
                double open_start_pos_now = open_start_position_sec_.load(std::memory_order_acquire);
                int64_t open_requested_at_ms_now = open_requested_at_ms_.load(std::memory_order_acquire);
                bool play_when_ready_now = player_core_get_play_when_ready(player_core_) != 0;
                bool seek_flow_active =
                        seek_lower_bound_active_.load(std::memory_order_acquire) ||
                        seek_recovery_active_.load(std::memory_order_acquire) ||
                        seek_audio_wait_video_.load(std::memory_order_acquire);
                // Secure/HLS tail can legitimately land a little beyond duration due to keyframe-ahead settle.
                // Treat small negative remain as near-end, otherwise force-complete watchdog never triggers.
                bool near_end = std::isfinite(remain_now) && remain_now >= -3.0 && remain_now <= 1.2;
                double tail_intent_window_sec = std::isfinite(dur_now)
                                                ? std::max(1.6, std::min(3.2, dur_now * 0.012))
                                                : 1.6;
                bool open_tail_intent = std::isfinite(dur_now)
                                        && std::isfinite(open_start_pos_now)
                                        && open_start_pos_now >= std::max(0.0, dur_now - tail_intent_window_sec);
                int64_t open_age_ms = open_requested_at_ms_now > 0 ? (now_empty - open_requested_at_ms_now) : -1;
                bool recent_open_tail_intent = open_tail_intent && open_age_ms >= 0 && open_age_ms <= 20000;
                bool opening_now = open_in_progress_.load(std::memory_order_acquire);
                bool first_frame_ready = first_frame_rendered_.load(std::memory_order_acquire);
                bool open_ready_for_tail_complete =
                        is_open_ready_for_tail_complete(opening_now, first_frame_ready);
                bool audio_pending_now = audio_start_pending_.load(std::memory_order_acquire);
                bool audio_rebuffer_now = audio_rebuffer_pending_.load(std::memory_order_acquire);
                int64_t core_io_stale_ms = -1;
                bool core_stale_io =
                        player_core_is_io_stale_for_playback(
                                player_core_, kCoreIoStaleRecoverMs, &core_io_stale_ms) != 0;
                int64_t tail_force_complete_ms = recent_open_tail_intent
                                                 ? kTailStallForceCompleteFastMs
                                                 : kTailStallForceCompleteMs;
                if ((now_empty - tail_stall_diag_last_log_ms) >= kTailStallDiagIntervalMs) {
                    tail_stall_diag_last_log_ms = now_empty;
                    SYNCI("evt=tail_stall_diag empty_ms=%" PRId64 " pos=%.3f dur=%.3f remain=%.3f near_end=%d pwr=%d seek_flow=%d loading=%d state=%d tail_intent=%d force_ms=%" PRId64 " open_start=%.3f open_age_ms=%" PRId64 " open_ready=%d first_frame=%d audio_pending=%d audio_rebuffer=%d stale_io=%d stale_ms=%" PRId64,
                          empty_ms,
                          pos_now,
                          dur_now,
                          remain_now,
                          near_end ? 1 : 0,
                          play_when_ready_now ? 1 : 0,
                          seek_flow_active ? 1 : 0,
                          is_loading_.load(std::memory_order_acquire) ? 1 : 0,
                          player_core_get_state(player_core_),
                          recent_open_tail_intent ? 1 : 0,
                          tail_force_complete_ms,
                          open_start_pos_now,
                          open_age_ms,
                          open_ready_for_tail_complete ? 1 : 0,
                          first_frame_ready ? 1 : 0,
                          audio_pending_now ? 1 : 0,
                          audio_rebuffer_now ? 1 : 0,
                          core_stale_io ? 1 : 0,
                          core_io_stale_ms);
                    const char* core_diag = player_core_get_runtime_diagnostic(player_core_);
                    SYNCI("evt=tail_stall_core_diag empty_ms=%" PRId64 " diag=%s",
                          empty_ms, core_diag ? core_diag : "");
                }
                bool video_stall_already_paused =
                        video_empty_stall_forced_pause_.load(std::memory_order_acquire);
                int64_t empty_stall_pause_ms =
                        core_stale_io ? kCoreIoStaleEmptyRecoverMs : kVideoEmptyStallPauseMs;
                bool non_tail_playing_empty_stall =
                        !near_end &&
                        !seek_flow_active &&
                        first_frame_ready &&
                        open_ready_for_tail_complete &&
                        empty_ms >= empty_stall_pause_ms &&
                        !playback_completed_latched_.load(std::memory_order_acquire) &&
                        (video_stall_already_paused ||
                         (play_when_ready_now &&
                          (!is_loading_.load(std::memory_order_acquire) || core_stale_io) &&
                          player_core_get_state(player_core_) == PLAYER_STATE_PLAYING));
                if (non_tail_playing_empty_stall) {
                    int64_t expected_zero = 0;
                    video_empty_stall_started_ms_.compare_exchange_strong(
                            expected_zero, now_empty, std::memory_order_acq_rel);
                    int64_t stall_started = video_empty_stall_started_ms_.load(std::memory_order_acquire);
                    int64_t stall_ms = stall_started > 0 ? (now_empty - stall_started) : 0;
                    if (!video_empty_stall_forced_pause_.exchange(true, std::memory_order_acq_rel)) {
                        is_loading_.store(true, std::memory_order_release);
                        suppress_stale_loading_true_until_ms_.store(0, std::memory_order_release);
                        player_core_pause(player_core_);
                        if (playItf_) {
                            setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, true);
                        }
                        audio_rebuffer_pending_.store(false, std::memory_order_release);
                        audio_rebuffer_deadline_ms_ = 0;
                        audio_rebuffer_paused_at_ms_ = 0;
                        audio_rebuffer_min_resume_at_ms_ = 0;
                        SYNCW("evt=video_empty_stall_forced_pause empty_ms=%" PRId64 " pos=%.3f dur=%.3f remain=%.3f state=%d source_local=%d encrypted=%d stale_io=%d stale_ms=%" PRId64,
                              empty_ms,
                              pos_now,
                              dur_now,
                              remain_now,
                              player_core_get_state(player_core_),
                              source_local_active_.load(std::memory_order_acquire) ? 1 : 0,
                              source_encrypted_active_.load(std::memory_order_acquire) ? 1 : 0,
                              core_stale_io ? 1 : 0,
                              core_io_stale_ms);
                        if (!pending_video_stall_reopen_.load(std::memory_order_acquire)) {
                            double max_recover_pos = (std::isfinite(dur_now) && dur_now > 0.35)
                                                    ? std::max(0.0, dur_now - 0.35)
                                                    : std::numeric_limits<double>::max();
                            double recover_pos = std::isfinite(pos_now) && pos_now > 0.0 ? pos_now : 0.0;
                            recover_pos = std::max(0.0, std::min(max_recover_pos, recover_pos));
                            pending_video_stall_reopen_pos_.store(recover_pos, std::memory_order_release);
                            pending_video_stall_reopen_.store(true, std::memory_order_release);
                            SYNCW("evt=video_empty_stall_request_forward_seek reason=%s empty_ms=%" PRId64 " recover_pos=%.3f dur=%.3f remain=%.3f stale_ms=%" PRId64,
                                  core_stale_io ? "stale_io_forced_pause" : "forced_pause",
                                  empty_ms, recover_pos, dur_now, remain_now, core_io_stale_ms);
                        }
                    }
                    int64_t recover_delay_ms = core_stale_io
                                               ? 0
                                               : (kVideoEmptyStallReopenMs - kVideoEmptyStallPauseMs);
                    if (stall_ms >= recover_delay_ms &&
                        !pending_video_stall_reopen_.load(std::memory_order_acquire)) {
                        double max_reopen_pos = (std::isfinite(dur_now) && dur_now > 0.35)
                                                ? std::max(0.0, dur_now - 0.35)
                                                : std::numeric_limits<double>::max();
                        double reopen_pos = std::isfinite(pos_now) && pos_now > 0.0 ? pos_now : 0.0;
                        reopen_pos = std::max(0.0, std::min(max_reopen_pos, reopen_pos));
                        pending_video_stall_reopen_pos_.store(reopen_pos, std::memory_order_release);
                        pending_video_stall_reopen_.store(true, std::memory_order_release);
                        SYNCW("evt=video_empty_stall_request_forward_seek reason=%s empty_ms=%" PRId64 " stall_ms=%" PRId64 " recover_pos=%.3f dur=%.3f remain=%.3f stale_ms=%" PRId64,
                              core_stale_io ? "stale_io" : "empty_stall",
                              empty_ms, stall_ms, reopen_pos, dur_now, remain_now, core_io_stale_ms);
                    }
                } else if (!video_empty_stall_forced_pause_.load(std::memory_order_acquire) &&
                           video_empty_stall_started_ms_.load(std::memory_order_acquire) != 0) {
                    resetVideoEmptyStallRecovery("empty_stall_condition_cleared");
                }
                if (near_end &&
                    play_when_ready_now &&
                    !seek_flow_active &&
                    open_ready_for_tail_complete &&
                    empty_ms >= tail_force_complete_ms &&
                    !playback_completed_latched_.load(std::memory_order_acquire)) {
                    SYNCI("evt=tail_stall_force_complete empty_ms=%" PRId64 " pos=%.3f dur=%.3f remain=%.3f tail_intent=%d force_ms=%" PRId64 " open_start=%.3f open_age_ms=%" PRId64,
                          empty_ms, pos_now, dur_now, remain_now,
                          recent_open_tail_intent ? 1 : 0,
                          tail_force_complete_ms,
                          open_start_pos_now,
                          open_age_ms);
                    playback_completed_latched_.store(true, std::memory_order_release);
                    is_loading_.store(false, std::memory_order_release);
                    suppress_stale_loading_true_until_ms_.store(now_empty + 15000, std::memory_order_release);
                    has_pending_playback_completed_.store(true, std::memory_order_release);
                }
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
    // Reduce duplicate hot-path logs: keep critical upload-path info on change,
    // and keep a sparse heartbeat for long sessions.
    static int s_last_use_pbo = -1;
    static int s_last_video_w = -1;
    static int s_last_video_h = -1;
    static int s_last_y_stride = -1;
    static int s_last_uv_stride = -1;
    static int s_last_v_stride = -1;
    static int s_last_uv_interleaved = -1;
    int use_pbo_i = use_pbo ? 1 : 0;
    int uv_interleaved_i = uv_interleaved ? 1 : 0;
    bool upload_path_changed = (s_last_use_pbo != use_pbo_i) ||
                               (s_last_video_w != width) ||
                               (s_last_video_h != height) ||
                               (s_last_y_stride != y_tex_w) ||
                               (s_last_uv_stride != uv_upload_stride) ||
                               (s_last_v_stride != v_tex_w) ||
                               (s_last_uv_interleaved != uv_interleaved_i);
    if (upload_path_changed) {
        PBOI("evt=upload_path_change use_pbo=%d video_w=%d video_h=%d y_stride=%d uv_stride=%d v_stride=%d uv_interleaved=%d",
             use_pbo_i, width, height, y_tex_w, uv_upload_stride, v_tex_w, uv_interleaved_i);
        s_last_use_pbo = use_pbo_i;
        s_last_video_w = width;
        s_last_video_h = height;
        s_last_y_stride = y_tex_w;
        s_last_uv_stride = uv_upload_stride;
        s_last_v_stride = v_tex_w;
        s_last_uv_interleaved = uv_interleaved_i;
    } else {
        PBOI_RATE(300, "evt=upload_path_heartbeat use_pbo=%d video_w=%d video_h=%d y_stride=%d uv_stride=%d v_stride=%d uv_interleaved=%d",
                  use_pbo_i, width, height, y_tex_w, uv_upload_stride, v_tex_w, uv_interleaved_i);
    }

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
        // 4K 下慢上传可能连续出现，按固定采样率输出，避免告警刷屏影响性能。
        LOGW_RATE(30, "[DIAG] Slow upload: %" PRId64 "ms | %dx%d Y_stride=%d pbo=%d",
                  upload_ms, width, height, y_tex_w, use_pbo ? 1 : 0);
        PBOW_RATE(30, "evt=slow_upload upload_ms=%" PRId64 " video_w=%d video_h=%d y_stride=%d use_pbo=%d",
                  upload_ms, width, height, y_tex_w, use_pbo ? 1 : 0);
    }

    gl_last_video_w_ = width;
    gl_last_video_h_ = height;
    seek_policy_video_w_.store(width, std::memory_order_release);
    seek_policy_video_h_.store(height, std::memory_order_release);
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
    std::lock_guard<std::mutex> opensl_lock(opensl_mutex_);
    SLresult result;

    int output_channels = normalize_audio_output_channels(channels);
    if (sample_rate <= 0 || output_channels <= 0) {
        LOGE("Invalid audio output params: sample_rate=%d, source_channels=%d, output_channels=%d",
             sample_rate, channels, output_channels);
        return false;
    }

    LOGI("Initializing audio output: %d Hz, source_channels=%d, output_channels=%d",
         sample_rate, channels, output_channels);

    result = slCreateEngine(&engineObject_, 0, nullptr, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to create engine: %d", result);
        return false;
    }
    
    result = (*engineObject_)->Realize(engineObject_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to realize engine: %d", result);
        destroyAudioOutputObjectsLocked();
        return false;
    }
    
    result = (*engineObject_)->GetInterface(engineObject_, SL_IID_ENGINE, &engineEngine_);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to get engine interface: %d", result);
        destroyAudioOutputObjectsLocked();
        return false;
    }

    result = (*engineEngine_)->CreateOutputMix(engineEngine_, &outputMixObject_, 0, nullptr, nullptr);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to create output mix: %d", result);
        destroyAudioOutputObjectsLocked();
        return false;
    }

    result = (*outputMixObject_)->Realize(outputMixObject_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to realize output mix: %d", result);
        destroyAudioOutputObjectsLocked();
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
    if (output_channels == 1) {
        channel_mask = SL_SPEAKER_FRONT_CENTER;
    } else {
        channel_mask = SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT;
    }
    
    SLDataFormat_PCM format_pcm = {
        SL_DATAFORMAT_PCM,
        static_cast<SLuint32>(output_channels),
        sl_sample_rate,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        SL_PCMSAMPLEFORMAT_FIXED_16,
        channel_mask,
        SL_BYTEORDER_LITTLEENDIAN
    };
    
    LOGI("Audio output config: %d Hz, %d channels, 16-bit", sample_rate, output_channels);

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
        destroyAudioOutputObjectsLocked();
        return false;
    }

    result = (*playerObject_)->Realize(playerObject_, SL_BOOLEAN_FALSE);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to realize audio player: %d", result);
        destroyAudioOutputObjectsLocked();
        return false;
    }

    result = (*playerObject_)->GetInterface(playerObject_, SL_IID_PLAY, &playItf_);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to get play interface: %d", result);
        destroyAudioOutputObjectsLocked();
        return false;
    }

    result = (*playerObject_)->GetInterface(playerObject_, SL_IID_BUFFERQUEUE, &bufferQueueItf_);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to get buffer queue interface: %d", result);
        destroyAudioOutputObjectsLocked();
        return false;
    }

    result = (*bufferQueueItf_)->RegisterCallback(bufferQueueItf_, audioCallback, this);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to register callback: %d", result);
        destroyAudioOutputObjectsLocked();
        return false;
    }
    
    // VolumeItf is intentionally omitted to avoid AppOps CONTROL_AUDIO permission checks.
    // Volume is managed entirely inside the core.
    volumeItf_ = nullptr;
    LOGI("Volume control: core-only (OpenSL ES VolumeItf disabled)");

    audio_sample_rate_ = sample_rate;
    audio_channels_    = output_channels;

    // Adaptive buffer sizing:
    // - default ~12ms to reduce callback underrun on busy devices
    // - enlarge for high-rate or 4K decode pipelines
    int target_buffer_ms = 12;
    float req_rate = requested_playback_rate_.load(std::memory_order_relaxed);
    bool high_bandwidth_pcm = sample_rate >= 44100 && output_channels >= 2;
    if (req_rate >= 2.5f) {
        target_buffer_ms = 22;
    } else if (req_rate >= 2.0f || high_bandwidth_pcm) {
        target_buffer_ms = 18;
    }
    audio_buffer_size_ = (sample_rate * output_channels * 2 * target_buffer_ms) / 1000;
    audio_buffer_size_ = (audio_buffer_size_ + 3) & ~3;  // 4-byte align

    if (audio_buffer_size_ > MAX_AUDIO_BUFFER_SIZE) {
        audio_buffer_size_ = MAX_AUDIO_BUFFER_SIZE;
        LOGW("Audio buffer size capped to %d bytes", MAX_AUDIO_BUFFER_SIZE);
    }
    if (audio_buffer_size_ < 1536) {
        audio_buffer_size_ = 1536;
    }

    LOGI("Audio buffer: %d bytes (%.1f ms)",
         audio_buffer_size_,
         (audio_buffer_size_ * 1000.0) / (sample_rate * output_channels * 2));

    // Estimate the hardware output queue latency (bytes already enqueued but
    // not yet heard).  The render thread subtracts this from the audio master
    // clock so it compares video PTS against "what the user is actually hearing"
    // rather than "what has been submitted to the driver".  Mirrors iOS logic in
    // HXCPlayerControl.mm (_audioOutputLatencySec).
    {
        double bytes_per_sec = (double)sample_rate * output_channels * 2.0; // 16-bit PCM
        double queued_sec    = (bytes_per_sec > 0.0)
                               ? (double)audio_buffer_size_ / bytes_per_sec
                               : 0.0;
        audio_output_latency_sec_ = std::min(0.200, std::max(0.0, queued_sec * 0.85));
        LOGI("Audio output latency estimate: %.1f ms", audio_output_latency_sec_ * 1000.0);
    }

    memset(audio_buffer_, 0, audio_buffer_size_);
    result = (*bufferQueueItf_)->Enqueue(bufferQueueItf_, audio_buffer_, audio_buffer_size_);
    if (result != SL_RESULT_SUCCESS) {
        LOGE("Failed to enqueue initial audio buffer: %d", result);
        destroyAudioOutputObjectsLocked();
        return false;
    }

    LOGI("Audio output initialized successfully with %d Hz, output_channels=%d", sample_rate, output_channels);
    audio_active_ = true;
    return true;
}

void AndroidPlayer::destroyAudioOutputObjectsLocked() {
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
}

void AndroidPlayer::destroyAudioOutput() {
    // Signal audio callback to stop accessing player_core_.
    audio_active_ = false;
    audio_start_pending_.store(false, std::memory_order_release);

    // Stop playback first so OpenSL stops scheduling new callbacks.  Protect
    // every OpenSL object call with opensl_mutex_; Android's OpenSL layer can
    // abort if SetPlayState/Realize/Destroy race on different threads.
    {
        std::lock_guard<std::mutex> opensl_lock(opensl_mutex_);
        if (playItf_) {
            SLresult result = (*playItf_)->SetPlayState(playItf_, SL_PLAYSTATE_STOPPED);
            if (result != SL_RESULT_SUCCESS) {
                LOGW("Failed to stop audio output before destroy: %d", result);
            }
        }
    }

    // Ensure any in-flight callback has exited before destroying buffer queues
    // or the player core state used by swr_convert.
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
    }

    {
        std::lock_guard<std::mutex> opensl_lock(opensl_mutex_);
        destroyAudioOutputObjectsLocked();
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
    int source_channels = player_core_get_audio_channels(player_core_);
    int output_channels = normalize_audio_output_channels(source_channels);
    LOGI("Audio info: sample_rate=%d, source_channels=%d, output_channels=%d",
         sample_rate, source_channels, output_channels);

    if (sample_rate <= 0 || output_channels <= 0) {
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
                            || audio_channels_ != output_channels;
    if (!need_recreate) {
        LOGI("Audio output already matched stream parameters");
        return;
    }

    if (audio_initialized_) {
        LOGI("Audio format changed, rebuilding output: %d/%d -> %d/%d (source_channels=%d)",
             audio_sample_rate_, audio_channels_, sample_rate, output_channels, source_channels);
        destroyAudioOutput();
    }

    if (!initAudioOutput(sample_rate, source_channels)) {
        LOGE("Failed to initialize audio output");
        audio_initialized_ = false;
        return;
    }

    audio_initialized_ = true;
    LOGI("Audio output initialized with stream parameters");
}

bool AndroidPlayer::primeAudioBufferQueue(const char* reason, bool clear_queue) {
    if (!bufferQueueItf_ || audio_buffer_size_ <= 0) {
        LOGW("evt=audio_prime_skip reason=%s detail=no_buffer_queue size=%d",
             reason ? reason : "", audio_buffer_size_);
        return false;
    }

    std::lock_guard<std::mutex> audio_lock(audio_mutex_);
    memset(audio_buffer_, 0, audio_buffer_size_);

    SLresult clear_ret = SL_RESULT_SUCCESS;
    SLresult enqueue_ret = SL_RESULT_RESOURCE_ERROR;
    {
        std::lock_guard<std::mutex> opensl_lock(opensl_mutex_);
        if (!bufferQueueItf_) {
            return false;
        }
        if (clear_queue) {
            clear_ret = (*bufferQueueItf_)->Clear(bufferQueueItf_);
        }
        enqueue_ret = (*bufferQueueItf_)->Enqueue(bufferQueueItf_, audio_buffer_, audio_buffer_size_);
    }

    bool ok = (!clear_queue || clear_ret == SL_RESULT_SUCCESS) && enqueue_ret == SL_RESULT_SUCCESS;
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    if (ok) {
        last_audio_prime_ms_.store(now, std::memory_order_release);
    }
    LOGW("evt=audio_prime_queue reason=%s clear=%d clear_ret=%d enqueue_ret=%d size=%d ok=%d",
         reason ? reason : "", clear_queue ? 1 : 0, clear_ret, enqueue_ret,
         audio_buffer_size_, ok ? 1 : 0);
    return ok;
}

bool AndroidPlayer::recreateAudioOutputForCurrentStream(const char* reason) {
    if (!player_core_) {
        return false;
    }

    int sample_rate = player_core_get_audio_sample_rate(player_core_);
    int source_channels = player_core_get_audio_channels(player_core_);
    int output_channels = normalize_audio_output_channels(source_channels);
    if (sample_rate <= 0 || output_channels <= 0) {
        LOGW("evt=audio_recreate_skip reason=%s sample_rate=%d source_channels=%d output_channels=%d",
             reason ? reason : "", sample_rate, source_channels, output_channels);
        return false;
    }

    LOGW("evt=audio_recreate_output reason=%s sample_rate=%d source_channels=%d output_channels=%d",
         reason ? reason : "", sample_rate, source_channels, output_channels);
    destroyAudioOutput();
    if (!initAudioOutput(sample_rate, source_channels)) {
        audio_initialized_ = false;
        return false;
    }
    audio_initialized_ = true;
    if (isAudioOutputEnabled()) {
        setOpenSLESPlayStateWithRetry(SL_PLAYSTATE_PLAYING, true);
    }
    return true;
}

SLresult AndroidPlayer::setOpenSLESPlayState(SLuint32 state, bool require_audible) {
    std::lock_guard<std::mutex> opensl_lock(opensl_mutex_);
    if (!playItf_) {
        return SL_RESULT_RESOURCE_ERROR;
    }
    if (require_audible && !isAudioOutputEnabled()) {
        return SL_RESULT_SUCCESS;
    }
    return (*playItf_)->SetPlayState(playItf_, state);
}

bool AndroidPlayer::isAudioOutputEnabled() const {
    return !muted_.load(std::memory_order_relaxed)
            && current_volume_.load(std::memory_order_relaxed) > 0.0f;
}

SLresult AndroidPlayer::setOpenSLESPlayStateWithRetry(SLuint32 state, bool require_audible, int max_retries) {
    SLresult last = SL_RESULT_RESOURCE_ERROR;
    for (int i = 0; i < max_retries; ++i) {
        last = setOpenSLESPlayState(state, require_audible);
        if (last == SL_RESULT_SUCCESS) {
            return last;
        }
        usleep(50000);
    }
    return last;
}

int AndroidPlayer::queryOpenSLESPlayState() {
    std::lock_guard<std::mutex> opensl_lock(opensl_mutex_);
    if (!playItf_) {
        return 0;
    }
    SLuint32 state = SL_PLAYSTATE_STOPPED;
    if ((*playItf_)->GetPlayState(playItf_, &state) != SL_RESULT_SUCCESS) {
        return 0;
    }
    if (state == SL_PLAYSTATE_PLAYING) return 1;
    if (state == SL_PLAYSTATE_PAUSED) return 2;
    return 3;
}

namespace {
constexpr int AUDIO_OUTPUT_STATE_IDLE = 0;
constexpr int AUDIO_OUTPUT_STATE_START_PENDING = 1;
constexpr int AUDIO_OUTPUT_STATE_PLAYING = 2;
constexpr int AUDIO_OUTPUT_STATE_PAUSED_SEEK = 3;
constexpr int AUDIO_OUTPUT_STATE_PAUSED_REBUFFER = 4;
constexpr int AUDIO_OUTPUT_STATE_STALLED = 5;
constexpr int AUDIO_OUTPUT_STATE_MUTED = 6;
constexpr int AUDIO_OUTPUT_STATE_ERROR = 7;
}

int AndroidPlayer::resolveAudioOutputState(int64_t now, int opensl_state) {
    if (!isAudioOutputEnabled()) {
        return AUDIO_OUTPUT_STATE_MUTED;
    }
    if (!audio_initialized_ || !audio_active_.load(std::memory_order_acquire) || !playItf_) {
        return AUDIO_OUTPUT_STATE_IDLE;
    }
    if (!player_core_ || !player_core_get_play_when_ready(player_core_)) {
        return AUDIO_OUTPUT_STATE_IDLE;
    }
    bool waiting_for_open_first_frame =
            open_in_progress_.load(std::memory_order_acquire) ||
            (player_core_is_video_stream_opened(player_core_) != 0 &&
             !first_frame_rendered_.load(std::memory_order_acquire));
    if (waiting_for_open_first_frame) {
        return AUDIO_OUTPUT_STATE_START_PENDING;
    }
    if (audio_start_pending_.load(std::memory_order_acquire)) {
        return AUDIO_OUTPUT_STATE_START_PENDING;
    }
    if (seek_audio_wait_video_.load(std::memory_order_acquire)) {
        return AUDIO_OUTPUT_STATE_PAUSED_SEEK;
    }
    if (audio_rebuffer_pending_.load(std::memory_order_acquire)) {
        return AUDIO_OUTPUT_STATE_PAUSED_REBUFFER;
    }
    int64_t last_audio_ms = last_effective_audio_output_ms_.load(std::memory_order_acquire);
    bool silent_too_long = last_audio_ms > 0 && (now - last_audio_ms) >= kAudioSilentThresholdMs;
    int64_t underrun_started_ms = audio_underrun_streak_started_ms_.load(std::memory_order_acquire);
    bool underrun_stuck = underrun_started_ms > 0 && (now - underrun_started_ms) >= 1200;
    if (opensl_state == 1 && !silent_too_long && !underrun_stuck) {
        return AUDIO_OUTPUT_STATE_PLAYING;
    }
    if (opensl_state == 2 || opensl_state == 3 || silent_too_long || underrun_stuck) {
        return AUDIO_OUTPUT_STATE_STALLED;
    }
    return AUDIO_OUTPUT_STATE_ERROR;
}

void AndroidPlayer::resetAudioHealthStateForOpen(const char* reason) {
    last_effective_audio_output_ms_.store(0, std::memory_order_release);
    last_audio_callback_ms_.store(0, std::memory_order_release);
    last_audio_prime_ms_.store(0, std::memory_order_release);
    audio_underrun_streak_started_ms_.store(0, std::memory_order_release);
    last_audio_underrun_ms_.store(0, std::memory_order_release);
    recent_audio_underrun_total_.store(0, std::memory_order_release);
    audio_health_recover_attempts_.store(0, std::memory_order_release);
    audio_av_split_started_ms_.store(0, std::memory_order_release);
    audio_av_split_forced_pause_.store(false, std::memory_order_release);
    LOGI("evt=audio_health_reset_for_open reason=%s", reason ? reason : "");
}

bool AndroidPlayer::forceResumeAudioOutput(int64_t now, double anchor_pts, const char* reason) {
    audio_rebuffer_pending_.store(false, std::memory_order_release);
    audio_rebuffer_deadline_ms_ = 0;
    audio_rebuffer_paused_at_ms_ = 0;
    audio_rebuffer_min_resume_at_ms_ = 0;
    seek_audio_wait_video_.store(false, std::memory_order_release);
    seek_audio_wait_deadline_ms_ = 0;
    audio_underrun_streak_started_ms_.store(0, std::memory_order_release);

    if (player_core_ && std::isfinite(anchor_pts) && anchor_pts >= 0.0) {
        player_core_anchor_clock(player_core_, anchor_pts);
    }
    if (!playItf_) {
        LOGW("evt=audio_force_resume_skip reason=%s detail=no_play_interface anchor=%.3f",
             reason ? reason : "", anchor_pts);
        return false;
    }
    if (!muted_.load(std::memory_order_relaxed) &&
        current_volume_.load(std::memory_order_relaxed) <= 0.0f) {
        current_volume_.store(1.0f, std::memory_order_relaxed);
        if (player_core_) {
            player_core_set_volume(player_core_, 1.0f);
        }
        LOGW("evt=audio_force_resume_restore_base_volume reason=%s volume=1.0",
             reason ? reason : "");
    }
    if (!isAudioOutputEnabled()) {
        LOGW("evt=audio_force_resume_skip reason=%s detail=muted anchor=%.3f",
             reason ? reason : "", anchor_pts);
        return false;
    }
    int opensl_state = queryOpenSLESPlayState();
    int64_t last_callback_ms = last_audio_callback_ms_.load(std::memory_order_acquire);
    bool callback_stalled = last_callback_ms <= 0 || (now - last_callback_ms) >= kAudioCallbackStallMs;
    bool primed = false;
    if (opensl_state != 1 || callback_stalled) {
        primed = primeAudioBufferQueue(reason, true);
    }
    SLresult r = setOpenSLESPlayStateWithRetry(SL_PLAYSTATE_PLAYING, true);
    LOGW("evt=audio_force_resume reason=%s result=%d anchor=%.3f opensl_before=%d callback_stalled=%d primed=%d",
         reason ? reason : "", r, anchor_pts, opensl_state, callback_stalled ? 1 : 0, primed ? 1 : 0);
    return r == SL_RESULT_SUCCESS;
}

bool AndroidPlayer::enforceAudioPauseDeadlines(int64_t now, const char* source) {
    if (!player_core_) return false;
    if (!isAudioOutputEnabled()) return false;

    bool play_when_ready = player_core_get_play_when_ready(player_core_) != 0;
    bool manual_pause_blocked = false;
    if (user_manual_pause_.load(std::memory_order_acquire)) {
        int64_t block_until = user_manual_pause_block_until_ms_.load(std::memory_order_acquire);
        manual_pause_blocked = block_until <= 0 || now < block_until;
    }
    if (!play_when_ready || manual_pause_blocked || seek_started_while_paused_.load(std::memory_order_acquire)) {
        return false;
    }

    double anchor = player_core_get_position(player_core_);
    if (!std::isfinite(anchor) || anchor < 0.0) {
        anchor = seek_target_sec_.load(std::memory_order_acquire);
    }
    if (!std::isfinite(anchor) || anchor < 0.0) anchor = 0.0;

    if (audio_start_pending_.load(std::memory_order_acquire)) {
        int64_t deadline = audio_start_deadline_ms_;
        bool deadline_elapsed = deadline <= 0 || now >= deadline;
        bool first_frame_ready = first_frame_rendered_.load(std::memory_order_acquire);
        if (deadline_elapsed || first_frame_ready) {
            audio_start_pending_.store(false, std::memory_order_release);
            audio_start_deadline_ms_ = 0;
            if (playItf_) {
                SLresult r = setOpenSLESPlayStateWithRetry(SL_PLAYSTATE_PLAYING, true);
                LOGW("evt=audio_pause_deadline_resume source=%s reason=start_pending result=%d first_frame=%d anchor=%.3f",
                     source ? source : "", r, first_frame_ready ? 1 : 0, anchor);
                return r == SL_RESULT_SUCCESS;
            }
        }
    }

    if (seek_audio_wait_video_.load(std::memory_order_acquire)) {
        bool deadline_elapsed = seek_audio_wait_deadline_ms_ <= 0 || now >= seek_audio_wait_deadline_ms_;
        if (deadline_elapsed) {
            setSeekPhase(SEEK_PHASE_FAILOVER, "audio_deadline_seek_wait");
            bool ok = forceResumeAudioOutput(now, anchor, "audio_pause_deadline_seek_wait");
            sync_warmup_frames_.store(24, std::memory_order_release);
            setSeekPhase(SEEK_PHASE_IDLE, "audio_deadline_seek_wait_settled");
            LOGW("evt=audio_pause_deadline_resume source=%s reason=seek_wait result=%d anchor=%.3f",
                 source ? source : "", ok ? 1 : 0, anchor);
            return ok;
        }
    }

    if (audio_rebuffer_pending_.load(std::memory_order_acquire)) {
        bool deadline_elapsed = audio_rebuffer_deadline_ms_ > 0 && now >= audio_rebuffer_deadline_ms_;
        bool deadline_missing_stale =
                audio_rebuffer_deadline_ms_ <= 0 &&
                audio_rebuffer_paused_at_ms_ > 0 &&
                (now - audio_rebuffer_paused_at_ms_) >= 1800;
        if (deadline_elapsed || deadline_missing_stale) {
            bool ok = forceResumeAudioOutput(now, anchor, "audio_pause_deadline_rebuffer");
            audio_rebuffer_cooldown_until_ms_ = now + 1200;
            LOGW("evt=audio_pause_deadline_resume source=%s reason=rebuffer result=%d anchor=%.3f deadline_elapsed=%d",
                 source ? source : "", ok ? 1 : 0, anchor, deadline_elapsed ? 1 : 0);
            return ok;
        }
    }

    return false;
}

void AndroidPlayer::checkAndRecoverAudioHealth(int64_t now) {
    if (!player_core_) return;
    if (!isAudioOutputEnabled()) return;
    if (system_music_volume_zero_.load(std::memory_order_acquire)) {
        audio_health_recover_attempts_.store(0, std::memory_order_release);
        audio_av_split_started_ms_.store(0, std::memory_order_release);
        LOGI_RATE(20, "evt=audio_health_recover_skip reason=system_music_volume_zero");
        return;
    }
    if (!player_core_get_play_when_ready(player_core_)) return;
    bool core_playing = player_core_is_playing(player_core_) != 0;
    int64_t last_progress_ms = loading_progress_last_advance_ms_.load(std::memory_order_acquire);
    bool progress_active = last_progress_ms > 0 && (now - last_progress_ms) <= 2500;
    if (!core_playing && !progress_active && !is_loading_.load(std::memory_order_acquire)) return;
    // 首帧前故意延迟音频启动；此阶段 OpenSL 可能为 PAUSED，不应误判为无声故障。
    if (audio_start_pending_.load(std::memory_order_acquire)) return;
    if (!first_frame_rendered_.load(std::memory_order_acquire)) return;
    if (open_in_progress_.load(std::memory_order_acquire)) return;
    bool seek_audio_wait_active = seek_audio_wait_video_.load(std::memory_order_acquire);
    bool seek_audio_wait_expired =
            seek_audio_wait_active &&
            (seek_audio_wait_deadline_ms_ <= 0 || now >= seek_audio_wait_deadline_ms_);
    if (seek_audio_wait_active && !seek_audio_wait_expired) {
        return;
    }

    int opensl_state = queryOpenSLESPlayState();
    int64_t last_audio_ms = last_effective_audio_output_ms_.load(std::memory_order_acquire);
    int64_t last_callback_ms = last_audio_callback_ms_.load(std::memory_order_acquire);
    bool silent_too_long = last_audio_ms > 0 && (now - last_audio_ms) >= kAudioSilentThresholdMs;
    bool callback_stalled = last_callback_ms > 0 && (now - last_callback_ms) >= kAudioCallbackStallMs;
    bool secure_audio_underrun_stuck = false;
    if (secure_session_active_.load(std::memory_order_acquire)) {
        int64_t underrun_started_ms = audio_underrun_streak_started_ms_.load(std::memory_order_acquire);
        secure_audio_underrun_stuck =
                underrun_started_ms > 0 &&
                (now - underrun_started_ms) >= 1800;
    }
    bool opensl_not_playing = opensl_state == 2 || opensl_state == 3;
    bool rebuffer_pending = audio_rebuffer_pending_.load(std::memory_order_acquire);
    bool rebuffer_deadline_expired = rebuffer_pending &&
                                     audio_rebuffer_deadline_ms_ > 0 &&
                                     now >= audio_rebuffer_deadline_ms_;
    bool rebuffer_lost_deadline = rebuffer_pending &&
                                  audio_rebuffer_deadline_ms_ <= 0 &&
                                  audio_rebuffer_paused_at_ms_ > 0 &&
                                  (now - audio_rebuffer_paused_at_ms_) >= 3200;
    bool rebuffer_stuck = rebuffer_deadline_expired || rebuffer_lost_deadline;
    bool audio_unhealthy = silent_too_long || callback_stalled || secure_audio_underrun_stuck ||
                           opensl_not_playing || rebuffer_stuck || seek_audio_wait_expired;
    bool av_split_progressing = progress_active && audio_unhealthy &&
                                !seek_audio_wait_active &&
                                !user_manual_pause_.load(std::memory_order_acquire);
    int64_t av_split_started = audio_av_split_started_ms_.load(std::memory_order_acquire);
    if (av_split_progressing) {
        int64_t expected_zero = 0;
        audio_av_split_started_ms_.compare_exchange_strong(
                expected_zero, now, std::memory_order_acq_rel);
        av_split_started = audio_av_split_started_ms_.load(std::memory_order_acquire);
    } else {
        audio_av_split_started_ms_.store(0, std::memory_order_release);
    }
    int64_t av_split_ms = av_split_started > 0 ? (now - av_split_started) : 0;

    if (!audio_unhealthy) {
        return;
    }

    double anchor = player_core_get_position(player_core_);
    if (!std::isfinite(anchor) || anchor < 0.0) anchor = 0.0;

    LOGW_RATE(8,
              "evt=audio_health_watchdog_observe silent=%d callback_stalled=%d secure_underrun=%d opensl_state=%d rebuffer_stuck=%d seek_wait_expired=%d progress_active=%d av_split=%d av_split_ms=%" PRId64 " core_playing=%d anchor=%.3f action=none",
              silent_too_long ? 1 : 0,
              callback_stalled ? 1 : 0,
              secure_audio_underrun_stuck ? 1 : 0,
              opensl_state,
              rebuffer_stuck ? 1 : 0,
              seek_audio_wait_expired ? 1 : 0,
              progress_active ? 1 : 0,
              av_split_progressing ? 1 : 0,
              av_split_ms,
              core_playing ? 1 : 0,
              anchor);
    const char* core_diag = player_core_get_runtime_diagnostic(player_core_);
    SYNCW_RATE(8, "evt=audio_health_core_diag anchor=%.3f action=none diag=%s",
               anchor, core_diag ? core_diag : "");
}

void AndroidPlayer::pausePlaybackForAudioSplit(int64_t now, double anchor_pts, const char* reason) {
    (void)now;
    if (!player_core_) return;
    audio_av_split_started_ms_.store(0, std::memory_order_release);
    audio_health_recover_attempts_.store(0, std::memory_order_release);
    audio_rebuffer_pending_.store(false, std::memory_order_release);
    audio_rebuffer_deadline_ms_ = 0;
    audio_rebuffer_paused_at_ms_ = 0;
    audio_rebuffer_min_resume_at_ms_ = 0;
    audio_rebuffer_cooldown_until_ms_ = 0;
    seek_audio_wait_video_.store(false, std::memory_order_release);
    seek_audio_wait_deadline_ms_ = 0;
    if (std::isfinite(anchor_pts) && anchor_pts >= 0.0) {
        player_core_anchor_clock(player_core_, anchor_pts);
    }
    audio_av_split_forced_pause_.store(true, std::memory_order_release);
    player_core_set_play_when_ready(player_core_, 0);
    if (playItf_) {
        setOpenSLESPlayState(SL_PLAYSTATE_PAUSED, false);
    }
    is_loading_.store(true, std::memory_order_release);
    suppress_stale_loading_true_until_ms_.store(0, std::memory_order_release);
    LOGW("evt=audio_video_split_pause reason=%s anchor=%.3f source_local=%d secure=%d opensl_state=%d",
         reason ? reason : "", anchor_pts,
         source_local_active_.load(std::memory_order_acquire) ? 1 : 0,
         secure_session_active_.load(std::memory_order_acquire) ? 1 : 0,
         queryOpenSLESPlayState());
}

void AndroidPlayer::rebuildAudioOutputFromStream(bool force_recreate, const char* reason) {
    if (!player_core_) return;
    if (force_recreate) {
        recreateAudioOutputForCurrentStream(reason);
    } else {
        ensureAudioOutputForCurrentStream();
        primeAudioBufferQueue(reason, false);
    }
    if (playItf_ && isAudioOutputEnabled()) {
        setOpenSLESPlayStateWithRetry(SL_PLAYSTATE_PLAYING, true);
    }
}

void AndroidPlayer::resetVideoEmptyStallRecovery(const char* reason) {
    bool was_forced = video_empty_stall_forced_pause_.exchange(false, std::memory_order_acq_rel);
    bool had_reopen = pending_video_stall_reopen_.exchange(false, std::memory_order_acq_rel);
    video_empty_stall_started_ms_.store(0, std::memory_order_release);
    pending_video_stall_reopen_pos_.store(-1.0, std::memory_order_release);
    if (was_forced || had_reopen) {
        SYNCI("evt=video_empty_stall_reset reason=%s forced=%d pending_recover=%d",
              reason ? reason : "",
              was_forced ? 1 : 0,
              had_reopen ? 1 : 0);
    }
}

void AndroidPlayer::setSystemMusicVolumeZero(bool volume_zero) {
    bool previous = system_music_volume_zero_.exchange(volume_zero, std::memory_order_acq_rel);
    if (previous != volume_zero) {
        LOGI("evt=system_music_volume_zero_changed value=%d", volume_zero ? 1 : 0);
    }
}

double AndroidPlayer::consumeVideoStallRecoverPosition() {
    if (!pending_video_stall_reopen_.exchange(false, std::memory_order_acq_rel)) {
        return -1.0;
    }
    double pos = pending_video_stall_reopen_pos_.load(std::memory_order_acquire);
    pending_video_stall_reopen_pos_.store(-1.0, std::memory_order_release);
    if (!std::isfinite(pos) || pos < 0.0) {
        pos = 0.0;
    }
    SYNCI("evt=video_empty_stall_consume_forward_seek pos=%.3f", pos);
    return pos;
}

void AndroidPlayer::getAudioHealthMetrics(int64_t* silent_for_ms,
                                          int* underrun_recent,
                                          int* opensl_state,
                                          int* recover_attempts,
                                          int* audio_output_state) {
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t last_ms = last_effective_audio_output_ms_.load(std::memory_order_acquire);
    int state = queryOpenSLESPlayState();
    int resolved_state = resolveAudioOutputState(now, state);
    bool start_pending = resolved_state == AUDIO_OUTPUT_STATE_START_PENDING;
    if (silent_for_ms) {
        *silent_for_ms = (!start_pending && last_ms > 0) ? std::max<int64_t>(0, now - last_ms) : 0;
    }
    if (underrun_recent) {
        *underrun_recent = recent_audio_underrun_total_.load(std::memory_order_acquire);
    }
    if (opensl_state) {
        *opensl_state = state;
    }
    if (recover_attempts) {
        *recover_attempts = audio_health_recover_attempts_.load(std::memory_order_acquire);
    }
    if (audio_output_state) {
        *audio_output_state = resolved_state;
    }
}

bool AndroidPlayer::recoverAudioOutput() {
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    double anchor = player_core_ ? player_core_get_position(player_core_) : 0.0;
    if (!std::isfinite(anchor) || anchor < 0.0) anchor = 0.0;
    bool ok = enforceAudioPauseDeadlines(now, "manual_recover") ||
              forceResumeAudioOutput(now, anchor, "manual_recover");
    if (!ok) {
        rebuildAudioOutputFromStream(true, "manual_recover");
        ok = queryOpenSLESPlayState() == 1;
    }
    LOGW("evt=audio_manual_recover result=%d opensl_state=%d anchor=%.3f", ok ? 1 : 0, queryOpenSLESPlayState(), anchor);
    return ok;
}

bool AndroidPlayer::rebuildAudioOutput() {
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    double anchor = player_core_ ? player_core_get_position(player_core_) : 0.0;
    if (!std::isfinite(anchor) || anchor < 0.0) anchor = 0.0;
    audio_rebuffer_pending_.store(false, std::memory_order_release);
    audio_rebuffer_deadline_ms_ = 0;
    audio_rebuffer_paused_at_ms_ = 0;
    audio_rebuffer_min_resume_at_ms_ = 0;
    seek_audio_wait_video_.store(false, std::memory_order_release);
    seek_audio_wait_deadline_ms_ = 0;
    audio_start_pending_.store(false, std::memory_order_release);
    audio_start_deadline_ms_ = 0;
    audio_underrun_streak_started_ms_.store(0, std::memory_order_release);
    rebuildAudioOutputFromStream(true, "manual_rebuild_audio");
    bool ok = queryOpenSLESPlayState() == 1;
    if (!ok && playItf_ && isAudioOutputEnabled()) {
        ok = forceResumeAudioOutput(now, anchor, "manual_rebuild_audio") || queryOpenSLESPlayState() == 1;
    }
    LOGW("evt=audio_manual_rebuild result=%d opensl_state=%d anchor=%.3f", ok ? 1 : 0, queryOpenSLESPlayState(), anchor);
    return ok;
}

bool AndroidPlayer::handleAudioRouteChanged(const char* reason) {
    int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    double anchor = player_core_ ? player_core_get_position(player_core_) : 0.0;
    if (!std::isfinite(anchor) || anchor < 0.0) anchor = 0.0;
    if (!player_core_ || !isAudioOutputEnabled()) {
        LOGW("evt=audio_route_recover_skip reason=%s detail=inactive_or_muted anchor=%.3f",
             reason ? reason : "", anchor);
        return false;
    }

    bool primed = primeAudioBufferQueue(reason, true);
    bool resumed = forceResumeAudioOutput(now, anchor, reason);
    bool ok = primed || resumed;
    if (!ok || queryOpenSLESPlayState() != 1) {
        rebuildAudioOutputFromStream(true, reason ? reason : "audio_route_changed");
        ok = queryOpenSLESPlayState() == 1;
    }
    LOGW("evt=audio_route_recover reason=%s primed=%d resumed=%d result=%d opensl_state=%d anchor=%.3f",
         reason ? reason : "", primed ? 1 : 0, resumed ? 1 : 0, ok ? 1 : 0,
         queryOpenSLESPlayState(), anchor);
    return ok;
}

void AndroidPlayer::audioCallback(SLAndroidSimpleBufferQueueItf bq, void* context) {
    auto* player = static_cast<AndroidPlayer*>(context);
    if (!player || !bq) {
        return;
    }
    player->onAudioData(bq);
}

void AndroidPlayer::onAudioData(SLAndroidSimpleBufferQueueItf bq) {
    if (!bq) {
        return;
    }
    // Hold audio_mutex_ for the entire callback body.
    // The destructor and openURL both acquire this lock BEFORE destroying/stopping
    // the core, so swr_convert() inside player_core_get_audio_data() is guaranteed
    // to finish before the SwrContext is freed.
    // Keep audio_mutex_ held until after Enqueue:
    // destroyAudioOutput/openURL acquire the same lock before tearing down core/OpenSL,
    // so this prevents enqueue-on-destroy races.
    std::lock_guard<std::mutex> lock(audio_mutex_);
    int64_t audio_now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    last_audio_callback_ms_.store(audio_now_ms, std::memory_order_release);

    if (!audio_active_ || !player_core_ || audio_buffer_size_ == 0) {
        int sz = audio_buffer_size_ > 0 ? audio_buffer_size_ : 4096;
        memset(audio_buffer_, 0, sz);
        SLresult enqueue_ret = SL_RESULT_RESOURCE_ERROR;
        {
            std::lock_guard<std::mutex> opensl_lock(opensl_mutex_);
            if (bufferQueueItf_ && bq == bufferQueueItf_) {
                enqueue_ret = (*bq)->Enqueue(bq, audio_buffer_, sz);
            }
        }
        if (enqueue_ret != SL_RESULT_SUCCESS) {
            LOGW_RATE(50, "[audio] enqueue silent buffer failed: ret=%d size=%d", enqueue_ret, sz);
        }
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
        audio_underrun_streak_started_ms_.store(0, std::memory_order_release);
        if (total_bytes_read < audio_buffer_size_) {
            audio_partial_count_++;
            memset(audio_buffer_ + total_bytes_read, 0,
                   audio_buffer_size_ - total_bytes_read);
        }
    } else {
        audio_underrun_count_++;
        last_audio_underrun_ms_.store(audio_now_ms, std::memory_order_release);
        int64_t expected_zero = 0;
        audio_underrun_streak_started_ms_.compare_exchange_strong(
                expected_zero, audio_now_ms, std::memory_order_acq_rel);
        memset(audio_buffer_, 0, audio_buffer_size_);
    }

    if (audio_cb_count_ % 200 == 0) {
        double pos = player_core_get_position(player_core_);
        if (audio_underrun_count_ > 0 || audio_partial_count_ > 0) {
            LOGW("[audio] cb=%d pos=%.1fs underrun=%d partial=%d (last 200)",
                 audio_cb_count_, pos, audio_underrun_count_, audio_partial_count_);
            recent_audio_underrun_total_.fetch_add(audio_underrun_count_, std::memory_order_relaxed);
        }
        audio_underrun_count_ = 0;
        audio_partial_count_  = 0;
    }

    if (total_bytes_read > 0) {
        last_effective_audio_output_ms_.store(audio_now_ms, std::memory_order_release);
        audio_health_recover_attempts_.store(0, std::memory_order_release);
        audio_av_split_started_ms_.store(0, std::memory_order_release);
        audio_av_split_forced_pause_.store(false, std::memory_order_release);
    }

    SLresult enqueue_ret = SL_RESULT_RESOURCE_ERROR;
    {
        std::lock_guard<std::mutex> opensl_lock(opensl_mutex_);
        if (bufferQueueItf_ && bq == bufferQueueItf_) {
            enqueue_ret = (*bq)->Enqueue(bq, audio_buffer_, audio_buffer_size_);
        }
    }
    if (enqueue_ret != SL_RESULT_SUCCESS) {
        LOGW_RATE(50, "[audio] enqueue output buffer failed: ret=%d size=%d", enqueue_ret, audio_buffer_size_);
    }
}
