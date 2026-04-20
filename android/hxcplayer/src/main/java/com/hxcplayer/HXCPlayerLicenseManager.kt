package com.hxcplayer

import android.content.Context
import android.util.Base64
import android.util.Log
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.nio.charset.StandardCharsets
import java.util.concurrent.Executors

object HXCPlayerLicenseManager {
    private const val TAG = "HXCPlayerLicense"
    private const val PREF_NAME = "hxcplayer_license"
    private const val PREF_KEY_PAYLOAD_B64 = "license_decrypted_json_b64_v1"

    @Volatile
    private var licenseCheckPassed = false

    private val worker = Executors.newSingleThreadExecutor()

    fun interface CheckCallback {
        fun onComplete(success: Boolean, error: Throwable?)
    }

    fun resetLicenseState(context: Context) {
        licenseCheckPassed = false
        context.applicationContext
            .getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
            .edit()
            .remove(PREF_KEY_PAYLOAD_B64)
            .apply()
    }

    fun isLicenseCheckPassed(context: Context): Boolean {
        if (licenseCheckPassed) return true
        val ok = validateCached(context.applicationContext)
        if (ok) {
            licenseCheckPassed = true
        }
        return ok
    }

    fun checkLicenseWithLicenseKey(
        context: Context,
        licenseKey: String,
        licenseUrl: String,
        callback: CheckCallback
    ) {
        if (licenseKey.isBlank() || licenseUrl.isBlank()) {
            callback.onComplete(false, IllegalArgumentException("License 参数无效"))
            return
        }
        worker.execute {
            try {
                val body = requestLicense(licenseKey, licenseUrl)
                val plainJson = HXCPlayerLicenseCrypto.decryptLicensePayload(body, licenseKey)
                val items = parseItems(plainJson)
                if (!validateItems(items, context.applicationContext.packageName)) {
                    licenseCheckPassed = false
                    callback.onComplete(false, IllegalStateException("未找到匹配 package_name 且未过期的 License"))
                    return@execute
                }

                savePayload(context.applicationContext, plainJson)
                licenseCheckPassed = true
                callback.onComplete(true, null)
            } catch (t: Throwable) {
                val fallbackOk = validateCached(context.applicationContext)
                if (fallbackOk) {
                    Log.w(TAG, "联网校验失败，使用缓存通过: ${t.message}")
                    licenseCheckPassed = true
                    callback.onComplete(true, null)
                } else {
                    licenseCheckPassed = false
                    callback.onComplete(false, t)
                }
            }
        }
    }

    private fun validateCached(context: Context): Boolean {
        val payload = loadPayload(context) ?: return false
        return try {
            val items = parseItems(payload)
            validateItems(items, context.packageName)
        } catch (e: Throwable) {
            false
        }
    }

    private fun savePayload(context: Context, plainJson: ByteArray) {
        val b64 = Base64.encodeToString(plainJson, Base64.NO_WRAP)
        context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
            .edit()
            .putString(PREF_KEY_PAYLOAD_B64, b64)
            .apply()
    }

    private fun loadPayload(context: Context): ByteArray? {
        val b64 = context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
            .getString(PREF_KEY_PAYLOAD_B64, null) ?: return null
        return try {
            Base64.decode(b64, Base64.DEFAULT)
        } catch (_: Throwable) {
            null
        }
    }

    @Throws(IOException::class)
    private fun requestLicense(licenseKey: String, licenseUrl: String): ByteArray {
        val preferGet = licenseUrl.contains("/license/getMobileLicense/", ignoreCase = true)
        val conn = (URL(licenseUrl).openConnection() as HttpURLConnection).apply {
            connectTimeout = 10000
            readTimeout = 10000
            requestMethod = if (preferGet) "GET" else "POST"
            useCaches = false
            doInput = true
            if (!preferGet) {
                doOutput = true
                setRequestProperty("Content-Type", "application/json")
            }
        }

        if (!preferGet) {
            val body = JSONObject().put("licenseKey", licenseKey).toString()
            conn.outputStream.use { it.write(body.toByteArray(StandardCharsets.UTF_8)) }
        }

        val code = conn.responseCode
        if (code !in 200..299) {
            throw IOException("License HTTP 状态码异常: $code")
        }
        val data = conn.inputStream.use { it.readBytesCompat() }
        if (data.isEmpty()) throw IOException("License 响应体为空")
        return data
    }

    private fun parseItems(plainJson: ByteArray): JSONArray {
        val text = String(plainJson, StandardCharsets.UTF_8)
        val trimmed = text.trim()
        return if (trimmed.startsWith("[")) {
            JSONArray(trimmed)
        } else {
            val obj = JSONObject(trimmed)
            obj.optJSONArray("terminalLicenses")
                ?: throw IllegalStateException("License JSON 不是数组，也不包含 terminalLicenses")
        }
    }

    private fun validateItems(items: JSONArray, appPackageName: String): Boolean {
        if (appPackageName.isBlank()) return false
        val now = System.currentTimeMillis() / 1000
        for (i in 0 until items.length()) {
            val d = items.optJSONObject(i) ?: continue
            val packageName = d.optString("package_name", "")
            if (packageName != appPackageName) continue
            val finishedAt = when (val f = d.opt("finished_at")) {
                is Number -> f.toLong()
                is String -> f.toLongOrNull() ?: 0L
                else -> 0L
            }
            if (finishedAt > now) return true
        }
        return false
    }

    private fun java.io.InputStream.readBytesCompat(): ByteArray {
        val bos = ByteArrayOutputStream()
        val buf = ByteArray(8 * 1024)
        while (true) {
            val n = read(buf)
            if (n <= 0) break
            bos.write(buf, 0, n)
        }
        return bos.toByteArray()
    }
}

