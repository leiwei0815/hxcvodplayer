package com.hxcplayer.monitor

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import org.json.JSONObject
import java.security.MessageDigest
import java.util.Locale
import java.util.UUID

/**
 * Android 播放监控会话（字段与 iOS 对齐）
 */
class HXCPlayerMonitorSession(
    context: Context,
    config: HXCPlayerMonitorConfig,
    private val sdkVersion: String = "1.0.13"
) {
    companion object {
        // 事件码（对齐 MonitorEventType 枚举）
        const val CODE_PLAY_SESSION_START = 0
        const val CODE_OPEN_BEGIN = 1
        const val CODE_OPEN_SUCCESS = 2
        const val CODE_OPEN_FAIL = 3
        const val CODE_STATE_CHANGE = 4
        const val CODE_FIRST_FRAME = 5
        const val CODE_POSITION_HEARTBEAT = 6
        const val CODE_SEEK_BEGIN = 7
        const val CODE_SEEK_COMPLETE = 8
        const val CODE_SEEK_FAIL = 9
        const val CODE_LOADING_BEGIN = 10
        const val CODE_LOADING_END = 11
        const val CODE_NETWORK_CHANGE = 12
        const val CODE_FFMPEG_IO = 13
        const val CODE_DECODE_ERROR = 14
        const val CODE_RENDER_ERROR = 15
        const val CODE_AUDIO_ERROR = 16
        const val CODE_PLAY_COMPLETE = 17
        const val CODE_PLAY_SESSION_END = 18
        const val CODE_DECODE_PATH_RESOLVED = 138
        const val CODE_OPEN_URL_RESOLVED = 139
        const val CODE_TRACE = 150
        const val CODE_NETWORK_SNAPSHOT = 160
        const val CODE_NETWORK_WEAK_SIGNAL = 162
        const val CODE_VIDEO_RENDER_STALL = 130
        const val CODE_USER_PLAY = 100
        const val CODE_USER_PAUSE = 101
        const val CODE_USER_SEEK = 102
        const val CODE_USER_STOP = 103
        const val CODE_USER_RATE_CHANGE = 104
        const val CODE_APP_ENTER_BACKGROUND = 105
        const val CODE_APP_ENTER_FOREGROUND = 106
        const val CODE_PIP_WILL_START = 110
        const val CODE_PIP_DID_START = 111
        const val CODE_PIP_WILL_STOP = 112
        const val CODE_PIP_DID_STOP = 113
        const val CODE_PIP_START_FAIL = 114
        const val CODE_PIP_RESTORE_UI = 115
        const val CODE_PIP_USER_START = 116
        const val CODE_PAGE_ENTER = 170
        const val CODE_AUTH_REQUEST = 171
        const val CODE_AUTH_SUCCESS = 172
        const val CODE_AUTH_FAIL = 173
        const val CODE_SWITCH_VIDEO = 174
        const val CODE_REPLAY = 175
        const val CODE_SWITCH_EPISODE = 176
        const val CODE_KERNEL_FALLBACK = 177
        const val CODE_AUTO_REOPEN = 178
        const val CODE_STALL_RECOVER = 179

        private val NETWORK_TYPED_CODES = setOf(
            CODE_PLAY_SESSION_START, CODE_OPEN_BEGIN, CODE_OPEN_SUCCESS, CODE_OPEN_FAIL,
            CODE_SEEK_COMPLETE, CODE_SEEK_FAIL, CODE_LOADING_BEGIN, CODE_LOADING_END,
            CODE_PLAY_COMPLETE, CODE_PLAY_SESSION_END, CODE_DECODE_PATH_RESOLVED,
            CODE_OPEN_URL_RESOLVED, CODE_DECODE_ERROR, CODE_NETWORK_SNAPSHOT,
            CODE_NETWORK_WEAK_SIGNAL, CODE_FFMPEG_IO, CODE_VIDEO_RENDER_STALL
        )

        fun eventNameToCode(name: String): Int = when (name) {
            "play_session_start" -> CODE_PLAY_SESSION_START
            "open_begin" -> CODE_OPEN_BEGIN
            "open_success" -> CODE_OPEN_SUCCESS
            "open_fail" -> CODE_OPEN_FAIL
            "state_change" -> CODE_STATE_CHANGE
            "first_frame" -> CODE_FIRST_FRAME
            "position_heartbeat" -> CODE_POSITION_HEARTBEAT
            "seek_begin" -> CODE_SEEK_BEGIN
            "seek_complete" -> CODE_SEEK_COMPLETE
            "seek_fail" -> CODE_SEEK_FAIL
            "loading_begin" -> CODE_LOADING_BEGIN
            "loading_end" -> CODE_LOADING_END
            "network_change" -> CODE_NETWORK_CHANGE
            "ffmpeg_io_event" -> CODE_FFMPEG_IO
            "decode_error" -> CODE_DECODE_ERROR
            "render_error" -> CODE_RENDER_ERROR
            "audio_error" -> CODE_AUDIO_ERROR
            "play_complete" -> CODE_PLAY_COMPLETE
            "play_session_end" -> CODE_PLAY_SESSION_END
            "decode_path_resolved" -> CODE_DECODE_PATH_RESOLVED
            "open_url_resolved" -> CODE_OPEN_URL_RESOLVED
            "video_render_stall" -> CODE_VIDEO_RENDER_STALL
            "trace" -> CODE_TRACE
            "network_snapshot" -> CODE_NETWORK_SNAPSHOT
            "network_weak_signal" -> CODE_NETWORK_WEAK_SIGNAL
            "user_play" -> CODE_USER_PLAY
            "user_pause" -> CODE_USER_PAUSE
            "user_seek" -> CODE_USER_SEEK
            "user_stop" -> CODE_USER_STOP
            "user_rate_change" -> CODE_USER_RATE_CHANGE
            "app_enter_background" -> CODE_APP_ENTER_BACKGROUND
            "app_enter_foreground" -> CODE_APP_ENTER_FOREGROUND
            "pip_will_start" -> CODE_PIP_WILL_START
            "pip_did_start" -> CODE_PIP_DID_START
            "pip_will_stop" -> CODE_PIP_WILL_STOP
            "pip_did_stop" -> CODE_PIP_DID_STOP
            "pip_start_fail" -> CODE_PIP_START_FAIL
            "pip_restore_ui" -> CODE_PIP_RESTORE_UI
            "pip_user_start" -> CODE_PIP_USER_START
            "page_enter" -> CODE_PAGE_ENTER
            "auth_request" -> CODE_AUTH_REQUEST
            "auth_success" -> CODE_AUTH_SUCCESS
            "auth_fail" -> CODE_AUTH_FAIL
            "switch_video" -> CODE_SWITCH_VIDEO
            "replay" -> CODE_REPLAY
            "switch_episode" -> CODE_SWITCH_EPISODE
            "kernel_fallback" -> CODE_KERNEL_FALLBACK
            "auto_reopen" -> CODE_AUTO_REOPEN
            "stall_recover" -> CODE_STALL_RECOVER
            else -> CODE_TRACE
        }

        fun defaultMessage(code: Int): String = when (code) {
            CODE_PLAY_SESSION_START -> "播放会话开始"
            CODE_OPEN_BEGIN -> "开始打开媒体"
            CODE_OPEN_SUCCESS -> "打开成功"
            CODE_OPEN_FAIL -> "打开失败"
            CODE_STATE_CHANGE -> "播放状态变更"
            CODE_FIRST_FRAME -> "首帧渲染"
            CODE_POSITION_HEARTBEAT -> "位置心跳"
            CODE_SEEK_BEGIN -> "开始 seek"
            CODE_SEEK_COMPLETE -> "seek 完成"
            CODE_SEEK_FAIL -> "seek 失败"
            CODE_LOADING_BEGIN -> "开始缓冲"
            CODE_LOADING_END -> "缓冲结束"
            CODE_NETWORK_CHANGE -> "网络变化"
            CODE_FFMPEG_IO -> "FFmpeg IO 事件"
            CODE_DECODE_ERROR -> "解码错误"
            CODE_RENDER_ERROR -> "渲染错误"
            CODE_AUDIO_ERROR -> "音频错误"
            CODE_PLAY_COMPLETE -> "播放完成"
            CODE_PLAY_SESSION_END -> "播放会话结束"
            CODE_DECODE_PATH_RESOLVED -> "视频解码路径确定"
            CODE_OPEN_URL_RESOLVED -> "播放地址已重定向"
            CODE_VIDEO_RENDER_STALL -> "视频渲染等待超时"
            CODE_TRACE -> ""
            CODE_NETWORK_SNAPSHOT -> "网络快照"
            CODE_NETWORK_WEAK_SIGNAL -> "弱网信号"
            CODE_USER_PLAY -> "用户播放"
            CODE_USER_PAUSE -> "用户暂停"
            CODE_USER_SEEK -> "用户 seek"
            CODE_USER_STOP -> "用户停止"
            CODE_USER_RATE_CHANGE -> "用户倍速变更"
            CODE_APP_ENTER_BACKGROUND -> "应用进入后台"
            CODE_APP_ENTER_FOREGROUND -> "应用进入前台"
            CODE_PIP_WILL_START -> "画中画即将开始"
            CODE_PIP_DID_START -> "画中画已开始"
            CODE_PIP_WILL_STOP -> "画中画即将停止"
            CODE_PIP_DID_STOP -> "画中画已停止"
            CODE_PIP_START_FAIL -> "画中画启动失败"
            CODE_PIP_RESTORE_UI -> "从画中画恢复界面"
            CODE_PIP_USER_START -> "用户启动画中画"
            CODE_PAGE_ENTER -> "进入播放页"
            CODE_AUTH_REQUEST -> "开始鉴权"
            CODE_AUTH_SUCCESS -> "鉴权成功"
            CODE_AUTH_FAIL -> "鉴权失败"
            CODE_SWITCH_VIDEO -> "切换视频"
            CODE_REPLAY -> "用户重播"
            CODE_SWITCH_EPISODE -> "切换上下集"
            CODE_KERNEL_FALLBACK -> "回退腾讯内核"
            CODE_AUTO_REOPEN -> "自动重开播放"
            CODE_STALL_RECOVER -> "卡顿恢复"
            else -> ""
        }

        fun defaultEventType(code: Int, errorCode: Int = 0, recoverable: Boolean = false): String = when (code) {
            CODE_OPEN_FAIL, CODE_SEEK_FAIL -> "error"
            CODE_LOADING_BEGIN -> "warn"
            CODE_VIDEO_RENDER_STALL -> "warn"
            CODE_TRACE, CODE_NETWORK_SNAPSHOT -> "trace"
            CODE_NETWORK_WEAK_SIGNAL -> "warn"
            CODE_DECODE_ERROR, CODE_RENDER_ERROR, CODE_AUDIO_ERROR ->
                if (recoverable) "warn" else "error"
            CODE_FFMPEG_IO -> if (errorCode != 0) "warn" else "info"
            CODE_AUTH_FAIL, CODE_KERNEL_FALLBACK -> "error"
            CODE_AUTO_REOPEN, CODE_STALL_RECOVER -> "warn"
            else -> "info"
        }
    }

    @Volatile
    var config: HXCPlayerMonitorConfig = config
        set(value) {
            field = value
            reporter.updateConfig(value)
        }

    var userContext: HXCPlayerMonitorUserContext? = null
        set(value) {
            field = value
            updateReporterUserId()
        }
    var metadata: HXCPlayerMonitorMetadata? = null
    var engineType: String = "custom"
    /** main=主课画面，small=三分屏小窗。 */
    @Volatile var playerRole: String = "main"

    var playSessionId: String = UUID.randomUUID().toString()
        private set

    private val appContext = context.applicationContext
    private val appName: String = try {
        context.applicationContext.applicationInfo
            .loadLabel(context.applicationContext.packageManager)
            .toString()
    } catch (_: Throwable) {
        context.applicationContext.packageName
    }
    private val reporter = HXCPlayerMonitorReporter(config, "android", sdkVersion, appName)
    private val sessionThread = HandlerThread("hxc-monitor-session").apply { start() }
    private val sessionHandler = Handler(sessionThread.looper)

    /**
     * 默认关闭。构造函数/设倍速/小窗 open 都可能早于 App 调用开关；
     * 主窗需显式 setMonitorReportingEnabled(true)。
     */
    @Volatile var reportingEnabled: Boolean = false
        set(value) {
            field = value
            reporter.setActive(value)
        }

    private var url: String? = null
    private var urlHash: String? = null
    private var dataSourceMode: Int = 0
    private var encrypted: Boolean = false
    private var startPosition: Double = 0.0
    private var duration: Double = 0.0
    private var videoWidth: Int = 0
    private var videoHeight: Int = 0
    private var lastPosition: Double = 0.0
    private var networkType: String = "unknown"
    private var networkExpensive: Boolean = false
    private var networkConstrained: Boolean = false
    private var totalStallMs: Long = 0
    private var reconnectCount: Int = 0
    private var rebufferCount: Int = 0
    private var lastErrorCode: Int = 0
    private var expectedDecodeMode: String? = null
    private var sessionActive: Boolean = false
    private var openBeginMs: Long = 0L
    private var traceSeq: Long = 0L

    private var lastNetworkSig: String = ""
    private var loadingOpen: Boolean = false
    private var loadingBeginEmitted: Boolean = false
    private var lastLoadingBeginAt: Long = 0L
    private var lastStateDetail: String = ""
    private var lastStateAt: Long = 0L
    private var lastErrorDedupeKey: String = ""
    private var lastErrorDedupeAt: Long = 0L
    private var lastGenericDedupeKey: String = ""
    private var lastGenericDedupeAt: Long = 0L
    private var lastLifecycleCode: Int = -1
    private var lastLifecycleAt: Long = 0L
    private var lastFirstFrameAt: Long = 0L
    private var networkSnapshotReady: Boolean = false

    private val heartbeatRunnable = object : Runnable {
        override fun run() {
            if (!sessionActive || !config.enabled) return
            emit(CODE_POSITION_HEARTBEAT, "position_heartbeat", "info",
                 message = "位置心跳", position = lastPosition, duration = duration,
                 immediate = false)
            sessionHandler.postDelayed(this, config.heartbeatIntervalMs.coerceAtLeast(1000L))
        }
    }

    private val connectivityManager =
        appContext.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
    private val networkCallback = object : ConnectivityManager.NetworkCallback() {
        override fun onCapabilitiesChanged(network: Network, caps: NetworkCapabilities) {
            val type = when {
                caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> "wifi"
                caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> "cellular"
                caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) -> "ethernet"
                else -> "other"
            }
            val expensive = !caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)
            val constrained = if (Build.VERSION.SDK_INT >= 28) {
                !caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_CONGESTED)
            } else false
            // 注册回调会立刻给一次当前网络，这不是变化，只记快照。
            if (!networkSnapshotReady) {
                networkSnapshotReady = true
                networkType = type
                networkExpensive = expensive
                networkConstrained = constrained
                lastNetworkSig = "$type|$expensive|$constrained"
                return
            }
            trackNetworkChange(type, expensive, constrained)
        }

        override fun onLost(network: Network) {
            trackNetworkChange("offline", false, false)
        }
    }

    init {
        try {
            connectivityManager?.registerDefaultNetworkCallback(networkCallback)
        } catch (_: Throwable) {
        }
        updateReporterUserId()
    }

    private fun updateReporterUserId() {
        val ctx = userContext
        val uid = ctx?.userId ?: ctx?.anonymousId ?: ctx?.deviceId ?: "anonymous"
        android.util.Log.d("HXCMonitor", "updateReporterUserId: userId=${ctx?.userId}, anonymousId=${ctx?.anonymousId}, deviceId=${ctx?.deviceId}, resolved=$uid")
        reporter.setUserId(uid)
    }

    fun beginSession(
        url: String?,
        dataSourceMode: Int,
        encrypted: Boolean,
        startPosition: Double,
        expectedDecodeMode: String? = null
    ) {
        sessionHandler.post {
            if (sessionActive) {
                endSessionLocked("replaced", lastPosition, duration, errorCode = 0, eventType = "info")
            }
            playSessionId = UUID.randomUUID().toString()
            this.url = url
            this.urlHash = sha256(url ?: "")
            this.dataSourceMode = dataSourceMode
            this.encrypted = encrypted
            this.startPosition = startPosition
            this.expectedDecodeMode = expectedDecodeMode
            this.sessionActive = true
            this.totalStallMs = 0
            this.reconnectCount = 0
            this.rebufferCount = 0
            this.lastErrorCode = 0
            this.openBeginMs = System.currentTimeMillis()
            traceSeq = 0
            loadingOpen = false
            loadingBeginEmitted = false
            lastStateDetail = ""
            lastErrorDedupeKey = ""
            lastGenericDedupeKey = ""
            lastFirstFrameAt = 0L
            refreshNetworkSnapshot()
            val detail = buildString {
                append("mode=").append(dataSourceMode)
                append(" encrypted=").append(if (encrypted) "YES" else "NO")
                append(" startPosition=").append("%.3f".format(Locale.US, startPosition))
                if (!expectedDecodeMode.isNullOrEmpty()) {
                    append(" expectedDecodeMode=").append(expectedDecodeMode)
                }
                append(" url=").append(url ?: "")
                append(" ").append(networkSnapshotDetail())
            }
            emit(CODE_PLAY_SESSION_START, "play_session_start", "info",
                 message = "播放会话开始", position = startPosition, duration = 0.0,
                 detail = detail, expectedDecodeMode = expectedDecodeMode,
                 attachNetworkType = true, immediate = true)
            restartHeartbeat()
        }
    }

    fun endSession(reason: String, position: Double, duration: Double) {
        sessionHandler.post {
            val failed = reason == "failed"
            endSessionLocked(
                reason = reason,
                position = position,
                duration = duration,
                errorCode = if (failed) lastErrorCode else 0,
                eventType = if (failed) "error" else "info"
            )
        }
    }

    private fun endSessionLocked(
        reason: String,
        position: Double,
        duration: Double,
        errorCode: Int,
        eventType: String
    ) {
        if (!sessionActive) return
        sessionActive = false
        stopHeartbeat()
        lastPosition = position
        this.duration = duration
        val detail = "endReason=$reason,${networkSnapshotDetail()}," +
                "totalStallMs=$totalStallMs,rebufferCount=$rebufferCount,reconnectCount=$reconnectCount"
        emit(CODE_PLAY_SESSION_END, "play_session_end", eventType,
             errorCode = errorCode, message = "播放会话结束",
             position = position, duration = duration,
             detail = detail, attachNetworkType = true, immediate = true)
        reporter.flush()
    }

    fun trackOpenSuccess(duration: Double, width: Int, height: Int, costMs: Long) {
        sessionHandler.post {
            this.duration = duration
            videoWidth = width
            videoHeight = height
            emit(CODE_OPEN_SUCCESS, "open_success", "info",
                 message = "打开成功", position = 0.0, duration = duration,
                 costMs = costMs, attachNetworkType = true, immediate = true)
        }
    }

    fun trackOpenFail(code: Int, message: String, position: Double, duration: Double) {
        sessionHandler.post {
            lastErrorCode = code
            emit(CODE_OPEN_FAIL, "open_fail", "error",
                 errorCode = code, message = message,
                 position = position, duration = duration,
                 detail = message, attachNetworkType = true, immediate = true)
            endSessionLocked("failed", position, duration, errorCode = code, eventType = "error")
        }
    }

    fun trackError(
        eventName: String,
        code: Int,
        message: String,
        position: Double,
        duration: Double
    ) {
        sessionHandler.post {
            lastErrorCode = code
            val eventCode = eventNameToCode(eventName)
            val eventType = if (eventName == "open_fail") "error" else "warn"
            emit(eventCode, eventName, eventType,
                 errorCode = code, message = message,
                 position = position, duration = duration,
                 detail = message, attachNetworkType = true, immediate = true)
        }
    }

    fun trackFirstFrame(position: Double, duration: Double, width: Int = 0, height: Int = 0) {
        sessionHandler.post {
            if (width > 0) videoWidth = width
            if (height > 0) videoHeight = height
            val cost = if (openBeginMs > 0) System.currentTimeMillis() - openBeginMs else 0L
            val sizeDetail = if (videoWidth > 0 && videoHeight > 0) "${videoWidth}x$videoHeight" else null
            emit(CODE_FIRST_FRAME, "first_frame", "info",
                 message = "首帧渲染", position = position, duration = duration,
                 detail = sizeDetail, costMs = cost, immediate = true)
        }
    }

    fun trackStateChange(state: String, position: Double, duration: Double) {
        sessionHandler.post {
            val raw = if (state.startsWith("state=")) state.substring(6) else state
            // LOADING 已有 loading_begin/end，避免一次卡顿刷两条状态。
            if (raw.equals("LOADING", ignoreCase = true) || raw.equals("BUFFERING", ignoreCase = true)) {
                return@post
            }
            val normalized = "state=${raw.lowercase()}"
            emit(CODE_STATE_CHANGE, "state_change", "info",
                 message = "播放状态变更", position = position, duration = duration,
                 detail = normalized, immediate = false)
        }
    }

    fun trackLoading(
        loading: Boolean,
        stallMs: Long,
        totalStallMs: Long,
        reconnectCount: Int,
        position: Double,
        duration: Double
    ) {
        sessionHandler.post {
            this.totalStallMs = totalStallMs
            this.reconnectCount = reconnectCount
            if (loading) {
                rebufferCount += 1
                emit(CODE_LOADING_BEGIN, "loading_begin", "warn",
                     message = "开始缓冲", position = position, duration = duration,
                     attachNetworkType = true, immediate = false)
            } else {
                emit(CODE_LOADING_END, "loading_end", "info",
                     message = "缓冲结束", position = position, duration = duration,
                     stallMs = stallMs, attachNetworkType = true, immediate = true)
            }
        }
    }

    fun trackComplete(position: Double, duration: Double) {
        sessionHandler.post {
            emit(CODE_PLAY_COMPLETE, "play_complete", "info",
                 message = "播放完成", position = position, duration = duration,
                 attachNetworkType = true, immediate = true)
        }
        endSession("completed", position, duration)
    }

    fun updatePosition(position: Double, duration: Double) {
        sessionHandler.post {
            lastPosition = position
            if (duration > 0) this.duration = duration
        }
    }

    fun trackNetworkChange(type: String, expensive: Boolean, constrained: Boolean) {
        sessionHandler.post {
            val sig = "$type|$expensive|$constrained"
            if (sig == lastNetworkSig) return@post
            lastNetworkSig = sig
            networkType = type
            networkExpensive = expensive
            networkConstrained = constrained
            emit(CODE_NETWORK_CHANGE, "network_change", "info",
                 message = "网络变化", position = lastPosition, duration = this.duration,
                 detail = type, attachNetworkType = true, immediate = false)
        }
    }

    /**
     * 通用事件入口：供 native 回调轮询使用，按 eventName 推导 eventCode/eventType/message。
     */
    fun trackNamed(
        eventName: String,
        position: Double,
        duration: Double,
        detail: String?,
        extra: Map<String, Any?>?,
        immediate: Boolean
    ) {
        sessionHandler.post {
            lastPosition = position
            if (duration > 0) this.duration = duration
            (extra?.get("totalStallMs") as? Number)?.toLong()?.let { totalStallMs = it }
            (extra?.get("reconnectCount") as? Number)?.toInt()?.let { reconnectCount = it }
            val eventCode = eventNameToCode(eventName)
            val errorCode = (extra?.get("errorCode") as? Number)?.toInt() ?: 0
            val recoverable = (extra?.get("recoverable") as? Boolean)
                ?: ((extra?.get("recoverable") as? Number)?.toInt() == 1)
            val coreMessage = (extra?.get("message") as? String)?.takeIf { it.isNotEmpty() }
            val tracePoint = (extra?.get("tracePoint") as? String)?.takeIf { it.isNotEmpty() }
            val phase = (extra?.get("phase") as? String)?.takeIf { it.isNotEmpty() }
            if (eventName == "trace") {
                emitTrace(
                    tracePoint = tracePoint,
                    phase = phase,
                    message = coreMessage ?: defaultMessage(CODE_TRACE),
                    detail = detail,
                    position = position,
                    duration = if (duration > 0) duration else this.duration
                )
                return@post
            }
            if (errorCode != 0) lastErrorCode = errorCode
            val eventType = defaultEventType(eventCode, errorCode, recoverable)
            val message = coreMessage ?: defaultMessage(eventCode)
            val costMs = (extra?.get("costMs") as? Number)?.toLong() ?: 0L
            val stallMs = (extra?.get("stallMs") as? Number)?.toLong()?.takeIf { it > 0 }
                ?: parseKvFromDetail(detail, "stallMs")?.toLongOrNull()
                ?: 0L
            val seekTarget = (extra?.get("seekTarget") as? Number)?.toDouble() ?: 0.0
            val seekLanding = (extra?.get("seekLanding") as? Number)?.toDouble() ?: 0.0
            val sendImmediate = immediate ||
                eventCode == CODE_FFMPEG_IO ||
                eventCode == CODE_VIDEO_RENDER_STALL
            val ffmpegCode = (extra?.get("ffmpegCode") as? Number)?.toInt() ?: 0
            var throughputKbps = (extra?.get("throughputKbps") as? Number)?.toInt() ?: -1
            if (throughputKbps < 0) {
                throughputKbps = parseKvFromDetail(detail, "throughputKbps")?.toIntOrNull() ?: -1
            }
            val bufferAheadSec = (extra?.get("bufferAheadSec") as? Number)?.toDouble()
                ?: parseKvFromDetail(detail, "bufferAheadSec")?.toDoubleOrNull()
                ?: -1.0
            val mediaUrl = (extra?.get("url") as? String)?.takeIf { it.isNotEmpty() }
                ?: parseKvFromDetail(detail, "resolvedUrl")
            val source = (extra?.get("source") as? String)?.takeIf { it.isNotEmpty() }
            val httpStatus = (extra?.get("httpStatus") as? Number)?.toInt()
                ?: parseKvFromDetail(detail, "httpStatus")?.toIntOrNull()
                ?: 0
            val stallCause = parseKvFromDetail(detail, "stallCause")
            val loadingReason = parseKvFromDetail(detail, "loadingReason")
            val resource = parseKvFromDetail(detail, "resource")
            val stage = parseKvFromDetail(detail, "stage")
            emit(eventCode, eventName, eventType,
                 errorCode = errorCode, message = message,
                 position = position, duration = if (duration > 0) duration else this.duration,
                 detail = detail, costMs = costMs, stallMs = stallMs,
                 seekTarget = seekTarget, seekLanding = seekLanding,
                 ffmpegCode = ffmpegCode, recoverable = recoverable,
                 throughputKbps = throughputKbps,
                 bufferAheadSec = bufferAheadSec,
                 mediaUrl = mediaUrl,
                 source = source,
                 httpStatus = httpStatus,
                 stallCause = stallCause,
                 loadingReason = loadingReason,
                 resource = resource,
                 stage = stage,
                 attachNetworkType = eventCode in NETWORK_TYPED_CODES,
                 immediate = sendImmediate)
        }
    }

    @JvmOverloads
    fun trackPageEvent(eventName: String, message: String? = null, detail: String? = null) {
        val extra = if (message.isNullOrEmpty()) null else mapOf("message" to message)
        trackNamed(eventName, lastPosition, duration, detail, extra, true)
    }

    fun shutdown() {
        sessionHandler.post {
            stopHeartbeat()
            try {
                connectivityManager?.unregisterNetworkCallback(networkCallback)
            } catch (_: Throwable) {
            }
            reporter.shutdown()
            sessionThread.quitSafely()
        }
    }

    private fun restartHeartbeat() {
        stopHeartbeat()
        if (!config.enabled || !config.enablePositionHeartbeat) return
        sessionHandler.postDelayed(heartbeatRunnable, config.heartbeatIntervalMs.coerceAtLeast(1000L))
    }

    private fun stopHeartbeat() {
        sessionHandler.removeCallbacks(heartbeatRunnable)
    }

    /**
     * 同类事件去重：避免 core 回调 + Kotlin 层、loading 抖动、系统网络回调连打。
     */
    private fun shouldSuppressDuplicate(
        eventCode: Int,
        eventName: String,
        errorCode: Int,
        detail: String?,
        source: String?
    ): Boolean {
        val now = System.currentTimeMillis()
        when (eventCode) {
            CODE_USER_PLAY -> {
                if (source == "auto_reopen") return true
            }
            CODE_LOADING_BEGIN -> {
                if (loadingOpen) return true
                loadingOpen = true
                if (now - lastLoadingBeginAt < 1500L) {
                    loadingBeginEmitted = false
                    return true
                }
                lastLoadingBeginAt = now
                loadingBeginEmitted = true
                return false
            }
            CODE_LOADING_END -> {
                if (!loadingOpen) return true
                loadingOpen = false
                val emit = loadingBeginEmitted
                loadingBeginEmitted = false
                return !emit
            }
            CODE_STATE_CHANGE -> {
                val d = detail.orEmpty()
                if (d == lastStateDetail && now - lastStateAt < 800L) return true
                lastStateDetail = d
                lastStateAt = now
                return false
            }
            CODE_FIRST_FRAME -> {
                if (now - lastFirstFrameAt < 2000L) return true
                lastFirstFrameAt = now
                return false
            }
            CODE_OPEN_FAIL, CODE_DECODE_ERROR, CODE_RENDER_ERROR, CODE_AUDIO_ERROR, CODE_FFMPEG_IO -> {
                val key = "$eventCode:$errorCode:${detail?.take(80).orEmpty()}"
                if (key == lastErrorDedupeKey && now - lastErrorDedupeAt < 1500L) return true
                lastErrorDedupeKey = key
                lastErrorDedupeAt = now
                return false
            }
            CODE_APP_ENTER_BACKGROUND, CODE_APP_ENTER_FOREGROUND -> {
                if (eventCode == lastLifecycleCode && now - lastLifecycleAt < 1500L) return true
                lastLifecycleCode = eventCode
                lastLifecycleAt = now
                return false
            }
        }
        val genericKey = "$eventCode:$eventName:$errorCode:${source.orEmpty()}:${detail?.take(60).orEmpty()}"
        if (genericKey == lastGenericDedupeKey && now - lastGenericDedupeAt < 400L) return true
        lastGenericDedupeKey = genericKey
        lastGenericDedupeAt = now
        return false
    }

    /**
     * 统一事件发送入口（字段结构对齐 PLAYER_MONITOR_SOCKET_PLAN.md §2.4）。
     * 关键字段：eventType / eventName / eventCode / errorCode / message / position / duration 顶层。
     */
    private fun emit(
        eventCode: Int,
        eventName: String,
        eventType: String,
        errorCode: Int = 0,
        message: String,
        position: Double,
        duration: Double,
        detail: String? = null,
        costMs: Long = 0,
        stallMs: Long = 0,
        seekTarget: Double = 0.0,
        seekLanding: Double = 0.0,
        ffmpegCode: Int = 0,
        recoverable: Boolean = false,
        throughputKbps: Int = -1,
        bufferAheadSec: Double = -1.0,
        mediaUrl: String? = null,
        expectedDecodeMode: String? = null,
        source: String? = null,
        httpStatus: Int = 0,
        stallCause: String? = null,
        loadingReason: String? = null,
        resource: String? = null,
        stage: String? = null,
        attachNetworkType: Boolean = false,
        immediate: Boolean
    ) {
        if (!config.enabled || !reportingEnabled) return
        if (shouldSuppressDuplicate(eventCode, eventName, errorCode, detail, source)) return
        val event = JSONObject()
        event.put("eventType", eventType)
        event.put("eventName", eventName)
        event.put("eventCode", eventCode)
        event.put("errorCode", errorCode)
        event.put("message", message)
        event.put("timestampMs", System.currentTimeMillis())
        event.put("playSessionId", playSessionId)
        event.put("position", position)
        event.put("duration", duration)
        event.put("engineType", engineType)
        event.put("sdkVersion", sdkVersion)
        event.put("recoverable", recoverable)
        attachIdentityFields(event, source)

        if (!detail.isNullOrEmpty()) event.put("detail", detail)
        if (!mediaUrl.isNullOrEmpty()) event.put("url", mediaUrl)
        if (costMs > 0) event.put("costMs", costMs)
        if (stallMs > 0) event.put("stallMs", stallMs)
        if (totalStallMs > 0) event.put("totalStallMs", totalStallMs)
        if (rebufferCount > 0) event.put("rebufferCount", rebufferCount)
        if (reconnectCount > 0) event.put("reconnectCount", reconnectCount)
        if (attachNetworkType && networkType.isNotEmpty()) event.put("networkType", networkType)
        if (seekTarget > 0) event.put("seekTarget", seekTarget)
        if (seekLanding > 0) event.put("seekLanding", seekLanding)
        if (ffmpegCode != 0) event.put("ffmpegCode", ffmpegCode)
        if (throughputKbps >= 0) event.put("throughputKbps", throughputKbps)
        if (bufferAheadSec >= 0) event.put("bufferAheadSec", bufferAheadSec)
        if (httpStatus > 0) event.put("httpStatus", httpStatus)
        if (!stallCause.isNullOrEmpty()) event.put("stallCause", stallCause)
        if (!loadingReason.isNullOrEmpty() && loadingReason != "none") event.put("loadingReason", loadingReason)
        if (!resource.isNullOrEmpty()) event.put("resource", resource)
        if (!stage.isNullOrEmpty()) event.put("stage", stage)
        if (!expectedDecodeMode.isNullOrEmpty()) event.put("expectedDecodeMode", expectedDecodeMode)

        if (eventCode == CODE_DECODE_PATH_RESOLVED) {
            val reason = parseKvFromDetail(detail, "reason")
            val actualDecodeMode = parseKvFromDetail(detail, "video")
            if (!actualDecodeMode.isNullOrEmpty()) {
                event.put("actualDecodeMode", actualDecodeMode)
                if (!reason.isNullOrEmpty()) event.put("decodePathReason", reason)
            }
        }

        if (eventType == "trace" && eventCode == CODE_NETWORK_SNAPSHOT) {
            traceSeq += 1
            event.put("traceSeq", traceSeq)
            event.put("tracePoint", "qoe.network_snapshot")
            event.put("phase", "io")
        }

        reporter.enqueue(event, immediate)
    }

    /** 对齐 iOS hxc_sendTraceLocked：eventName 空串、eventCode=0。 */
    private fun emitTrace(
        tracePoint: String?,
        phase: String?,
        message: String,
        detail: String?,
        position: Double,
        duration: Double
    ) {
        if (!config.enabled || !reportingEnabled) return
        traceSeq += 1
        val event = JSONObject()
        event.put("eventType", "trace")
        event.put("eventName", "")
        event.put("eventCode", 0)
        event.put("errorCode", 0)
        event.put("message", message)
        event.put("timestampMs", System.currentTimeMillis())
        event.put("traceSeq", traceSeq)
        event.put("playSessionId", playSessionId)
        event.put("position", position)
        event.put("duration", duration)
        if (!tracePoint.isNullOrEmpty()) event.put("tracePoint", tracePoint)
        if (!phase.isNullOrEmpty()) event.put("phase", phase)
        if (!detail.isNullOrEmpty()) event.put("detail", detail)
        event.put("recoverable", false)
        event.put("engineType", engineType)
        event.put("sdkVersion", sdkVersion)
        attachIdentityFields(event, null)
        reporter.enqueue(event, false)
    }

    private fun refreshNetworkSnapshot() {
        val cm = connectivityManager ?: return
        val network = cm.activeNetwork ?: run {
            networkType = "offline"
            networkExpensive = false
            networkConstrained = false
            return
        }
        val caps = cm.getNetworkCapabilities(network) ?: return
        networkType = when {
            caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI) -> "wifi"
            caps.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) -> "cellular"
            caps.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET) -> "ethernet"
            else -> "other"
        }
        networkExpensive = !caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)
        networkConstrained = if (Build.VERSION.SDK_INT >= 28) {
            !caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_CONGESTED)
        } else false
    }

    private fun networkSnapshotDetail(): String {
        val connected = if (networkType != "offline" && networkType != "unknown") 1 else 0
        return "type=$networkType,expensive=${if (networkExpensive) 1 else 0}," +
                "constrained=${if (networkConstrained) 1 else 0},connected=$connected"
    }

    private fun sha256(input: String): String {
        val md = MessageDigest.getInstance("SHA-256")
        val bytes = md.digest(input.toByteArray(Charsets.UTF_8))
        return bytes.joinToString("") { "%02x".format(it) }
    }

    /** 从 detail 字符串解析 key=value 形式的字段（对齐 iOS hxc_monitor_parse_kv_from_detail）。 */
    private fun parseKvFromDetail(detail: String?, key: String): String? {
        if (detail.isNullOrEmpty()) return null
        val token = "$key="
        val start = detail.indexOf(token)
        if (start < 0) return null
        var end = detail.indexOf(',', start + token.length)
        if (end < 0) end = detail.length
        val value = detail.substring(start + token.length, end).trim()
        return value.ifEmpty { null }
    }

    private fun attachIdentityFields(event: JSONObject, source: String?) {
        event.put("playerRole", playerRole)
        val videoId = metadata?.businessVideoId?.takeIf { it.isNotEmpty() }
            ?: userContext?.businessVideoId?.takeIf { it.isNotEmpty() }
        if (!videoId.isNullOrEmpty() && !event.has("businessVideoId")) {
            event.put("businessVideoId", videoId)
        }
        val traceId = metadata?.traceId?.takeIf { it.isNotEmpty() }
            ?: userContext?.traceId?.takeIf { it.isNotEmpty() }
        if (!traceId.isNullOrEmpty() && !event.has("traceId")) {
            event.put("traceId", traceId)
        }
        metadata?.extra?.forEach { (key, value) ->
            if (key.isNotEmpty() && !value.isNullOrEmpty() && !event.has(key)) {
                event.put(key, value)
            }
        }
        if (!source.isNullOrEmpty()) {
            event.put("source", source)
        }
    }
}
