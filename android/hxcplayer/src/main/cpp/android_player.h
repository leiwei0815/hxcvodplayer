#ifndef ANDROID_PLAYER_H
#define ANDROID_PLAYER_H

#include <android/native_window.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <cstdint>
#include <limits>
#include <vector>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

// EGL / OpenGL ES 3.0  (PBO requires GLES3)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

// Forward declarations
struct PlayerCoreHandle;
namespace hxcplayer { class ICustomDataSource; }

/**
 * Android player – EGL/GLES2 video + OpenSL ES audio.
 *
 * Design principles (aligned with Tencent/IJKPlayer):
 *   - EGL context is created ONCE on the render thread at start-up.
 *   - EGL window surface is created / destroyed on the same render thread
 *     whenever the ANativeWindow changes, protected by a condition variable.
 *   - No "warmup frame" counter.  The first decoded frame is displayed as soon
 *     as the A/V clock difference is within the normal sync window.
 *   - condition_variable drives the render loop instead of fixed sleeps.
 */
class AndroidPlayer {
public:
    enum SeekPhase {
        SEEK_PHASE_IDLE = 0,
        SEEK_PHASE_PRIME = 1,
        SEEK_PHASE_CONVERGE = 2,
        SEEK_PHASE_VERIFY = 3,
        SEEK_PHASE_RESUME = 4,
        SEEK_PHASE_FAILOVER = 5
    };
    AndroidPlayer();
    ~AndroidPlayer();

    // --- Surface management ---
    // setSurface may be called from any thread (UI / JNI).
    void setSurface(ANativeWindow* window);
    void updateSurfaceSize(int width, int height);

    // --- Open methods ---
    bool openURL(const char* url);
    bool openURL(const char* url, double start_position);
    bool openWithCustomHTTP(const char* url, int timeout_ms = 30000,
                            int max_retries = 3, bool encrypted_file = false,
                            double start_position = 0.0);
    bool openWithCustomFile(const char* path, size_t avio_buffer_size = 64 * 1024,
                            bool encrypted_file = false, double start_position = 0.0);
    bool openWithSecureSession(const char* url,
                               double      start_position,
                               const char* auth_token,
                               const char* video_id,
                               const char* device_id          = nullptr,
                               const char* secret_id          = nullptr,
                               const char* nonce              = nullptr,
                               const char* play_session_id    = nullptr,
                               const char* secure_headers     = nullptr,
                               int64_t     session_expire_at_ms = 0,
                               int         key_mode           = 0,
                               const char* key_material_b64   = nullptr,
                               const char* key_iv_hex         = nullptr);
    bool openWithSecureHLS(const char* url,
                           double      start_position,
                           const char* auth_token,
                           const char* video_id,
                           const char* device_id          = nullptr,
                           const char* secret_id          = nullptr,
                           const char* nonce              = nullptr,
                           const char* play_session_id    = nullptr,
                           const char* secure_headers     = nullptr,
                           int64_t     session_expire_at_ms = 0,
                           int         key_mode           = 0,
                           const char* key_material_b64   = nullptr,
                           const char* key_iv_hex         = nullptr);

    // --- Playback control ---
    void   play();
    void   pause();
    void   stop();
    void   seekTo(double position);
    void   seekToWithIntent(double position, bool resume_after_seek);
    void   setPlaybackRate(float rate);
    void   setVolume(float volume);
    void   setMuted(bool muted);
    void   setAspectRatioMode(int mode);   // 0=FIT, 1=FILL
    void   setDecodeMode(int mode);        // 0=software, 1=hardware
    void   setSecureSeekTuning(double drop_only_window_backward_sec,
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
                               int audio_wait_deadline_large_ms);
    void   resetSecureSeekTuning();
    int    getDecodeMode() const;

    // --- State queries ---
    double getDuration() const;
    double getPosition() const;
    int    getState() const;
    int    getPipelineState() const;
    bool   getPlayWhenReady() const;
    bool   isPlaying() const;
    void   setPlayWhenReady(bool play_when_ready);
    bool   isLoading() const;
    bool   isHardwareDecodingActive() const;
    bool   isSeekSessionActive() const;
    bool   consumeLastError(int& error_code, std::string& error_message);
    bool   consumePlaybackCompleted();
    void   settleSeekSessionFromApp(bool by_timeout);

    // 音频健康监测与恢复（供业务层/JNI 查询与触发）
    void   getAudioHealthMetrics(int64_t* silent_for_ms,
                                 int* underrun_recent,
                                 int* opensl_state,
                                 int* recover_attempts,
                                 int* audio_output_state);
    bool   recoverAudioOutput();
    bool   rebuildAudioOutput();
    bool   handleAudioRouteChanged(const char* reason);
    void   setSystemMusicVolumeZero(bool volume_zero);
    double consumeVideoStallRecoverPosition();

private:
    // -----------------------------------------------------------------------
    // Core
    // -----------------------------------------------------------------------
    // 串行化控制面 API（open/stop/play/pause/seek/release），避免并发操作 core。
    mutable std::mutex api_mutex_;
    PlayerCoreHandle* player_core_;

    // -----------------------------------------------------------------------
    // Surface state  (protected by render_mutex_ / render_cv_)
    // -----------------------------------------------------------------------
    // render_mutex_ guards: native_window_, surface_width_, surface_height_,
    //                        pending_window_ (hot-swap request), stop_requested_
    std::mutex              render_mutex_;
    std::condition_variable render_cv_;

    ANativeWindow* native_window_;      // window currently bound to EGL surface
    ANativeWindow* pending_window_;     // non-null = a hot-swap is requested
    bool           window_changed_;     // render thread should rebuild EGL surface
    bool           stop_requested_;     // render thread should exit

    int surface_width_;
    int surface_height_;
    int aspect_ratio_mode_;  // 0=FIT, 1=FILL
    int decode_mode_;        // 0=software, 1=hardware

    // -----------------------------------------------------------------------
    // EGL / OpenGL ES  (render thread only – never touch from other threads)
    // -----------------------------------------------------------------------
    EGLDisplay egl_display_;
    EGLContext egl_context_;
    EGLSurface egl_surface_;
    EGLConfig  egl_config_;

    GLuint gl_program_;
    GLuint gl_tex_y_;
    GLuint gl_tex_u_;
    GLuint gl_tex_v_;
    GLint  gl_uniform_y_;
    GLint  gl_uniform_u_;
    GLint  gl_uniform_v_;
    GLint  gl_uniform_uv_interleaved_;
    GLint  gl_uniform_uv_swap_;
    GLint  gl_attrib_pos_;
    GLint  gl_attrib_tex_;
    GLint  gl_attrib_tex_uv_;
    int    gl_last_video_w_;
    int    gl_last_video_h_;
    std::atomic<int> seek_policy_video_w_{0};
    std::atomic<int> seek_policy_video_h_{0};
    bool   gl_last_uv_interleaved_;
    bool   gl_last_uv_swap_;
    int    gl_last_uv_tex_w_;
    // Auto-detect NV12/NV21 (UV vs VU) for interleaved chroma path.
    bool   gl_uv_swap_decided_{false};
    bool   gl_uv_swap_selected_{false};
    int    gl_uv_swap_votes_{0};
    int    gl_uv_swap_probe_budget_{24};

    // PBO double-buffer for async 4K texture upload (>=1920 wide)
    // Slots: [0,1]=Y  [2,3]=U/UV  [4,5]=V(only planar path)
    GLuint gl_pbo_y_[6];
    int    gl_pbo_y_sz_;
    int    gl_pbo_uv_sz_;
    int    gl_pbo_idx_;         // current write slot (0 or 1)
    bool   gl_pbo_first_frame_;

    // Last rendered frame – redrawn on surface hot-swap
    std::vector<uint8_t> last_frame_y_;
    std::vector<uint8_t> last_frame_u_;
    std::vector<uint8_t> last_frame_v_;
    int last_frame_width_{0};
    int last_frame_height_{0};
    int last_frame_y_stride_{0};
    int last_frame_u_stride_{0};
    int last_frame_v_stride_{0};
    int64_t last_frame_cache_ms_{0};

    // EGL / GL helpers (called only from render thread)
    bool initEGLContext();
    bool initEGLSurface(ANativeWindow* win);
    void destroyEGLSurface();
    void destroyEGL();
    bool initGLProgram();
    void destroyGLProgram();
    void redrawLastFrame();
    // Reset render-side stream heuristics/caches on source switch.
    void resetRenderStateForStreamSwitch();
    bool ensurePBOs(int y_w, int y_h, int uv_w, int uv_h, int uv_bpp = 1);

    // -----------------------------------------------------------------------
    // Render thread
    // -----------------------------------------------------------------------
    std::thread       render_thread_;
    std::atomic<bool> render_running_{false};

    void renderLoop();
    int  renderFrame(void* y_data, void* u_data, void* v_data,
                     int y_linesize, int u_linesize, int v_linesize,
                     int width, int height,
                     int64_t* out_upload_ms     = nullptr,
                     int64_t* out_max_upload_ms = nullptr);

    // -----------------------------------------------------------------------
    // OpenSL ES audio
    // -----------------------------------------------------------------------
    SLObjectItf  engineObject_;
    SLEngineItf  engineEngine_;
    SLObjectItf  outputMixObject_;
    SLObjectItf  playerObject_;
    SLPlayItf    playItf_;
    SLAndroidSimpleBufferQueueItf bufferQueueItf_;
    SLVolumeItf  volumeItf_;
    bool audio_initialized_;
    int  audio_sample_rate_;
    int  audio_channels_;

    bool initAudioOutput(int sample_rate, int channels);
    void destroyAudioOutput();
    void destroyAudioOutputObjectsLocked();
    void ensureAudioOutputForCurrentStream();
    bool primeAudioBufferQueue(const char* reason, bool clear_queue);
    bool recreateAudioOutputForCurrentStream(const char* reason);
    bool isAudioOutputEnabled() const;
    SLresult setOpenSLESPlayState(SLuint32 state, bool require_audible);
    static void audioCallback(SLAndroidSimpleBufferQueueItf bq, void* context);
    void onAudioData(SLAndroidSimpleBufferQueueItf bq);

    static constexpr int MAX_AUDIO_BUFFER_SIZE = 8192;
    uint8_t           audio_buffer_[MAX_AUDIO_BUFFER_SIZE];
    int               audio_buffer_size_;
    std::mutex        audio_mutex_;
    std::mutex        opensl_mutex_;
    std::atomic<bool>  audio_active_{false};
    std::atomic<int>   audio_cb_in_flight_{0};
    std::atomic<float> current_volume_{1.0f}; // last value passed to setVolume()
    std::atomic<bool>  muted_{false};
    std::atomic<float> requested_playback_rate_{1.0f}; // rate requested by upper layer
    std::atomic<bool> seek_just_happened_{false};
    // When true: audio start is deferred until the first video frame is rendered
    // (avoids audible sound before any picture on dual-player scenarios).
    std::atomic<bool> audio_start_pending_{false};
    int64_t           audio_start_deadline_ms_{0}; // wall-clock deadline for the deferred start
    // Open-first-frame guards: keep loading/audio gate until first frame really renders.
    std::atomic<bool> first_frame_rendered_{false};
    int64_t           first_frame_wait_started_ms_{0};
    // High-rate rebuffer: when video starvation lasts too long, temporarily
    // pause OpenSL audio to avoid "audio keeps moving but video frozen".
    std::atomic<bool> audio_rebuffer_pending_{false};
    int64_t           audio_rebuffer_deadline_ms_{0};
    int64_t           audio_rebuffer_paused_at_ms_{0};
    int64_t           audio_rebuffer_min_resume_at_ms_{0};
    // Prevent high-rate starvation logic from rapidly re-triggering pause/resume oscillation.
    int64_t           audio_rebuffer_cooldown_until_ms_{0};
    // AV sync helpers (mirroring iOS HXCPlayerControl logic)
    double            audio_output_latency_sec_{0.0}; // estimated hw output queue delay
    std::atomic<int>  sync_warmup_frames_{0};         // relaxed sync window after open/seek
    double            last_sync_video_pts_{std::numeric_limits<double>::quiet_NaN()};
    // Seek 后短时间内用于“快速追赶”的目标位点（仅高倍速/高分辨率启用）。
    std::atomic<double> seek_target_sec_{-1.0};
    // Seek 发起时的播放位置，用于识别 backward seek 并过滤未来陈旧帧。
    std::atomic<double> seek_from_sec_{-1.0};
    std::atomic<int>    seek_fast_catchup_frames_{0};
    std::atomic<int>    seek_nonsecure_4k_reseek_count_{0};
    int64_t             seek_catchup_deadline_ms_{0};
    // Tencent-like seek lower-bound gate: drop frames older than seek target
    // until the first frame reaches target PTS.
    std::atomic<bool>   seek_lower_bound_active_{false};
    int64_t             seek_lower_bound_deadline_ms_{0};
    // Recovery state machine: while active, disable normal AV sync branches.
    std::atomic<bool>   seek_recovery_active_{false};
    int64_t             seek_recovery_deadline_ms_{0};
    // During seek, wait for first target video frame before resuming audio.
    std::atomic<bool>   seek_audio_wait_video_{false};
    int64_t             seek_audio_wait_deadline_ms_{0};
    // Triple-gate counter: require consecutive "target+sync ready" frames
    // before resuming audio after seek.
    std::atomic<int>    seek_resume_stable_hits_{0};
    // Whether this seek should resume playback automatically after converge.
    // Captured at seek dispatch from current core intent (playing/playWhenReady).
    std::atomic<bool>   seek_resume_on_complete_{true};
    // Marks seek requests issued while player was paused/manual-pause semantic.
    // In this mode timeout failover must not force autoplay/soft-rebuild.
    std::atomic<bool>   seek_started_while_paused_{false};
    // -1: follow current core intent, 0: keep paused after seek, 1: resume play after seek
    std::atomic<int>    seek_resume_intent_override_{-1};
    std::atomic<uint64_t> seek_session_seq_{0};
    std::atomic<uint64_t> seek_session_active_id_{0};
    std::atomic<int>    seek_phase_{SEEK_PHASE_IDLE};
    std::atomic<int>    seek_verify_hits_{0};
    // If fallback issued play but core didn't flip to playing immediately,
    // keep a short retry window to avoid "seek settled but no autoplay".
    std::atomic<bool>   seek_force_resume_pending_{false};
    std::atomic<int64_t> seek_force_resume_deadline_ms_{0};
    std::atomic<int64_t> seek_force_resume_next_try_ms_{0};
    std::atomic<int>    seek_force_resume_retry_count_{0};
    std::atomic<bool>   seek_force_resume_nudged_{false};
    // User manually pressed pause recently; suppress seek/failover auto-resume.
    std::atomic<bool>   user_manual_pause_{false};
    std::atomic<int64_t> user_manual_pause_block_until_ms_{0};
    // Per-seek anti-loop budgets:
    // - failover budget: how many times this seek session can arm force-resume retries
    // - soft-rebuild budget: how many in-session soft-rebuild attempts are allowed
    std::atomic<int>    seek_failover_budget_left_{2};
    std::atomic<int>    seek_soft_rebuild_budget_left_{1};
    // Secure HLS / encrypted session: seek sync uses keyframe-ahead landing strategy.
    std::atomic<bool>   secure_session_active_{false};
    // Source profile for seek-policy split.
    std::atomic<bool>   source_local_active_{false};
    std::atomic<bool>   source_encrypted_active_{false};
    // Track open intent to distinguish "start near tail" from normal playback.
    std::atomic<double> open_start_position_sec_{-1.0};
    std::atomic<int64_t> open_requested_at_ms_{0};
    // Seek recovery diagnostics and adaptive lower-bound relaxation.
    int64_t             seek_started_at_ms_{0};
    int                 seek_lower_bound_drop_count_{0};
    // Secure seek precise landing: when first hit is too far ahead, allow bounded reseek.
    std::atomic<int>    secure_seek_precise_reseek_count_{0};
    int64_t             secure_seek_precise_reseek_cooldown_until_ms_{0};
    // Secure forward seek adaptive pre-roll bias.
    // Learns "first landed ahead amount" so next large forward seek dispatches
    // to an earlier point and avoids repeatedly hitting the same future keyframe cluster.
    std::atomic<double> secure_forward_seek_bias_sec_{0.0};
    std::atomic<int>    secure_forward_seek_bias_hits_{0};
    std::atomic<int64_t> secure_forward_seek_bias_last_update_ms_{0};
    std::atomic<int>    secure_seek_stall_streak_{0};
    std::atomic<int64_t> secure_seek_stall_last_ms_{0};
    // Externalized secure-seek tuning knobs (runtime adjustable via JNI).
    double              secure_drop_only_window_backward_sec_{5.0};
    double              secure_drop_only_window_forward_sec_{8.0};
    double              secure_drop_only_window_large_seek_bonus_sec_{2.0};
    double              secure_drop_only_window_elapsed_bonus_sec_{1.5};
    int64_t             secure_drop_only_window_elapsed_threshold_ms_{1800};
    double              secure_accept_future_backward_early_sec_{2.5};
    double              secure_accept_future_forward_early_sec_{4.0};
    double              secure_accept_future_backward_mid_sec_{6.0};
    double              secure_accept_future_forward_mid_sec_{8.0};
    double              secure_accept_future_backward_late_sec_{10.0};
    double              secure_accept_future_forward_late_sec_{14.0};
    int64_t             secure_accept_mid_elapsed_ms_{2600};
    int64_t             secure_accept_late_elapsed_ms_{4200};
    int64_t             secure_lower_bound_deadline_normal_ms_{2700};
    int64_t             secure_lower_bound_deadline_large_ms_{3200};
    int64_t             secure_recovery_deadline_normal_ms_{5200};
    int64_t             secure_recovery_deadline_large_ms_{6400};
    int64_t             secure_audio_wait_deadline_normal_ms_{3500};
    int64_t             secure_audio_wait_deadline_large_ms_{4800};
    double              secure_forward_preroll_base_sec_{3.5};
    double              secure_forward_preroll_span_gain_{0.0011};
    double              secure_forward_preroll_learned_gain_{0.50};
    double              secure_forward_preroll_max_sec_{6.8};
    int64_t             secure_forward_preroll_bias_expire_ms_{45000};
    // High-rate cadence: avoid long "all-drop then force old frame" behavior.
    int                 consecutive_drop_count_{0};
    // Severe-lag detector for soft re-anchor (prevents endless drop loops).
    int64_t             severe_lag_start_ms_{0};
    // Severe-lag detector for "video stuck but queue not empty" audio pause.
    int64_t             severe_lag_audio_pause_start_ms_{0};
    int64_t             last_soft_reanchor_ms_{0};
    int                 soft_reanchor_count_{0};
    int               audio_cb_count_{0};
    int               audio_underrun_count_{0};
    int               audio_partial_count_{0};
    // 音频健康看门狗：检测 OpenSL 暂停或 underrun 导致的长时间无声。
    std::atomic<int64_t> last_effective_audio_output_ms_{0};
    std::atomic<int64_t> audio_underrun_streak_started_ms_{0};
    std::atomic<int64_t> last_audio_underrun_ms_{0};
    std::atomic<int>     audio_health_recover_attempts_{0};
    std::atomic<int64_t> last_audio_health_check_ms_{0};
    std::atomic<int>     recent_audio_underrun_total_{0};
    std::atomic<int64_t> last_audio_callback_ms_{0};
    std::atomic<int64_t> last_audio_prime_ms_{0};
    std::atomic<int64_t> audio_av_split_started_ms_{0};
    std::atomic<bool>    audio_av_split_forced_pause_{false};
    std::atomic<bool>    system_music_volume_zero_{false};
    std::atomic<int64_t> video_empty_stall_started_ms_{0};
    std::atomic<bool>    video_empty_stall_forced_pause_{false};
    std::atomic<bool>    pending_video_stall_reopen_{false};
    std::atomic<double>  pending_video_stall_reopen_pos_{-1.0};
    static constexpr int64_t kAudioSilentThresholdMs = 2500;
    static constexpr int64_t kAudioCallbackStallMs = 1800;
    static constexpr int64_t kAudioAvSplitPauseMs = 6500;
    static constexpr int64_t kVideoEmptyStallPauseMs = 6000;
    static constexpr int64_t kVideoEmptyStallReopenMs = 9000;
    static constexpr int64_t kCoreIoStaleRecoverMs = 8000;
    static constexpr int64_t kCoreIoStaleEmptyRecoverMs = 2000;

    int    queryOpenSLESPlayState();
    int    resolveAudioOutputState(int64_t now, int opensl_state);
    SLresult setOpenSLESPlayStateWithRetry(SLuint32 state, bool require_audible, int max_retries = 3);
    bool   forceResumeAudioOutput(int64_t now, double anchor_pts, const char* reason);
    bool   enforceAudioPauseDeadlines(int64_t now, const char* source);
    void   checkAndRecoverAudioHealth(int64_t now);
    void   resetAudioHealthStateForOpen(const char* reason);
    void   rebuildAudioOutputFromStream(bool force_recreate, const char* reason);
    void   pausePlaybackForAudioSplit(int64_t now, double anchor_pts, const char* reason);
    void   resetVideoEmptyStallRecovery(const char* reason);

    // -----------------------------------------------------------------------
    // Event forwarding (core callbacks -> JNI poll)
    // -----------------------------------------------------------------------
    std::atomic<bool> is_loading_{false};
    // Ignore stale "loading=false" callbacks briefly during pre-stop/open switch.
    std::atomic<bool> suppress_transient_loading_false_{false};
    std::atomic<int64_t> suppress_transient_loading_false_until_ms_{0};
    // After seek/app-settle, some devices keep stale loading=true while position keeps progressing.
    // This window allows native isLoading() to mask that stale true until next real loading edge.
    std::atomic<int64_t> suppress_stale_loading_true_until_ms_{0};
    std::atomic<double> loading_progress_last_pos_{-1.0};
    std::atomic<int64_t> loading_progress_last_advance_ms_{0};
    // Diagnostic: capture position backward-jump around loading state changes.
    std::atomic<double> state_last_reported_pos_{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<int64_t> state_last_backward_jump_log_ms_{0};
    static void loadingStateCallback(bool is_loading, void* user_data);

    std::mutex        error_mutex_;
    std::atomic<bool> has_pending_error_{false};
    int               last_error_code_{0};
    std::string       last_error_message_;
    // SecureHLS probe errors are internal and must not leak before open settles.
    std::atomic<bool> suppress_secure_hw_probe_errors_{false};
    static void errorStateCallback(int error_code, const char* error_msg, void* user_data);

    std::atomic<bool> has_pending_playback_completed_{false};
    // One-shot latch: once completed is detected, block repeated force-complete
    // injections and suppress stale loading=true until next explicit play/seek/open.
    std::atomic<bool> playback_completed_latched_{false};
    // Open lifecycle marker: heavy demux/network open is in progress.
    std::atomic<bool> open_in_progress_{false};
    // If play() arrives during open lock contention, defer once until open settles.
    std::atomic<bool> pending_play_after_open_{false};
    static void playbackCompletedCallback(void* user_data);

    // Seek state helpers: keep session cleanup and resume-intent decision centralized.
    void setSeekPhase(int new_phase, const char* reason);
    void resetSeekFlowState(bool clear_session_id,
                            bool clear_paused_origin,
                            bool reset_budgets,
                            bool reset_fast_catchup);
    bool resolveSeekResumeOnComplete(int seek_resume_override,
                                     bool core_paused_now,
                                     bool user_manual_pause_now);

    void trySeekAudioWaitDeadlineFallback(int64_t now,
                                          double pts,
                                          bool likely_4k,
                                          int64_t& post_seek_ahead_bypass_until_ms);
    void resumeSeekAudioAfterKeyframeAhead(int64_t now,
                                           double pts,
                                           double& clock,
                                           double& delay,
                                           double seek_target_now,
                                           bool is_backward_seek,
                                           bool large_forward_seek,
                                           bool likely_4k,
                                           int64_t& post_seek_ahead_bypass_until_ms);
};

extern "C" void hxc_sdk_set_runtime_log_level(int level);
extern "C" int hxc_sdk_get_runtime_log_level();

#endif // ANDROID_PLAYER_H
