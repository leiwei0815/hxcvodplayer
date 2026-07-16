package com.hxcplayer

/**
 * Describes whether the native audio output is actually audible.
 *
 * Video progress can continue while OpenSL is paused or underrunning; this state
 * gives the app a separate source of truth for "can the user hear it".
 */
enum class AudioOutputState(val nativeCode: Int) {
    IDLE(0),
    START_PENDING(1),
    PLAYING(2),
    PAUSED_SEEK(3),
    PAUSED_REBUFFER(4),
    STALLED(5),
    MUTED(6),
    ERROR(7);

    companion object {
        @JvmStatic
        fun fromNativeCode(code: Int): AudioOutputState {
            return values().firstOrNull { it.nativeCode == code } ?: IDLE
        }
    }
}
