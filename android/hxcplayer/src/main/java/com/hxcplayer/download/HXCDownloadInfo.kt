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
    var childDownloadKey: String = ""
    var child: HXCDownloadInfo? = null
}
