package com.hxcplayer.download

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

internal class HXCDownloadStore(context: Context) {
    private val rootDir = File(context.filesDir, "hxc_downloads")
    private val stateFile = File(rootDir, "download_state.json")

    fun load(): List<HXCDownloadInfo> {
        if (!stateFile.exists()) return emptyList()
        return runCatching {
            val text = stateFile.readText()
            if (text.isBlank()) return emptyList()
            val array = JSONArray(text)
            buildList {
                for (i in 0 until array.length()) {
                    val obj = array.optJSONObject(i) ?: continue
                    add(fromJson(obj))
                }
            }
        }.getOrDefault(emptyList())
    }

    fun save(infos: List<HXCDownloadInfo>) {
        runCatching {
            if (!rootDir.exists()) rootDir.mkdirs()
            val array = JSONArray()
            infos.forEach { array.put(toJson(it)) }
            stateFile.writeText(array.toString())
        }
    }

    private fun toJson(info: HXCDownloadInfo): JSONObject {
        return JSONObject().apply {
            put("downloadKey", info.downloadKey)
            put("userId", info.userId)
            put("videoId", info.videoId)
            put("fileId", info.fileId)
            put("classId", info.classId)
            put("chapterId", info.chapterId)
            put("courseId", info.courseId)
            put("videoName", info.videoName)
            put("courseName", info.courseName)
            put("imagePath", info.imagePath)
            put("fileSize", info.fileSize)
            put("duration", info.duration)
            put("localPath", info.localPath)
            put("progress", info.progress.toDouble())
            put("downloadedBytes", info.downloadedBytes)
            put("totalBytes", info.totalBytes)
            put("status", info.status.name)
            put("isWaiting", info.isWaiting)
            put("errorMessage", info.errorMessage)
            put("resolvedUrl", info.resolvedUrl)
            put("secureHeaders", info.secureHeaders)
            put("isEncrypted", info.isEncrypted)
            put("progressStage", info.progressStage)
            put("stageProgress", info.stageProgress.toDouble())
            put("overallProgress", info.overallProgress.toDouble())
            put("childDownloadKey", info.childDownloadKey)
        }
    }

    private fun fromJson(obj: JSONObject): HXCDownloadInfo {
        return HXCDownloadInfo(
            downloadKey = obj.optString("downloadKey"),
            userId = obj.optString("userId"),
            videoId = obj.optString("videoId")
        ).apply {
            fileId = obj.optString("fileId")
            classId = obj.optString("classId")
            chapterId = obj.optString("chapterId")
            courseId = obj.optString("courseId")
            videoName = obj.optString("videoName")
            courseName = obj.optString("courseName")
            imagePath = obj.optString("imagePath")
            fileSize = obj.optLong("fileSize")
            duration = obj.optLong("duration")
            localPath = obj.optString("localPath")
            progress = obj.optDouble("progress", 0.0).toFloat()
            downloadedBytes = obj.optLong("downloadedBytes")
            totalBytes = obj.optLong("totalBytes")
            status = runCatching { HXCDownloadStatus.valueOf(obj.optString("status")) }
                .getOrElse { HXCDownloadStatus.IDLE }
            isWaiting = obj.optBoolean("isWaiting", false)
            errorMessage = obj.optString("errorMessage")
            resolvedUrl = obj.optString("resolvedUrl")
            secureHeaders = obj.optString("secureHeaders")
            isEncrypted = obj.optBoolean("isEncrypted", secureHeaders.isNotBlank())
            progressStage = obj.optString("progressStage", "IDLE")
            stageProgress = obj.optDouble("stageProgress", progress.toDouble()).toFloat()
            overallProgress = obj.optDouble("overallProgress", progress.toDouble()).toFloat()
            childDownloadKey = obj.optString("childDownloadKey")
        }
    }
}
