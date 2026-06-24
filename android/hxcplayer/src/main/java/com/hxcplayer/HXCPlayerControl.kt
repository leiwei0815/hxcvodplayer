package com.hxcplayer

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.graphics.SurfaceTexture
import android.util.Log
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.TextureView
import android.view.View
import kotlin.jvm.JvmOverloads
import kotlin.math.abs
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicLong
import java.net.HttpURLConnection
import java.net.URL
import org.json.JSONObject

/**
 * HXC Player 控制类
 * 类似 iOS 的 HXCPlayerControl，内部管理视频渲染视图
 *
 * 用法:
 * ```
 * // 默认 SurfaceView
 * val player = HXCPlayerControl(context)
 *
 * // 或选用 TextureView（适合列表、转场、与 View 变换联动等场景）
 * val playerTex = HXCPlayerControl(context, VideoRenderViewType.TEXTURE_VIEW)
 *
 * val view = player.renderView
 * containerLayout.addView(view)
 *
 * player.openURL("http://example.com/video.mp4")
 * player.play()
 * ```
 */
class HXCPlayerControl @JvmOverloads constructor(
    private val context: Context,
    private val renderViewType: VideoRenderViewType = VideoRenderViewType.SURFACE_VIEW
) {
    /** 视频输出目标：SurfaceView（默认）或 TextureView */
    enum class VideoRenderViewType {
        /**
         * 独立 Surface，系统合成层上显示；全屏、功耗与兼容性通常更好。
         */
        SURFACE_VIEW,

        /**
         * 与窗口普通 View 同一图层，可做位移、透明度、动画；适合 RecyclerView、共享元素转场等。
         */
        TEXTURE_VIEW
    }

    companion object {
        private const val TAG = "HXCPlayerControl"
        private const val MIN_PLAYBACK_RATE = 0.5f
        private const val MAX_PLAYBACK_RATE = 3.0f
        private const val MAX_PLAYBACK_RATE_SNAP_EPSILON = 0.01f

        init {
            System.loadLibrary("hxcplayer")
        }
        
        private val dataSourceConfigLock = Any()
        private var gTimeoutMs: Int = 30000
        private var gMaxRetries: Int = 3
        private var gAvioBufferSize: Int = 64 * 1024
        private var gHasConfigured: Boolean = false

        // ========== 日志配置（静态方法） ==========

        /**
         * 启用文件日志
         * @param logDir 日志文件目录
         * @param prefix 日志文件前缀（默认 "hxcplayer"）
         */
        @JvmStatic
        external fun enableFileLogging(logDir: String, prefix: String = "hxcplayer")

        /**
         * 禁用文件日志
         */
        @JvmStatic
        external fun disableFileLogging()

        /**
         * 设置日志级别
         * @param level 0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR
         */
        @JvmStatic
        external fun setLogLevel(level: Int)

        /**
         * 当前日志级别（与 [setLogLevel] 一致：0~3）
         */
        @JvmStatic
        external fun getLogLevel(): Int

        /**
         * 设置日志保留天数
         * @param days 保留天数（默认 7 天）
         */
        @JvmStatic
        external fun setLogRetentionDays(days: Int)

        /**
         * 当前文件日志目录（未启用文件日志时为空字符串）
         */
        @JvmStatic
        external fun getLogDirectory(): String

        /**
         * 获取当前日志文件路径
         */
        @JvmStatic
        external fun getCurrentLogFile(): String

        /**
         * 在播放前调用一次，用全局默认值覆盖数据源配置字段（对齐 iOS 的 configureDefaultConfig）。
         * 传 null 表示恢复内置默认值。
         */
        @JvmStatic
        fun configureDefaultConfig(config: PlayerDataSourceConfig?) {
            synchronized(dataSourceConfigLock) {
                if (config == null) {
                    gHasConfigured = false
                    return
                }
                gTimeoutMs = config.timeoutMs
                gMaxRetries = config.maxRetries
                gAvioBufferSize = config.avioBufferSize
                gHasConfigured = true
            }
        }

        @JvmStatic
        fun defaultConfig(): PlayerDataSourceConfig {
            return PlayerDataSourceConfig.defaultConfig()
        }
    }

    // 播放器状态
    enum class PlayerState {
        IDLE,
        OPENING,
        LOADING,
        PLAYING,
        PAUSED,
        STOPPED,
        ERROR
    }

    /** 底层流水线状态（参考主流播放器） */
    enum class PipelineState {
        IDLE,
        PREPARING,
        BUFFERING,
        READY,
        ENDED,
        ERROR
    }

    /**
     * SDK 对外统一播放快照（单一真相源）。
     *
     * 业务层建议仅根据此快照渲染 UI，避免在 Activity 中再做状态推断。
     */
    data class PlaybackSnapshot(
        val state: PlayerState,
        val pipelineState: PipelineState,
        val playWhenReady: Boolean,
        val isPlaying: Boolean,
        val isLoading: Boolean,
        val position: Double,
        val duration: Double,
        val shouldShowPlayingUi: Boolean,
        val updatedAtMs: Long
    )

    /** 与 iOS 对齐的数据源模式 */
    enum class PlayerDataSourceMode {
        DEFAULT,
        CUSTOM_HTTP,
        CUSTOM_FILE,
        SECURE_HLS
    }

    /** 解码模式（默认硬解，失败后由内核自动回退软解） */
    enum class DecodeMode {
        SOFTWARE,
        HARDWARE
    }

    /** 对齐 iOS 的视频模型（fileId + sign + secretId + timestamp） */
    class PlayerVideo {
        var videoId: String = ""
        var sign: String = ""
        var secretId: String = ""
        var timestamp: String = ""
        // 历史字段，保留兼容
        var appId: Int = 0
    }

    /** 对齐 iOS：全局数据源默认配置 */
    class PlayerDataSourceConfig {
        var timeoutMs: Int = 30000
        var maxRetries: Int = 3
        var avioBufferSize: Int = 64 * 1024

        companion object {
            @JvmStatic
            fun defaultConfig(): PlayerDataSourceConfig {
                return PlayerDataSourceConfig()
            }
        }
    }

    /**
     * SecureHLS seek 调参配置（运行时生效，默认值与 SDK 内置策略一致）。
     * 用于快速适配不同加密源 GOP 分布，无需改 C++ 代码。
     */
    class SecureSeekTuningConfig {
        var dropOnlyWindowBackwardSec: Double = 5.0
        var dropOnlyWindowForwardSec: Double = 8.0
        var acceptFutureBackwardEarlySec: Double = 2.5
        var acceptFutureForwardEarlySec: Double = 4.0
        var acceptFutureBackwardMidSec: Double = 6.0
        var acceptFutureForwardMidSec: Double = 8.0
        var acceptFutureBackwardLateSec: Double = 10.0
        var acceptFutureForwardLateSec: Double = 14.0
        var lowerBoundDeadlineNormalMs: Int = 2700
        var lowerBoundDeadlineLargeMs: Int = 3200
        var recoveryDeadlineNormalMs: Int = 5200
        var recoveryDeadlineLargeMs: Int = 6400
        var audioWaitDeadlineNormalMs: Int = 4400
        var audioWaitDeadlineLargeMs: Int = 5600

        companion object {
            @JvmStatic
            fun defaultConfig(): SecureSeekTuningConfig {
                return SecureSeekTuningConfig()
            }
        }
    }

    /**
     * SecureHLS seek 调参预设。
     * - PRECISION_FIRST: 默认，优先落点准确
     * - SPEED_FIRST: 优先更快恢复画面
     */
    enum class SecureSeekPreset {
        PRECISION_FIRST,
        PRECISION_STABLE_BACKWARD,
        SPEED_FIRST
    }

    /**
     * 对外统一播放模型：只传一个 model 即可。
     *
     * - [url] 必填
     * - [mode] 对应默认/自定义 HTTP/自定义本地文件
     * - [encryptedFile] 是否按核心约定解密文件头
     * - [video] 对齐 iOS：通过 fileid+appid+sign 播放（Android 当前未实现该分支，预留）
     */
    class PlayerDataSourcePlayModel {
        var url: String = ""
        var mode: PlayerDataSourceMode = PlayerDataSourceMode.DEFAULT
        var encryptedFile: Boolean = false
        var video: PlayerVideo? = null

        companion object {
            @JvmStatic
            fun modelWithURL(
                url: String,
                mode: PlayerDataSourceMode,
                encryptedFile: Boolean
            ): PlayerDataSourcePlayModel {
                return PlayerDataSourcePlayModel().apply {
                    this.url = url
                    this.mode = mode
                    this.encryptedFile = encryptedFile
                }
            }
        }
    }

    /**
     * 与 `hxc_player_core_c_bridge.h` 中 `PlayerErrorCodeC` 数值一致（HTTP / 网络段亦同）。
     * 其它负数可能为 FFmpeg `AVERROR_*` 等，请结合 [PlayerCallback.onPlayerError] 的 message。
     *
     * iOS 的 `HXCPlayerErrorCode` 另有画中画、音频会话等**平台独有**错误码（如 -1015、-1016），Android 核心不会返回，此处不定义。
     */
    object PlayerErrorCode {
        const val NONE = 0
        const val INVALID_URL = -1001
        const val OPEN_INPUT_FAILED = -1002
        const val FIND_STREAM_INFO_FAILED = -1003
        const val NO_VIDEO_STREAM = -1004
        const val NO_AUDIO_STREAM = -1005
        const val CODEC_NOT_FOUND = -1006
        const val CODEC_OPEN_FAILED = -1007
        const val ALLOC_CONTEXT_FAILED = -1008
        const val SDL_INIT_FAILED = -1009
        const val AUDIO_DEVICE_OPEN_FAILED = -1010
        const val SEEK_FAILED = -1011
        const val READ_FRAME_FAILED = -1012
        const val DECODE_FAILED = -1013
        const val OUT_OF_MEMORY = -1014
        const val INPUT_INVALID_DATA = -1018
        const val NOT_SUPPORT = -1019
        const val UNKNOWN = -1099

        const val NET_CONNECTION_TIMEOUT = -2001
        const val NET_CONNECTION_REFUSED = -2002
        const val NET_UNREACHABLE = -2003

        const val HTTP_BAD_REQUEST = -3001
        const val HTTP_NOT_FOUND = -3002
        const val HTTP_SERVER_ERROR = -3003
        const val HTTP_UNAUTHORIZED = -3004
        const val HTTP_FORBIDDEN = -3005

        const val LICENSE_VALIDATION_FAILED = -4001
        const val SECURE_AUTH_FAILED = -4101
        const val SECURE_AUTH_EXPIRED = -4102
        const val SECURE_KEY_EXPIRED = -4103
        const val SECURE_KEY_INVALID = -4104
        const val SECURE_REPLAY_BLOCKED = -4105
        const val SECURE_CLOCK_SKEW = -4106
    }

    // 回调接口
    interface PlayerCallback {
        fun onPlayerStateChanged(state: PlayerState)
        fun onPlayerPositionUpdated(position: Double, duration: Double)
        fun onPlayerError(errorCode: Int, errorMessage: String)  // 添加 errorCode 参数
        // 新增：错误是否可恢复（可重试/切源）
        fun onPlayerErrorWithRecoverability(errorCode: Int, errorMessage: String, recoverable: Boolean) {}
        // 网络抖动/缓冲中状态变化（默认空实现，兼容旧代码）
        fun onPlayerLoadingChanged(isLoading: Boolean) {}
        // 弱网 QoE 指标：当前卡顿时长/累计卡顿时长/自动恢复次数
        fun onNetworkQoEUpdated(currentStallMs: Long, totalStallMs: Long, reconnectCount: Int) {}
        // seek 进入可用状态（SDK 侧统一结算）后回调，业务层可据此收敛 loading / 更新 UI 状态。
        fun onSeekCompleted(
            requestId: Long,
            targetPosition: Double,
            currentPosition: Double,
            elapsedMs: Long,
            byTimeout: Boolean
        ) {}
    }

    /**
     * 播放完成回调接口。
     * 视频播放到末尾自然结束时触发，在主线程回调。
     */
    interface PlaybackCompletedCallback {
        fun onPlaybackCompleted()
    }

    /**
     * 统一状态快照回调（主线程）。
     */
    fun interface PlaybackSnapshotCallback {
        fun onPlaybackSnapshotChanged(snapshot: PlaybackSnapshot)
    }

    /**
     * Java 友好的统一状态监听。业务侧可直接消费字段，无需反射读取 Kotlin data class。
     *
     * SDK 保证该回调与 [PlaybackSnapshotCallback] 使用同一份快照语义，并在主线程触发。
     */
    interface PlaybackSnapshotListener {
        fun onPlaybackSnapshotChanged(
            state: PlayerState,
            pipelineState: PipelineState,
            playWhenReady: Boolean,
            isPlaying: Boolean,
            isLoading: Boolean,
            position: Double,
            duration: Double,
            shouldShowPlayingUi: Boolean,
            updatedAtMs: Long
        )
    }

    /**
     * 异步 [playWithModelAsync] / [openWithPlayModelAsync] 结果回调。
     * 使用 SAM，便于 Java 调用方避免 Kotlin `Unit` 与 Java `void` 类型不兼容。
     */
    fun interface PlayModelAsyncCallback {
        fun onResult(accepted: Boolean)
    }

    private var nativeHandle: Long = 0
    private var callback: PlayerCallback? = null
    private var completedCallback: PlaybackCompletedCallback? = null
    private var playbackSnapshotCallback: PlaybackSnapshotCallback? = null
    private var playbackSnapshotListener: PlaybackSnapshotListener? = null
    private var lastPlayerState: PlayerState? = null
    private var lastPipelineState: PipelineState? = null
    private var lastIsPlayingState: Boolean? = null
    private var lastPlaybackSnapshot: PlaybackSnapshot? = null
    private var sdkDiagVersionLogged: Boolean = false
    private var updateExecutor: ScheduledExecutorService? = null
    private var lastLoadingState: Boolean? = null
    private var loadingCandidateState: Boolean? = null
    private var loadingCandidateSinceMs: Long = 0L
    private val openExecutor = Executors.newSingleThreadExecutor()
    private val playbackCommandGeneration = AtomicLong(0L)
    private val playbackCommandAppliedGeneration = AtomicLong(0L)
    @Volatile
    private var desiredPlayWhenReady: Boolean = false
    private val mainHandler = Handler(Looper.getMainLooper())
    private val loadingShowDebounceMs = 450L
    private val loadingHideDebounceMs = 150L
    // 播放中若位置持续前进，抑制瞬时 loading=true，避免 UI 闪一下。
    private val loadingShowProgressSuppressWindowMs = 1200L
    private val loadingProgressSuppressMinStepSec = 0.04
    // Seek 场景优先保证“先隐藏 loading 再恢复播放”，避免拖慢体感。
    private val seekLoadingHideDebounceMs = 0L
    private val seekLoadingReshowSuppressMs = 900L
    private val seekDetectMinJumpSec = 1.0
    // 首开时 loading=false 可能早于首帧真正上屏，增加保护窗口避免黑闪。
    private val openLoadingHideMinDelayMs = 700L
    private val openLoadingHideHardMaxDelayMs = 1800L
    private val openLoadingHideMinPosDeltaSec = 0.06
    private var openLoadingHideProtectUntilMs: Long = 0L
    private var openLoadingHideHardDeadlineMs: Long = 0L
    private var openLoadingGuardStartPosSec: Double = -1.0
    private var loadingSessionLikelySeek: Boolean = false
    private var suppressLoadingShowUntilMs: Long = 0L
    private var lastPositionForLoadingHeuristicSec: Double = Double.NaN
    private var lastBackwardJumpLogAtMs: Long = 0L
    private val seekCompletionTimeoutMs = 7600L
    // Native seek session occasionally stays active while position has already converged.
    // Keep SDK completion bounded to avoid long loading tail and follow-up play deadlocks.
    private val seekCompletionNativeConvergedWatchdogMs = 10500L
    private val seekCompletionConvergedStableMs = 420L
    private val seekCompletionNearTargetSec = 1.5
    private val seekCompletionMovedFromOldSec = 0.9
    private val seekCompletionPostConfirmWindowMs = 900L
    private val seekCompletionUnhealthyPostConfirmWindowMs = 2600L
    private val seekCompletionPostConfirmMinProgressSec = 0.12
    private val seekPostConfirmReopenBackoffSec = 0.30
    private val seekPostConfirmReopenCooldownMs = 5000L
    private var playStallRecoveryEnabled = true
    private var playLoopRecoveryEnabled = true
    private var playbackMetricsLogEnabled = true
    private var manualPlayHardRecoverEnabled = true
    private var playStallDetectDelayMs = 1700L
    private var playStallRetryDelayMs = 1200L
    private var playStallPausedStateDetectDelayMs = 1100L
    private var playStallLoadingDetectDelayMs = 2600L
    private var playStallMinProgressSec = 0.20
    private var playStallRecoverReseekBackSec = 0.30
    private var playStallRecoverCooldownMs = 5000L
    private var playLoopBackwardThresholdSec = 2.2
    private var playLoopMinForwardSpanSec = 6.0
    private var playLoopRequiredHits = 3
    private var playLoopWindowMs = 12000L
    private var playLoopRecoverCooldownMs = 15000L
    private var playLoopRecoverBackoffSec = 0.20
    private var manualPlayHardRecoverDelayMs = 3200L
    private var manualPlayHardRecoverCooldownMs = 8000L
    private var manualPlayHardRecoverMinProgressSec = 0.10
    private var pendingSeekRequestId: Long = 0L
    private var pendingSeekTargetSec: Double = Double.NaN
    private var pendingSeekFromSec: Double = Double.NaN
    private var pendingSeekStartAtMs: Long = 0L
    private var pendingSeekLoadingObserved: Boolean = false
    private var pendingSeekActive: Boolean = false
    private var pendingSeekConvergedSinceMs: Long = 0L
    private var pendingSeekPostConfirmActive: Boolean = false
    private var pendingSeekPostConfirmStartAtMs: Long = 0L
    private var pendingSeekPostConfirmBasePosSec: Double = Double.NaN
    private var pendingSeekHadPostConfirm: Boolean = false
    private var pendingSeekPostConfirmTimedOut: Boolean = false
    private var pendingSeekTriggeredReopen: Boolean = false
    private var lastSeekPostConfirmReopenAtMs: Long = 0L
    private var pendingSeekLastWatchdogLogAtMs: Long = 0L
    // 诊断：量化 native seek 结束到 UI loading 隐藏的间隔。
    private var seekLoadingSyncRequestId: Long = -1L
    private var seekNativeInactiveAtMs: Long = 0L
    private var seekNativeInactivePosSec: Double = Double.NaN
    private var seekLoadingSyncGapLogged: Boolean = false
    private var reopenUiAnchorActive: Boolean = false
    private var reopenUiAnchorPosSec: Double = Double.NaN
    private var reopenUiAnchorUntilMs: Long = 0L
    private var reopenUiAnchorArmedAtMs: Long = 0L
    private val reopenUiAnchorMaxHoldMs = 2600L
    private val reopenUiAnchorMinHoldMs = 900L
    private val reopenUiAnchorZeroGuardSec = 1.2
    private val reopenUiAnchorReleaseNearSec = 1.5
    private var playStallCheckArmed = false
    private var playStallArmedAtMs: Long = 0L
    private var playStallBasePosSec: Double = Double.NaN
    private var playStallRecoverStage: Int = 0
    private var playStallLastRecoverAtMs: Long = 0L
    private var playStallLastPlayReqAtMs: Long = 0L
    private var manualPlayHardRecoverPending = false
    private var manualPlayHardRecoverArmedAtMs: Long = 0L
    private var manualPlayHardRecoverBasePosSec: Double = Double.NaN
    private var manualPlayHardRecoverLastAtMs: Long = 0L
    private var playLoopMaxPosSec: Double = Double.NaN
    private var playLoopAnchorPosSec: Double = Double.NaN
    private var playLoopHitCount: Int = 0
    private var playLoopWindowStartMs: Long = 0L
    private var playLoopLastRecoverAtMs: Long = 0L
    private var playLoopSuppressUntilMs: Long = 0L
    private var metricsLastLogAtMs: Long = 0L
    private val metricsLogIntervalMs: Long = 30000L
    private var metricsSeekCompletedCount: Long = 0L
    private var metricsSeekCompletedTimeoutCount: Long = 0L
    private var metricsPlayStallRecoverSeekCount: Long = 0L
    private var metricsPlayStallRecoverReopenCount: Long = 0L
    private var metricsPlayLoopRecoverCount: Long = 0L
    private var metricsManualPlayHardRecoverCount: Long = 0L
    // Minimal diagnostics for seek-settle state mismatch (debug level only).
    private var seekSettleDiagUntilMs: Long = 0L
    private var seekSettleDiagRequestId: Long = -1L
    private var seekSettleDiagLogged: Boolean = false
    private var seekSettleDiagStartPosSec: Double = Double.NaN
    private val lifecycleLock = Any()
    // 串行化 open/reopen/stop，避免与 release 并发触发 native 资源竞态。
    private val openSerialLock = Any()
    @Volatile private var isReleased = false
    private var autoReopenOnRecoverableErrorEnabled: Boolean = false
    private var autoReopenMaxAttempts: Int = 1
    private var autoReopenAttemptCount: Int = 0
    private var autoReopenInFlight: Boolean = false
    @Volatile private var secondarySyncMode: Boolean = false
    private var networkLoadingSinceMs: Long = 0L
    private var networkTotalStallMs: Long = 0L
    private var networkReconnectCount: Int = 0
    private val openSessionLoopSuppressMs = 2500L
    private val positionDurationClampEpsilonSec = 0.20
    private val playbackEndClampThresholdSec = 0.80
    @Volatile private var preferredPlaybackRate: Float = 1.0f
    private var cachedLogLevel: Int = 1
    private var cachedLogLevelAtMs: Long = 0L
    private var lastOpenUrl: String? = null
    private var lastOpenStartPosition: Double = 0.0
    private var lastOpenPlayModel: PlayerDataSourcePlayModel? = null
    private var currentSourceCategory: String = "unknown"
    @Volatile private var decodeMode: DecodeMode = DecodeMode.HARDWARE
    private var decodeFallbackTriedForCurrentSource: Boolean = false
    private var decodeFallbackLastAtMs: Long = 0L
    private val secureHlsAuthUrl = "https://console-api.huaxiacloud.net/third_party/verify/sign"

    /** TextureView 模式下由我方从 [SurfaceTexture] 创建的包装 Surface，需在适当时机 [Surface.release] */
    private var textureDecoderSurface: Surface? = null

    /**
     * 用于承载解码输出的视图（[SurfaceView] 或 [TextureView]），请加入布局。
     */
    val renderView: View

    /**
     * 与 [renderView] 为同一实例；保留该属性以兼容旧代码（类型由 [SurfaceView] 变为通用 [View]）。
     */
    val videoView: View
        get() = renderView
    init {
        nativeHandle = nativeCreate()
        applySecureSeekPreset(SecureSeekPreset.PRECISION_FIRST)
        renderView = createRenderView()
        startPositionUpdates()
    }

    // 设置回调
    fun setCallback(callback: PlayerCallback) {
        this.callback = callback
        dispatchCurrentStateSnapshot(callback)
    }

    /**
     * 设置播放完成回调。
     * 视频自然播放到末尾时在主线程触发 [PlaybackCompletedCallback.onPlaybackCompleted]。
     */
    fun setPlaybackCompletedCallback(callback: PlaybackCompletedCallback?) {
        this.completedCallback = callback
    }

    /**
     * 设置统一状态快照回调。
     */
    fun setPlaybackSnapshotCallback(callback: PlaybackSnapshotCallback?) {
        this.playbackSnapshotCallback = callback
        callback ?: return
        mainHandler.post {
            if (isReleased || this.playbackSnapshotCallback !== callback) return@post
            callback.onPlaybackSnapshotChanged(getPlaybackSnapshot())
        }
    }

    /**
     * 设置 Java 友好的统一状态监听。
     *
     * 新业务建议优先使用该接口；旧 [PlaybackSnapshotCallback] 保留兼容。
     */
    fun setPlaybackSnapshotListener(listener: PlaybackSnapshotListener?) {
        this.playbackSnapshotListener = listener
        listener ?: return
        mainHandler.post {
            if (isReleased || this.playbackSnapshotListener !== listener) return@post
            dispatchPlaybackSnapshotToListener(listener, getPlaybackSnapshot())
        }
    }

    /**
     * 查询当前统一状态快照（线程安全，实时计算）。
     */
    fun getPlaybackSnapshot(): PlaybackSnapshot {
        val handle = currentHandle()
        val now = SystemClock.elapsedRealtime()
        if (handle == 0L || isReleased) {
            return PlaybackSnapshot(
                state = PlayerState.IDLE,
                pipelineState = PipelineState.IDLE,
                playWhenReady = false,
                isPlaying = false,
                isLoading = false,
                position = 0.0,
                duration = 0.0,
                shouldShowPlayingUi = false,
                updatedAtMs = now
            )
        }
        val rawPosition = nativeGetPosition(handle)
        val duration = nativeGetDuration(handle)
        val coreStateRaw = nativeGetState(handle)
        val pipelineStateRaw = nativeGetPipelineState(handle)
        val playWhenReady = nativeGetPlayWhenReady(handle)
        val isPlaying = nativeIsPlaying(handle)
        val loading = isLoading()
        val resolved = coerceStateWithLoading(
            resolveUnifiedState(coreStateRaw, pipelineStateRaw, playWhenReady, isPlaying),
            loading
        )
        return buildPlaybackSnapshot(
            state = resolved,
            pipeline = mapPipelineState(pipelineStateRaw),
            playWhenReady = playWhenReady,
            isPlayingNow = isPlaying,
            loading = loading,
            position = rawPosition,
            duration = duration,
            nowMs = now
        )
    }

    /**
     * 配置“可恢复错误后自动重开”能力（默认关闭）。
     */
    fun configureWeakNetworkRecovery(enabled: Boolean, maxAttempts: Int = 1) {
        autoReopenOnRecoverableErrorEnabled = enabled
        autoReopenMaxAttempts = maxAttempts.coerceAtLeast(0)
    }

    /**
     * 配置播放稳定性策略开关（建议灰度发布时使用）。
     */
    fun configurePlaybackStability(
        enablePlayStallRecovery: Boolean = true,
        enablePlaybackLoopRecovery: Boolean = true,
        enableMetricsLog: Boolean = true
    ) {
        playStallRecoveryEnabled = enablePlayStallRecovery
        playLoopRecoveryEnabled = enablePlaybackLoopRecovery
        playbackMetricsLogEnabled = enableMetricsLog
    }

    /**
     * 配置“回环重复播放”守卫阈值。
     */
    fun configurePlaybackLoopGuard(
        backwardThresholdSec: Double = 2.2,
        minForwardSpanSec: Double = 6.0,
        requiredHits: Int = 3,
        windowMs: Long = 12000L,
        recoverCooldownMs: Long = 15000L,
        recoverBackoffSec: Double = 0.20
    ) {
        playLoopBackwardThresholdSec = backwardThresholdSec.coerceAtLeast(0.8)
        playLoopMinForwardSpanSec = minForwardSpanSec.coerceAtLeast(playLoopBackwardThresholdSec + 1.5)
        playLoopRequiredHits = requiredHits.coerceIn(2, 5)
        playLoopWindowMs = windowMs.coerceAtLeast(4000L)
        playLoopRecoverCooldownMs = recoverCooldownMs.coerceAtLeast(5000L)
        playLoopRecoverBackoffSec = recoverBackoffSec.coerceIn(0.05, 1.50)
    }

    private fun clonePlayModel(model: PlayerDataSourcePlayModel): PlayerDataSourcePlayModel {
        return PlayerDataSourcePlayModel().apply {
            url = model.url
            mode = model.mode
            encryptedFile = model.encryptedFile
            video = model.video?.let { source ->
                PlayerVideo().apply {
                    videoId = source.videoId
                    sign = source.sign
                    secretId = source.secretId
                    timestamp = source.timestamp
                    appId = source.appId
                }
            }
        }
    }

    private fun notifyNetworkQoE(currentStallMs: Long) {
        callback?.onNetworkQoEUpdated(currentStallMs, networkTotalStallMs, networkReconnectCount)
    }

    private fun dispatchPlaybackSnapshot(snapshot: PlaybackSnapshot) {
        playbackSnapshotCallback?.onPlaybackSnapshotChanged(snapshot)
        playbackSnapshotListener?.let { listener ->
            dispatchPlaybackSnapshotToListener(listener, snapshot)
        }
    }

    private fun dispatchPlaybackSnapshotToListener(
        listener: PlaybackSnapshotListener,
        snapshot: PlaybackSnapshot
    ) {
        listener.onPlaybackSnapshotChanged(
            snapshot.state,
            snapshot.pipelineState,
            snapshot.playWhenReady,
            snapshot.isPlaying,
            snapshot.isLoading,
            snapshot.position,
            snapshot.duration,
            snapshot.shouldShowPlayingUi,
            snapshot.updatedAtMs
        )
    }

    private fun currentHandle(): Long {
        synchronized(lifecycleLock) {
            return nativeHandle
        }
    }

    private fun getCachedLogLevel(): Int {
        val now = SystemClock.elapsedRealtime()
        if ((now - cachedLogLevelAtMs) <= 1500L) {
            return cachedLogLevel
        }
        cachedLogLevel = try {
            getLogLevel()
        } catch (_: Throwable) {
            cachedLogLevel
        }
        cachedLogLevelAtMs = now
        return cachedLogLevel
    }

    private fun canEmitInfoLog(): Boolean {
        return try {
            getCachedLogLevel() <= 1
        } catch (_: Throwable) {
            false
        }
    }

    private fun logInfo(message: String) {
        if (canEmitInfoLog()) {
            Log.i(TAG, message)
        }
    }

    private fun canEmitDebugDiagLog(): Boolean {
        return try {
            getCachedLogLevel() == 0
        } catch (_: Throwable) {
            false
        }
    }

    private fun shouldEmitCompletedToApp(
        state: PlayerState,
        playWhenReady: Boolean
    ): Boolean {
        // Mature player rule: completion is terminal and should not be emitted
        // during OPENING/LOADING while autoplay intent is still active.
        val inOpeningWindow = state == PlayerState.OPENING || state == PlayerState.LOADING
        return !(inOpeningWindow && playWhenReady)
    }

    /**
     * When UI page (re)binds callback (e.g. entering from floating window),
     * immediately push current player snapshot so UI buttons don't rely on stale state.
     */
    private fun dispatchCurrentStateSnapshot(target: PlayerCallback) {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        val loading = isLoading()
        val state = coerceStateWithLoading(getState(), loading)
        val position = applyUiPositionAnchorIfNeeded(
            rawPositionSec = getPosition(),
            loading = loading,
            state = state,
            nowMs = SystemClock.elapsedRealtime()
        )
        val duration = getDuration()
        val now = SystemClock.elapsedRealtime()
        val pipeline = getPipelineState()
        val playWhenReady = getPlayWhenReady()
        val checkIsPlaying = isPlaying()
        val snapshot = buildPlaybackSnapshot(
            state = state,
            pipeline = pipeline,
            playWhenReady = playWhenReady,
            isPlayingNow = checkIsPlaying,
            loading = loading,
            position = position,
            duration = duration,
            nowMs = now
        )
        lastPlaybackSnapshot = snapshot
        lastPlayerState = state
        lastLoadingState = loading
        loadingCandidateState = loading
        loadingCandidateSinceMs = now
        mainHandler.post {
            if (isReleased || this.callback !== target) return@post
            target.onPlayerStateChanged(state)
            if (duration > 0) {
                target.onPlayerPositionUpdated(position, duration)
            }
            target.onPlayerLoadingChanged(loading)
            dispatchPlaybackSnapshot(snapshot)
        }
    }

    private fun armOpenLoadingHideGuard() {
        val now = SystemClock.elapsedRealtime()
        openLoadingHideProtectUntilMs = now + openLoadingHideMinDelayMs
        openLoadingHideHardDeadlineMs = now + openLoadingHideHardMaxDelayMs
        openLoadingGuardStartPosSec = getPosition()
    }

    private fun armReopenUiAnchorIfNeeded(startPositionSec: Double) {
        if (!startPositionSec.isFinite() || startPositionSec <= reopenUiAnchorZeroGuardSec) {
            reopenUiAnchorActive = false
            reopenUiAnchorPosSec = Double.NaN
            reopenUiAnchorUntilMs = 0L
            reopenUiAnchorArmedAtMs = 0L
            return
        }
        val now = SystemClock.elapsedRealtime()
        reopenUiAnchorActive = true
        reopenUiAnchorPosSec = startPositionSec
        reopenUiAnchorUntilMs = now + reopenUiAnchorMaxHoldMs
        reopenUiAnchorArmedAtMs = now
        logInfo("evt=reopen_ui_anchor_arm start=$startPositionSec hold_ms=$reopenUiAnchorMaxHoldMs")
    }

    private fun releaseReopenUiAnchor(reason: String) {
        if (!reopenUiAnchorActive) return
        reopenUiAnchorActive = false
        reopenUiAnchorPosSec = Double.NaN
        reopenUiAnchorUntilMs = 0L
        reopenUiAnchorArmedAtMs = 0L
        logInfo("evt=reopen_ui_anchor_release reason=$reason")
    }

    private fun applyUiPositionAnchorIfNeeded(
        rawPositionSec: Double,
        loading: Boolean,
        state: PlayerState,
        nowMs: Long
    ): Double {
        val seekUiPosition = resolveSeekUiPositionMask(rawPositionSec, loading, state)
        if (seekUiPosition != null) {
            return seekUiPosition
        }
        if (!reopenUiAnchorActive) return rawPositionSec
        val anchor = reopenUiAnchorPosSec
        if (!anchor.isFinite()) {
            releaseReopenUiAnchor("anchor_nan")
            return rawPositionSec
        }
        if (nowMs >= reopenUiAnchorUntilMs) {
            releaseReopenUiAnchor("timeout")
            return rawPositionSec
        }
        if (!rawPositionSec.isFinite()) {
            return anchor
        }
        val recovering = loading || state == PlayerState.OPENING || state == PlayerState.LOADING
        val holdElapsedMs = if (reopenUiAnchorArmedAtMs > 0L) {
            nowMs - reopenUiAnchorArmedAtMs
        } else {
            reopenUiAnchorMaxHoldMs
        }
        if (holdElapsedMs < reopenUiAnchorMinHoldMs) {
            return anchor
        }
        if (abs(rawPositionSec - anchor) <= reopenUiAnchorReleaseNearSec || rawPositionSec > anchor) {
            releaseReopenUiAnchor("near_anchor")
            return rawPositionSec
        }
        if (recovering && rawPositionSec <= reopenUiAnchorZeroGuardSec) {
            // Reopen path can transiently report near-zero position before secure open applies start pos.
            // Keep UI position anchored to avoid "jump to 0 then back to target".
            return anchor
        }
        return rawPositionSec
    }

    private fun resolveSeekUiPositionMask(
        rawPositionSec: Double,
        loading: Boolean,
        state: PlayerState
    ): Double? {
        if (!pendingSeekActive) return null
        val target = pendingSeekTargetSec
        if (!target.isFinite() || target < 0.0) return null
        val seekLoadingUi = loading || state == PlayerState.LOADING || state == PlayerState.OPENING
        if (!seekLoadingUi) return null

        // Match mature players: while seek loading is visible, expose the pending seek
        // position to UI and keep the internal clock/free-frame catchup private.
        return target
    }

    // ========== iOS 对齐属性（property 风格 setter/getter）==========

    /**
     * 起始播放位置（秒），对齐 iOS `startPosition` property。
     * 在 [playURL]/[playWithModel] 之前设置，打开时生效。
     */
    private var pendingStartPosition: Double = 0.0

    fun setStartPosition(position: Double) {
        pendingStartPosition = maxOf(0.0, position)
    }

    fun getStartPosition(): Double = pendingStartPosition

    /**
     * 是否在打开成功后自动播放，对齐 iOS `autoPlayer` property，默认 true。
     * 设置为 false 时 [playURL]/[playWithModel] 打开后保持暂停，需手动调 [play]。
     */
    private var autoPlayer: Boolean = true

    fun setAutoPlayer(auto: Boolean) {
        autoPlayer = auto
    }

    fun isAutoPlayer(): Boolean = autoPlayer

    // ========== iOS 对齐方法 ==========

    /**
     * 打开 URL（对齐 iOS `playURL:`）。
     * 按 [autoPlayer] 决定是否自动播放，使用 [pendingStartPosition] 作为起始位置。
     */
    fun playURL(url: String): Boolean {
        val pos = pendingStartPosition
        pendingStartPosition = 0.0  // 消费后重置，防止下次意外复用
        val ok = openURL(url, pos)
        if (ok && autoPlayer) play()
        return ok
    }

    /**
     * 使用播放模型打开（对齐 iOS `playWithModel:`）。
     * 按 [autoPlayer] 决定是否自动播放，使用 [pendingStartPosition] 作为起始位置。
     */
    fun playWithModel(model: PlayerDataSourcePlayModel): Boolean {
        val pos = pendingStartPosition
        pendingStartPosition = 0.0  // 消费后重置
        val ok = openWithPlayModel(model, pos)
        if (ok && autoPlayer) play()
        return ok
    }

    /**
     * 跳转到指定位置（秒），对齐 iOS `seekToPosition:`。
     */
    fun seekToPosition(position: Double) = seekTo(position)

    /**
     * 重新播放（对齐 iOS `replay`）。
     * 从头（position=0）重新打开最后一次的 URL/Model。
     */
    fun replay() = replayFrom(0.0)

    /**
     * 从指定位置重新播放（对齐业务层 `replayFrom(startPositionSec)`）。
     * 重新打开最后一次的 URL/Model，从 [startPositionSec] 秒开始。
     */
    fun replayFrom(startPositionSec: Double) {
        val safeStart = maxOf(0.0, startPositionSec)
        armReopenUiAnchorIfNeeded(safeStart)
        val replayModel = lastOpenPlayModel
        val replayUrl = lastOpenUrl
        when {
            replayModel != null -> {
                openWithPlayModel(replayModel, safeStart)
                if (autoPlayer) play()
            }
            !replayUrl.isNullOrBlank() -> {
                openURL(replayUrl, safeStart)
                if (autoPlayer) play()
            }
        }
    }

    /**
     * [openWithPlayModel] 带起始位置重载（支持 [playWithModel] 传入 startPosition）。
     */
    fun openWithPlayModel(model: PlayerDataSourcePlayModel, startPosition: Double): Boolean {
        return synchronized(openSerialLock) {
            val handle = currentHandle()
            if (handle == 0L || isReleased) {
                dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "播放器已释放，无法打开播放模型")
                return@synchronized false
            }
            if (!licenseAllowedOrNotify("openWithPlayModel")) return@synchronized false
            val workingModel = clonePlayModel(model)
            val cfg = effectiveDataSourceConfig()
            var secureHeaders: String? = null
            if (workingModel.video != null) {
                try {
                    val authResult = performSecureHlsAuth(cfg, workingModel.video!!)
                    workingModel.url = authResult.playUrl
                    workingModel.encryptedFile = authResult.encrypted
                    secureHeaders = authResult.secureHeaders
                    val needsSecure = authResult.encrypted || secureHeaders.isNullOrBlank().not()
                    if (needsSecure) {
                        workingModel.mode = PlayerDataSourceMode.SECURE_HLS
                    } else if (workingModel.mode == PlayerDataSourceMode.SECURE_HLS &&
                        !workingModel.url.contains(".m3u8", ignoreCase = true)
                    ) {
                        workingModel.mode = PlayerDataSourceMode.DEFAULT
                    }
                } catch (e: Exception) {
                    dispatchError(PlayerErrorCode.SECURE_AUTH_FAILED, e.message ?: "SecureHLS 鉴权失败")
                    return@synchronized false
                }
            }
            if (workingModel.url.isBlank()) {
                dispatchError(PlayerErrorCode.INVALID_URL, "播放参数无效：video 和 url 不能同时为空")
                return@synchronized false
            }
            validatePlayModelInput(workingModel)?.let { reason ->
                dispatchError(PlayerErrorCode.INVALID_URL, reason)
                return@synchronized false
            }

            lastOpenUrl = workingModel.url
            lastOpenStartPosition = maxOf(0.0, startPosition)
            lastOpenPlayModel = clonePlayModel(workingModel)
            currentSourceCategory = resolveSourceCategory(
                url = workingModel.url,
                mode = workingModel.mode,
                encryptedFile = workingModel.encryptedFile
            )
            decodeFallbackTriedForCurrentSource = false
            logInfo("evt=open_source_classify category=$currentSourceCategory mode=${workingModel.mode} encrypted=${workingModel.encryptedFile} start_pos=$lastOpenStartPosition")
            resetPlaybackHealthStateForOpen("openWithPlayModel", lastOpenStartPosition)
            if (!autoReopenInFlight) {
                autoReopenAttemptCount = 0
                networkTotalStallMs = 0L
                networkReconnectCount = 0
            }
            applyDecodeModeForHandle(handle)
            val result = when (workingModel.mode) {
                PlayerDataSourceMode.DEFAULT ->
                    nativeOpenURLWithStartPosition(handle, workingModel.url, lastOpenStartPosition)
                PlayerDataSourceMode.CUSTOM_HTTP -> {
                    nativeOpenWithCustomHTTP(handle, workingModel.url, cfg.timeoutMs, cfg.maxRetries, workingModel.encryptedFile)
                }
                PlayerDataSourceMode.CUSTOM_FILE -> {
                    nativeOpenWithCustomFile(handle, workingModel.url, cfg.avioBufferSize, workingModel.encryptedFile)
                }
                PlayerDataSourceMode.SECURE_HLS -> {
                    val video = workingModel.video
                    nativeOpenWithSecureSession(
                        handle = handle,
                        url = workingModel.url,
                        startPosition = lastOpenStartPosition,
                        authToken = null,
                        videoId = video?.videoId,
                        deviceId = null,
                        secretId = video?.secretId,
                        nonce = null,
                        playSessionId = null,
                        secureHeaders = secureHeaders,
                        sessionExpireAtMs = 0L,
                        keyMode = 0,
                        keyMaterialB64 = null,
                        keyIvHex = null
                    )
                }
            }
            if (!result) {
                openLoadingHideProtectUntilMs = 0L
                openLoadingHideHardDeadlineMs = 0L
                openLoadingGuardStartPosSec = -1.0
                dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "打开失败: mode=${workingModel.mode}, url=${workingModel.url}")
            } else {
                armOpenLoadingHideGuard()
            }
            result
        }
    }

    private fun validatePlayModelInput(model: PlayerDataSourcePlayModel): String? {
        if (model.url.isBlank()) {
            return "播放地址为空"
        }
        return when (model.mode) {
            PlayerDataSourceMode.CUSTOM_FILE -> validateLocalPathStrict(model.url, "CustomFile")
            PlayerDataSourceMode.DEFAULT -> validateLocalPathIfNeeded(model.url)
            PlayerDataSourceMode.CUSTOM_HTTP,
            PlayerDataSourceMode.SECURE_HLS -> null
        }
    }

    private fun validateLocalPathIfNeeded(path: String): String? {
        if (!looksLikeLocalPath(path)) {
            return null
        }
        return validateLocalPathStrict(path, "Default")
    }

    private fun validateLocalPathStrict(path: String, scene: String): String? {
        val normalized = normalizeLocalPath(path)
        if (normalized.isBlank()) {
            return "$scene 本地路径为空"
        }
        if (normalized.startsWith("content://")) {
            return null
        }
        val file = File(normalized)
        if (!file.exists() || !file.isFile || file.length() <= 0L) {
            return "$scene 本地文件不可用: $normalized"
        }
        return null
    }

    private fun looksLikeLocalPath(path: String): Boolean {
        if (path.isBlank()) return false
        val lower = path.lowercase()
        if (lower.startsWith("http://") || lower.startsWith("https://") || lower.startsWith("hxc://")) {
            return false
        }
        return lower.startsWith("file://")
            || lower.startsWith("content://")
            || lower.startsWith("/")
            || (path.length > 2 && path[1] == ':' && path[0].isLetter())
    }

    private fun normalizeLocalPath(path: String): String {
        return if (path.startsWith("file://")) path.removePrefix("file://") else path
    }

    private fun resolveSourceCategory(url: String, mode: PlayerDataSourceMode, encryptedFile: Boolean): String {
        val normalized = url.trim().lowercase()
        val isLocal = looksLikeLocalPath(url)
        val isM3u8 = normalized.contains(".m3u8")
        return when {
            mode == PlayerDataSourceMode.SECURE_HLS -> "secure_hls"
            mode == PlayerDataSourceMode.CUSTOM_FILE -> "local_file_custom"
            isLocal && isM3u8 && encryptedFile -> "local_hls_encrypted"
            isLocal && isM3u8 -> "local_hls_plain"
            isLocal && encryptedFile -> "local_file_encrypted"
            isLocal -> "local_file_plain"
            encryptedFile -> "remote_encrypted"
            else -> "remote_plain"
        }
    }

    fun setDecodeMode(mode: DecodeMode) {
        decodeMode = mode
        val handle = currentHandle()
        if (handle != 0L && !isReleased) {
            nativeSetDecodeMode(handle, if (mode == DecodeMode.HARDWARE) 1 else 0)
        }
    }

    fun getDecodeMode(): DecodeMode = decodeMode

    private fun maybeFallbackToSoftwareDecode(
        reason: String,
        nowMs: Long,
        positionSec: Double,
        state: PlayerState,
        loading: Boolean,
        isPlayingNow: Boolean
    ): Boolean {
        if (decodeFallbackTriedForCurrentSource) return false
        if (decodeMode != DecodeMode.HARDWARE) return false
        if (currentSourceCategory != "remote_plain") return false
        val handle = currentHandle()
        if (handle == 0L || isReleased) return false
        if (!nativeIsHardwareDecodingActive(handle)) return false
        val likelyFrozen = loading || state == PlayerState.LOADING || !isPlayingNow
        if (!likelyFrozen) return false

        decodeFallbackTriedForCurrentSource = true
        decodeFallbackLastAtMs = nowMs
        decodeMode = DecodeMode.SOFTWARE
        nativeSetDecodeMode(handle, 0)
        Log.w(
            TAG,
            "evt=decode_fallback_to_software reason=$reason source_category=$currentSourceCategory " +
                "pos=$positionSec state=${state.name} loading=$loading is_playing=$isPlayingNow"
        )
        return true
    }

    private fun applyDecodeModeForHandle(handle: Long) {
        nativeSetDecodeMode(handle, if (decodeMode == DecodeMode.HARDWARE) 1 else 0)
    }

    /**
     * 应用 SecureHLS seek 调参（对当前 player 实例立即生效）。
     */
    fun setSecureSeekTuning(config: SecureSeekTuningConfig) {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        nativeSetSecureSeekTuning(
            handle = handle,
            dropOnlyWindowBackwardSec = config.dropOnlyWindowBackwardSec,
            dropOnlyWindowForwardSec = config.dropOnlyWindowForwardSec,
            acceptFutureBackwardEarlySec = config.acceptFutureBackwardEarlySec,
            acceptFutureForwardEarlySec = config.acceptFutureForwardEarlySec,
            acceptFutureBackwardMidSec = config.acceptFutureBackwardMidSec,
            acceptFutureForwardMidSec = config.acceptFutureForwardMidSec,
            acceptFutureBackwardLateSec = config.acceptFutureBackwardLateSec,
            acceptFutureForwardLateSec = config.acceptFutureForwardLateSec,
            lowerBoundDeadlineNormalMs = config.lowerBoundDeadlineNormalMs,
            lowerBoundDeadlineLargeMs = config.lowerBoundDeadlineLargeMs,
            recoveryDeadlineNormalMs = config.recoveryDeadlineNormalMs,
            recoveryDeadlineLargeMs = config.recoveryDeadlineLargeMs,
            audioWaitDeadlineNormalMs = config.audioWaitDeadlineNormalMs,
            audioWaitDeadlineLargeMs = config.audioWaitDeadlineLargeMs
        )
    }

    /**
     * 应用 SecureHLS seek 预设参数。
     * 默认推荐 [SecureSeekPreset.PRECISION_FIRST]。
     */
    fun applySecureSeekPreset(preset: SecureSeekPreset = SecureSeekPreset.PRECISION_FIRST) {
        val config = when (preset) {
            SecureSeekPreset.PRECISION_FIRST -> SecureSeekTuningConfig.defaultConfig().apply {
                // 默认即精准优先，这里显式设置以便后续维护可读
                dropOnlyWindowBackwardSec = 5.0
                dropOnlyWindowForwardSec = 8.0
                acceptFutureBackwardEarlySec = 2.5
                acceptFutureForwardEarlySec = 4.0
                acceptFutureBackwardMidSec = 6.0
                acceptFutureForwardMidSec = 8.0
                acceptFutureBackwardLateSec = 10.0
                acceptFutureForwardLateSec = 14.0
                lowerBoundDeadlineNormalMs = 2700
                lowerBoundDeadlineLargeMs = 3200
                recoveryDeadlineNormalMs = 5200
                recoveryDeadlineLargeMs = 6400
                audioWaitDeadlineNormalMs = 4400
                audioWaitDeadlineLargeMs = 5600
            }
            SecureSeekPreset.PRECISION_STABLE_BACKWARD -> SecureSeekTuningConfig.defaultConfig().apply {
                // 平衡档（偏速度）：
                // 1) 仍保持 backward 稳定性，但适度放宽接受窗口
                // 2) 缩短 deadline，降低长 loading 时长
                dropOnlyWindowBackwardSec = 4.5
                dropOnlyWindowForwardSec = 8.0
                acceptFutureBackwardEarlySec = 2.8
                acceptFutureForwardEarlySec = 5.0
                acceptFutureBackwardMidSec = 6.5
                acceptFutureForwardMidSec = 10.0
                acceptFutureBackwardLateSec = 9.5
                acceptFutureForwardLateSec = 15.5
                lowerBoundDeadlineNormalMs = 2600
                lowerBoundDeadlineLargeMs = 3200
                recoveryDeadlineNormalMs = 4800
                recoveryDeadlineLargeMs = 6200
                audioWaitDeadlineNormalMs = 3800
                audioWaitDeadlineLargeMs = 5200
            }
            SecureSeekPreset.SPEED_FIRST -> SecureSeekTuningConfig.defaultConfig().apply {
                // 更快恢复：放宽前滚/接收窗口并缩短超时等待
                dropOnlyWindowBackwardSec = 7.0
                dropOnlyWindowForwardSec = 11.0
                acceptFutureBackwardEarlySec = 4.0
                acceptFutureForwardEarlySec = 6.0
                acceptFutureBackwardMidSec = 8.0
                acceptFutureForwardMidSec = 11.0
                acceptFutureBackwardLateSec = 12.0
                acceptFutureForwardLateSec = 17.0
                lowerBoundDeadlineNormalMs = 2000
                lowerBoundDeadlineLargeMs = 2500
                recoveryDeadlineNormalMs = 4200
                recoveryDeadlineLargeMs = 5200
                audioWaitDeadlineNormalMs = 3200
                audioWaitDeadlineLargeMs = 4200
            }
        }
        setSecureSeekTuning(config)
    }

    /**
     * 恢复 SecureHLS seek 默认调参。
     */
    fun resetSecureSeekTuning() {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        nativeResetSecureSeekTuning(handle)
    }

    private fun resetPlaybackHealthStateForOpen(reason: String, startPosition: Double) {
        val now = SystemClock.elapsedRealtime()
        val shouldPreserveReopenUiAnchor = reopenUiAnchorActive &&
                startPosition.isFinite() &&
                reopenUiAnchorPosSec.isFinite() &&
                abs(reopenUiAnchorPosSec - startPosition) <= 0.25 &&
                now <= reopenUiAnchorUntilMs
        lastLoadingState = null
        lastPlayerState = null
        lastPipelineState = null
        lastIsPlayingState = null
        lastPlaybackSnapshot = null
        loadingSessionLikelySeek = false
        loadingCandidateState = null
        loadingCandidateSinceMs = 0L
        suppressLoadingShowUntilMs = 0L
        lastPositionForLoadingHeuristicSec = Double.NaN
        lastBackwardJumpLogAtMs = 0L

        pendingSeekActive = false
        pendingSeekLoadingObserved = false
        pendingSeekTargetSec = Double.NaN
        pendingSeekFromSec = Double.NaN
        pendingSeekStartAtMs = 0L
        pendingSeekConvergedSinceMs = 0L
        pendingSeekPostConfirmActive = false
        pendingSeekPostConfirmStartAtMs = 0L
        pendingSeekPostConfirmBasePosSec = Double.NaN
        pendingSeekHadPostConfirm = false
        pendingSeekPostConfirmTimedOut = false
        pendingSeekTriggeredReopen = false
        pendingSeekLastWatchdogLogAtMs = 0L
        seekLoadingSyncRequestId = -1L
        seekNativeInactiveAtMs = 0L
        seekNativeInactivePosSec = Double.NaN
        seekLoadingSyncGapLogged = false

        if (!shouldPreserveReopenUiAnchor) {
            reopenUiAnchorActive = false
            reopenUiAnchorPosSec = Double.NaN
            reopenUiAnchorUntilMs = 0L
            reopenUiAnchorArmedAtMs = 0L
        }
        playStallCheckArmed = false
        playStallArmedAtMs = 0L
        playStallBasePosSec = Double.NaN
        playStallRecoverStage = 0
        playStallLastRecoverAtMs = 0L
        playStallLastPlayReqAtMs = 0L
        manualPlayHardRecoverPending = false
        manualPlayHardRecoverArmedAtMs = 0L
        manualPlayHardRecoverBasePosSec = Double.NaN
        manualPlayHardRecoverLastAtMs = 0L
        lastSeekPostConfirmReopenAtMs = 0L

        playLoopMaxPosSec = Double.NaN
        playLoopAnchorPosSec = Double.NaN
        playLoopHitCount = 0
        playLoopWindowStartMs = 0L
        playLoopLastRecoverAtMs = 0L
        playLoopSuppressUntilMs = now + openSessionLoopSuppressMs

        logInfo(
            "evt=session_state_reset reason=$reason start_pos=$startPosition " +
                "loop_suppress_ms=$openSessionLoopSuppressMs source_category=$currentSourceCategory " +
                "preserve_reopen_ui_anchor=$shouldPreserveReopenUiAnchor"
        )
    }

    private fun clampPositionForDuration(rawPosition: Double, duration: Double): Double {
        if (!rawPosition.isFinite() || rawPosition < 0.0) return 0.0
        if (!duration.isFinite() || duration <= 0.0) return rawPosition
        return if (rawPosition > duration && (rawPosition - duration) <= positionDurationClampEpsilonSec) {
            duration
        } else if (rawPosition >= duration + positionDurationClampEpsilonSec) {
            duration
        } else {
            rawPosition
        }
    }

    private fun isRecoverableErrorCode(errorCode: Int): Boolean {
        return when (errorCode) {
            PlayerErrorCode.OPEN_INPUT_FAILED,
            PlayerErrorCode.READ_FRAME_FAILED,
            PlayerErrorCode.DECODE_FAILED -> true

            // Core 层网络/HTTP 负数错误码（与 C 层定义对齐）
            -2001, // PLAYER_ERROR_NET_CONNECTION_TIMEOUT
            -2002, // PLAYER_ERROR_NET_CONNECTION_REFUSED
            -2003, // PLAYER_ERROR_NET_UNREACHABLE
            -3003  // PLAYER_ERROR_HTTP_SERVER_ERROR
            -> true

            // 明确不可恢复：鉴权/权限/资源不存在/参数问题
            -3002, // PLAYER_ERROR_HTTP_NOT_FOUND
            -3004, // PLAYER_ERROR_HTTP_UNAUTHORIZED
            -3005, // PLAYER_ERROR_HTTP_FORBIDDEN
            PlayerErrorCode.INVALID_URL,
            PlayerErrorCode.ALLOC_CONTEXT_FAILED,
            PlayerErrorCode.CODEC_NOT_FOUND,
            PlayerErrorCode.CODEC_OPEN_FAILED,
            PlayerErrorCode.NO_VIDEO_STREAM,
            PlayerErrorCode.NO_AUDIO_STREAM
            -> false

            else -> false
        }
    }

    private fun dispatchError(errorCode: Int, errorMessage: String) {
        val recoverable = isRecoverableErrorCode(errorCode)
        maybeAutoReopen(errorCode, recoverable)
        callback?.onPlayerError(errorCode, errorMessage)
        callback?.onPlayerErrorWithRecoverability(errorCode, errorMessage, recoverable)
    }

    private fun maybeAutoReopen(errorCode: Int, recoverable: Boolean) {
        if (!recoverable) return
        if (secondarySyncMode) return
        if (!autoReopenOnRecoverableErrorEnabled) return
        if (autoReopenInFlight) return
        if (autoReopenAttemptCount >= autoReopenMaxAttempts) return

        val retryModel = lastOpenPlayModel?.let { clonePlayModel(it) }
        val retryUrl = lastOpenUrl
        if (retryModel == null && retryUrl.isNullOrBlank()) return

        autoReopenInFlight = true
        autoReopenAttemptCount += 1
        networkReconnectCount += 1
        notifyNetworkQoE(0L)

        val retryStart = getPosition().takeIf { it > 0.0 } ?: lastOpenStartPosition
        mainHandler.postDelayed({
            if (isReleased) {
                autoReopenInFlight = false
                return@postDelayed
            }
            val ok = if (retryModel != null) {
                openWithPlayModel(retryModel, retryStart)
            } else {
                openURL(retryUrl!!, retryStart)
            }
            if (ok) {
                play()
            }
            autoReopenInFlight = false
        }, 300L)
    }

    private fun createRenderView(): View {
        return when (renderViewType) {
            VideoRenderViewType.SURFACE_VIEW -> createSurfaceRenderView()
            VideoRenderViewType.TEXTURE_VIEW -> createTextureRenderView()
        }
    }

    private fun effectiveDataSourceConfig(): PlayerDataSourceConfig {
        val cfg = PlayerDataSourceConfig.defaultConfig()
        synchronized(dataSourceConfigLock) {
            if (gHasConfigured) {
                cfg.timeoutMs = gTimeoutMs
                cfg.maxRetries = gMaxRetries
                cfg.avioBufferSize = gAvioBufferSize
            }
        }
        return cfg
    }

    private data class SecureAuthResult(
        val playUrl: String,
        val encrypted: Boolean,
        val secureHeaders: String
    )

    private fun firstNonEmpty(root: JSONObject, vararg keys: String): String {
        for (key in keys) {
            val value = root.optString(key, "")
            if (value.isNotBlank()) return value
        }
        return ""
    }

    @Throws(Exception::class)
    private fun performSecureHlsAuth(config: PlayerDataSourceConfig, video: PlayerVideo): SecureAuthResult {
        if (video.videoId.isBlank() || video.sign.isBlank() || video.secretId.isBlank()) {
            throw IllegalArgumentException("SecureHLS 缺少必要参数：videoId/sign/secretId")
        }
        val connection = (URL(secureHlsAuthUrl).openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            connectTimeout = config.timeoutMs
            readTimeout = config.timeoutMs
            doInput = true
            doOutput = true
            setRequestProperty("Content-Type", "application/json")
        }
        try {
            val requestJson = JSONObject().apply {
                put("secret_id", video.secretId)
                put("file_id", video.videoId)
                put("sign", video.sign)
                put("timestamp", video.timestamp)
                put("client_type", "Android")
            }
            connection.outputStream.use {
                it.write(requestJson.toString().toByteArray(Charsets.UTF_8))
                it.flush()
            }

            val code = connection.responseCode
            val responseText = (if (code in 200..299) connection.inputStream else connection.errorStream)
                ?.bufferedReader(Charsets.UTF_8)?.use { it.readText() } ?: ""
            if (responseText.isBlank()) {
                throw IllegalStateException("SecureHLS 鉴权响应为空")
            }
            val root = JSONObject(responseText)
            val hasBizCode = root.has("code") && !root.isNull("code")
            val bizCode = if (hasBizCode) root.optLong("code", 200L) else 200L
            if (code !in 200..299 || (hasBizCode && bizCode != 200L)) {
                throw IllegalStateException(root.optString("msg", "SecureHLS 鉴权失败"))
            }

            val data = root.optJSONObject("data") ?: root
            val playUrl = firstNonEmpty(data, "play_url", "url")
            if (playUrl.isBlank()) {
                throw IllegalStateException("SecureHLS 鉴权缺少 play_url")
            }

            val encrypted = data.optInt("encrypt_type", 0) == 1 || data.optBoolean("is_encrypted", false)
            var secureHeaders = data.optString("secure_headers", "")
            if (encrypted && secureHeaders.isBlank()) {
                secureHeaders = buildString {
                    append("P-HX-SecretID: ${video.secretId}\r\n")
                    append("P-HX-FileId: ${video.videoId}\r\n")
                    append("P-HX-Timestamp: ${video.timestamp}\r\n")
                    append("P-HX-Sign: ${video.sign}\r\n")
                    append("P-HX-Terminal-Type: Android\r\n")
                }
            }
            return SecureAuthResult(playUrl, encrypted, secureHeaders)
        } finally {
            connection.disconnect()
        }
    }

    /**
     * 执行 License 校验（网络优先，失败回退缓存）。
     * 成功后当前进程会被标记为通过，后续播放门禁可直接放行。
     */
    fun checkLicense(licenseKey: String, licenseUrl: String, callback: (Boolean, Throwable?) -> Unit) {
        HXCPlayerLicenseManager.checkLicenseWithLicenseKey(context, licenseKey, licenseUrl) { success, error ->
            callback(success, error)
        }
    }

    fun resetLicenseState() {
        HXCPlayerLicenseManager.resetLicenseState(context)
    }

    private fun licenseAllowedOrNotify(action: String): Boolean {
        if (HXCPlayerLicenseManager.isLicenseCheckPassed(context)) return true
        callback?.onPlayerError(PlayerErrorCode.LICENSE_VALIDATION_FAILED, "License 校验失败: $action")
        return false
    }

    private fun createSurfaceRenderView(): View {
        return SurfaceView(context).apply {
            holder.addCallback(object : SurfaceHolder.Callback {
                override fun surfaceCreated(holder: SurfaceHolder) {
                    setSurface(holder.surface)
                }

                override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                    updateSurfaceSize(width, height)
                }

                override fun surfaceDestroyed(holder: SurfaceHolder) {
                    setSurface(null)
                }
            })
        }
    }

    private fun createTextureRenderView(): View {
        return TextureView(context).apply {
            surfaceTextureListener = object : TextureView.SurfaceTextureListener {
                override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) {
                    textureDecoderSurface?.release()
                    textureDecoderSurface = Surface(surface)
                    setSurface(textureDecoderSurface)
                    if (width > 0 && height > 0) {
                        updateSurfaceSize(width, height)
                    }
                }

                override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) {
                    updateSurfaceSize(width, height)
                }

                override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean {
                    setSurface(null)
                    textureDecoderSurface?.release()
                    textureDecoderSurface = null
                    return true
                }

                override fun onSurfaceTextureUpdated(surface: SurfaceTexture) {
                }
            }
        }
    }

    // 内部方法：设置渲染 Surface
    private fun setSurface(surface: Surface?) {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        nativeSetSurface(handle, surface)
    }

    // 内部方法：更新 Surface 尺寸
    private fun updateSurfaceSize(width: Int, height: Int) {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        nativeUpdateSurfaceSize(handle, width, height)
    }

    // 打开 URL
    fun openURL(url: String): Boolean {
        return openURL(url, 0.0)
    }

    // 打开 URL 并指定起始位置（秒）
    // 注意：该接口为同步调用，可能阻塞调用线程（例如网络抖动时）
    fun openURL(url: String, startPosition: Double): Boolean {
        return synchronized(openSerialLock) {
            val handle = currentHandle()
            if (handle == 0L || isReleased) {
                dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "播放器已释放，无法打开 URL: $url")
                return@synchronized false
            }
            if (url.isBlank()) {
                dispatchError(PlayerErrorCode.INVALID_URL, "URL 不能为空")
                return@synchronized false
            }
            validateLocalPathIfNeeded(url)?.let { reason ->
                dispatchError(PlayerErrorCode.INVALID_URL, reason)
                return@synchronized false
            }
            if (!licenseAllowedOrNotify("openURL")) {
                return@synchronized false
            }
            lastOpenUrl = url
            lastOpenStartPosition = startPosition
            lastOpenPlayModel = null
            currentSourceCategory = resolveSourceCategory(
                url = url,
                mode = PlayerDataSourceMode.DEFAULT,
                encryptedFile = false
            )
            decodeFallbackTriedForCurrentSource = false
            logInfo("evt=open_source_classify category=$currentSourceCategory mode=${PlayerDataSourceMode.DEFAULT} encrypted=false start_pos=$startPosition")
            resetPlaybackHealthStateForOpen("openURL", startPosition)
            if (!autoReopenInFlight) {
                autoReopenAttemptCount = 0
                networkTotalStallMs = 0L
                networkReconnectCount = 0
            }
            applyDecodeModeForHandle(handle)
            // 始终用带起始位置的接口，确保 startPosition=0 时也能清除上次残留进度
            val result = nativeOpenURLWithStartPosition(handle, url, startPosition)

            if (!result) {
                openLoadingHideProtectUntilMs = 0L
                openLoadingHideHardDeadlineMs = 0L
                openLoadingGuardStartPosSec = -1.0
                dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "无法打开 URL: $url")
            } else {
                armOpenLoadingHideGuard()
            }
            result
        }
    }

    /**
     * 异步打开 URL，避免阻塞 UI 线程。
     * 结果通过已有 callback 回调返回（失败会触发 onPlayerError）。
     */
    fun openURLAsync(url: String, startPosition: Double = 0.0) {
        if (isReleased) {
            dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "播放器已释放，无法打开 URL: $url")
            return
        }
        if (url.isBlank()) {
            dispatchError(PlayerErrorCode.INVALID_URL, "URL 不能为空")
            return
        }
        validateLocalPathIfNeeded(url)?.let { reason ->
            dispatchError(PlayerErrorCode.INVALID_URL, reason)
            return
        }
        playStallCheckArmed = false
        lastOpenUrl = url
        lastOpenStartPosition = startPosition
        lastOpenPlayModel = null
        currentSourceCategory = resolveSourceCategory(
            url = url,
            mode = PlayerDataSourceMode.DEFAULT,
            encryptedFile = false
        )
        decodeFallbackTriedForCurrentSource = false
        logInfo("evt=open_source_classify category=$currentSourceCategory mode=${PlayerDataSourceMode.DEFAULT} encrypted=false start_pos=$startPosition")
        resetPlaybackHealthStateForOpen("openURLAsync", startPosition)
        if (!autoReopenInFlight) {
            autoReopenAttemptCount = 0
            networkTotalStallMs = 0L
            networkReconnectCount = 0
        }
        try {
            openExecutor.execute {
                val handle = currentHandle()
                if (handle == 0L || isReleased) {
                    mainHandler.post {
                        dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "播放器已释放，无法打开 URL: $url")
                    }
                    return@execute
                }
                applyDecodeModeForHandle(handle)
                // 始终用带起始位置的接口
                val result = nativeOpenURLWithStartPosition(handle, url, startPosition)

                mainHandler.post {
                    if (!result && !isReleased) {
                        openLoadingHideProtectUntilMs = 0L
                        openLoadingHideHardDeadlineMs = 0L
                        openLoadingGuardStartPosSec = -1.0
                        dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "无法打开 URL: $url")
                    } else if (result && !isReleased) {
                        armOpenLoadingHideGuard()
                    }
                }
            }
        } catch (_: RejectedExecutionException) {
            dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "播放器线程已关闭，无法打开 URL: $url")
        }
    }

    /**
     * 统一入口：使用播放模型打开（对齐 iOS 的 openWithPlayModel）。
     */
    fun openWithPlayModel(model: PlayerDataSourcePlayModel): Boolean {
        val handle = currentHandle()
        if (handle == 0L || isReleased) {
            dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "播放器已释放，无法打开播放模型")
            return false
        }
        if (model.url.isBlank()) {
            dispatchError(PlayerErrorCode.INVALID_URL, "URL 不能为空")
            return false
        }
        if (!licenseAllowedOrNotify("openWithPlayModel")) {
            return false
        }
        lastOpenUrl = model.url
        lastOpenStartPosition = 0.0
        lastOpenPlayModel = clonePlayModel(model)
        if (!autoReopenInFlight) {
            autoReopenAttemptCount = 0
            networkTotalStallMs = 0L
            networkReconnectCount = 0
        }
        // 无 startPosition 的重载委托给带 startPosition 版本（传 0.0 从头开始）
        return openWithPlayModel(model, 0.0)
    }

    /**
     * 异步打开播放模型，避免主线程执行鉴权和打开流程导致卡顿。
     */
    fun openWithPlayModelAsync(
        model: PlayerDataSourcePlayModel,
        startPosition: Double = 0.0,
        callback: PlayModelAsyncCallback? = null
    ) {
        if (isReleased) {
            callback?.let { cb -> mainHandler.post { cb.onResult(false) } }
            return
        }
        val safeModel = clonePlayModel(model)
        try {
            openExecutor.execute {
                val result = openWithPlayModel(safeModel, startPosition)
                mainHandler.post {
                    if (!isReleased) {
                        callback?.onResult(result)
                    }
                }
            }
        } catch (_: RejectedExecutionException) {
            callback?.let { cb -> mainHandler.post { cb.onResult(false) } }
        }
    }

    /**
     * 异步 playWithModel（包含 open+play），适合业务层直接替换主线程调用。
     */
    fun playWithModelAsync(
        model: PlayerDataSourcePlayModel,
        startPosition: Double = pendingStartPosition,
        callback: PlayModelAsyncCallback? = null
    ) {
        openWithPlayModelAsync(model, startPosition) { opened ->
            if (opened && !isReleased && autoPlayer) {
                play()
            }
            callback?.onResult(opened)
        }
    }

    // 使用自定义 HTTP 模式打开（支持 Range 下载）
    // encryptedFile：是否与核心层约定一致，对文件头前 100 字节解密（默认 false）
    fun openWithCustomHTTP(url: String, timeoutMs: Int = 30000, maxRetries: Int = 3, encryptedFile: Boolean = false): Boolean {
        val handle = currentHandle()
        if (handle == 0L || isReleased) {
            dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "播放器已释放，无法打开自定义 HTTP: $url")
            return false
        }
        if (!licenseAllowedOrNotify("openWithCustomHTTP")) {
            return false
        }
        applyDecodeModeForHandle(handle)
        val result = nativeOpenWithCustomHTTP(handle, url, timeoutMs, maxRetries, encryptedFile)
        if (!result) {
            dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "无法打开自定义 HTTP: $url")
        }
        return result
    }

    /**
     * 使用自定义本地文件模式打开（核心层 CustomFile：LocalFileDataSource + CustomAVIO）。
     * @param path 本地绝对路径（如 context.filesDir 或 Environment 下的路径）
     * @param avioBufferSize AVIO 读缓冲，字节，默认 64KB；≤0 时 JNI 侧会退回 64KB
     * @param encryptedFile 是否对文件头前 100 字节按核心约定解密
     */
    fun openWithCustomFile(path: String, avioBufferSize: Int = 64 * 1024, encryptedFile: Boolean = false): Boolean {
        val handle = currentHandle()
        if (handle == 0L || isReleased) {
            dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "播放器已释放，无法打开本地文件(CustomFile): $path")
            return false
        }
        if (!licenseAllowedOrNotify("openWithCustomFile")) {
            return false
        }
        applyDecodeModeForHandle(handle)
        val result = nativeOpenWithCustomFile(handle, path, avioBufferSize, encryptedFile)
        if (!result) {
            dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "无法打开本地文件(CustomFile): $path")
        }
        return result
    }

    // 播放
    fun play() {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        if (!licenseAllowedOrNotify("play")) {
            return
        }
        val requestAtMs = SystemClock.elapsedRealtime()
        playStallLastPlayReqAtMs = requestAtMs
        playStallCheckArmed = true
        playStallArmedAtMs = playStallLastPlayReqAtMs
        playStallBasePosSec = getPosition()
        playStallRecoverStage = 0
        manualPlayHardRecoverPending = true
        manualPlayHardRecoverArmedAtMs = playStallLastPlayReqAtMs
        manualPlayHardRecoverBasePosSec = playStallBasePosSec
        val generation = nextPlaybackCommandGeneration(true)
        enqueuePlaybackCommand("play", generation) { current ->
            nativePlay(current)
            // 某些设备/解码链路在 pause->play 后会把速率短暂回落到 1.0，
            // play 后立刻回放业务侧目标倍速，保证体感一致。
            nativeSetPlaybackRate(current, preferredPlaybackRate)
        }
    }

    // 暂停
    fun pause() {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        playStallCheckArmed = false
        manualPlayHardRecoverPending = false
        val generation = nextPlaybackCommandGeneration(false)
        enqueuePlaybackCommand("pause", generation) { current ->
            nativePause(current)
        }
    }

    // 停止
    fun stop() {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        val generation = nextPlaybackCommandGeneration(false)
        enqueuePlaybackCommand("stop", generation, false) { current ->
            nativeStop(current)
        }
        playStallCheckArmed = false
        manualPlayHardRecoverPending = false
        networkLoadingSinceMs = 0L
    }

    // 跳转
    fun seekTo(position: Double) {
        val resumeAfterSeek = isPlaying() || getPlayWhenReady()
        seekToWithIntent(position, resumeAfterSeek)
    }

    // 跳转（显式指定 seek 完成后是否恢复播放）
    fun seekToWithIntent(position: Double, resumeAfterSeek: Boolean) {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        if (!licenseAllowedOrNotify("seekTo")) {
            return
        }
        var target = maxOf(0.0, position)
        val duration = getDuration()
        if (duration > 0.0) {
            val maxSeek = maxOf(0.0, duration - 0.35)
            if (target > maxSeek) {
                logInfo(
                    "[sync] seek target clamped: req=%.3f -> %.3f (duration=%.3f)".format(
                        target,
                        maxSeek,
                        duration
                    )
                )
                target = maxSeek
            }
        }
        pendingSeekRequestId += 1L
        pendingSeekTargetSec = target
        pendingSeekFromSec = clampPositionForDuration(getPosition(), duration)
        pendingSeekStartAtMs = SystemClock.elapsedRealtime()
        pendingSeekLoadingObserved = false
        pendingSeekActive = true
        pendingSeekConvergedSinceMs = 0L
        pendingSeekPostConfirmActive = false
        pendingSeekPostConfirmStartAtMs = 0L
        pendingSeekPostConfirmBasePosSec = Double.NaN
        pendingSeekHadPostConfirm = false
        pendingSeekPostConfirmTimedOut = false
        pendingSeekTriggeredReopen = false
        pendingSeekLastWatchdogLogAtMs = 0L
        seekLoadingSyncRequestId = pendingSeekRequestId
        seekNativeInactiveAtMs = 0L
        seekNativeInactivePosSec = Double.NaN
        seekLoadingSyncGapLogged = false
        loadingSessionLikelySeek = true
        playStallCheckArmed = false
        manualPlayHardRecoverPending = false
        val generation = playbackCommandGeneration.get()
        enqueuePlaybackCommand("seek_to_with_intent", generation, false) { current ->
            nativeSeekToWithIntent(current, target, resumeAfterSeek)
        }
    }

    fun getLastSeekRequestId(): Long {
        return pendingSeekRequestId
    }

    // 设置播放速度
    fun setPlaybackRate(rate: Float) {
        var normalizedRate = if (rate.isFinite()) rate else 1.0f
        normalizedRate = normalizedRate.coerceIn(MIN_PLAYBACK_RATE, MAX_PLAYBACK_RATE)
        if (abs(normalizedRate - MAX_PLAYBACK_RATE) <= MAX_PLAYBACK_RATE_SNAP_EPSILON) {
            normalizedRate = MAX_PLAYBACK_RATE
        }
        preferredPlaybackRate = normalizedRate
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        nativeSetPlaybackRate(handle, normalizedRate)
    }

    // 设置音量
    fun setVolume(volume: Float) {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        nativeSetVolume(handle, volume)
    }

    /**
     * 三分屏小窗/副画面同步模式。
     *
     * 副播放器只跟随主播放器做画面同步，不应触发 reopen 等重恢复链路，
     * 否则双实例 seek 时会相互抢占解码/音频资源并把全局 loading 拉长。
     */
    fun setSecondarySyncMode(enabled: Boolean) {
        secondarySyncMode = enabled
    }

    // 设置比例模式
    fun setAspectRatioMode(fill: Boolean) {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        nativeSetAspectRatioMode(handle, if (fill) 1 else 0)
    }

    // 获取时长
    fun getDuration(): Double {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return 0.0
        return nativeGetDuration(handle)
    }

    // 获取当前位置
    fun getPosition(): Double {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return 0.0
        return nativeGetPosition(handle)
    }

    // 获取状态
    fun getState(): PlayerState {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return PlayerState.IDLE
        val coreState = nativeGetState(handle)
        val pipelineState = nativeGetPipelineState(handle)
        val playWhenReady = nativeGetPlayWhenReady(handle)
        val isPlaying = nativeIsPlaying(handle)
        return resolveUnifiedState(coreState, pipelineState, playWhenReady, isPlaying)
    }

    fun getPipelineState(): PipelineState {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return PipelineState.IDLE
        return mapPipelineState(nativeGetPipelineState(handle))
    }

    fun getPlayWhenReady(): Boolean {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return false
        if (playbackCommandGeneration.get() != playbackCommandAppliedGeneration.get()) {
            return desiredPlayWhenReady
        }
        return nativeGetPlayWhenReady(handle)
    }

    fun isPlaying(): Boolean {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return false
        return nativeIsPlaying(handle)
    }

    fun setPlayWhenReady(playWhenReady: Boolean) {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        val generation = nextPlaybackCommandGeneration(playWhenReady)
        enqueuePlaybackCommand("set_play_when_ready", generation) { current ->
            nativeSetPlayWhenReady(current, playWhenReady)
        }
    }

    private fun nextPlaybackCommandGeneration(targetPlayWhenReady: Boolean): Long {
        desiredPlayWhenReady = targetPlayWhenReady
        return playbackCommandGeneration.incrementAndGet()
    }

    private fun enqueuePlaybackCommand(
        command: String,
        generation: Long,
        allowDropByNewerGeneration: Boolean = true,
        action: (Long) -> Unit
    ) {
        try {
            openExecutor.execute {
                if (isReleased) return@execute
                val latest = playbackCommandGeneration.get()
                if (allowDropByNewerGeneration && generation != latest) {
                    logInfo("evt=control_drop cmd=$command generation=$generation latest=$latest")
                    return@execute
                }
                val handle = currentHandle()
                if (handle == 0L || isReleased) return@execute
                try {
                    action(handle)
                    playbackCommandAppliedGeneration.set(generation)
                } catch (t: Throwable) {
                    logInfo("evt=control_failed cmd=$command err=${t.message ?: "unknown"}")
                }
            }
        } catch (_: RejectedExecutionException) {
            logInfo("evt=control_rejected cmd=$command")
        }
    }

    // 仅映射 core 主状态（PlayerStateC）。
    // LOADING 不来自 core 主状态枚举，而是由 pipelineState=BUFFERING 在 resolveUnifiedState() 中推导。
    private fun mapPlayerState(raw: Int): PlayerState {
        return when (raw) {
            -1 -> PlayerState.ERROR
            0 -> PlayerState.IDLE
            1 -> PlayerState.OPENING
            2 -> PlayerState.PLAYING
            3 -> PlayerState.PAUSED
            4 -> PlayerState.STOPPED
            else -> PlayerState.IDLE
        }
    }

    private fun mapPipelineState(raw: Int): PipelineState {
        return when (raw) {
            0 -> PipelineState.IDLE
            1 -> PipelineState.PREPARING
            2 -> PipelineState.BUFFERING
            3 -> PipelineState.READY
            4 -> PipelineState.ENDED
            5 -> PipelineState.ERROR
            else -> PipelineState.IDLE
        }
    }

    private fun resolveUnifiedState(
        coreStateRaw: Int,
        pipelineStateRaw: Int,
        playWhenReady: Boolean,
        isPlayingNow: Boolean
    ): PlayerState {
        return when {
            coreStateRaw == -1 || pipelineStateRaw == 5 -> PlayerState.ERROR
            coreStateRaw == 4 || pipelineStateRaw == 4 -> PlayerState.STOPPED
            coreStateRaw == 0 || pipelineStateRaw == 0 -> PlayerState.IDLE
            coreStateRaw == 1 || pipelineStateRaw == 1 -> PlayerState.OPENING
            // 统一状态语义：未请求播放（playWhenReady=false）时，不能对外报 PLAYING。
            // 某些机型/链路在 open 初期会短暂返回 isPlaying=true 或 coreState=PLAYING，
            // 若直接透传会导致上层出现 "PLAYING -> PAUSED -> PLAYING" 抖动与按钮错态。
            !playWhenReady -> PlayerState.PAUSED
            isPlayingNow -> PlayerState.PLAYING
            pipelineStateRaw == 2 -> PlayerState.LOADING
            coreStateRaw == 3 -> PlayerState.PAUSED
            else -> mapPlayerState(coreStateRaw)
        }
    }

    private fun maybeLogSeekSettleKeyDiag(
        nowMs: Long,
        positionSec: Double,
        coreStateRaw: Int,
        pipelineStateRaw: Int,
        playWhenReady: Boolean,
        isPlayingNow: Boolean,
        resolvedState: PlayerState,
        loading: Boolean
    ) {
        if (!canEmitDebugDiagLog()) return
        if (seekSettleDiagLogged) return
        if (nowMs > seekSettleDiagUntilMs) return
        if (seekSettleDiagRequestId < 0L) return
        val progressed = if (seekSettleDiagStartPosSec.isNaN()) 0.0 else (positionSec - seekSettleDiagStartPosSec)
        val semanticMismatch = playWhenReady &&
                !isPlayingNow &&
                (resolvedState == PlayerState.LOADING || resolvedState == PlayerState.PAUSED) &&
                progressed >= 0.35
        if (!semanticMismatch) return
        seekSettleDiagLogged = true
        Log.d(
            TAG,
            "evt=seek_settle_key_diag id=$seekSettleDiagRequestId pos=$positionSec progressed=$progressed " +
                "core=$coreStateRaw pipeline=$pipelineStateRaw pwr=$playWhenReady playing=$isPlayingNow " +
                "resolved=${resolvedState.name} loading=$loading"
        )
    }

    private fun coerceStateWithLoading(state: PlayerState, loading: Boolean): PlayerState {
        // Only coerce PLAYING->LOADING for seek-like sessions.
        // If loading flag gets temporarily stale during replay/open switch,
        // forcing LOADING here can block upper-layer "main is playing" gates.
        val seekLikeLoading = loading && loadingSessionLikelySeek
        return if (seekLikeLoading && state == PlayerState.PLAYING) PlayerState.LOADING else state
    }

    private fun buildPlaybackSnapshot(
        state: PlayerState,
        pipeline: PipelineState,
        playWhenReady: Boolean,
        isPlayingNow: Boolean,
        loading: Boolean,
        position: Double,
        duration: Double,
        nowMs: Long
    ): PlaybackSnapshot {
        val shouldShowPlayingUi = when (state) {
            PlayerState.PLAYING -> true
            PlayerState.LOADING, PlayerState.OPENING -> playWhenReady
            else -> false
        }
        return PlaybackSnapshot(
            state = state,
            pipelineState = pipeline,
            playWhenReady = playWhenReady,
            isPlaying = isPlayingNow,
            isLoading = loading,
            position = position,
            duration = duration,
            shouldShowPlayingUi = shouldShowPlayingUi,
            updatedAtMs = nowMs
        )
    }

    private fun hasSnapshotDispatchDiff(old: PlaybackSnapshot?, new: PlaybackSnapshot): Boolean {
        if (old == null) return true
        val semanticChanged = old.state != new.state ||
            old.pipelineState != new.pipelineState ||
            old.playWhenReady != new.playWhenReady ||
            old.isPlaying != new.isPlaying ||
            old.isLoading != new.isLoading ||
            old.shouldShowPlayingUi != new.shouldShowPlayingUi
        if (semanticChanged) return true
        if (abs(old.position - new.position) >= 0.25) return true
        if (abs(old.duration - new.duration) >= 0.50) return true
        return new.updatedAtMs - old.updatedAtMs >= 1000L
    }

    /**
     * 当前视频是否处于硬解状态（硬解失败回退软解后返回 false）。
     */
    fun isHardwareDecodingActive(): Boolean {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return false
        return nativeIsHardwareDecodingActive(handle)
    }

    // 启动位置更新定时器
    private fun startPositionUpdates() {
        updateExecutor = Executors.newSingleThreadScheduledExecutor()
        updateExecutor?.scheduleAtFixedRate({
            try {
                val handle = currentHandle()
                if (handle == 0L || isReleased) {
                    return@scheduleAtFixedRate
                }

                val rawPosition = getPosition()
                val duration = getDuration()
                val boundedRawPosition = clampPositionForDuration(rawPosition, duration)
                val coreStateRaw = nativeGetState(handle)
                val pipelineStateRaw = nativeGetPipelineState(handle)
                val playWhenReady = nativeGetPlayWhenReady(handle)
                val isPlaying = nativeIsPlaying(handle)
                if (!sdkDiagVersionLogged) {
                    sdkDiagVersionLogged = true
                    logInfo("evt=sdk_diag_version value=20260605_semantic_progress_playing_fix")
                }
                val loading = isLoading()
                val state = coerceStateWithLoading(
                    resolveUnifiedState(coreStateRaw, pipelineStateRaw, playWhenReady, isPlaying),
                    loading
                )
                val now = SystemClock.elapsedRealtime()
                val position = applyUiPositionAnchorIfNeeded(
                    rawPositionSec = boundedRawPosition,
                    loading = loading,
                    state = state,
                    nowMs = now
                )
                if (duration > 0.0 &&
                    rawPosition > duration + positionDurationClampEpsilonSec &&
                    canEmitDebugDiagLog()
                ) {
                    Log.d(
                        TAG,
                        "evt=position_duration_clamp raw=$rawPosition clamped=$position duration=$duration " +
                            "state=${state.name} loading=$loading"
                    )
                }
                val pipeline = mapPipelineState(pipelineStateRaw)
                val snapshot = buildPlaybackSnapshot(
                    state = state,
                    pipeline = pipeline,
                    playWhenReady = playWhenReady,
                    isPlayingNow = isPlaying,
                    loading = loading,
                    position = position,
                    duration = duration,
                    nowMs = now
                )
                val shouldDispatchSnapshot = hasSnapshotDispatchDiff(lastPlaybackSnapshot, snapshot)
                if (shouldDispatchSnapshot) {
                    lastPlaybackSnapshot = snapshot
                }

                if (lastPlayerState != state) {
                    lastPlayerState = state
                    mainHandler.post { callback?.onPlayerStateChanged(state) }
                }
                lastPipelineState = pipeline
                lastIsPlayingState = isPlaying
                if (duration > 0) {
                    callback?.onPlayerPositionUpdated(position, duration)
                }
                if (shouldDispatchSnapshot) {
                    mainHandler.post {
                        dispatchPlaybackSnapshot(snapshot)
                    }
                }

                maybeRecoverPlayStall(
                    handle = handle,
                    nowMs = now,
                    positionSec = boundedRawPosition,
                    durationSec = duration,
                    loading = loading,
                    state = state,
                    isPlayingNow = isPlaying,
                    playWhenReady = playWhenReady
                )
                maybeRecoverManualPlayHardStall(
                    nowMs = now,
                    positionSec = boundedRawPosition,
                    durationSec = duration,
                    loading = loading,
                    state = state,
                    isPlayingNow = isPlaying,
                    playWhenReady = playWhenReady
                )
                maybeRecoverPlaybackLoop(
                    handle = handle,
                    nowMs = now,
                    positionSec = boundedRawPosition,
                    durationSec = duration,
                    loading = loading,
                    state = state,
                    isPlayingNow = isPlaying,
                    playWhenReady = playWhenReady
                )
                maybeLogSeekSettleKeyDiag(
                    nowMs = now,
                    positionSec = boundedRawPosition,
                    coreStateRaw = coreStateRaw,
                    pipelineStateRaw = pipelineStateRaw,
                    playWhenReady = playWhenReady,
                    isPlayingNow = isPlaying,
                    resolvedState = state,
                    loading = loading
                )
                maybeLogPlaybackMetrics(now, boundedRawPosition, state, loading)
                val recentForwardProgress =
                    !lastPositionForLoadingHeuristicSec.isNaN() &&
                            (position - lastPositionForLoadingHeuristicSec) >= loadingProgressSuppressMinStepSec
                if (loadingSessionLikelySeek && !pendingSeekActive && playWhenReady && recentForwardProgress) {
                    // Seek 会话结束后，若已经持续推进，清掉 seek-like 标记，避免状态长期被 LOADING 绑住。
                    loadingSessionLikelySeek = false
                }
                if (loadingCandidateState == null || loadingCandidateState != loading) {
                    if (loading) {
                        val likelySeekByJump = !lastPositionForLoadingHeuristicSec.isNaN() &&
                            abs(position - lastPositionForLoadingHeuristicSec) >= seekDetectMinJumpSec
                        loadingSessionLikelySeek = likelySeekByJump
                    }
                    loadingCandidateState = loading
                    loadingCandidateSinceMs = now
                } else {
                    var debounceMs = if (loading) loadingShowDebounceMs else loadingHideDebounceMs
                    if (loading && !pendingSeekActive && playWhenReady && recentForwardProgress) {
                        // 正在稳定播放推进时，忽略短暂 loading=true 抖动，防止转圈闪烁。
                        debounceMs = maxOf(debounceMs, loadingShowProgressSuppressWindowMs)
                        loadingCandidateSinceMs = now
                    }
                    if (loading && now < suppressLoadingShowUntilMs) {
                        debounceMs = maxOf(debounceMs, suppressLoadingShowUntilMs - now)
                    }
                    if (!loading && loadingSessionLikelySeek) {
                        debounceMs = seekLoadingHideDebounceMs
                    }
                    if (!loading && openLoadingHideProtectUntilMs > 0L) {
                        val posAdvanced = openLoadingGuardStartPosSec < 0.0 ||
                                (position - openLoadingGuardStartPosSec) >= openLoadingHideMinPosDeltaSec
                        val inMinDelay = now < openLoadingHideProtectUntilMs
                        val inHardGuard = !posAdvanced && now < openLoadingHideHardDeadlineMs
                        if (inMinDelay || inHardGuard) {
                            val guardLeft = if (inMinDelay) {
                                openLoadingHideProtectUntilMs - now
                            } else {
                                120L
                            }
                            debounceMs = maxOf(debounceMs, guardLeft)
                        }
                    }
                    if (lastLoadingState != loading && (now - loadingCandidateSinceMs) >= debounceMs) {
                        lastLoadingState = loading
                        if (!loading) {
                            openLoadingHideProtectUntilMs = 0L
                            openLoadingHideHardDeadlineMs = 0L
                            openLoadingGuardStartPosSec = -1.0
                            if (loadingSessionLikelySeek) {
                                suppressLoadingShowUntilMs = now + seekLoadingReshowSuppressMs
                            }
                            loadingSessionLikelySeek = false
                            if (!seekLoadingSyncGapLogged && seekLoadingSyncRequestId > 0L && seekNativeInactiveAtMs > 0L) {
                                val hideGapMs = (now - seekNativeInactiveAtMs).coerceAtLeast(0L)
                                val progressSinceInactive = if (seekNativeInactivePosSec.isNaN()) {
                                    Double.NaN
                                } else {
                                    position - seekNativeInactivePosSec
                                }
                                if (hideGapMs >= 800L) {
                                    val msg =
                                        "evt=seek_loading_sync_gap id=$seekLoadingSyncRequestId " +
                                            "native_inactive_to_loading_hide_ms=$hideGapMs " +
                                            "native_inactive_pos=$seekNativeInactivePosSec " +
                                            "loading_hide_pos=$position progress_since_inactive=$progressSinceInactive " +
                                            "state=${state.name} play_when_ready=$playWhenReady is_playing=$isPlaying"
                                    Log.w(TAG, msg)
                                }
                                seekLoadingSyncGapLogged = true
                            }
                        }
                        mainHandler.post {
                            callback?.onPlayerLoadingChanged(loading)
                            if (loading) {
                                networkLoadingSinceMs = SystemClock.elapsedRealtime()
                                notifyNetworkQoE(0L)
                            } else {
                                val nowMs = SystemClock.elapsedRealtime()
                                val currentStall = if (networkLoadingSinceMs > 0L) {
                                    (nowMs - networkLoadingSinceMs).coerceAtLeast(0L)
                                } else {
                                    0L
                                }
                                networkLoadingSinceMs = 0L
                                networkTotalStallMs += currentStall
                                notifyNetworkQoE(currentStall)
                            }
                        }
                    }
                }

                if (pendingSeekActive) {
                    val seekPosition = boundedRawPosition
                    if (loading) {
                        pendingSeekLoadingObserved = true
                    }
                    val seekElapsedMs = now - pendingSeekStartAtMs
                    val target = pendingSeekTargetSec
                    val from = pendingSeekFromSec
                    val largeSeek = !target.isNaN() && !from.isNaN() && abs(target - from) >= 25.0
                    val nonSecureSeek = !currentSourceCategory.startsWith("secure_") &&
                            !currentSourceCategory.contains("encrypted")
                    val localNonSecureSeek = nonSecureSeek && currentSourceCategory.startsWith("local_")
                    val nearTargetToleranceSec = when {
                        largeSeek && localNonSecureSeek -> 3.2
                        largeSeek && nonSecureSeek -> 2.2
                        else -> seekCompletionNearTargetSec
                    }
                    val nearTarget = !target.isNaN() && abs(seekPosition - target) <= nearTargetToleranceSec
                    val movedFromOldPos = !from.isNaN() && abs(seekPosition - from) >= seekCompletionMovedFromOldSec
                    val secureForwardSeek = currentSourceCategory.startsWith("secure_") &&
                            !target.isNaN() &&
                            !from.isNaN() &&
                            target > from + 0.5
                    val secureForwardNearTarget = secureForwardSeek &&
                            !target.isNaN() &&
                            abs(seekPosition - target) <= 0.55
                    val convergedNow = if (secureForwardSeek) {
                        secureForwardNearTarget
                    } else if (largeSeek && nonSecureSeek) {
                        nearTarget
                    } else {
                        nearTarget || movedFromOldPos
                    }
                    if (convergedNow) {
                        if (pendingSeekConvergedSinceMs <= 0L) {
                            pendingSeekConvergedSinceMs = now
                        }
                    } else {
                        pendingSeekConvergedSinceMs = 0L
                    }
                    val convergedStable = pendingSeekConvergedSinceMs > 0L
                            && (now - pendingSeekConvergedSinceMs) >= seekCompletionConvergedStableMs
                    val loadingRecovered = pendingSeekLoadingObserved && !loading
                    val progressRecovered = playWhenReady &&
                            recentForwardProgress &&
                            seekElapsedMs >= 600L &&
                            (localNonSecureSeek || secondarySyncMode)
                    val effectiveLoadingRecovered = loadingRecovered || progressRecovered
                    val nativeSeekActive = nativeIsSeekSessionActive(handle)
                    if (!nativeSeekActive &&
                        seekLoadingSyncRequestId == pendingSeekRequestId &&
                        seekNativeInactiveAtMs <= 0L
                    ) {
                        seekNativeInactiveAtMs = now
                        seekNativeInactivePosSec = seekPosition
                    }
                    val nativeConvergedButStuck = nativeSeekActive
                            && seekElapsedMs >= seekCompletionNativeConvergedWatchdogMs
                            && convergedStable
                    if ((loading || nativeSeekActive) &&
                        seekElapsedMs >= 3200L &&
                        (now - pendingSeekLastWatchdogLogAtMs) >= 2000L) {
                        pendingSeekLastWatchdogLogAtMs = now
                        Log.i(
                            TAG,
                            "evt=seek_pending_watchdog id=$pendingSeekRequestId target=$target from=$from pos=$seekPosition ui_pos=$position elapsed_ms=$seekElapsedMs loading=$loading loading_observed=$pendingSeekLoadingObserved loading_recovered=$loadingRecovered play_when_ready=$playWhenReady state=${state.name} native_seek_active=$nativeSeekActive converged_now=$convergedNow converged_stable=$convergedStable native_converged_stuck=$nativeConvergedButStuck post_confirm_active=$pendingSeekPostConfirmActive source_category=$currentSourceCategory"
                        )
                    }
                    // Prefer native seek settle as source of truth, but bound wait time
                    // when native session remains active despite already converged position.
                    val nativeSeekSettled = !nativeSeekActive || nativeConvergedButStuck
                    val hardTimeout = seekElapsedMs >= seekCompletionTimeoutMs
                            && !effectiveLoadingRecovered
                    val secureForwardFarFromTarget = secureForwardSeek && !secureForwardNearTarget
                    if (secureForwardFarFromTarget &&
                        nativeSeekSettled &&
                        !hardTimeout &&
                        seekElapsedMs >= 2400L &&
                        (now - pendingSeekLastWatchdogLogAtMs) >= 1200L
                    ) {
                        pendingSeekLastWatchdogLogAtMs = now
                        Log.w(
                            TAG,
                            "evt=seek_completion_blocked_far_secure_forward id=$pendingSeekRequestId " +
                                "target=$target from=$from pos=$seekPosition ui_pos=$position elapsed_ms=$seekElapsedMs " +
                                "distance=${abs(seekPosition - target)} loading=$loading loading_recovered=$loadingRecovered " +
                                "native_seek_active=$nativeSeekActive moved_from_old=$movedFromOldPos source_category=$currentSourceCategory"
                        )
                    }
                    val shouldComplete = nativeSeekSettled &&
                            (if (secureForwardFarFromTarget) {
                                hardTimeout
                            } else {
                                effectiveLoadingRecovered || convergedStable || hardTimeout
                            })
                    if (shouldComplete) {
                        var forceTimeoutByNoProgress = false
                        val needPostConfirm = playWhenReady && convergedStable && !hardTimeout && (!effectiveLoadingRecovered || !isPlaying)
                        if (needPostConfirm) {
                            if (!pendingSeekPostConfirmActive) {
                                pendingSeekHadPostConfirm = true
                                pendingSeekPostConfirmActive = true
                                pendingSeekPostConfirmStartAtMs = now
                                pendingSeekPostConfirmBasePosSec = seekPosition
                                Log.i(
                                    TAG,
                                    "evt=seek_post_confirm_arm target=$target pos=$seekPosition ui_pos=$position elapsed_ms=$seekElapsedMs"
                                )
                                return@scheduleAtFixedRate
                            }
                            val confirmElapsedMs = now - pendingSeekPostConfirmStartAtMs
                            val progressedAfterConfirm = !pendingSeekPostConfirmBasePosSec.isNaN() &&
                                    (seekPosition - pendingSeekPostConfirmBasePosSec) >= seekCompletionPostConfirmMinProgressSec
                            val playingAfterConfirm = nativeIsPlaying(handle)
                            val unhealthyAfterConfirm = playWhenReady &&
                                    largeSeek &&
                                    nonSecureSeek &&
                                    !effectiveLoadingRecovered &&
                                    (loading || state == PlayerState.LOADING || !playingAfterConfirm)
                            if (!progressedAfterConfirm && confirmElapsedMs < seekCompletionPostConfirmWindowMs) {
                                return@scheduleAtFixedRate
                            }
                            if (progressedAfterConfirm &&
                                unhealthyAfterConfirm &&
                                confirmElapsedMs < seekCompletionUnhealthyPostConfirmWindowMs) {
                                if ((now - pendingSeekLastWatchdogLogAtMs) >= 1000L) {
                                    pendingSeekLastWatchdogLogAtMs = now
                                    Log.i(
                                        TAG,
                                        "evt=seek_post_confirm_hold_unhealthy target=$target pos=$seekPosition ui_pos=$position elapsed_ms=$seekElapsedMs confirm_elapsed_ms=$confirmElapsedMs loading=$loading state=${state.name} playing=$playingAfterConfirm source_category=$currentSourceCategory"
                                    )
                                }
                                return@scheduleAtFixedRate
                            }
                            if (!progressedAfterConfirm) {
                                forceTimeoutByNoProgress = true
                                pendingSeekPostConfirmTimedOut = true
                                Log.w(
                                    TAG,
                                    "evt=seek_post_confirm_timeout_no_progress target=$target pos=$seekPosition ui_pos=$position elapsed_ms=$seekElapsedMs confirm_elapsed_ms=$confirmElapsedMs progressed=$progressedAfterConfirm playing=$playingAfterConfirm"
                                )
                                if (playWhenReady && !secondarySyncMode) {
                                    val sinceLastReopenMs = if (lastSeekPostConfirmReopenAtMs > 0L) {
                                        now - lastSeekPostConfirmReopenAtMs
                                    } else {
                                        Long.MAX_VALUE
                                    }
                                    if (sinceLastReopenMs >= seekPostConfirmReopenCooldownMs) {
                                        val reopenStart = maxOf(0.0, target - seekPostConfirmReopenBackoffSec)
                                        lastSeekPostConfirmReopenAtMs = now
                                        pendingSeekTriggeredReopen = true
                                        metricsPlayStallRecoverReopenCount += 1L
                                        Log.w(
                                            TAG,
                                            "evt=seek_post_confirm_timeout_reopen target=$target reopen_start=$reopenStart elapsed_ms=$seekElapsedMs"
                                        )
                                        openExecutor.execute {
                                            if (isReleased) return@execute
                                            replayFrom(reopenStart)
                                        }
                                    } else {
                                        logInfo("evt=seek_post_confirm_timeout_reopen_skip reason=cooldown since_last_ms=$sinceLastReopenMs cooldown_ms=$seekPostConfirmReopenCooldownMs")
                                    }
                                }
                            } else {
                                logInfo("evt=seek_post_confirm_pass target=$target pos=$seekPosition ui_pos=$position confirm_elapsed_ms=$confirmElapsedMs progressed=$progressedAfterConfirm playing=$playingAfterConfirm")
                            }
                        }
                        val requestId = pendingSeekRequestId
                        val summaryHadPostConfirm = pendingSeekHadPostConfirm
                        val summaryPostConfirmTimedOut = pendingSeekPostConfirmTimedOut
                        val summaryTriggeredReopen = pendingSeekTriggeredReopen
                        pendingSeekActive = false
                        pendingSeekLoadingObserved = false
                        pendingSeekTargetSec = Double.NaN
                        pendingSeekFromSec = Double.NaN
                        pendingSeekStartAtMs = 0L
                        pendingSeekConvergedSinceMs = 0L
                        pendingSeekPostConfirmActive = false
                        pendingSeekPostConfirmStartAtMs = 0L
                        pendingSeekPostConfirmBasePosSec = Double.NaN
                        pendingSeekHadPostConfirm = false
                        pendingSeekPostConfirmTimedOut = false
                        pendingSeekTriggeredReopen = false
                        pendingSeekLastWatchdogLogAtMs = 0L
                        metricsSeekCompletedCount += 1L
                        // 关键语义：
                        // 1) 位置未收敛时，hard-timeout / native-watchdog 仍按超时完成；
                        // 2) 位置已收敛但仍长期 loading（典型：状态卡在 LOADING 且不再前进）也要按超时完成，
                        //    以便上层进入统一恢复链路，避免“seek_summary 显示完成但界面持续 loading”。
                        val timeoutDueToLoadingStuck = hardTimeout &&
                                !effectiveLoadingRecovered &&
                                loading &&
                                playWhenReady &&
                                state == PlayerState.LOADING
                        val completeByTimeout = forceTimeoutByNoProgress
                                || timeoutDueToLoadingStuck
                                || ((hardTimeout || nativeConvergedButStuck) && !convergedStable)
                        if (completeByTimeout) {
                            metricsSeekCompletedTimeoutCount += 1L
                        }
                        if (timeoutDueToLoadingStuck) {
                            Log.w(
                                TAG,
                                "evt=seek_complete_timeout_loading_stuck target=$target pos=$seekPosition ui_pos=$position elapsed_ms=$seekElapsedMs hard_timeout=$hardTimeout native_converged_stuck=$nativeConvergedButStuck loading=$loading state=${state.name} source_category=$currentSourceCategory"
                            )
                        }
                        nativeSettleSeekSession(handle, completeByTimeout)
                        var timeoutTriggeredReopen = false
                        val secureSource = currentSourceCategory.startsWith("secure_")
                        if (timeoutDueToLoadingStuck && playWhenReady && secureSource && !secondarySyncMode) {
                            val sinceLastReopenMs = if (lastSeekPostConfirmReopenAtMs > 0L) {
                                now - lastSeekPostConfirmReopenAtMs
                            } else {
                                Long.MAX_VALUE
                            }
                            if (sinceLastReopenMs >= seekPostConfirmReopenCooldownMs) {
                                val reopenStart = maxOf(0.0, target - seekPostConfirmReopenBackoffSec)
                                lastSeekPostConfirmReopenAtMs = now
                                timeoutTriggeredReopen = true
                                metricsPlayStallRecoverReopenCount += 1L
                                Log.w(
                                    TAG,
                                    "evt=seek_timeout_loading_stuck_reopen target=$target reopen_start=$reopenStart elapsed_ms=$seekElapsedMs source_category=$currentSourceCategory"
                                )
                                openExecutor.execute {
                                    if (isReleased) return@execute
                                    replayFrom(reopenStart)
                                }
                            } else {
                                logInfo("evt=seek_timeout_loading_stuck_reopen_skip reason=cooldown since_last_ms=$sinceLastReopenMs cooldown_ms=$seekPostConfirmReopenCooldownMs source_category=$currentSourceCategory")
                            }
                        }
                        val summaryReopenTriggered = summaryTriggeredReopen || timeoutTriggeredReopen
                        val unhealthyAfterComplete = playWhenReady &&
                                (
                                        (!effectiveLoadingRecovered && (loading || state == PlayerState.LOADING))
                                                || (!isPlaying && !effectiveLoadingRecovered)
                                        )
                        if (unhealthyAfterComplete) {
                            val unhealthyMsg =
                                "evt=seek_completed_unhealthy id=$requestId target=$target pos=$seekPosition ui_pos=$position elapsed_ms=$seekElapsedMs by_timeout=$completeByTimeout loading=$loading loading_recovered=$loadingRecovered progress_recovered=$progressRecovered state=${state.name} play_when_ready=$playWhenReady is_playing=$isPlaying native_seek_active=$nativeSeekActive source_category=$currentSourceCategory"
                            if (completeByTimeout || loading) {
                                Log.w(TAG, unhealthyMsg)
                            } else {
                                logInfo(unhealthyMsg)
                            }
                            val unhealthyCheckRequestId = requestId
                            val unhealthyCheckBasePos = seekPosition
                            mainHandler.postDelayed({
                                if (isReleased) return@postDelayed
                                // Skip if a newer seek session has already started.
                                if (pendingSeekActive || pendingSeekRequestId != unhealthyCheckRequestId) {
                                    return@postDelayed
                                }
                                val checkHandle = currentHandle()
                                if (checkHandle == 0L) return@postDelayed
                                val checkPos = getPosition()
                                val checkLoading = isLoading()
                                val checkPlayWhenReady = getPlayWhenReady()
                                val checkIsPlaying = isPlaying()
                                val checkState = coerceStateWithLoading(getState(), checkLoading)
                                val progressed = checkPos - unhealthyCheckBasePos
                                val recovered =
                                    (progressed >= 0.35) ||
                                            (!checkLoading && progressed >= 0.12) ||
                                            (checkPlayWhenReady && checkIsPlaying && progressed >= 0.05)
                                if (recovered) {
                                    logInfo(
                                        "evt=seek_unhealthy_followup_resolved id=$unhealthyCheckRequestId pos=$checkPos progressed=$progressed loading=$checkLoading state=${checkState.name} play_when_ready=$checkPlayWhenReady is_playing=$checkIsPlaying"
                                    )
                                } else {
                                    Log.w(
                                        TAG,
                                        "evt=seek_unhealthy_followup_unresolved id=$unhealthyCheckRequestId pos=$checkPos progressed=$progressed loading=$checkLoading state=${checkState.name} play_when_ready=$checkPlayWhenReady is_playing=$checkIsPlaying source_category=$currentSourceCategory"
                                    )
                                    val secureSource = currentSourceCategory.startsWith("secure_")
                                    val shouldReopenForUnresolved =
                                        checkPlayWhenReady &&
                                                !secondarySyncMode &&
                                                secureSource &&
                                                !checkIsPlaying &&
                                                (checkLoading || checkState == PlayerState.LOADING) &&
                                                progressed < 0.12
                                    if (shouldReopenForUnresolved) {
                                        val followupNow = SystemClock.elapsedRealtime()
                                        val sinceLastReopenMs = if (lastSeekPostConfirmReopenAtMs > 0L) {
                                            followupNow - lastSeekPostConfirmReopenAtMs
                                        } else {
                                            Long.MAX_VALUE
                                        }
                                        if (sinceLastReopenMs >= seekPostConfirmReopenCooldownMs) {
                                            val reopenStart = maxOf(0.0, checkPos - seekPostConfirmReopenBackoffSec)
                                            lastSeekPostConfirmReopenAtMs = followupNow
                                            metricsPlayStallRecoverReopenCount += 1L
                                            Log.w(
                                                TAG,
                                                "evt=seek_unhealthy_followup_reopen id=$unhealthyCheckRequestId reopen_start=$reopenStart pos=$checkPos state=${checkState.name} loading=$checkLoading source_category=$currentSourceCategory"
                                            )
                                            openExecutor.execute {
                                                if (isReleased) return@execute
                                                replayFrom(reopenStart)
                                            }
                                        } else {
                                            logInfo("evt=seek_unhealthy_followup_reopen_skip reason=cooldown id=$unhealthyCheckRequestId since_last_ms=$sinceLastReopenMs cooldown_ms=$seekPostConfirmReopenCooldownMs source_category=$currentSourceCategory")
                                        }
                                    }
                                }
                            }, 1600L)
                        }
                        logInfo("evt=seek_completed id=$requestId target=$target pos=$seekPosition ui_pos=$position elapsed_ms=$seekElapsedMs by_timeout=$completeByTimeout loading_recovered=$loadingRecovered progress_recovered=$progressRecovered converged_stable=$convergedStable hard_timeout=$hardTimeout native_converged_stuck=$nativeConvergedButStuck source_category=$currentSourceCategory")
                        logInfo("evt=seek_summary id=$requestId target=$target from=$from pos=$seekPosition ui_pos=$position elapsed_ms=$seekElapsedMs by_timeout=$completeByTimeout loading=$loading loading_recovered=$loadingRecovered progress_recovered=$progressRecovered native_seek_active=$nativeSeekActive converged_stable=$convergedStable hard_timeout=$hardTimeout native_converged_stuck=$nativeConvergedButStuck timeout_due_to_loading_stuck=$timeoutDueToLoadingStuck post_confirm=$summaryHadPostConfirm post_confirm_timeout=$summaryPostConfirmTimedOut reopen_triggered=$summaryReopenTriggered play_when_ready=$playWhenReady state=${state.name} source_category=$currentSourceCategory")
                        // Minimal diagnostics: arm a short post-seek window and only
                        // emit one key mismatch log when play intent and semantic diverge.
                        seekSettleDiagUntilMs = now + 2000L
                        seekSettleDiagRequestId = requestId
                        seekSettleDiagLogged = false
                        seekSettleDiagStartPosSec = seekPosition
                        if (canEmitDebugDiagLog()) {
                            Log.d(
                                TAG,
                                "evt=seek_settle_key_diag_arm id=$requestId until_ms=$seekSettleDiagUntilMs " +
                                    "pos=$seekPosition ui_pos=$position target=$target state=${state.name} pwr=$playWhenReady playing=$isPlaying"
                            )
                        }
                        // Seek 完成后若目标语义是继续播放，重新武装 stall 监测，
                        // 防止“状态=PLAYING 但位置不再推进”长期卡住。
                        if (playWhenReady) {
                            playStallCheckArmed = true
                            playStallArmedAtMs = now
                            playStallBasePosSec = seekPosition
                            if (manualPlayHardRecoverEnabled) {
                                manualPlayHardRecoverPending = true
                                manualPlayHardRecoverArmedAtMs = now
                                manualPlayHardRecoverBasePosSec = seekPosition
                            }
                        }
                        mainHandler.post {
                            callback?.onSeekCompleted(
                                requestId = requestId,
                                targetPosition = target,
                                currentPosition = position,
                                elapsedMs = seekElapsedMs,
                                byTimeout = completeByTimeout
                            )
                        }
                    }
                }
                if (!lastPositionForLoadingHeuristicSec.isNaN()) {
                    val backwardJump = lastPositionForLoadingHeuristicSec - position
                    if (!pendingSeekActive &&
                        playWhenReady &&
                        backwardJump >= 2.0 &&
                        (now - lastBackwardJumpLogAtMs) >= 2500L
                    ) {
                        lastBackwardJumpLogAtMs = now
                        Log.w(
                            TAG,
                            "evt=position_backward_jump delta=$backwardJump from=$lastPositionForLoadingHeuristicSec to=$position loading=$loading state=${state.name} native_seek_active=${nativeIsSeekSessionActive(handle)}"
                        )
                    }
                }
                lastPositionForLoadingHeuristicSec = position

                // 透传播放中错误（由 core 错误回调写入，Java 侧轮询消费）
                val outCode = IntArray(1)
                val errorMessage = nativeConsumeLastError(handle, outCode)
                if (errorMessage != null && !isReleased) {
                    mainHandler.post {
                        dispatchError(outCode[0], errorMessage)
                    }
                }

                // 透传播放完成事件（由 core 回调写入，Java 侧轮询消费）
                if (nativeConsumePlaybackCompleted(handle) && !isReleased) {
                    val emitCompletedToApp = shouldEmitCompletedToApp(state, playWhenReady)
                    if (!emitCompletedToApp) {
                        if (canEmitDebugDiagLog()) {
                            Log.d(
                                TAG,
                                "evt=playback_completed_blocked_opening_window " +
                                    "state=${state.name} pwr=$playWhenReady loading=$loading pos=$position duration=$duration"
                            )
                        }
                    } else {
                        logInfo("[播放完成] Kotlin 层：收到播放完成事件，position=${getPosition()} duration=${getDuration()} state=${getState()}")
                        mainHandler.post {
                            if (completedCallback != null) {
                                logInfo("[播放完成] Kotlin 层：派发 onPlaybackCompleted 到应用层")
                                completedCallback?.onPlaybackCompleted()
                            } else {
                                Log.w(TAG, "[播放完成] Kotlin 层：completedCallback 为 null，未派发（请调用 setPlaybackCompletedCallback 注册）")
                            }
                        }
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }, 0, 100, TimeUnit.MILLISECONDS)
    }

    private fun maybeRecoverPlayStall(
        handle: Long,
        nowMs: Long,
        positionSec: Double,
        durationSec: Double,
        loading: Boolean,
        state: PlayerState,
        isPlayingNow: Boolean,
        playWhenReady: Boolean
    ) {
        if (secondarySyncMode) return
        if (!playStallRecoveryEnabled) return
        if (!playStallCheckArmed) return
        if (pendingSeekActive) return
        if (!playWhenReady) {
            playStallCheckArmed = false
            return
        }
        val pausedButWantsPlay = state == PlayerState.PAUSED && playWhenReady
        val loadingButWantsPlay = loading && playWhenReady
        if (!(isPlayingNow || state == PlayerState.PLAYING || pausedButWantsPlay || loadingButWantsPlay)) {
            return
        }
        if ((nowMs - playStallLastPlayReqAtMs) > 15000L) {
            playStallCheckArmed = false
            return
        }
        if (durationSec <= 0.0 || positionSec < 0.0) {
            return
        }
        if ((durationSec - positionSec) <= playbackEndClampThresholdSec) {
            playStallCheckArmed = false
            manualPlayHardRecoverPending = false
            playStallRecoverStage = 0
            logInfo("evt=play_stall_recover_skip reason=near_end pos=$positionSec duration=$durationSec")
            return
        }
        if (!playStallBasePosSec.isFinite()) {
            playStallBasePosSec = positionSec
            playStallArmedAtMs = nowMs
            return
        }
        val progressed = positionSec - playStallBasePosSec
        if (progressed >= playStallMinProgressSec) {
            playStallCheckArmed = false
            return
        }
        val elapsedMs = nowMs - playStallArmedAtMs
        if (pausedButWantsPlay && elapsedMs < playStallPausedStateDetectDelayMs) {
            return
        }
        val requiredDelayMs = if (playStallRecoverStage <= 0) {
            playStallDetectDelayMs
        } else {
            playStallRetryDelayMs
        }
        val minDelayWithLoadingMs = if (loading) maxOf(requiredDelayMs, playStallLoadingDetectDelayMs) else requiredDelayMs
        if (elapsedMs < minDelayWithLoadingMs) {
            return
        }
        if (playStallRecoverStage <= 0) {
            if ((nowMs - playStallLastRecoverAtMs) < playStallRecoverCooldownMs) {
                logInfo("evt=play_stall_recover_skip reason=cooldown since_last_ms=${nowMs - playStallLastRecoverAtMs}")
                playStallCheckArmed = false
                return
            }
            val maxRecoverTarget = maxOf(0.0, durationSec - 0.35)
            val recoverTarget = maxOf(0.0, minOf(maxRecoverTarget, positionSec - playStallRecoverReseekBackSec))
            val reseekDelta = abs(positionSec - recoverTarget)
            if (reseekDelta < 0.01) {
                // 首播/起播初期常见 0->0 no-op seek：直接重开更快打破“PLAYING 但进度不走”。
                val reopenStart = recoverTarget
                playStallCheckArmed = false
                playStallRecoverStage = 0
                playStallLastRecoverAtMs = nowMs
                manualPlayHardRecoverPending = false
                metricsPlayStallRecoverReopenCount += 1L
                logInfo("evt=play_stall_recover_noop_seek_reopen base=$positionSec target=$recoverTarget delta=$reseekDelta duration=$durationSec loading=$loading state=${state.name}")
                maybeFallbackToSoftwareDecode(
                    reason = "play_stall_noop_reopen",
                    nowMs = nowMs,
                    positionSec = positionSec,
                    state = state,
                    loading = loading,
                    isPlayingNow = isPlayingNow
                )
                openExecutor.execute {
                    if (isReleased) return@execute
                    replayFrom(reopenStart)
                }
                return
            }
            playStallRecoverStage = 1
            playStallArmedAtMs = nowMs
            playStallBasePosSec = recoverTarget
            pendingSeekRequestId += 1L
            pendingSeekTargetSec = recoverTarget
            pendingSeekFromSec = positionSec
            pendingSeekStartAtMs = nowMs
            pendingSeekLoadingObserved = false
            pendingSeekActive = true
            pendingSeekConvergedSinceMs = 0L
            pendingSeekPostConfirmActive = false
            pendingSeekPostConfirmStartAtMs = 0L
            pendingSeekPostConfirmBasePosSec = Double.NaN
            pendingSeekHadPostConfirm = false
            pendingSeekPostConfirmTimedOut = false
            pendingSeekTriggeredReopen = false
            pendingSeekLastWatchdogLogAtMs = 0L
            seekLoadingSyncRequestId = pendingSeekRequestId
            seekNativeInactiveAtMs = 0L
            seekNativeInactivePosSec = Double.NaN
            seekLoadingSyncGapLogged = false
            loadingSessionLikelySeek = true
            logInfo("evt=play_stall_recover_seek base=$positionSec target=$recoverTarget duration=$durationSec loading=$loading state=${state.name}")
            metricsPlayStallRecoverSeekCount += 1L
            nativeSeekToWithIntent(handle, recoverTarget, true)
            return
        }
        val maxReopenStart = maxOf(0.0, durationSec - 0.35)
        val reopenStart = maxOf(0.0, minOf(maxReopenStart, positionSec - playStallRecoverReseekBackSec))
        if (manualPlayHardRecoverEnabled && manualPlayHardRecoverPending) {
            // 旧逻辑在这里直接 return，会导致“stall_recover 被挡住、hard_recover 又未触发”的卡死窗口。
            // 统一由 stall_recover 执行一次 reopen，并撤销 hard_recover 挂起，避免双重 reopen。
            manualPlayHardRecoverPending = false
            logInfo("evt=play_stall_recover_reopen_takeover reason=manual_hard_recover_pending base=$positionSec reopen_start=$reopenStart")
        }
        playStallCheckArmed = false
        playStallRecoverStage = 0
        playStallLastRecoverAtMs = nowMs
        logInfo("evt=play_stall_recover_reopen base=$positionSec reopen_start=$reopenStart duration=$durationSec")
        metricsPlayStallRecoverReopenCount += 1L
        maybeFallbackToSoftwareDecode(
            reason = "play_stall_reopen",
            nowMs = nowMs,
            positionSec = positionSec,
            state = state,
            loading = loading,
            isPlayingNow = isPlayingNow
        )
        openExecutor.execute {
            if (isReleased) return@execute
            replayFrom(reopenStart)
        }
    }

    private fun maybeRecoverPlaybackLoop(
        handle: Long,
        nowMs: Long,
        positionSec: Double,
        durationSec: Double,
        loading: Boolean,
        state: PlayerState,
        isPlayingNow: Boolean,
        playWhenReady: Boolean
    ) {
        if (secondarySyncMode) return
        if (!playLoopRecoveryEnabled) return
        if (durationSec <= 0.0 || positionSec < 0.0) return
        if (nowMs < playLoopSuppressUntilMs) {
            playLoopMaxPosSec = if (positionSec.isFinite()) positionSec else Double.NaN
            playLoopAnchorPosSec = playLoopMaxPosSec
            playLoopHitCount = 0
            playLoopWindowStartMs = 0L
            if (canEmitDebugDiagLog()) {
                Log.d(
                    TAG,
                    "evt=play_loop_recover_suppressed reason=open_window pos=$positionSec " +
                        "duration=$durationSec suppress_left_ms=${playLoopSuppressUntilMs - nowMs}"
                )
            }
            return
        }
        if (playLoopMaxPosSec.isFinite() && playLoopMaxPosSec > durationSec + 1.0) {
            Log.w(
                TAG,
                "evt=play_loop_state_reset reason=max_exceeds_duration max=$playLoopMaxPosSec " +
                    "pos=$positionSec duration=$durationSec source_category=$currentSourceCategory"
            )
            playLoopMaxPosSec = positionSec
            playLoopAnchorPosSec = positionSec
            playLoopHitCount = 0
            playLoopWindowStartMs = 0L
            return
        }
        if ((durationSec - positionSec) <= playbackEndClampThresholdSec) {
            playLoopMaxPosSec = positionSec
            playLoopAnchorPosSec = positionSec
            playLoopHitCount = 0
            playLoopWindowStartMs = 0L
            return
        }
        if (pendingSeekActive || loading || !playWhenReady) {
            if (positionSec.isFinite()) {
                playLoopMaxPosSec = positionSec
                playLoopAnchorPosSec = positionSec
            }
            playLoopHitCount = 0
            playLoopWindowStartMs = 0L
            return
        }
        val wantsPlayback = isPlayingNow || state == PlayerState.PLAYING || (state == PlayerState.PAUSED && playWhenReady)
        if (!wantsPlayback) return

        if (!playLoopMaxPosSec.isFinite() || positionSec > playLoopMaxPosSec) {
            playLoopMaxPosSec = positionSec
        }
        if (!playLoopAnchorPosSec.isFinite()) {
            playLoopAnchorPosSec = playLoopMaxPosSec
        }

        val backwardSec = playLoopMaxPosSec - positionSec
        val forwardSpanSec = playLoopMaxPosSec - playLoopAnchorPosSec
        if (backwardSec < playLoopBackwardThresholdSec || forwardSpanSec < playLoopMinForwardSpanSec) {
            return
        }

        if (playLoopWindowStartMs <= 0L || (nowMs - playLoopWindowStartMs) > playLoopWindowMs) {
            playLoopWindowStartMs = nowMs
            playLoopHitCount = 1
        } else {
            playLoopHitCount += 1
        }

        if (playLoopHitCount < playLoopRequiredHits) {
            return
        }
        if ((nowMs - playLoopLastRecoverAtMs) < playLoopRecoverCooldownMs) {
            return
        }

        val maxPosBeforeRecover = playLoopMaxPosSec
        val maxRecoverTarget = maxOf(0.0, durationSec - 0.35)
        val recoverTarget = maxOf(0.0, minOf(maxRecoverTarget, maxPosBeforeRecover - playLoopRecoverBackoffSec))
        playLoopLastRecoverAtMs = nowMs
        playLoopHitCount = 0
        playLoopWindowStartMs = 0L
        playLoopAnchorPosSec = recoverTarget
        playLoopMaxPosSec = recoverTarget

        pendingSeekRequestId += 1L
        pendingSeekTargetSec = recoverTarget
        pendingSeekFromSec = positionSec
        pendingSeekStartAtMs = nowMs
        pendingSeekLoadingObserved = false
        pendingSeekActive = true
        pendingSeekConvergedSinceMs = 0L
        pendingSeekPostConfirmActive = false
        pendingSeekPostConfirmStartAtMs = 0L
        pendingSeekPostConfirmBasePosSec = Double.NaN
        pendingSeekHadPostConfirm = false
        pendingSeekPostConfirmTimedOut = false
        pendingSeekTriggeredReopen = false
        pendingSeekLastWatchdogLogAtMs = 0L
        seekLoadingSyncRequestId = pendingSeekRequestId
        seekNativeInactiveAtMs = 0L
        seekNativeInactivePosSec = Double.NaN
        seekLoadingSyncGapLogged = false
        loadingSessionLikelySeek = true

        Log.w(
            TAG,
            "evt=play_loop_recover_seek pos=$positionSec max=$maxPosBeforeRecover back=$backwardSec target=$recoverTarget hits=$playLoopHitCount required=$playLoopRequiredHits"
        )
        metricsPlayLoopRecoverCount += 1L
        nativeSeekToWithIntent(handle, recoverTarget, true)
    }

    private fun maybeRecoverManualPlayHardStall(
        nowMs: Long,
        positionSec: Double,
        durationSec: Double,
        loading: Boolean,
        state: PlayerState,
        isPlayingNow: Boolean,
        playWhenReady: Boolean
    ) {
        if (secondarySyncMode) return
        if (!manualPlayHardRecoverEnabled) return
        if (!manualPlayHardRecoverPending) return
        if (pendingSeekActive) return
        if (!playWhenReady) {
            manualPlayHardRecoverPending = false
            return
        }
        if ((nowMs - playStallLastPlayReqAtMs) > 15000L) {
            manualPlayHardRecoverPending = false
            return
        }
        if (!manualPlayHardRecoverBasePosSec.isFinite()) {
            manualPlayHardRecoverBasePosSec = positionSec
            manualPlayHardRecoverArmedAtMs = nowMs
            return
        }
        val progressed = positionSec - manualPlayHardRecoverBasePosSec
        if (progressed >= manualPlayHardRecoverMinProgressSec) {
            manualPlayHardRecoverPending = false
            return
        }
        if ((nowMs - manualPlayHardRecoverArmedAtMs) < manualPlayHardRecoverDelayMs) {
            return
        }
        if ((nowMs - manualPlayHardRecoverLastAtMs) < manualPlayHardRecoverCooldownMs) {
            manualPlayHardRecoverPending = false
            logInfo("evt=manual_play_hard_recover_skip reason=cooldown since_last_ms=${nowMs - manualPlayHardRecoverLastAtMs}")
            return
        }
        if (durationSec <= 0.0 || positionSec < 0.0) {
            return
        }
        // 当点播放后持续卡在 loading/paused，或虽是 PLAYING 但位置长期不动时，直接重开解冻。
        val likelyPlayingButFrozen = state == PlayerState.PLAYING && isPlayingNow && progressed < manualPlayHardRecoverMinProgressSec
        val likelyStuck = loading || state == PlayerState.PAUSED || !isPlayingNow || likelyPlayingButFrozen
        if (!likelyStuck) {
            return
        }
        val maxReopenStart = maxOf(0.0, durationSec - 0.35)
        val reopenStart = maxOf(0.0, minOf(maxReopenStart, positionSec - playStallRecoverReseekBackSec))
        manualPlayHardRecoverPending = false
        playStallCheckArmed = false
        playStallRecoverStage = 0
        manualPlayHardRecoverLastAtMs = nowMs
        metricsManualPlayHardRecoverCount += 1L
        Log.w(
            TAG,
            "evt=manual_play_hard_recover_reopen base=$positionSec reopen_start=$reopenStart state=${state.name} loading=$loading"
        )
        maybeFallbackToSoftwareDecode(
            reason = "manual_hard_recover_reopen",
            nowMs = nowMs,
            positionSec = positionSec,
            state = state,
            loading = loading,
            isPlayingNow = isPlayingNow
        )
        openExecutor.execute {
            if (isReleased) return@execute
            replayFrom(reopenStart)
        }
    }

    private fun maybeLogPlaybackMetrics(
        nowMs: Long,
        positionSec: Double,
        state: PlayerState,
        loading: Boolean
    ) {
        if (!playbackMetricsLogEnabled) return
        if (metricsLastLogAtMs > 0L && (nowMs - metricsLastLogAtMs) < metricsLogIntervalMs) return
        metricsLastLogAtMs = nowMs
        logInfo(
            "evt=playback_stability_metrics " +
                "seek_completed=$metricsSeekCompletedCount " +
                "seek_timeout=$metricsSeekCompletedTimeoutCount " +
                "stall_recover_seek=$metricsPlayStallRecoverSeekCount " +
                "stall_recover_reopen=$metricsPlayStallRecoverReopenCount " +
                "manual_play_hard_recover=$metricsManualPlayHardRecoverCount " +
                "loop_recover=$metricsPlayLoopRecoverCount " +
                "state=${state.name} loading=$loading pos=$positionSec"
        )
    }

    // 释放资源
    fun release() {
        synchronized(lifecycleLock) {
            if (isReleased) return
            isReleased = true
        }
        updateExecutor?.shutdownNow()
        // 不在主线程等待 openExecutor/锁，避免 Activity.onDestroy ANR。
        // 真实 native 释放放到 openExecutor 串行收尾，确保与 open/reopen 同队列。
        try {
            openExecutor.execute {
                finalizeReleaseOnWorker()
            }
        } catch (_: RejectedExecutionException) {
            Thread {
                finalizeReleaseOnWorker()
            }.start()
        }
    }

    private fun finalizeReleaseOnWorker() {
        // 与 openWithPlayModel 的串行锁对齐，确保不会与正在进行的 open 交叉释放。
        synchronized(openSerialLock) {
            // lifecycle barrier only
        }

        val handle: Long
        synchronized(lifecycleLock) {
            handle = nativeHandle
            nativeHandle = 0
        }
        if (handle != 0L) {
            nativeSetSurface(handle, null)
            textureDecoderSurface?.release()
            textureDecoderSurface = null
            nativeRelease(handle)
        }

        lastLoadingState = null
        loadingCandidateState = null
        loadingCandidateSinceMs = 0L
        loadingSessionLikelySeek = false
        suppressLoadingShowUntilMs = 0L
        lastPositionForLoadingHeuristicSec = Double.NaN
        lastBackwardJumpLogAtMs = 0L
        pendingSeekActive = false
        pendingSeekLoadingObserved = false
        pendingSeekTargetSec = Double.NaN
        pendingSeekFromSec = Double.NaN
        pendingSeekStartAtMs = 0L
        pendingSeekConvergedSinceMs = 0L
        pendingSeekPostConfirmActive = false
        pendingSeekPostConfirmStartAtMs = 0L
        pendingSeekPostConfirmBasePosSec = Double.NaN
        pendingSeekHadPostConfirm = false
        pendingSeekPostConfirmTimedOut = false
        pendingSeekTriggeredReopen = false
        lastSeekPostConfirmReopenAtMs = 0L
        pendingSeekLastWatchdogLogAtMs = 0L
        seekLoadingSyncRequestId = -1L
        seekNativeInactiveAtMs = 0L
        seekNativeInactivePosSec = Double.NaN
        seekLoadingSyncGapLogged = false
        reopenUiAnchorActive = false
        reopenUiAnchorPosSec = Double.NaN
        reopenUiAnchorUntilMs = 0L
        playStallCheckArmed = false
        playStallArmedAtMs = 0L
        playStallBasePosSec = Double.NaN
        playStallRecoverStage = 0
        playStallLastRecoverAtMs = 0L
        playStallLastPlayReqAtMs = 0L
        manualPlayHardRecoverPending = false
        manualPlayHardRecoverArmedAtMs = 0L
        manualPlayHardRecoverBasePosSec = Double.NaN
        manualPlayHardRecoverLastAtMs = 0L
        playLoopMaxPosSec = Double.NaN
        playLoopAnchorPosSec = Double.NaN
        playLoopHitCount = 0
        playLoopWindowStartMs = 0L
        playLoopLastRecoverAtMs = 0L
        playLoopSuppressUntilMs = 0L
        metricsLastLogAtMs = 0L
        metricsSeekCompletedCount = 0L
        metricsSeekCompletedTimeoutCount = 0L
        metricsPlayStallRecoverSeekCount = 0L
        metricsPlayStallRecoverReopenCount = 0L
        metricsPlayLoopRecoverCount = 0L
        networkLoadingSinceMs = 0L
        networkTotalStallMs = 0L
        networkReconnectCount = 0
        autoReopenAttemptCount = 0
        autoReopenInFlight = false
        decodeFallbackTriedForCurrentSource = false
        decodeFallbackLastAtMs = 0L
        lastOpenUrl = null
        lastOpenPlayModel = null
        lastOpenStartPosition = 0.0
        lastPlayerState = null
        lastPipelineState = null
        lastIsPlayingState = null
        lastPlaybackSnapshot = null
        desiredPlayWhenReady = false
        val currentGeneration = playbackCommandGeneration.get()
        playbackCommandAppliedGeneration.set(currentGeneration)
        openExecutor.shutdown()
    }

    // Native 方法声明
    private external fun nativeCreate(): Long
    private external fun nativeRelease(handle: Long)
    private external fun nativeSetSurface(handle: Long, surface: Surface?)
    private external fun nativeUpdateSurfaceSize(handle: Long, width: Int, height: Int)
    private external fun nativeOpenURL(handle: Long, url: String): Boolean
    private external fun nativeOpenURLWithStartPosition(handle: Long, url: String, startPosition: Double): Boolean
    private external fun nativeOpenWithCustomHTTP(handle: Long, url: String, timeoutMs: Int, maxRetries: Int, encryptedFile: Boolean): Boolean
    private external fun nativeOpenWithCustomFile(handle: Long, path: String, avioBufferSize: Int, encryptedFile: Boolean): Boolean
    private external fun nativeOpenWithSecureSession(
        handle: Long,
        url: String,
        startPosition: Double,
        authToken: String?,
        videoId: String?,
        deviceId: String?,
        secretId: String?,
        nonce: String?,
        playSessionId: String?,
        secureHeaders: String?,
        sessionExpireAtMs: Long,
        keyMode: Int,
        keyMaterialB64: String?,
        keyIvHex: String?
    ): Boolean
    private external fun nativePlay(handle: Long)
    private external fun nativePause(handle: Long)
    private external fun nativeStop(handle: Long)
    private external fun nativeSeekTo(handle: Long, position: Double)
    private external fun nativeSeekToWithIntent(handle: Long, position: Double, resumeAfterSeek: Boolean)
    private external fun nativeSetPlaybackRate(handle: Long, rate: Float)
    private external fun nativeSetVolume(handle: Long, volume: Float)
    private external fun nativeSetAspectRatioMode(handle: Long, mode: Int)
    private external fun nativeSetDecodeMode(handle: Long, mode: Int)
    private external fun nativeSetSecureSeekTuning(
        handle: Long,
        dropOnlyWindowBackwardSec: Double,
        dropOnlyWindowForwardSec: Double,
        acceptFutureBackwardEarlySec: Double,
        acceptFutureForwardEarlySec: Double,
        acceptFutureBackwardMidSec: Double,
        acceptFutureForwardMidSec: Double,
        acceptFutureBackwardLateSec: Double,
        acceptFutureForwardLateSec: Double,
        lowerBoundDeadlineNormalMs: Int,
        lowerBoundDeadlineLargeMs: Int,
        recoveryDeadlineNormalMs: Int,
        recoveryDeadlineLargeMs: Int,
        audioWaitDeadlineNormalMs: Int,
        audioWaitDeadlineLargeMs: Int
    )
    private external fun nativeResetSecureSeekTuning(handle: Long)
    private external fun nativeGetDuration(handle: Long): Double
    private external fun nativeGetPosition(handle: Long): Double
    private external fun nativeGetState(handle: Long): Int
    private external fun nativeGetPipelineState(handle: Long): Int
    private external fun nativeGetPlayWhenReady(handle: Long): Boolean
    private external fun nativeIsPlaying(handle: Long): Boolean
    private external fun nativeSetPlayWhenReady(handle: Long, playWhenReady: Boolean)
    private external fun nativeIsLoading(handle: Long): Boolean
    private external fun nativeIsHardwareDecodingActive(handle: Long): Boolean
    private external fun nativeIsSeekSessionActive(handle: Long): Boolean
    private external fun nativeConsumeLastError(handle: Long, outCode: IntArray): String?
    private external fun nativeConsumePlaybackCompleted(handle: Long): Boolean
    private external fun nativeSettleSeekSession(handle: Long, byTimeout: Boolean)

    // 获取当前是否处于加载中（可用于主动查询 UI 状态）
    fun isLoading(): Boolean {
        val handle = currentHandle()
        if (handle == 0L || isReleased) {
            return false
        }
        return nativeIsLoading(handle)
    }
}
