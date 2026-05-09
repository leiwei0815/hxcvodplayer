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
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit

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

    /** 与 iOS 对齐的数据源模式 */
    enum class PlayerDataSourceMode {
        DEFAULT,
        CUSTOM_HTTP,
        CUSTOM_FILE
    }

    /** 解码模式（默认软解，播放前设置） */
    enum class DecodeMode {
        SOFTWARE,
        HARDWARE
    }

    /** 对齐 iOS 的视频模型（fileid + appid + sign） */
    class PlayerVideo {
        var videoId: Int = 0
        var sign: String = ""
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
    }

    /**
     * 播放完成回调接口。
     * 视频播放到末尾自然结束时触发，在主线程回调。
     */
    interface PlaybackCompletedCallback {
        fun onPlaybackCompleted()
    }

    private var nativeHandle: Long = 0
    private var callback: PlayerCallback? = null
    private var completedCallback: PlaybackCompletedCallback? = null
    private var lastPlayerState: PlayerState? = null
    private var lastPipelineState: PipelineState? = null
    private var lastIsPlayingState: Boolean? = null
    private var updateExecutor: ScheduledExecutorService? = null
    private var lastLoadingState: Boolean? = null
    private var loadingCandidateState: Boolean? = null
    private var loadingCandidateSinceMs: Long = 0L
    private val openExecutor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val loadingShowDebounceMs = 300L
    private val loadingHideDebounceMs = 150L
    private val lifecycleLock = Any()
    @Volatile private var isReleased = false
    private var autoReopenOnRecoverableErrorEnabled: Boolean = false
    private var autoReopenMaxAttempts: Int = 1
    private var autoReopenAttemptCount: Int = 0
    private var autoReopenInFlight: Boolean = false
    private var networkLoadingSinceMs: Long = 0L
    private var networkTotalStallMs: Long = 0L
    private var networkReconnectCount: Int = 0
    private var lastOpenUrl: String? = null
    private var lastOpenStartPosition: Double = 0.0
    private var lastOpenPlayModel: PlayerDataSourcePlayModel? = null
    @Volatile private var decodeMode: DecodeMode = DecodeMode.SOFTWARE

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
        renderView = createRenderView()
        startPositionUpdates()
    }

    // 设置回调
    fun setCallback(callback: PlayerCallback) {
        this.callback = callback
    }

    /**
     * 设置播放完成回调。
     * 视频自然播放到末尾时在主线程触发 [PlaybackCompletedCallback.onPlaybackCompleted]。
     */
    fun setPlaybackCompletedCallback(callback: PlaybackCompletedCallback?) {
        this.completedCallback = callback
    }

    /**
     * 配置“可恢复错误后自动重开”能力（默认关闭）。
     */
    fun configureWeakNetworkRecovery(enabled: Boolean, maxAttempts: Int = 1) {
        autoReopenOnRecoverableErrorEnabled = enabled
        autoReopenMaxAttempts = maxAttempts.coerceAtLeast(0)
    }

    private fun clonePlayModel(model: PlayerDataSourcePlayModel): PlayerDataSourcePlayModel {
        return PlayerDataSourcePlayModel().apply {
            url = model.url
            mode = model.mode
            encryptedFile = model.encryptedFile
            video = model.video
        }
    }

    private fun notifyNetworkQoE(currentStallMs: Long) {
        callback?.onNetworkQoEUpdated(currentStallMs, networkTotalStallMs, networkReconnectCount)
    }

    private fun currentHandle(): Long {
        synchronized(lifecycleLock) {
            return nativeHandle
        }
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
        val handle = currentHandle()
        if (handle == 0L || isReleased) {
            dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "播放器已释放，无法打开播放模型")
            return false
        }
        if (model.url.isBlank()) {
            dispatchError(PlayerErrorCode.INVALID_URL, "URL 不能为空")
            return false
        }
        if (!licenseAllowedOrNotify("openWithPlayModel")) return false
        lastOpenUrl = model.url
        lastOpenStartPosition = maxOf(0.0, startPosition)
        lastOpenPlayModel = clonePlayModel(model)
        if (!autoReopenInFlight) {
            autoReopenAttemptCount = 0
            networkTotalStallMs = 0L
            networkReconnectCount = 0
        }
        applyDecodeModeForHandle(handle)
        val result = when (model.mode) {
            PlayerDataSourceMode.DEFAULT ->
                nativeOpenURLWithStartPosition(handle, model.url, lastOpenStartPosition)
            PlayerDataSourceMode.CUSTOM_HTTP -> {
                val cfg = effectiveDataSourceConfig()
                nativeOpenWithCustomHTTP(handle, model.url, cfg.timeoutMs, cfg.maxRetries, model.encryptedFile)
            }
            PlayerDataSourceMode.CUSTOM_FILE -> {
                val cfg = effectiveDataSourceConfig()
                nativeOpenWithCustomFile(handle, model.url, cfg.avioBufferSize, model.encryptedFile)
            }
        }
        if (!result) {
            dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "打开失败: mode=${model.mode}, url=${model.url}")
        }
        return result
    }

    fun setDecodeMode(mode: DecodeMode) {
        decodeMode = mode
        val handle = currentHandle()
        if (handle != 0L && !isReleased) {
            nativeSetDecodeMode(handle, if (mode == DecodeMode.HARDWARE) 1 else 0)
        }
    }

    fun getDecodeMode(): DecodeMode = decodeMode

    private fun applyDecodeModeForHandle(handle: Long) {
        nativeSetDecodeMode(handle, if (decodeMode == DecodeMode.HARDWARE) 1 else 0)
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
                openWithPlayModel(retryModel)
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
        val handle = currentHandle()
        if (handle == 0L || isReleased) {
            dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "播放器已释放，无法打开 URL: $url")
            return false
        }
        if (!licenseAllowedOrNotify("openURL")) {
            return false
        }
        lastOpenUrl = url
        lastOpenStartPosition = startPosition
        lastOpenPlayModel = null
        if (!autoReopenInFlight) {
            autoReopenAttemptCount = 0
            networkTotalStallMs = 0L
            networkReconnectCount = 0
        }
        applyDecodeModeForHandle(handle)
        // 始终用带起始位置的接口，确保 startPosition=0 时也能清除上次残留进度
        val result = nativeOpenURLWithStartPosition(handle, url, startPosition)

        if (!result) {
            dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "无法打开 URL: $url")
        }
        return result
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
        lastOpenUrl = url
        lastOpenStartPosition = startPosition
        lastOpenPlayModel = null
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

                if (!result && !isReleased) {
                    mainHandler.post {
                        dispatchError(PlayerErrorCode.OPEN_INPUT_FAILED, "无法打开 URL: $url")
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
        nativePlay(handle)
    }

    // 暂停
    fun pause() {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        nativePause(handle)
    }

    // 停止
    fun stop() {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        nativeStop(handle)
        networkLoadingSinceMs = 0L
    }

    // 跳转
    fun seekTo(position: Double) {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        if (!licenseAllowedOrNotify("seekTo")) {
            return
        }
        nativeSeekTo(handle, position)
    }

    // 设置播放速度
    fun setPlaybackRate(rate: Float) {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        var normalizedRate = if (rate.isFinite()) rate else 1.0f
        normalizedRate = normalizedRate.coerceIn(MIN_PLAYBACK_RATE, MAX_PLAYBACK_RATE)
        if (abs(normalizedRate - MAX_PLAYBACK_RATE) <= MAX_PLAYBACK_RATE_SNAP_EPSILON) {
            normalizedRate = MAX_PLAYBACK_RATE
        }
        nativeSetPlaybackRate(handle, normalizedRate)
    }

    // 设置音量
    fun setVolume(volume: Float) {
        val handle = currentHandle()
        if (handle == 0L || isReleased) return
        nativeSetVolume(handle, volume)
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
        nativeSetPlayWhenReady(handle, playWhenReady)
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
        if (coreStateRaw == -1 || pipelineStateRaw == 5) {
            return PlayerState.ERROR
        }
        if (coreStateRaw == 4 || pipelineStateRaw == 4) {
            return PlayerState.STOPPED
        }
        if (coreStateRaw == 0 || pipelineStateRaw == 0) {
            return PlayerState.IDLE
        }
        if (coreStateRaw == 1 || pipelineStateRaw == 1) {
            return PlayerState.OPENING
        }
        if (pipelineStateRaw == 2) {
            return PlayerState.LOADING
        }
        if (isPlayingNow) {
            return PlayerState.PLAYING
        }
        if (coreStateRaw == 3 || !playWhenReady) {
            return PlayerState.PAUSED
        }
        return mapPlayerState(coreStateRaw)
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

                val position = getPosition()
                val duration = getDuration()
                val coreStateRaw = nativeGetState(handle)
                val pipelineStateRaw = nativeGetPipelineState(handle)
                val playWhenReady = nativeGetPlayWhenReady(handle)
                val isPlaying = nativeIsPlaying(handle)
                val state = resolveUnifiedState(coreStateRaw, pipelineStateRaw, playWhenReady, isPlaying)

                if (lastPlayerState != state) {
                    lastPlayerState = state
                    mainHandler.post { callback?.onPlayerStateChanged(state) }
                }
                lastPipelineState = mapPipelineState(pipelineStateRaw)
                lastIsPlayingState = isPlaying
                if (duration > 0) {
                    callback?.onPlayerPositionUpdated(position, duration)
                }

                val loading = isLoading()
                val now = SystemClock.elapsedRealtime()
                if (loadingCandidateState == null || loadingCandidateState != loading) {
                    loadingCandidateState = loading
                    loadingCandidateSinceMs = now
                } else {
                    val debounceMs = if (loading) loadingShowDebounceMs else loadingHideDebounceMs
                    if (lastLoadingState != loading && (now - loadingCandidateSinceMs) >= debounceMs) {
                        lastLoadingState = loading
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
                    Log.i(TAG, "[播放完成] Kotlin 层：收到播放完成事件，position=${getPosition()} duration=${getDuration()} state=${getState()}")
                    mainHandler.post {
                        if (completedCallback != null) {
                            Log.i(TAG, "[播放完成] Kotlin 层：派发 onPlaybackCompleted 到应用层")
                            completedCallback?.onPlaybackCompleted()
                        } else {
                            Log.w(TAG, "[播放完成] Kotlin 层：completedCallback 为 null，未派发（请调用 setPlaybackCompletedCallback 注册）")
                        }
                    }
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }, 0, 100, TimeUnit.MILLISECONDS)
    }

    // 释放资源
    fun release() {
        synchronized(lifecycleLock) {
            if (isReleased) return
            isReleased = true
        }

        updateExecutor?.shutdownNow()
        openExecutor.shutdownNow()
        try {
            updateExecutor?.awaitTermination(800, TimeUnit.MILLISECONDS)
            openExecutor.awaitTermination(800, TimeUnit.MILLISECONDS)
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
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
        networkLoadingSinceMs = 0L
        networkTotalStallMs = 0L
        networkReconnectCount = 0
        autoReopenAttemptCount = 0
        autoReopenInFlight = false
        lastOpenUrl = null
        lastOpenPlayModel = null
        lastOpenStartPosition = 0.0
        lastPlayerState = null
        lastPipelineState = null
        lastIsPlayingState = null
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
    private external fun nativePlay(handle: Long)
    private external fun nativePause(handle: Long)
    private external fun nativeStop(handle: Long)
    private external fun nativeSeekTo(handle: Long, position: Double)
    private external fun nativeSetPlaybackRate(handle: Long, rate: Float)
    private external fun nativeSetVolume(handle: Long, volume: Float)
    private external fun nativeSetAspectRatioMode(handle: Long, mode: Int)
    private external fun nativeSetDecodeMode(handle: Long, mode: Int)
    private external fun nativeGetDuration(handle: Long): Double
    private external fun nativeGetPosition(handle: Long): Double
    private external fun nativeGetState(handle: Long): Int
    private external fun nativeGetPipelineState(handle: Long): Int
    private external fun nativeGetPlayWhenReady(handle: Long): Boolean
    private external fun nativeIsPlaying(handle: Long): Boolean
    private external fun nativeSetPlayWhenReady(handle: Long, playWhenReady: Boolean)
    private external fun nativeIsLoading(handle: Long): Boolean
    private external fun nativeIsHardwareDecodingActive(handle: Long): Boolean
    private external fun nativeConsumeLastError(handle: Long, outCode: IntArray): String?
    private external fun nativeConsumePlaybackCompleted(handle: Long): Boolean

    // 获取当前是否处于加载中（可用于主动查询 UI 状态）
    fun isLoading(): Boolean {
        val handle = currentHandle()
        if (handle == 0L || isReleased) {
            return false
        }
        return nativeIsLoading(handle)
    }
}
