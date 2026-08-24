package com.hxcplayer.monitor

/**
 * 播放监控配置与用户上下文（与 iOS HXCPlayerMonitorConfig 对齐）
 */
data class HXCPlayerMonitorUserContext(
    var userId: String? = null,
    var anonymousId: String? = null,
    var deviceId: String? = null,
    var tenantId: String? = null,
    var appSessionId: String? = null,
    var businessVideoId: String? = null,
    var traceId: String? = null,
    var extra: Map<String, String>? = null
)

data class HXCPlayerMonitorMetadata(
    var businessVideoId: String? = null,
    var traceId: String? = null,
    var extra: Map<String, String>? = null
)

data class HXCPlayerMonitorConfig(
    var enabled: Boolean = true,
    var endpoint: String? = "wss://log-reporting.huaxiacloud.net/ws",
    var appId: String? = null,
    var uploadFullUrl: Boolean = true,
    var heartbeatIntervalMs: Long = 5000L,
    var batchSize: Int = 10,
    var flushIntervalMs: Long = 5000L,
    var maxQueueSize: Int = 200,
    var requestTimeoutMs: Int = 3000,
    var maxRetryCount: Int = 2,
    /** 调试日志开关：开启后在 logcat 输出事件发送/连接状态，TAG=HXCMonitor */
    var debugLog: Boolean = false
)
