package com.hxcplayer

import org.junit.Assert.assertEquals
import org.junit.Test

class HXCPlayerControlAudioHealthTest {
    @Test
    fun audioHealthEventCarriesMetricsActionAndReason() {
        val metrics = HXCPlayerControl.AudioHealthMetrics(
            silentForMs = 3500L,
            underrunRecent = 2,
            openslState = 2,
            recoverAttempts = 1
        )

        val event = HXCPlayerControl.AudioHealthEvent(
            metrics = metrics,
            action = HXCPlayerControl.AudioHealthAction.RECOVERED,
            reason = HXCPlayerControl.AudioHealthReason.OPENSL_NOT_PLAYING
        )

        assertEquals(metrics, event.metrics)
        assertEquals(HXCPlayerControl.AudioHealthAction.RECOVERED, event.action)
        assertEquals(HXCPlayerControl.AudioHealthReason.OPENSL_NOT_PLAYING, event.reason)
    }
}
