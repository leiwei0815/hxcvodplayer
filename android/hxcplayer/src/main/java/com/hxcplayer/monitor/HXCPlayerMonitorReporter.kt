package com.hxcplayer.monitor

import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import okhttp3.HttpUrl.Companion.toHttpUrlOrNull
import org.json.JSONObject
import java.util.ArrayDeque
import java.util.concurrent.TimeUnit

/**
 * WebSocket 实时上报（失败不影响播放）。
 *
 * 连接 URL：{endpoint}?user_id={userId}&terminal={terminalTag}
 * 消息体：{"user_id": "...", "terminal": "...", "content": {event}}
 */
class HXCPlayerMonitorReporter(
    @Volatile private var config: HXCPlayerMonitorConfig,
    private val terminalType: String,
    private val sdkVersion: String
) {
    @Volatile private var userId: String = "anonymous"

    private val thread = HandlerThread("hxc-monitor-reporter").apply { start() }
    private val handler = Handler(thread.looper)

    private val pendingQueue = ArrayDeque<String>()
    @Volatile private var webSocket: WebSocket? = null
    @Volatile private var connecting: Boolean = false
    @Volatile private var reconnectAttempts: Int = 0
    @Volatile private var shuttingDown: Boolean = false

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
            reconnect()
        }
    }

    fun setUserId(userId: String?) {
        handler.post {
            val newUid = if (userId.isNullOrBlank()) "anonymous" else userId
            if (newUid == this.userId) return@post
            this.userId = newUid
            // user_id 是 URL 参数，变化后必须重连
            reconnect()
        }
    }

    fun enqueue(event: JSONObject, immediate: Boolean) {
        handler.post {
            if (shuttingDown || !config.enabled || config.endpoint.isNullOrBlank()) {
                return@post
            }
            val message = JSONObject()
                .put("user_id", this.userId)
                .put("terminal", terminalTag())
                .put("content", event)
            val text = message.toString()
            if (config.debugLog) {
                Log.d(TAG, "enqueue: eventName=${event.optString("eventName")} " +
                        "eventCode=${event.optInt("eventCode")} " +
                        "eventType=${event.optString("eventType")} " +
                        "errorCode=${event.optInt("errorCode")} " +
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
            webSocket?.cancel()
            webSocket = null
            connecting = false
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
        val comp = base.toHttpUrlOrNull() ?: return null
        val builder = comp.newBuilder()
            .addQueryParameter("user_id", this.userId)
            .addQueryParameter("terminal", terminalTag())
        // 强制 wss/ws scheme
        if (comp.scheme == "https") builder.scheme("wss")
        else if (comp.scheme == "http") builder.scheme("ws")
        return builder.build().toString()
    }

    private fun connectIfNeeded() {
        if (shuttingDown || !config.enabled) return
        if (webSocket != null || connecting) return
        val url = webSocketUrl() ?: return
        connecting = true
        val request = Request.Builder().url(url).build()
        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(ws: WebSocket, response: Response) {
                connecting = false
                reconnectAttempts = 0
                if (config.debugLog) Log.d(TAG, "ws connected: ${response.code} url=$url")
                drainPending()
            }

            override fun onClosed(ws: WebSocket, code: Int, reason: String) {
                onDisconnected("closed code=$code reason=$reason")
            }

            override fun onFailure(ws: WebSocket, t: Throwable, response: Response?) {
                onDisconnected("failure ${t.message}")
            }
        })
    }

    private fun onDisconnected(reason: String) {
        webSocket = null
        connecting = false
        if (shuttingDown) return
        Log.w(TAG, "ws disconnected: $reason")
        scheduleReconnect()
    }

    private fun scheduleReconnect() {
        if (shuttingDown) return
        reconnectAttempts += 1
        // 指数退避，上限 30s
        val delayMs = Math.min(30_000L, 1000L * (1L shl Math.min(reconnectAttempts, 5)))
        handler.postDelayed({ connectIfNeeded() }, delayMs)
    }

    private fun reconnect() {
        webSocket?.cancel()
        webSocket = null
        connecting = false
        connectIfNeeded()
    }

    private fun sendText(text: String) {
        val ws = webSocket
        if (ws == null) {
            connectIfNeeded()
            if (webSocket == null) {
                while (pendingQueue.size >= config.maxQueueSize && pendingQueue.isNotEmpty()) {
                    pendingQueue.removeFirst()
                    Log.w(TAG, "pending overflow, drop oldest")
                }
                pendingQueue.addLast(text)
                if (config.debugLog) Log.d(TAG, "queued (ws not ready), pending=${pendingQueue.size}")
            }
            return
        }
        val ok = ws.send(text)
        if (config.debugLog) {
            Log.d(TAG, if (ok) "sent ok, len=${text.length}" else "send failed")
        }
        if (!ok) {
            // 发送失败：重连，该消息不重发（实时事件过期意义不大）
            ws.cancel()
            webSocket = null
            scheduleReconnect()
        }
    }

    private fun drainPending() {
        val ws = webSocket ?: return
        while (pendingQueue.isNotEmpty()) {
            val text = pendingQueue.removeFirst()
            if (!ws.send(text)) {
                pendingQueue.addFirst(text)
                break
            }
        }
    }

    companion object {
        private const val TAG = "HXCMonitorReporter"
    }
}
