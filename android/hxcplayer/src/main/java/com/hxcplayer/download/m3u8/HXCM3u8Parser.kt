package com.hxcplayer.download.m3u8

object HXCM3u8Parser {

    fun parse(baseUrl: String, content: String): HXCM3u8Info {
        val lines = content.lines()
        val header = mutableListOf<String>()
        val tsList = mutableListOf<HXCTsInfo>()
        var duration = 0f
        var index = 0
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
                }

                line.startsWith("#EXTINF") -> {
                    duration = parseExtInf(line)
                }

                line.startsWith("#") -> Unit

                else -> {
                    val abs = toAbsoluteUrl(baseUrl, line)
                    val local = String.format("%04d.ts", index++)
                    tsList.add(HXCTsInfo(abs, local, duration))
                    duration = 0f
                }
            }
        }
        return HXCM3u8Info(tsList = tsList, headerLines = header)
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
}
