package com.hxcplayer.download

import java.io.File

data class HXCDownloadConfig(
    val downloadRootDir: File? = null,
    val maxConcurrent: Int = 2,
    val connectTimeoutMs: Int = 30_000,
    val readTimeoutMs: Int = 30_000,
    val secureAuthUrl: String = "https://console-api.huaxiacloud.net/third_party/verify/sign"
) {
    /**
     * m3u8 资源下载失败重试次数（包含首次执行）。
     */
    var resourceRetryCount: Int = 3

    /**
     * 资源下载重试基础退避时长。
     */
    var resourceRetryBaseDelayMs: Long = 200L

    /**
     * 资源下载重试最大退避时长。
     */
    var resourceRetryMaxDelayMs: Long = 1_000L

    fun resourceRetryCount(value: Int): HXCDownloadConfig {
        resourceRetryCount = value.coerceAtLeast(1)
        return this
    }

    fun resourceRetryBaseDelayMs(value: Long): HXCDownloadConfig {
        resourceRetryBaseDelayMs = value.coerceAtLeast(0L)
        return this
    }

    fun resourceRetryMaxDelayMs(value: Long): HXCDownloadConfig {
        resourceRetryMaxDelayMs = value.coerceAtLeast(0L)
        return this
    }
}
