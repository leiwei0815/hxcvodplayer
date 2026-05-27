package com.hxcplayer.download

import java.io.File

data class HXCDownloadConfig(
    val downloadRootDir: File? = null,
    val maxConcurrent: Int = 2,
    val connectTimeoutMs: Int = 30_000,
    val readTimeoutMs: Int = 30_000,
    val secureAuthUrl: String = "https://console-api.huaxiacloud.net/third_party/verify/sign"
)
