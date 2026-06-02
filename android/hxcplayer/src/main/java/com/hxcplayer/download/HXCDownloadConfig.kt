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
     * 下载任务级失败重试次数（包含首次执行）。
     * 覆盖鉴权、清单拉取、主文件下载等非分片级失败。
     */
    var taskRetryCount: Int = 3

    /**
     * 任务级重试基础退避时长。
     */
    var taskRetryBaseDelayMs: Long = 1_000L

    /**
     * 任务级重试最大退避时长。
     */
    var taskRetryMaxDelayMs: Long = 8_000L

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

    fun taskRetryCount(value: Int): HXCDownloadConfig {
        taskRetryCount = value.coerceAtLeast(1)
        return this
    }

    fun taskRetryBaseDelayMs(value: Long): HXCDownloadConfig {
        taskRetryBaseDelayMs = value.coerceAtLeast(0L)
        return this
    }

    fun taskRetryMaxDelayMs(value: Long): HXCDownloadConfig {
        taskRetryMaxDelayMs = value.coerceAtLeast(0L)
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
