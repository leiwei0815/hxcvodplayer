package com.hxcplayer.download

class HXCDownloadRequest(
    val userId: String,
    val videoId: String
) {
    var plainUrl: String = ""
    var secureCredential: HXCSecureCredential? = null
    var fileId: String = ""
    var classId: String = ""
    var chapterId: String = ""
    var courseId: String = ""
    var videoName: String = ""
    var courseName: String = ""
    var imagePath: String = ""
    var fileSize: Long = 0L
    var duration: Long = 0L
    var encrypted: Boolean = false

    fun plainUrl(url: String): HXCDownloadRequest {
        this.plainUrl = url
        return this
    }

    fun secureCredential(credential: HXCSecureCredential): HXCDownloadRequest {
        this.secureCredential = credential
        return this
    }

    fun fileId(value: String): HXCDownloadRequest {
        fileId = value
        return this
    }

    fun classId(value: String): HXCDownloadRequest {
        classId = value
        return this
    }

    fun chapterId(value: String): HXCDownloadRequest {
        chapterId = value
        return this
    }

    fun courseId(value: String): HXCDownloadRequest {
        courseId = value
        return this
    }

    fun videoName(value: String): HXCDownloadRequest {
        videoName = value
        return this
    }

    fun courseName(value: String): HXCDownloadRequest {
        courseName = value
        return this
    }

    fun imagePath(value: String): HXCDownloadRequest {
        imagePath = value
        return this
    }

    fun fileSize(value: Long): HXCDownloadRequest {
        fileSize = value
        return this
    }

    fun duration(value: Long): HXCDownloadRequest {
        duration = value
        return this
    }

    fun encrypted(value: Boolean): HXCDownloadRequest {
        encrypted = value
        return this
    }

    fun buildDownloadKey(): String {
        val raw = "${userId}_${courseId}_${chapterId}_${videoId}_${fileId}"
        return raw.replace("[^a-zA-Z0-9_\\-]".toRegex(), "_")
    }
}
