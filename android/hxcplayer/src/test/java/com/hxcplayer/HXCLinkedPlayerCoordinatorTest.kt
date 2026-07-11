package com.hxcplayer

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class HXCLinkedPlayerCoordinatorTest {
    @Test
    fun prefersSoftRateWhenSecondaryDriftIsModerate() {
        val coordinator = HXCLinkedPlayerCoordinator()

        val decision = coordinator.evaluate(
            HXCLinkedPlayerCoordinator.Sample(
                mainPositionSec = 100.0,
                mainDurationSec = 600.0,
                secondaryPositionSec = 104.0,
                secondaryDurationSec = 602.0,
                playbackRate = 1.0f,
                nowMs = 10_000L
            )
        )

        assertEquals(HXCLinkedPlayerCoordinator.DecisionType.ADJUST_RATE, decision.type)
        assertTrue(decision.playbackRate < 1.0f)
    }

    @Test
    fun requestsSeekWhenSecondaryDriftExceedsHardTolerance() {
        val coordinator = HXCLinkedPlayerCoordinator()

        val decision = coordinator.evaluate(
            HXCLinkedPlayerCoordinator.Sample(
                mainPositionSec = 100.0,
                mainDurationSec = 600.0,
                secondaryPositionSec = 120.0,
                secondaryDurationSec = 602.0,
                playbackRate = 1.0f,
                nowMs = 10_000L
            )
        )

        assertEquals(HXCLinkedPlayerCoordinator.DecisionType.SEEK, decision.type)
        assertEquals(102.0, decision.targetPositionSec, 0.001)
    }
}
