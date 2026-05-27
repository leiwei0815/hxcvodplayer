package com.hxcplayer.download.m3u8

data class HXCM3u8Info(
    val tsList: List<HXCTsInfo>,
    val headerLines: List<String>,
    val keyUrl: String? = null
) {
    val hasKey: Boolean
        get() = !keyUrl.isNullOrBlank()
}
