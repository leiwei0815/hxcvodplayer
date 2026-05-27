package com.hxcplayer.download.m3u8

object HXCM3u8Parser {

    fun parse(baseUrl: String, content: String): HXCM3u8Info {
        val lines = content.lines()
        val header = mutableListOf<String>()
        val tsList = mutableListOf<HXCTsInfo>()
        var keyUrl: String? = null
        var duration = 0f
        var index = 0
        var byteRangeLength = -1L
        var byteRangeOffset = -1L
        for (raw in lines) {
            val line = raw.trim()
            if (line.isEmpty()) continue
            when {
                line.startsWith("#EXTM3U") ||
                    line.startsWith("#EXT-X-VERSION") ||
                    line.startsWith("#EXT-X-TARGETDURATION") ||
                    line.startsWith("#EXT-X-MEDIA-SEQUENCE") ||
                    line.startsWith("#EXT-X-PLAYLIST-TYPE") ||
                    line.startsWith("#EXT-X-KEY") -> {
                    header.add(line)
                    if (line.startsWith("#EXT-X-KEY")) {
                        keyUrl = extractKeyUrl(line)
                    }
                }
                line.startsWith("#EXT-X-BYTERANGE") -> {
                    val parsed = parseByteRange(line, byteRangeOffset + if (byteRangeLength > 0) byteRangeLength else 0)
                    byteRangeLength = parsed.first
                    byteRangeOffset = parsed.second
                }

                line.startsWith("#EXTINF") -> {
                    duration = parseExtInf(line)
                }

                line.startsWith("#") -> Unit

                else -> {
                    val abs = toAbsoluteUrl(baseUrl, line)
                    val local = String.format("%04d.ts", index++)
                    if (line.contains("?") && line.contains("start=") && line.contains("end=")) {
                        val (start, end) = parseStartEndQuery(line)
                        val rawUrl = toAbsoluteUrl(baseUrl, line)
                        val cleanUrl = toAbsoluteUrl(baseUrl, stripQuery(line))
                        tsList.add(HXCTsInfo(cleanUrl, local, duration, rawUrl = rawUrl, rangeStart = start, rangeEnd = end))
                    } else if (byteRangeLength > 0) {
                        val start = byteRangeOffset
                        val end = byteRangeOffset + byteRangeLength - 1
                        tsList.add(HXCTsInfo(abs, local, duration, rawUrl = abs, rangeStart = start, rangeEnd = end))
                        byteRangeLength = -1L
                    } else {
                        tsList.add(HXCTsInfo(abs, local, duration))
                    }
                    duration = 0f
                }
            }
        }
        return HXCM3u8Info(tsList = tsList, headerLines = header, keyUrl = keyUrl)
    }

    fun toAbsoluteUrl(baseUrl: String, relativeOrAbsolute: String): String {
        if (relativeOrAbsolute.startsWith("http://") || relativeOrAbsolute.startsWith("https://")) {
            return relativeOrAbsolute
        }
        if (relativeOrAbsolute.startsWith("/")) {
            val schemeEnd = baseUrl.indexOf("://")
            if (schemeEnd < 0) return relativeOrAbsolute
            val hostEnd = baseUrl.indexOf("/", schemeEnd + 3)
            val host = if (hostEnd > 0) baseUrl.substring(0, hostEnd) else baseUrl
            return host + relativeOrAbsolute
        }
        val lastSlash = baseUrl.lastIndexOf("/")
        val base = if (lastSlash > 0) baseUrl.substring(0, lastSlash + 1) else "$baseUrl/"
        return base + relativeOrAbsolute
    }

    private fun parseExtInf(line: String): Float {
        return try {
            val colon = line.indexOf(':')
            val comma = line.indexOf(',', colon)
            val value = if (comma > colon) line.substring(colon + 1, comma) else line.substring(colon + 1)
            value.trim().toFloat()
        } catch (_: Throwable) {
            0f
        }
    }

    private fun stripQuery(url: String): String {
        val q = url.indexOf('?')
        return if (q > 0) url.substring(0, q) else url
    }

    private fun parseStartEndQuery(line: String): Pair<Long, Long> {
        return try {
            val q = line.indexOf('?')
            if (q < 0) return -1L to -1L
            var s = -1L
            var e = -1L
            val query = line.substring(q + 1)
            query.split("&").forEach { p ->
                when {
                    p.startsWith("start=") -> s = p.substringAfter("start=").toLongOrNull() ?: -1L
                    p.startsWith("end=") -> e = p.substringAfter("end=").toLongOrNull() ?: -1L
                }
            }
            s to e
        } catch (_: Throwable) {
            -1L to -1L
        }
    }

    private fun parseByteRange(line: String, prevEnd: Long): Pair<Long, Long> {
        return try {
            val value = line.substringAfter(':').trim()
            val at = value.indexOf('@')
            val length: Long
            val offset: Long
            if (at >= 0) {
                length = value.substring(0, at).trim().toLong()
                offset = value.substring(at + 1).trim().toLong()
            } else {
                length = value.toLong()
                offset = if (prevEnd >= 0) prevEnd else 0L
            }
            length to offset
        } catch (_: Throwable) {
            -1L to 0L
        }
    }

    private fun extractKeyUrl(line: String): String? {
        val start = line.indexOf("URI=\"")
        if (start < 0) return null
        val from = start + 5
        val end = line.indexOf('"', from)
        return if (end > from) line.substring(from, end) else null
    }
}
