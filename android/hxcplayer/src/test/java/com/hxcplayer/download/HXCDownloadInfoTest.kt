package com.hxcplayer.download

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class HXCDownloadInfoTest {
    @Test
    fun parentChildProgressAggregatesUntilBothFinished() {
        val parent = HXCDownloadInfo("parent", "u1", "v1").apply {
            status = HXCDownloadStatus.FINISH
            progress = 1f
        }
        val child = HXCDownloadInfo("child", "u1", "v1_child").apply {
            isChildTask = true
            status = HXCDownloadStatus.RUNNING
            progress = 0.5f
        }

        parent.attachChild(child)

        assertEquals(HXCDownloadStatus.RUNNING, parent.status)
        assertEquals(1f, parent.mainProgress, 0.001f)
        assertEquals(0.75f, parent.progress, 0.001f)
        assertTrue(parent.child === child)
    }
}
