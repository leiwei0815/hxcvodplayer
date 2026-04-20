package com.hxcplayer

import android.util.Base64
import org.json.JSONArray
import org.json.JSONObject
import java.nio.charset.StandardCharsets
import javax.crypto.Cipher
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.SecretKeySpec

internal object HXCPlayerLicenseCrypto {

    class LicenseCryptoException(message: String, cause: Throwable? = null) : Exception(message, cause)

    @Throws(LicenseCryptoException::class)
    fun decryptLicensePayload(encryptedData: ByteArray, licenseKey: String): ByteArray {
        if (encryptedData.isEmpty()) {
            throw LicenseCryptoException("License 响应体为空")
        }

        val payloadData = extractPayloadData(encryptedData)
        if (payloadData.size <= 16) {
            throw LicenseCryptoException("License payload 长度不足")
        }

        val iv = payloadData.copyOfRange(0, 16)
        val cipher = payloadData.copyOfRange(16, payloadData.size)
        if (cipher.isEmpty()) {
            throw LicenseCryptoException("License 密文为空")
        }

        val keyData = licenseKey.toByteArray(StandardCharsets.UTF_8)
        if (keyData.size != 32) {
            throw LicenseCryptoException("licenseKey 必须为 32 字节（UTF-8）")
        }

        val plainBytes = try {
            val cipherInst = Cipher.getInstance("AES/CBC/PKCS5Padding")
            cipherInst.init(
                Cipher.DECRYPT_MODE,
                SecretKeySpec(keyData, "AES"),
                IvParameterSpec(iv)
            )
            cipherInst.doFinal(cipher)
        } catch (e: Exception) {
            throw LicenseCryptoException("CCCrypt(AES/CBC/PKCS5Padding) 解密失败", e)
        }

        val jsonArray = parsePlainBinaryToArray(plainBytes)
        return jsonArray.toString().toByteArray(StandardCharsets.UTF_8)
    }

    @Throws(LicenseCryptoException::class)
    private fun extractPayloadData(raw: ByteArray): ByteArray {
        val asString = raw.toString(StandardCharsets.UTF_8).trim()
        if (asString.isNotEmpty()) {
            try {
                val obj = JSONObject(asString)
                val b64 = obj.optString("terminalLicenses", "")
                if (b64.isNotEmpty()) {
                    return Base64.decode(b64, Base64.DEFAULT)
                }
            } catch (_: Exception) {
                // ignore
            }

            try {
                val decoded = Base64.decode(asString, Base64.DEFAULT)
                if (decoded.isNotEmpty()) return decoded
            } catch (_: Exception) {
                // ignore
            }
        }
        return raw
    }

    @Throws(LicenseCryptoException::class)
    private fun parsePlainBinaryToArray(plain: ByteArray): JSONArray {
        var off = 0
        val arr = JSONArray()

        while (off < plain.size) {
            // 允许尾部 0 填充
            if (isAllZeroTail(plain, off)) break

            val recordStart = off
            val userId = readU64BE(plain, off)
            off += 8

            val packageName = readLenPrefixedAscii(plain, off)
            off += 4 + packageName.second

            val bundleId = readLenPrefixedAscii(plain, off)
            off += 4 + bundleId.second

            ensureReadable(plain, off, 1, "version")
            val version = plain[off].toInt() and 0xFF
            off += 1

            val functionalScope = readLenPrefixedAscii(plain, off)
            off += 4 + functionalScope.second

            val startedAt = readU32BE(plain, off)
            off += 4
            val finishedAt = readU32BE(plain, off)
            off += 4

            val item = JSONObject()
            item.put("user_id", userId)
            item.put("package_name", packageName.first)
            item.put("bundle_id", bundleId.first)
            item.put("version", version)
            item.put("functional_scope", functionalScope.first)
            item.put("started_at", startedAt)
            item.put("finished_at", finishedAt)
            arr.put(item)

            if (off <= recordStart) {
                throw LicenseCryptoException("License 明文解析失败：偏移未推进")
            }
        }

        if (arr.length() == 0) {
            throw LicenseCryptoException("License 明文解析失败：无有效记录")
        }
        return arr
    }

    private fun isAllZeroTail(data: ByteArray, offset: Int): Boolean {
        for (i in offset until data.size) {
            if (data[i].toInt() != 0) return false
        }
        return true
    }

    @Throws(LicenseCryptoException::class)
    private fun readLenPrefixedAscii(data: ByteArray, offset: Int): Pair<String, Int> {
        ensureReadable(data, offset, 4, "len_prefix")
        val b0 = data[offset].toInt() and 0xFF
        val b1 = data[offset + 1].toInt() and 0xFF
        val b2 = data[offset + 2].toInt() and 0xFF
        val b3 = data[offset + 3].toInt() and 0xFF
        val len = when {
            b0 == 0 && b1 == 0 && b2 == 0 -> b3
            b1 == 0 && b2 == 0 && b3 == 0 -> b0
            else -> throw LicenseCryptoException("License 长度头异常: [$b0,$b1,$b2,$b3]")
        }
        if (len == 0) return "" to 0
        ensureReadable(data, offset + 4, len, "ascii")
        val s = String(data, offset + 4, len, StandardCharsets.ISO_8859_1)
        return s to len
    }

    @Throws(LicenseCryptoException::class)
    private fun readU32BE(data: ByteArray, offset: Int): Long {
        ensureReadable(data, offset, 4, "u32")
        return ((data[offset].toLong() and 0xFF) shl 24) or
            ((data[offset + 1].toLong() and 0xFF) shl 16) or
            ((data[offset + 2].toLong() and 0xFF) shl 8) or
            (data[offset + 3].toLong() and 0xFF)
    }

    @Throws(LicenseCryptoException::class)
    private fun readU64BE(data: ByteArray, offset: Int): Long {
        ensureReadable(data, offset, 8, "u64")
        var v = 0L
        for (i in 0 until 8) {
            v = (v shl 8) or (data[offset + i].toLong() and 0xFF)
        }
        return v
    }

    @Throws(LicenseCryptoException::class)
    private fun ensureReadable(data: ByteArray, offset: Int, need: Int, field: String) {
        if (offset < 0 || need < 0 || offset + need > data.size) {
            throw LicenseCryptoException("License 明文解析失败：字段 $field 越界 (off=$offset need=$need total=${data.size})")
        }
    }
}

