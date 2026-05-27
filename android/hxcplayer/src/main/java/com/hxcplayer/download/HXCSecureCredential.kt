package com.hxcplayer.download

data class HXCSecureCredential(
    val videoId: String,
    val sign: String,
    val secretId: String,
    val timestamp: String
)
