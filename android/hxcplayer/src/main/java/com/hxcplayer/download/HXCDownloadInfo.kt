package com.hxcplayer.download

data class HXCDownloadInfo(
    val downloadKey: String,
    val userId: String,
    val videoId: String
) {
    var fileId: String = ""
    var classId: String = ""
    var chapterId: String = ""
    var courseId: String = ""
    var videoName: String = ""
    var courseName: String = ""
    var imagePath: String = ""
    var fileSize: Long = 0L
    var duration: Long = 0L
    var localPath: String = ""
    var progress: Float = 0f
    var downloadedBytes: Long = 0L
    var totalBytes: Long = 0L
    var status: HXCDownloadStatus = HXCDownloadStatus.IDLE
    var isWaiting: Boolean = false
    var errorMessage: String = ""
    var resolvedUrl: String = ""
    var secureHeaders: String = ""
    var isEncrypted: Boolean = false
    var progressStage: String = "IDLE"
    var stageProgress: Float = 0f
    var overallProgress: Float = 0f
    var mainProgress: Float = 0f
    var isChildTask: Boolean = false
    var child: HXCDownloadInfo? = null

    fun attachChild(childInfo: HXCDownloadInfo?): HXCDownloadInfo {
        child = childInfo?.apply { isChildTask = true }
        refreshAggregateState()
        return this
    }

    fun refreshAggregateState(): HXCDownloadInfo {
        val childInfo = child ?: return this
        childInfo.isChildTask = true
        val resolvedMainProgress = nodeProgress(this).also { mainProgress = it }
        val childProgress = nodeProgress(childInfo)
        progress = ((resolvedMainProgress + childProgress) / 2f).coerceIn(0f, 1f)
        overallProgress = progress
        status = aggregateStatus(this, childInfo)
        isWaiting = status == HXCDownloadStatus.WAITING || childInfo.isWaiting
        return this
    }

    private fun nodeProgress(info: HXCDownloadInfo): Float {
        if (info.status == HXCDownloadStatus.FINISH) return 1f
        if (info.progress > 0f) return info.progress.coerceIn(0f, 1f)
        return if (info.totalBytes > 0L && info.downloadedBytes >= 0L) {
            (info.downloadedBytes.toFloat() / info.totalBytes.toFloat()).coerceIn(0f, 1f)
        } else {
            0f
        }
    }

    private fun aggregateStatus(main: HXCDownloadInfo, child: HXCDownloadInfo): HXCDownloadStatus {
        if (main.status == HXCDownloadStatus.FINISH && child.status == HXCDownloadStatus.FINISH) {
            return HXCDownloadStatus.FINISH
        }
        if (main.status == HXCDownloadStatus.ERROR || child.status == HXCDownloadStatus.ERROR) {
            return HXCDownloadStatus.ERROR
        }
        if (main.status == HXCDownloadStatus.RUNNING || child.status == HXCDownloadStatus.RUNNING) {
            return HXCDownloadStatus.RUNNING
        }
        if (main.status == HXCDownloadStatus.WAITING || child.status == HXCDownloadStatus.WAITING ||
            main.isWaiting || child.isWaiting) {
            return HXCDownloadStatus.WAITING
        }
        if (main.status == HXCDownloadStatus.PAUSE || child.status == HXCDownloadStatus.PAUSE) {
            return HXCDownloadStatus.PAUSE
        }
        return HXCDownloadStatus.IDLE
    }
}
