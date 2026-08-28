package com.hxcplayer.monitor

import android.app.Activity
import android.app.Application
import android.content.Context
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import com.hxcplayer.download.HXCDownloadInfo
import com.hxcplayer.download.HXCDownloadStatus
import org.json.JSONObject
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger

/**
 * 下载监控（对齐 iOS HXCVDownloadMonitorSession）。
 *
 * 事件码 200/202–209/213/214。Android 没有后台 HLS 自动暂停、前台恢复、NSURLSession 迁移，
 * 因此不发 210–212。
 */
object HXCDownloadMonitorSession {
    const val CODE_DOWNLOAD_START = 200
    const val CODE_DOWNLOAD_STATE_CHANGE = 202
    const val CODE_DOWNLOAD_COMPLETE = 203
    const val CODE_DOWNLOAD_FAIL = 204
    const val CODE_DOWNLOAD_CANCEL = 205
    const val CODE_DOWNLOAD_CACHE_DELETE = 206
    const val CODE_DOWNLOAD_DELETE = 207
    const val CODE_DOWNLOAD_ENTER_BACKGROUND = 208
    const val CODE_DOWNLOAD_ENTER_FOREGROUND = 209
    const val CODE_DOWNLOAD_RESUME_REJECT = 213
    const val CODE_DOWNLOAD_RESUME_QUEUED = 214

    private const val SDK_VERSION = "1.0.17"
    private const val MODULE_TYPE = "download"

    @Volatile
    private var inited: Boolean = false
    private var reporter: HXCPlayerMonitorReporter? = null
    private val lastStatus = ConcurrentHashMap<String, HXCDownloadStatus>()
    private val startedActivities = AtomicInteger(0)
    private val appInBackground = AtomicBoolean(false)
    private val lifecycleHandler = Handler(Looper.getMainLooper())
    private val emitBackgroundRunnable = Runnable {
        if (startedActivities.get() <= 0 && appInBackground.compareAndSet(false, true)) {
            emitEnterBackground("platform=android")
        }
    }
    private val emitForegroundRunnable = Runnable {
        if (startedActivities.get() > 0 && appInBackground.compareAndSet(true, false)) {
            emitEnterForeground("platform=android")
        }
    }

    fun init(context: Context) {
        if (inited) return
        synchronized(this) {
            if (inited) return
            val app = context.applicationContext
            val appName = try {
                app.applicationInfo.loadLabel(app.packageManager).toString()
            } catch (_: Throwable) {
                app.packageName
            }
            reporter = HXCPlayerMonitorReporter(
                HXCPlayerMonitorConfig().apply {
                    debugLog = true
                },
                "android",
                SDK_VERSION,
                appName
            ).also { it.setActive(true) }
            (app as? Application)?.registerActivityLifecycleCallbacks(lifecycleCallbacks)
            inited = true
        }
    }

    fun resetTaskStates() {
        lastStatus.clear()
    }

    fun noteStatus(downloadKey: String, status: HXCDownloadStatus) {
        lastStatus[downloadKey] = status
    }

    fun emitStart(info: HXCDownloadInfo) {
        sendItem(
            eventCode = CODE_DOWNLOAD_START,
            eventName = "download_start",
            eventType = "info",
            errorCode = 0,
            message = "下载任务创建",
            info = info,
            extra = null
        )
    }

    fun onStatusChanged(info: HXCDownloadInfo) {
        val previous = lastStatus.put(info.downloadKey, info.status)
        if (previous == info.status) return
        when (info.status) {
            HXCDownloadStatus.FINISH -> emitComplete(info)
            HXCDownloadStatus.ERROR -> emitFail(info)
            else -> {
                if (previous == null && info.status == HXCDownloadStatus.WAITING) {
                    // start 已覆盖「任务创建」，避免冷启动/新建任务再刷一条 waiting。
                    return
                }
                val prevName = stateName(previous)
                sendItem(
                    eventCode = CODE_DOWNLOAD_STATE_CHANGE,
                    eventName = "download_state_change",
                    eventType = "info",
                    errorCode = 0,
                    message = "下载状态变更: $prevName -> ${stateName(info.status)}",
                    info = info,
                    extra = "previousState=$prevName"
                )
            }
        }
    }

    fun emitDelete(
        info: HXCDownloadInfo,
        removeFiles: Boolean,
        cacheRemoved: Boolean,
        persistenceSuccess: Boolean,
        errorMessage: String? = null
    ) {
        lastStatus.remove(info.downloadKey)
        val success = persistenceSuccess && (!removeFiles || cacheRemoved)
        val eventName = if (removeFiles) "download_cache_delete" else "download_delete"
        val eventCode = if (removeFiles) CODE_DOWNLOAD_CACHE_DELETE else CODE_DOWNLOAD_DELETE
        val message = when {
            removeFiles && success -> "下载缓存已删除"
            removeFiles && !cacheRemoved -> errorMessage?.ifBlank { null } ?: "删除下载缓存失败"
            removeFiles -> errorMessage?.ifBlank { null } ?: "删除下载记录失败"
            persistenceSuccess -> "下载记录已删除"
            else -> errorMessage?.ifBlank { null } ?: "删除下载记录失败"
        }
        val extra = "removeFiles=${yesNo(removeFiles)}, cacheRemoved=${yesNo(cacheRemoved)}, " +
            "persistenceSuccess=${yesNo(persistenceSuccess)}"
        sendItem(
            eventCode = eventCode,
            eventName = eventName,
            eventType = if (success) "info" else "error",
            errorCode = if (success) 0 else -1,
            message = message,
            info = info,
            extra = extra
        )
    }

    fun emitResumeReject(info: HXCDownloadInfo?, downloadKey: String?, reason: String) {
        val msg = reason.ifBlank { "恢复下载被拒绝" }
        if (info != null) {
            sendItem(
                eventCode = CODE_DOWNLOAD_RESUME_REJECT,
                eventName = "download_resume_reject",
                eventType = "error",
                errorCode = -1,
                message = msg,
                info = info,
                extra = "reason=$msg"
            )
            return
        }
        sendBare(
            eventCode = CODE_DOWNLOAD_RESUME_REJECT,
            eventName = "download_resume_reject",
            eventType = "error",
            errorCode = -1,
            message = msg,
            downloadTaskId = downloadKey.orEmpty(),
            detail = "reason=$msg",
            userId = null
        )
    }

    fun emitResumeQueued(info: HXCDownloadInfo) {
        sendItem(
            eventCode = CODE_DOWNLOAD_RESUME_QUEUED,
            eventName = "download_resume_queued",
            eventType = "info",
            errorCode = 0,
            message = "恢复下载已入队等待",
            info = info,
            extra = "action=resume,queued=YES"
        )
    }

    private fun emitComplete(info: HXCDownloadInfo) {
        sendItem(
            eventCode = CODE_DOWNLOAD_COMPLETE,
            eventName = "download_complete",
            eventType = "info",
            errorCode = 0,
            message = "下载完成",
            info = info,
            extra = null
        )
    }

    private fun emitFail(info: HXCDownloadInfo) {
        val msg = info.errorMessage.ifBlank { "下载失败" }
        sendItem(
            eventCode = CODE_DOWNLOAD_FAIL,
            eventName = "download_fail",
            eventType = "error",
            errorCode = -1,
            message = msg,
            info = info,
            extra = info.errorMessage.ifBlank { null }
        )
    }

    private fun emitEnterBackground(detail: String?) {
        sendBare(
            eventCode = CODE_DOWNLOAD_ENTER_BACKGROUND,
            eventName = "download_enter_background",
            eventType = "info",
            errorCode = 0,
            message = "应用进入后台（下载）",
            downloadTaskId = "",
            detail = detail,
            userId = null
        )
    }

    private fun emitEnterForeground(detail: String?) {
        sendBare(
            eventCode = CODE_DOWNLOAD_ENTER_FOREGROUND,
            eventName = "download_enter_foreground",
            eventType = "info",
            errorCode = 0,
            message = "应用回到前台（下载）",
            downloadTaskId = "",
            detail = detail,
            userId = null
        )
    }

    private val lifecycleCallbacks = object : Application.ActivityLifecycleCallbacks {
        override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) = Unit

        override fun onActivityStarted(activity: Activity) {
            val count = startedActivities.incrementAndGet()
            if (count == 1) {
                lifecycleHandler.removeCallbacks(emitBackgroundRunnable)
                lifecycleHandler.removeCallbacks(emitForegroundRunnable)
                lifecycleHandler.postDelayed(emitForegroundRunnable, 800L)
            }
        }

        override fun onActivityResumed(activity: Activity) = Unit
        override fun onActivityPaused(activity: Activity) = Unit

        override fun onActivityStopped(activity: Activity) {
            val count = startedActivities.decrementAndGet()
            if (count <= 0) {
                startedActivities.set(0)
                lifecycleHandler.removeCallbacks(emitBackgroundRunnable)
                lifecycleHandler.removeCallbacks(emitForegroundRunnable)
                lifecycleHandler.postDelayed(emitBackgroundRunnable, 800L)
            }
        }

        override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) = Unit
        override fun onActivityDestroyed(activity: Activity) = Unit
    }

    private fun sendItem(
        eventCode: Int,
        eventName: String,
        eventType: String,
        errorCode: Int,
        message: String,
        info: HXCDownloadInfo,
        extra: String?
    ) {
        val event = baseEvent(eventType, eventName, eventCode, errorCode, message)
        event.put("downloadTaskId", info.downloadKey)
        event.put("progress", info.progress.toDouble())
        event.put("bytesWritten", info.downloadedBytes)
        event.put("totalBytes", info.totalBytes)
        if (info.videoId.isNotBlank()) event.put("videoId", info.videoId)
        if (info.fileId.isNotBlank()) event.put("fileId", info.fileId)
        if (info.chapterId.isNotBlank()) event.put("chapterId", info.chapterId)
        if (info.courseId.isNotBlank()) event.put("courseId", info.courseId)
        val detail = detailForItem(info, extra)
        if (detail.isNotBlank()) event.put("detail", detail)
        enqueue(event, info.userId)
    }

    private fun sendBare(
        eventCode: Int,
        eventName: String,
        eventType: String,
        errorCode: Int,
        message: String,
        downloadTaskId: String,
        detail: String?,
        userId: String?
    ) {
        val event = baseEvent(eventType, eventName, eventCode, errorCode, message)
        event.put("downloadTaskId", downloadTaskId)
        if (!detail.isNullOrBlank()) event.put("detail", detail)
        enqueue(event, userId)
    }

    private fun baseEvent(
        eventType: String,
        eventName: String,
        eventCode: Int,
        errorCode: Int,
        message: String
    ): JSONObject {
        return JSONObject()
            .put("eventType", eventType)
            .put("eventName", eventName)
            .put("eventCode", eventCode)
            .put("errorCode", errorCode)
            .put("message", message)
            .put("timestampMs", System.currentTimeMillis())
            .put("moduleType", MODULE_TYPE)
            .put("sdkVersion", SDK_VERSION)
    }

    private fun enqueue(event: JSONObject, userId: String?) {
        val r = reporter ?: return
        if (!userId.isNullOrBlank()) {
            r.setUserId(userId)
        }
        r.enqueue(event, true)
    }

    private fun detailForItem(info: HXCDownloadInfo, extra: String?): String {
        val parts = ArrayList<String>(6)
        parts.add("downloadType=${downloadType(info)}")
        parts.add("state=${stateName(info.status)}")
        if (info.resolvedUrl.isNotBlank()) {
            parts.add("url=${info.resolvedUrl}")
        }
        parts.add("identifier=${info.downloadKey}")
        if (info.progressStage.isNotBlank()) {
            parts.add("stage=${info.progressStage}")
        }
        if (!extra.isNullOrBlank()) {
            parts.add(extra)
        }
        return parts.joinToString(", ")
    }

    private fun downloadType(info: HXCDownloadInfo): String {
        val url = info.resolvedUrl.lowercase()
        val path = info.localPath.lowercase()
        return if (url.contains(".m3u8") || path.contains(".m3u8") || path.endsWith("index.m3u8")) {
            "hls"
        } else {
            "progressive"
        }
    }

    private fun stateName(status: HXCDownloadStatus?): String {
        return when (status) {
            HXCDownloadStatus.WAITING -> "waiting"
            HXCDownloadStatus.RUNNING -> "running"
            HXCDownloadStatus.PAUSE -> "paused"
            HXCDownloadStatus.ERROR -> "failed"
            HXCDownloadStatus.FINISH -> "completed"
            HXCDownloadStatus.IDLE -> "idle"
            null -> "unknown"
        }
    }

    private fun yesNo(value: Boolean): String = if (value) "YES" else "NO"
}
