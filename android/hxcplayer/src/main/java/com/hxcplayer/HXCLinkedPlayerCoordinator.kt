package com.hxcplayer

import kotlin.math.abs

/**
 * 主/副播放器同步决策器。
 *
 * SDK 只输出“副播放器应如何追主播放器”的决策，App 仍负责 UI、小窗显隐和实际调用。
 */
class HXCLinkedPlayerCoordinator @JvmOverloads constructor(
    private val config: Config = Config()
) {
    data class Config(
        val maxOffsetSec: Double = 10 * 60.0,
        val softToleranceSec: Double = 2.0,
        val hardToleranceSec: Double = 4.0,
        val nearEndWindowSec: Double = 240.0,
        val nearEndHardToleranceSec: Double = 6.0,
        val seekCooldownMs: Long = 5000L,
        val softRateStep: Float = 0.02f,
        val maxRateDelta: Float = 0.04f
    )

    data class Sample(
        val mainPositionSec: Double,
        val mainDurationSec: Double,
        val secondaryPositionSec: Double,
        val secondaryDurationSec: Double,
        val playbackRate: Float,
        val nowMs: Long
    )

    enum class DecisionType {
        NONE,
        ADJUST_RATE,
        SEEK
    }

    data class Decision(
        val type: DecisionType,
        val targetPositionSec: Double = Double.NaN,
        val playbackRate: Float = Float.NaN,
        val driftSec: Double = 0.0,
        val reason: String = ""
    ) {
        companion object {
            @JvmField
            val NONE = Decision(DecisionType.NONE)
        }
    }

    private var lastSeekAtMs: Long = 0L

    fun reset() {
        lastSeekAtMs = 0L
    }

    fun evaluate(sample: Sample): Decision {
        if (!isValid(sample)) {
            return Decision.NONE
        }
        val offsetSec = sample.secondaryDurationSec - sample.mainDurationSec
        if (abs(offsetSec) > config.maxOffsetSec) {
            return Decision.NONE
        }
        val expectedSecondarySec = expectedSecondaryPosition(sample, offsetSec)
        val driftSec = sample.secondaryPositionSec - expectedSecondarySec
        val absDriftSec = abs(driftSec)
        val nearEnd = (sample.mainDurationSec - sample.mainPositionSec) <= config.nearEndWindowSec ||
                (sample.secondaryDurationSec - sample.secondaryPositionSec) <= config.nearEndWindowSec
        val hardTolerance = if (nearEnd) config.nearEndHardToleranceSec else config.hardToleranceSec

        if (absDriftSec <= config.softToleranceSec) {
            return Decision.NONE
        }
        if (absDriftSec < hardTolerance) {
            return softRateDecision(sample.playbackRate, driftSec)
        }
        if (sample.nowMs - lastSeekAtMs < config.seekCooldownMs) {
            return softRateDecision(sample.playbackRate, driftSec, "seek_cooling")
        }
        lastSeekAtMs = sample.nowMs
        return Decision(
            type = DecisionType.SEEK,
            targetPositionSec = expectedSecondarySec,
            driftSec = driftSec,
            reason = if (nearEnd) "near_end_hard_seek" else "hard_seek"
        )
    }

    private fun softRateDecision(
        baseRate: Float,
        driftSec: Double,
        reason: String = "soft_sync"
    ): Decision {
        val safeBaseRate = if (baseRate.isFinite() && baseRate > 0f) baseRate else 1.0f
        val direction = if (driftSec > 0) -1f else 1f
        val delta = (config.softRateStep * abs(driftSec).coerceAtMost(2.0).toFloat())
            .coerceAtMost(config.maxRateDelta)
        return Decision(
            type = DecisionType.ADJUST_RATE,
            playbackRate = (safeBaseRate + direction * delta).coerceAtLeast(0.5f),
            driftSec = driftSec,
            reason = reason
        )
    }

    private fun expectedSecondaryPosition(sample: Sample, offsetSec: Double): Double {
        val expected = if (offsetSec > 0.0) {
            sample.mainPositionSec + offsetSec
        } else {
            sample.mainPositionSec
        }
        return expected.coerceIn(0.0, sample.secondaryDurationSec)
    }

    private fun isValid(sample: Sample): Boolean {
        return sample.mainPositionSec > 0.0 &&
                sample.secondaryPositionSec > 0.0 &&
                sample.mainDurationSec > sample.mainPositionSec &&
                sample.secondaryDurationSec > sample.secondaryPositionSec
    }
}
