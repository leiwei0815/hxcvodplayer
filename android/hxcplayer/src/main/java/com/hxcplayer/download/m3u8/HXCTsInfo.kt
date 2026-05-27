package com.hxcplayer.download.m3u8

data class HXCTsInfo(
    val url: String,
    val localName: String,
    val duration: Float,
    val rawUrl: String = url,
    val rangeStart: Long = -1L,
    val rangeEnd: Long = -1L
) {
    fun hasByteRange(): Boolean = rangeStart >= 0L && rangeEnd >= rangeStart
}
