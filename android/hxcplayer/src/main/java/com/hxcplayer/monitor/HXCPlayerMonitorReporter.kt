package com.hxcplayer.monitor

import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import org.json.JSONObject
import java.util.ArrayDeque
import java.util.concurrent.TimeUnit

/**
 * WebSocket 实时上报（失败不影响播放）。
 *
 * 后台列表「会话」列是握手时服务端生成的 session_id，不是客户端 UUID。
 * 服务端只读 URL 的 user_id / terminal，并在首帧回传 {"type":"session","session_id":"..."}。
 * 与 iPad 对齐：收到该 ID 后写入信封和 content.playSessionId，切集时重连换一条连接。
 *
 * 连接 URL：{endpoint}?user_id={userId}&terminal={terminalTag}&session_id={playSessionId}
 * （session_id 为可选提示；服务端若忽略，则以首帧 ID 为准）
 * 消息体：{"app_name":"...","user_id":"...","terminal":"...","playSessionId":"...","content":{event}}
 */
class HXCPlayerMonitorReporter(
    @Volatile private var config: HXCPlayerMonitorConfig,
    private val terminalType: String,
    private val sdkVersion: String,
    private val appName: String = ""
) {
    @Volatile private var userId: String = "anonymous"
    @Volatile private var playSessionId: String = ""
    /** 服务端握手回传的连接 ID，与后台列表列一致。 */
    @Volatile private var boundSessionId: String = ""

    var onBoundSessionId: ((String) -> Unit)? = null

    private val thread = HandlerThread("hxc-monitor-reporter").apply { start() }
    private val handler = Handler(thread.looper)

    private val pendingQueue = ArrayDeque<String>()
    @Volatile private var webSocket: WebSocket? = null
    @Volatile private var connecting: Boolean = false
    @Volatile private var reconnectAttempts: Int = 0
    @Volatile private var shuttingDown: Boolean = false
    /** 主动 cancel/换 userId 重连，不应当成故障去指数退避再连一次。 */
    @Volatile private var suppressDisconnectReconnect: Boolean = false
    @Volatile private var reporterActive: Boolean = false
    @Volatile private var connectGeneration: Int = 0

    private val client: OkHttpClient by lazy {
        OkHttpClient.Builder()
            .connectTimeout(15, TimeUnit.SECONDS)
            .readTimeout(0, TimeUnit.MILLISECONDS) // WebSocket 长连接
            .pingInterval(30, TimeUnit.SECONDS)    // OkHttp 自带 ping 保活
            .build()
    }

    fun updateConfig(config: HXCPlayerMonitorConfig) {
        handler.post {
            this.config = config
            if (webSocket != null || connecting) {
                reconnect()
            }
        }
    }

    fun setPlaySessionId(sessionId: String?) {
        handler.post {
            val newId = sessionId?.trim().orEmpty()
            if (newId == this.playSessionId) return@post
            this.playSessionId = newId
            if (newId.isBlank()) return@post
            // 切集换会话：必须换一条 WS，后台才会生成新的 session_id。
            if (webSocket != null || connecting) {
                reconnect()
            } else {
                connectIfNeeded()
            }
        }
    }

    fun setUserId(userId: String?) {
        handler.post {
            val newUid = if (userId.isNullOrBlank()) "anonymous" else userId
            if (newUid == this.userId) return@post
            this.userId = newUid
            // user_id 是 URL 参数。尚未建连时只更新字段，避免 anonymous→真实用户空打一条再被 cancel。
            if (webSocket != null || connecting) {
                reconnect()
            }
        }
    }

    fun setActive(active: Boolean) {
        handler.post {
            if (reporterActive == active) return@post
            reporterActive = active
            if (!active) {
                closeQuietly()
            }
        }
    }

    fun enqueue(event: JSONObject, immediate: Boolean) {
        handler.post {
            if (shuttingDown || !reporterActive || !config.enabled || config.endpoint.isNullOrBlank()) {
                return@post
            }
            val message = JSONObject()
                .put("app_name", appName.ifBlank { "unknown" })
                .put("user_id", this.userId)
                .put("terminal", terminalTag())
                .put("playSessionId", event.optString("playSessionId"))
                .put("content", event)
            val text = message.toString()
            if (config.debugLog) {
                Log.d(TAG, "enqueue: eventName=${event.optString("eventName")} " +
                        "eventCode=${event.optInt("eventCode")} " +
                        "eventType=${event.optString("eventType")} " +
                        "errorCode=${event.optInt("errorCode")} " +
                        "playerRole=${event.optString("playerRole")} " +
                        "playSessionId=${event.optString("playSessionId")} " +
                        "userId=${this.userId} " +
                        "immediate=$immediate")
            }
            sendText(text)
            if (immediate) {
                connectIfNeeded()
            }
        }
    }

    fun flush() {
        handler.post {
            connectIfNeeded()
            drainPending()
        }
    }

    fun shutdown() {
        handler.post {
            shuttingDown = true
            closeQuietly()
            thread.quitSafely()
        }
    }

    private fun terminalTag(): String = when (terminalType) {
        "ios" -> "iOS"
        "macos" -> "Mac"
        "android" -> "Android"
        "windows" -> "Windows"
        else -> if (terminalType.isNotEmpty()) terminalType.replaceFirstChar { it.uppercase() } else "Unknown"
    }

    private fun webSocketUrl(): String? {
        val base = config.endpoint ?: return null
        val separator = if (base.contains("?")) "&" else "?"
        var url = "$base${separator}user_id=${urlEncode(userId)}&terminal=${urlEncode(terminalTag())}"
        if (playSessionId.isNotBlank()) {
            url += "&session_id=${urlEncode(playSessionId)}"
        }
        return url
    }

    private fun urlEncode(value: String): String {
        return java.net.URLEncoder.encode(value, "UTF-8")
    }

    private fun connectIfNeeded() {
        if (shuttingDown || !reporterActive || !config.enabled) return
        if (webSocket != null || connecting) return
        val url = webSocketUrl() ?: return
        connecting = true
        boundSessionId = ""
        val generation = ++connectGeneration
        val request = Request.Builder().url(url).build()
        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(ws: WebSocket, response: Response) {
                handler.post {
                    if (generation != connectGeneration) return@post
                    connecting = false
                    reconnectAttempts = 0
                    if (config.debugLog) Log.d(TAG, "ws connected: ${response.code} url=$url, waiting session frame")
                    handler.postDelayed({ fallbackBindIfNeeded(generation) }, SESSION_FRAME_FALLBACK_MS)
                }
            }

            override fun onMessage(ws: WebSocket, text: String) {
                handler.post { handleServerMessage(generation, text) }
            }

            override fun onClosed(ws: WebSocket, code: Int, reason: String) {
                handler.post {
                    if (generation != connectGeneration) return@post
                    onDisconnected("closed code=$code reason=$reason", quiet = false)
                }
            }

            override fun onFailure(ws: WebSocket, t: Throwable, response: Response?) {
                handler.post {
                    if (generation != connectGeneration) return@post
                    onDisconnected("failure ${t.message}", quiet = isCanceled(t))
                }
            }
        })
    }

    private fun handleServerMessage(generation: Int, text: String) {
        if (generation != connectGeneration) return
        val obj = try {
            JSONObject(text)
        } catch (_: Exception) {
            return
        }
        if (obj.optString("type") != "session") return
        val sid = obj.optString("session_id").trim()
        if (sid.isBlank()) return
        bindSessionId(sid, fromServer = true)
    }

    private fun fallbackBindIfNeeded(generation: Int) {
        if (generation != connectGeneration) return
        if (shuttingDown || boundSessionId.isNotBlank()) return
        val fallback = playSessionId.ifBlank { return }
        if (config.debugLog) Log.d(TAG, "session frame timeout, fallback playSessionId=$fallback")
        bindSessionId(fallback, fromServer = false)
    }

    private fun bindSessionId(sid: String, fromServer: Boolean) {
        if (sid.isBlank()) return
        if (sid == boundSessionId) {
            drainPending()
            return
        }
        boundSessionId = sid
        playSessionId = sid
        if (config.debugLog) {
            Log.d(TAG, "bound session_id=$sid fromServer=$fromServer")
        }
        onBoundSessionId?.invoke(sid)
        drainPending()
    }

    private fun stampSessionId(text: String): String {
        val sid = boundSessionId
        if (sid.isBlank()) return text
        return try {
            val obj = JSONObject(text)
            obj.put("playSessionId", sid)
            obj.optJSONObject("content")?.put("playSessionId", sid)
            obj.toString()
        } catch (_: Exception) {
            text
        }
    }

    private fun onDisconnected(reason: String, quiet: Boolean) {
        webSocket = null
        connecting = false
        boundSessionId = ""
        if (shuttingDown) return
        val suppress = quiet || suppressDisconnectReconnect
        suppressDisconnectReconnect = false
        if (suppress) {
            if (config.debugLog) Log.d(TAG, "ws closed (reconnect/cancel): $reason")
            return
        }
        Log.w(TAG, "ws disconnected: $reason")
        scheduleReconnect()
    }

    private fun scheduleReconnect() {
        if (shuttingDown || !reporterActive) return
        reconnectAttempts += 1
        // 指数退避，上限 30s
        val delayMs = Math.min(30_000L, 1000L * (1L shl Math.min(reconnectAttempts, 5)))
        handler.postDelayed({ connectIfNeeded() }, delayMs)
    }

    private fun reconnect() {
        closeQuietly()
        connectIfNeeded()
    }

    private fun closeQuietly() {
        suppressDisconnectReconnect = webSocket != null || connecting
        connectGeneration += 1
        webSocket?.cancel()
        webSocket = null
        connecting = false
        boundSessionId = ""
    }

    private fun isCanceled(t: Throwable): Boolean {
        return t is java.io.IOException && (
            t.message.equals("Canceled", ignoreCase = true) ||
                t.message.equals("Socket closed", ignoreCase = true)
            )
    }

    private fun sendText(text: String) {
        val ws = webSocket
        if (ws == null || connecting || boundSessionId.isBlank()) {
            connectIfNeeded()
            while (pendingQueue.size >= config.maxQueueSize && pendingQueue.isNotEmpty()) {
                pendingQueue.removeFirst()
                Log.w(TAG, "pending overflow, drop oldest")
            }
            pendingQueue.addLast(text)
            if (config.debugLog) {
                val reason = when {
                    ws == null || connecting -> "ws not ready"
                    else -> "waiting session_id"
                }
                Log.d(TAG, "queued ($reason), pending=${pendingQueue.size}")
            }
            return
        }
        val stamped = stampSessionId(text)
        val ok = ws.send(stamped)
        if (config.debugLog) {
            Log.d(TAG, if (ok) "sent ok, len=${stamped.length} playSessionId=$boundSessionId" else "send failed")
        }
        if (!ok) {
            // 发送失败：重连，该消息不重发（实时事件过期意义不大）
            ws.cancel()
            webSocket = null
            boundSessionId = ""
            scheduleReconnect()
        }
    }

    private fun drainPending() {
        val ws = webSocket ?: return
        if (boundSessionId.isBlank()) return
        while (pendingQueue.isNotEmpty()) {
            val text = stampSessionId(pendingQueue.removeFirst())
            if (!ws.send(text)) {
                pendingQueue.addFirst(text)
                break
            }
        }
    }

    companion object {
        private const val TAG = "HXCMonitorReporter"
        private const val SESSION_FRAME_FALLBACK_MS = 2000L
    }
}
