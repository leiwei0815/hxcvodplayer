package com.hxcplayer.test

import android.util.Log
import java.net.HttpURLConnection
import java.net.URL
import kotlinx.coroutines.*

/**
 * URL 诊断工具
 * 用于测试和分析视频 URL 的可访问性
 */
object URLDiagnostics {
    private const val TAG = "URLDiagnostics"
    
    /**
     * 诊断 URL 并输出详细信息
     */
    fun diagnose(url: String, callback: (Result) -> Unit) {
        CoroutineScope(Dispatchers.IO).launch {
            val result = Result()
            result.url = url
            
            try {
                Log.d(TAG, "=".repeat(50))
                Log.d(TAG, "开始诊断 URL:")
                Log.d(TAG, url)
                Log.d(TAG, "=".repeat(50))
                
                val startTime = System.currentTimeMillis()
                val connection = URL(url).openConnection() as HttpURLConnection
                
                // 设置超时
                connection.connectTimeout = 10000
                connection.readTimeout = 10000
                
                // 设置常用请求头（模拟浏览器）
                connection.setRequestProperty("User-Agent", 
                    "Mozilla/5.0 (Linux; Android 10) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.120 Mobile Safari/537.36")
                connection.setRequestProperty("Accept", "*/*")
                connection.setRequestProperty("Connection", "keep-alive")
                
                Log.d(TAG, "正在连接...")
                connection.connect()
                
                val connectTime = System.currentTimeMillis() - startTime
                
                // 获取响应信息
                result.responseCode = connection.responseCode
                result.responseMessage = connection.responseMessage
                result.contentType = connection.contentType ?: "unknown"
                result.contentLength = connection.contentLength.toLong()
                result.connectTime = connectTime
                
                Log.d(TAG, "连接成功！")
                Log.d(TAG, "响应码: ${result.responseCode}")
                Log.d(TAG, "响应消息: ${result.responseMessage}")
                Log.d(TAG, "内容类型: ${result.contentType}")
                Log.d(TAG, "内容大小: ${formatSize(result.contentLength)}")
                Log.d(TAG, "连接耗时: ${connectTime}ms")
                
                // 检查响应头
                Log.d(TAG, "\n响应头:")
                connection.headerFields.forEach { (key, values) ->
                    if (key != null) {
                        Log.d(TAG, "  $key: ${values.joinToString(", ")}")
                    }
                }
                
                // 尝试读取一些数据
                if (result.responseCode == 200) {
                    Log.d(TAG, "\n尝试读取数据...")
                    val buffer = ByteArray(1024)
                    val bytesRead = connection.inputStream.read(buffer)
                    if (bytesRead > 0) {
                        Log.d(TAG, "成功读取 $bytesRead 字节")
                        result.canRead = true
                        
                        // 检查文件头（MP4 签名）
                        val header = buffer.slice(0 until minOf(12, bytesRead)).toByteArray()
                        if (isMP4(header)) {
                            Log.d(TAG, "✅ 检测到 MP4 文件格式")
                            result.isValidMP4 = true
                        } else {
                            Log.w(TAG, "⚠️ 文件头不是标准 MP4 格式")
                        }
                    }
                }
                
                result.success = (result.responseCode == 200 && result.canRead)
                
                connection.disconnect()
                
            } catch (e: Exception) {
                Log.e(TAG, "诊断失败: ${e.message}", e)
                result.error = e.message ?: "Unknown error"
            }
            
            Log.d(TAG, "=".repeat(50))
            Log.d(TAG, "诊断完成")
            Log.d(TAG, "结果: ${if (result.success) "✅ 成功" else "❌ 失败"}")
            Log.d(TAG, "=".repeat(50))
            
            withContext(Dispatchers.Main) {
                callback(result)
            }
        }
    }
    
    private fun isMP4(header: ByteArray): Boolean {
        if (header.size < 12) return false
        // MP4 文件通常以 ftyp 开头（偏移 4-8 字节）
        return header[4] == 'f'.code.toByte() &&
               header[5] == 't'.code.toByte() &&
               header[6] == 'y'.code.toByte() &&
               header[7] == 'p'.code.toByte()
    }
    
    private fun formatSize(bytes: Long): String {
        return when {
            bytes < 0 -> "未知"
            bytes < 1024 -> "$bytes B"
            bytes < 1024 * 1024 -> "${bytes / 1024} KB"
            bytes < 1024 * 1024 * 1024 -> "${bytes / 1024 / 1024} MB"
            else -> "${bytes / 1024 / 1024 / 1024} GB"
        }
    }
    
    data class Result(
        var url: String = "",
        var success: Boolean = false,
        var responseCode: Int = 0,
        var responseMessage: String = "",
        var contentType: String = "",
        var contentLength: Long = -1,
        var connectTime: Long = 0,
        var canRead: Boolean = false,
        var isValidMP4: Boolean = false,
        var error: String? = null
    ) {
        fun getSummary(): String {
            return buildString {
                appendLine("URL 诊断结果")
                appendLine("=".repeat(40))
                appendLine("URL: $url")
                appendLine("状态: ${if (success) "✅ 可访问" else "❌ 不可访问"}")
                appendLine("响应码: $responseCode $responseMessage")
                appendLine("内容类型: $contentType")
                appendLine("文件大小: ${formatSize(contentLength)}")
                appendLine("连接耗时: ${connectTime}ms")
                appendLine("可读取数据: ${if (canRead) "是" else "否"}")
                appendLine("有效的MP4: ${if (isValidMP4) "是" else "否"}")
                if (error != null) {
                    appendLine("错误信息: $error")
                }
                appendLine("=".repeat(40))
            }
        }
    }
}
