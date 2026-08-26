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
import java.util.UUID

/**
 * Android 播放监控会话（字段与 iOS 对齐）
 */
class HXCPlayerMonitorSession(
    context: Context,
    config: HXCPlayerMonitorConfig,
    private val sdkVersion: String = "1.0.0"
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
        const val CODE_TRACE = 150
        const val CODE_NETWORK_SNAPSHOT = 160
        const val CODE_NETWORK_WEAK_SIGNAL = 162
        const val CODE_USER_PLAY = 100
        const val CODE_USER_PAUSE = 101
        const val CODE_USER_SEEK = 102
        const val CODE_USER_STOP = 103
        const val CODE_USER_RATE_CHANGE = 104
        const val CODE_APP_ENTER_BACKGROUND = 105
        const val CODE_APP_ENTER_FOREGROUND = 106

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
            else -> ""
        }

        fun defaultEventType(code: Int, errorCode: Int = 0, recoverable: Boolean = false): String = when (code) {
            CODE_OPEN_FAIL, CODE_SEEK_FAIL -> "error"
            CODE_LOADING_BEGIN -> "warn"
            CODE_TRACE, CODE_NETWORK_SNAPSHOT -> "trace"
            CODE_NETWORK_WEAK_SIGNAL -> "warn"
            CODE_DECODE_ERROR, CODE_RENDER_ERROR, CODE_AUDIO_ERROR ->
                if (recoverable) "warn" else "error"
            CODE_FFMPEG_IO -> if (errorCode != 0) "warn" else "info"
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

    var playSessionId: String = UUID.randomUUID().toString()
        private set

    private val appContext = context.applicationContext
    private val reporter = HXCPlayerMonitorReporter(config, "android", sdkVersion)
    private val sessionThread = HandlerThread("hxc-monitor-session").apply { start() }
    private val sessionHandler = Handler(sessionThread.looper)

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
    private var sessionActive: Boolean = false
    private var openBeginMs: Long = 0L
    private var traceSeq: Long = 0L

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
        startPosition: Double
    ) {
        sessionHandler.post {
            if (sessionActive) {
                endSession("replaced", lastPosition, duration)
            }
            playSessionId = UUID.randomUUID().toString()
            this.url = url
            this.urlHash = sha256(url ?: "")
            this.dataSourceMode = dataSourceMode
            this.encrypted = encrypted
            this.startPosition = startPosition
            this.sessionActive = true
            this.totalStallMs = 0
            this.reconnectCount = 0
            this.openBeginMs = System.currentTimeMillis()
            traceSeq = 0
            emit(CODE_PLAY_SESSION_START, "play_session_start", "info",
                 message = "播放会话开始", position = startPosition, duration = duration,
                 immediate = true)
            restartHeartbeat()
        }
    }

    fun endSession(reason: String, position: Double, duration: Double) {
        sessionHandler.post {
            if (!sessionActive) return@post
            sessionActive = false
            stopHeartbeat()
            lastPosition = position
            this.duration = duration
            emit(CODE_PLAY_SESSION_END, "play_session_end", "info",
                 message = "播放会话结束", position = position, duration = duration,
                 detail = reason, immediate = true)
            reporter.flush()
        }
    }

    fun trackOpenSuccess(duration: Double, width: Int, height: Int, costMs: Long) {
        sessionHandler.post {
            this.duration = duration
            videoWidth = width
            videoHeight = height
            emit(CODE_OPEN_SUCCESS, "open_success", "info",
                 message = "打开成功", position = startPosition, duration = duration,
                 costMs = costMs, immediate = true)
        }
    }

    fun trackOpenFail(code: Int, message: String, position: Double, duration: Double) {
        trackError("open_fail", code, message, position, duration)
        endSession("failed", position, duration)
    }

    fun trackError(
        eventName: String,
        code: Int,
        message: String,
        position: Double,
        duration: Double
    ) {
        sessionHandler.post {
            val eventCode = eventNameToCode(eventName)
            val eventType = if (eventName == "open_fail") "error" else "warn"
            emit(eventCode, eventName, eventType,
                 errorCode = code, message = message,
                 position = position, duration = duration, immediate = true)
        }
    }

    fun trackFirstFrame(position: Double, duration: Double) {
        sessionHandler.post {
            val cost = if (openBeginMs > 0) System.currentTimeMillis() - openBeginMs else 0L
            emit(CODE_FIRST_FRAME, "first_frame", "info",
                 message = "首帧渲染", position = position, duration = duration,
                 costMs = cost, immediate = true)
        }
    }

    fun trackStateChange(state: String, position: Double, duration: Double) {
        sessionHandler.post {
            emit(CODE_STATE_CHANGE, "state_change", "info",
                 message = "播放状态变更", position = position, duration = duration,
                 detail = state, immediate = false)
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
                emit(CODE_LOADING_BEGIN, "loading_begin", "warn",
                     message = "开始缓冲", position = position, duration = duration,
                     immediate = false)
            } else {
                emit(CODE_LOADING_END, "loading_end", "info",
                     message = "缓冲结束", position = position, duration = duration,
                     stallMs = stallMs, immediate = true)
            }
        }
    }

    fun trackComplete(position: Double, duration: Double) {
        sessionHandler.post {
            emit(CODE_PLAY_COMPLETE, "play_complete", "info",
                 message = "播放完成", position = position, duration = duration,
                 immediate = true)
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
            networkType = type
            networkExpensive = expensive
            networkConstrained = constrained
            emit(CODE_NETWORK_CHANGE, "network_change", "info",
                 message = "网络变化", position = lastPosition, duration = this.duration,
                 detail = type, immediate = false)
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
            val eventCode = eventNameToCode(eventName)
            val errorCode = (extra?.get("errorCode") as? Int) ?: 0
            val recoverable = (extra?.get("recoverable") as? Boolean) ?: false
            val eventType = defaultEventType(eventCode, errorCode, recoverable)
            val message = defaultMessage(eventCode)
            val costMs = (extra?.get("costMs") as? Long) ?: 0L
            val stallMs = (extra?.get("stallMs") as? Long) ?: 0L
            val seekTarget = (extra?.get("seekTarget") as? Double) ?: 0.0
            val seekLanding = (extra?.get("seekLanding") as? Double) ?: 0.0
            val ffmpegCode = (extra?.get("ffmpegCode") as? Int) ?: 0
            // 网络快照：从 detail 解析 throughputKbps
            var throughputKbps = (extra?.get("throughputKbps") as? Int) ?: -1
            if (eventCode == CODE_NETWORK_SNAPSHOT && throughputKbps < 0) {
                throughputKbps = parseKvFromDetail(detail, "throughputKbps")?.toIntOrNull() ?: -1
            }
            emit(eventCode, eventName, eventType,
                 errorCode = errorCode, message = message,
                 position = position, duration = if (duration > 0) duration else this.duration,
                 detail = detail, costMs = costMs, stallMs = stallMs,
                 seekTarget = seekTarget, seekLanding = seekLanding,
                 ffmpegCode = ffmpegCode, recoverable = recoverable,
                 throughputKbps = throughputKbps, immediate = immediate)
        }
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
        if (!config.enabled) return
        sessionHandler.postDelayed(heartbeatRunnable, config.heartbeatIntervalMs.coerceAtLeast(1000L))
    }

    private fun stopHeartbeat() {
        sessionHandler.removeCallbacks(heartbeatRunnable)
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
        immediate: Boolean
    ) {
        if (!config.enabled) return
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

        if (!url.isNullOrEmpty()) {
            event.put("url", if (config.uploadFullUrl) url else "")
        }
        if (!urlHash.isNullOrEmpty()) event.put("urlHash", urlHash)

        if (!detail.isNullOrEmpty()) event.put("detail", detail)
        if (costMs > 0) event.put("costMs", costMs)
        if (stallMs > 0) event.put("stallMs", stallMs)
        if (totalStallMs > 0) event.put("totalStallMs", totalStallMs)
        if (reconnectCount > 0) event.put("reconnectCount", reconnectCount)
        if (networkType != "unknown") event.put("networkType", networkType)
        if (seekTarget > 0) event.put("seekTarget", seekTarget)
        if (seekLanding > 0) event.put("seekLanding", seekLanding)
        if (ffmpegCode != 0) event.put("ffmpegCode", ffmpegCode)
        if (throughputKbps >= 0) event.put("throughputKbps", throughputKbps)

        // 解码路径确定事件：从 detail 解析 actualDecodeMode / decodePathReason
        if (eventCode == CODE_DECODE_PATH_RESOLVED) {
            val reason = parseKvFromDetail(detail, "reason")
            val actualDecodeMode = parseKvFromDetail(detail, "video")
            if (!actualDecodeMode.isNullOrEmpty()) {
                event.put("actualDecodeMode", actualDecodeMode)
                if (!reason.isNullOrEmpty()) event.put("decodePathReason", reason)
            }
        }

        if (eventType == "trace") {
            traceSeq += 1
            event.put("traceSeq", traceSeq)
        }

        event.put("terminalType", "android")
        event.put("appVersion", appVersion())
        event.put("osVersion", "Android ${Build.VERSION.RELEASE}")
        event.put("deviceModel", Build.MODEL ?: "unknown")

        userContext?.userId?.let { event.put("userId", it) }
        userContext?.anonymousId?.let { event.put("anonymousId", it) }
        userContext?.deviceId?.let { event.put("deviceId", it) }
        userContext?.tenantId?.let { event.put("tenantId", it) }
        userContext?.appSessionId?.let { event.put("appSessionId", it) }
        val biz = metadata?.businessVideoId ?: userContext?.businessVideoId
        biz?.let { event.put("businessVideoId", it) }

        reporter.enqueue(event, immediate)
    }

    private fun appVersion(): String {
        return try {
            val pi = appContext.packageManager.getPackageInfo(appContext.packageName, 0)
            val versionName = pi.versionName ?: ""
            val code = if (Build.VERSION.SDK_INT >= 28) pi.longVersionCode else @Suppress("DEPRECATION") pi.versionCode.toLong()
            if (versionName.isEmpty()) "$code" else "$versionName($code)"
        } catch (_: Throwable) {
            "unknown"
        }
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
}
