package com.hxcplayer.download

/**
 * HTTP 下载异常，携带状态码用于重试判定。
 */
class HXCDownloadHttpException(
    val statusCode: Int,
    val scene: String,
    message: String = "HTTP $statusCode ($scene)"
) : Exception(message)

/**
 * 明确不可重试的业务/参数异常。
 */
class HXCDownloadNonRetryableException(message: String) : Exception(message)
