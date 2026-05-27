package com.hxcplayer.download

import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

data class HXCResolvedDownloadSource(
    val url: String,
    val secureHeaders: String,
    val encrypted: Boolean
)

object HXCSecureDownloadAuthResolver {

    @Throws(Exception::class)
    fun resolve(
        request: HXCDownloadRequest,
        config: HXCDownloadConfig
    ): HXCResolvedDownloadSource {
        if (request.plainUrl.isNotBlank()) {
            return HXCResolvedDownloadSource(
                url = request.plainUrl,
                secureHeaders = "",
                encrypted = false
            )
        }
        val secure = request.secureCredential
            ?: throw IllegalArgumentException("下载缺少 plainUrl 或 secureCredential")
        if (secure.videoId.isBlank() || secure.sign.isBlank() || secure.secretId.isBlank()) {
            throw IllegalArgumentException("加密下载鉴权参数不完整：videoId/sign/secretId")
        }
        val connection = (URL(config.secureAuthUrl).openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            connectTimeout = config.connectTimeoutMs
            readTimeout = config.readTimeoutMs
            doInput = true
            doOutput = true
            setRequestProperty("Content-Type", "application/json")
        }
        try {
            val payload = JSONObject().apply {
                put("secret_id", secure.secretId)
                put("file_id", secure.videoId)
                put("sign", secure.sign)
                put("timestamp", secure.timestamp)
                put("client_type", "Android")
            }
            connection.outputStream.use {
                it.write(payload.toString().toByteArray(Charsets.UTF_8))
                it.flush()
            }
            val code = connection.responseCode
            val body = (if (code in 200..299) connection.inputStream else connection.errorStream)
                ?.bufferedReader(Charsets.UTF_8)?.use { it.readText() } ?: ""
            if (body.isBlank()) {
                throw IllegalStateException("下载鉴权响应为空")
            }
            val root = JSONObject(body)
            val hasBizCode = root.has("code") && !root.isNull("code")
            val bizCode = if (hasBizCode) root.optLong("code", 200L) else 200L
            if (code !in 200..299 || (hasBizCode && bizCode != 200L)) {
                throw IllegalStateException(root.optString("msg", "下载鉴权失败"))
            }
            val data = root.optJSONObject("data") ?: root
            val playUrl = firstNonEmpty(data, "download_url", "play_url", "url")
            if (playUrl.isBlank()) {
                throw IllegalStateException("下载鉴权成功但未返回可下载 URL")
            }
            val encrypted = data.optInt("encrypt_type", 0) == 1 || data.optBoolean("is_encrypted", false)
            var secureHeaders = data.optString("secure_headers", "")
            if (encrypted && secureHeaders.isBlank()) {
                secureHeaders = buildString {
                    append("P-HX-SecretID: ${secure.secretId}\r\n")
                    append("P-HX-FileId: ${secure.videoId}\r\n")
                    append("P-HX-Timestamp: ${secure.timestamp}\r\n")
                    append("P-HX-Sign: ${secure.sign}\r\n")
                    append("P-HX-Terminal-Type: Android\r\n")
                }
            }
            return HXCResolvedDownloadSource(
                url = playUrl,
                secureHeaders = secureHeaders,
                encrypted = encrypted
            )
        } finally {
            connection.disconnect()
        }
    }

    private fun firstNonEmpty(root: JSONObject, vararg keys: String): String {
        for (key in keys) {
            val value = root.optString(key, "")
            if (value.isNotBlank()) return value
        }
        return ""
    }
}
