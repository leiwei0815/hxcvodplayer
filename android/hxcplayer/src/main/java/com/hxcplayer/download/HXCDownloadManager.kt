package com.hxcplayer.download

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.hxcplayer.BuildConfig
import com.hxcplayer.download.m3u8.HXCM3u8Parser
import java.io.File
import java.io.RandomAccessFile
import java.net.URI
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.Future
import java.util.concurrent.atomic.AtomicBoolean

object HXCDownloadManager {
    private const val TAG = "HXCDownloadManager"

    private lateinit var appContext: Context
    private var config: HXCDownloadConfig = HXCDownloadConfig()
    private var inited = false
    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile
    private var listener: HXCDownloadListener? = null

    private var ioExecutor: ExecutorService = Executors.newFixedThreadPool(2)
    private val downloads = ConcurrentHashMap<String, HXCDownloadInfo>()
    private val runningFutures = ConcurrentHashMap<String, Future<*>>()
    private val cancelledFlags = ConcurrentHashMap<String, AtomicBoolean>()
    private var store: HXCDownloadStore? = null

    @Synchronized
    fun init(context: Context, config: HXCDownloadConfig = HXCDownloadConfig()) {
        appContext = context.applicationContext
        this.config = config.copy(maxConcurrent = config.maxConcurrent.coerceAtLeast(1))
        ioExecutor.shutdownNow()
        ioExecutor = Executors.newFixedThreadPool(this.config.maxConcurrent)
        store = HXCDownloadStore(appContext)
        downloads.clear()
        store?.load()?.forEach {
            if (it.status == HXCDownloadStatus.RUNNING || it.status == HXCDownloadStatus.WAITING) {
                it.status = HXCDownloadStatus.PAUSE
                it.isWaiting = false
            }
            downloads[it.downloadKey] = it
        }
        inited = true
        notifyListChanged()
    }

    fun setListener(listener: HXCDownloadListener?) {
        this.listener = listener
        notifyListChanged()
    }

    fun startDownload(request: HXCDownloadRequest) {
        startDownload(listOf(request))
    }

    fun startDownload(requests: List<HXCDownloadRequest>) {
        ensureInit()
        requests.forEach { req ->
            val key = req.buildDownloadKey()
            val info = downloads[key] ?: createInfoFromRequest(req)
            downloads[key] = info
            info.status = HXCDownloadStatus.WAITING
            info.isWaiting = true
            postStatusChanged(info)
            notifyListChanged()

            val future = ioExecutor.submit {
                val cancelFlag = AtomicBoolean(false)
                cancelledFlags[key] = cancelFlag
                try {
                    val source = HXCSecureDownloadAuthResolver.resolve(req, config)
                    info.resolvedUrl = source.url
                    info.secureHeaders = source.secureHeaders
                    runDownload(info, cancelFlag)
                } catch (t: Throwable) {
                    info.status = HXCDownloadStatus.ERROR
                    info.errorMessage = t.message ?: "download failed"
                    info.isWaiting = false
                    postStatusChanged(info)
                    notifyListChanged()
                    d("download error key=$key msg=${info.errorMessage}")
                } finally {
                    runningFutures.remove(key)
                }
            }
            runningFutures[key] = future
        }
    }

    fun pauseDownload(downloadKey: String) {
        val info = downloads[downloadKey] ?: return
        cancelledFlags[downloadKey]?.set(true)
        runningFutures[downloadKey]?.cancel(true)
        info.status = HXCDownloadStatus.PAUSE
        info.isWaiting = false
        postStatusChanged(info)
        notifyListChanged()
    }

    fun pauseAll() {
        getAllDownloads()
            .filter { it.status == HXCDownloadStatus.RUNNING || it.status == HXCDownloadStatus.WAITING }
            .forEach { pauseDownload(it.downloadKey) }
    }

    fun resumeDownload(downloadKey: String) {
        val info = downloads[downloadKey] ?: return
        if (info.resolvedUrl.isBlank()) {
            info.status = HXCDownloadStatus.ERROR
            info.errorMessage = "resume failed: missing resolved url"
            postStatusChanged(info)
            notifyListChanged()
            return
        }
        val request = HXCDownloadRequest(info.userId, info.videoId)
            .plainUrl(info.resolvedUrl)
            .fileId(info.fileId)
            .classId(info.classId)
            .chapterId(info.chapterId)
            .courseId(info.courseId)
            .videoName(info.videoName)
            .courseName(info.courseName)
            .imagePath(info.imagePath)
            .fileSize(info.fileSize)
            .duration(info.duration)
        startDownload(request)
    }

    fun resumeAll() {
        getAllDownloads()
            .filter { it.status == HXCDownloadStatus.PAUSE || it.status == HXCDownloadStatus.ERROR }
            .forEach { resumeDownload(it.downloadKey) }
    }

    fun deleteDownload(downloadKey: String, deleteFile: Boolean = true) {
        cancelledFlags[downloadKey]?.set(true)
        runningFutures[downloadKey]?.cancel(true)
        runningFutures.remove(downloadKey)
        cancelledFlags.remove(downloadKey)
        val info = downloads.remove(downloadKey) ?: return
        if (deleteFile && info.localPath.isNotBlank()) {
            kotlin.runCatching { File(info.localPath).delete() }
        }
        notifyListChanged()
    }

    fun deleteDownloads(keys: List<String>, deleteFile: Boolean = true) {
        keys.forEach { deleteDownload(it, deleteFile) }
    }

    fun deleteDownloads(infos: List<HXCDownloadInfo>, deleteFile: Boolean = true) {
        deleteDownloads(infos.map { it.downloadKey }, deleteFile)
    }

    fun getAllDownloads(): List<HXCDownloadInfo> = downloads.values.toList()

    fun getDownloadsByStatus(status: HXCDownloadStatus): List<HXCDownloadInfo> {
        return downloads.values.filter { it.status == status }
    }

    fun queryByChapterAndVideo(chapterId: String, videoId: String): HXCDownloadInfo? {
        return downloads.values.firstOrNull { it.chapterId == chapterId && it.videoId == videoId }
    }

    private fun runDownload(info: HXCDownloadInfo, cancelFlag: AtomicBoolean) {
        val key = info.downloadKey
        val dir = config.downloadRootDir ?: File(appContext.filesDir, "hxc_downloads")
        if (!dir.exists()) {
            dir.mkdirs()
        }
        val ext = when {
            info.resolvedUrl.contains(".m3u8", ignoreCase = true) -> "m3u8"
            else -> "mp4"
        }
        val targetFile = File(dir, "${key}.${ext}.cache")
        info.localPath = targetFile.absolutePath

        if (ext == "m3u8") {
            runM3u8Download(info, cancelFlag, dir)
            return
        }

        val existing = if (targetFile.exists()) targetFile.length() else 0L
        val conn = (URL(info.resolvedUrl).openConnection() as HttpURLConnection).apply {
            requestMethod = "GET"
            connectTimeout = config.connectTimeoutMs
            readTimeout = config.readTimeoutMs
            if (existing > 0L) {
                setRequestProperty("Range", "bytes=$existing-")
            }
            applyHeaders(this, info.secureHeaders)
        }

        conn.connect()
        val code = conn.responseCode
        if (code !in 200..299 && code != 206) {
            conn.disconnect()
            throw IllegalStateException("HTTP $code")
        }

        val total = if (code == 206) {
            existing + conn.contentLengthLong.coerceAtLeast(0L)
        } else {
            conn.contentLengthLong.coerceAtLeast(0L)
        }
        info.totalBytes = total
        info.downloadedBytes = existing
        info.fileSize = if (info.fileSize <= 0L && total > 0L) total / (1024 * 1024) else info.fileSize
        info.status = HXCDownloadStatus.RUNNING
        info.isWaiting = false
        postStatusChanged(info)

        RandomAccessFile(targetFile, "rw").use { raf ->
            if (existing > 0L) {
                raf.seek(existing)
            }
            conn.inputStream.use { input ->
                val buffer = ByteArray(8 * 1024)
                while (true) {
                    if (cancelFlag.get() || Thread.currentThread().isInterrupted) {
                        info.status = HXCDownloadStatus.PAUSE
                        info.isWaiting = false
                        postStatusChanged(info)
                        notifyListChanged()
                        return
                    }
                    val len = input.read(buffer)
                    if (len <= 0) break
                    raf.write(buffer, 0, len)
                    info.downloadedBytes += len
                    if (info.totalBytes > 0L) {
                        info.progress = (info.downloadedBytes.toFloat() / info.totalBytes.toFloat())
                            .coerceIn(0f, 1f)
                    }
                    postProgress(info)
                }
            }
        }
        conn.disconnect()
        info.progress = 1f
        info.status = HXCDownloadStatus.FINISH
        info.isWaiting = false
        postStatusChanged(info)
        notifyListChanged()
        d("download finish key=$key path=${info.localPath}")
    }

    private fun runM3u8Download(info: HXCDownloadInfo, cancelFlag: AtomicBoolean, rootDir: File) {
        val m3u8Dir = File(rootDir, info.downloadKey)
        if (!m3u8Dir.exists()) {
            m3u8Dir.mkdirs()
        }
        val (content, finalUrl) = fetchTextWithFinalUrl(info.resolvedUrl, info.secureHeaders)
        val parsed = HXCM3u8Parser.parse(finalUrl, content)
        if (parsed.tsList.isEmpty()) {
            throw IllegalStateException("m3u8 ts list is empty")
        }
        info.status = HXCDownloadStatus.RUNNING
        info.isWaiting = false
        info.totalBytes = parsed.tsList.size.toLong()
        postStatusChanged(info)

        var finished = 0L
        parsed.tsList.forEach { ts ->
            if (cancelFlag.get() || Thread.currentThread().isInterrupted) {
                info.status = HXCDownloadStatus.PAUSE
                info.isWaiting = false
                postStatusChanged(info)
                notifyListChanged()
                return
            }
            val tsFile = File(m3u8Dir, ts.localName)
            if (!tsFile.exists() || tsFile.length() <= 0L) {
                downloadSimpleFile(ts.url, tsFile, info.secureHeaders)
            }
            finished += 1
            info.downloadedBytes = finished
            info.progress = (finished.toFloat() / info.totalBytes.toFloat()).coerceIn(0f, 1f)
            postProgress(info)
        }
        val localM3u8 = File(m3u8Dir, "index.m3u8")
        val body = buildString {
            append("#EXTM3U\n")
            parsed.headerLines.forEach { line ->
                if (!line.startsWith("#EXTM3U")) {
                    append(line).append('\n')
                }
            }
            parsed.tsList.forEach { ts ->
                append("#EXTINF:${ts.duration},\n")
                append(ts.localName).append('\n')
            }
            append("#EXT-X-ENDLIST\n")
        }
        localM3u8.writeText(body)
        info.localPath = localM3u8.absolutePath
        info.progress = 1f
        info.status = HXCDownloadStatus.FINISH
        info.isWaiting = false
        postStatusChanged(info)
        notifyListChanged()
        d("m3u8 finish key=${info.downloadKey} path=${info.localPath}")
    }

    private fun fetchTextWithFinalUrl(url: String, headers: String): Pair<String, String> {
        val conn = (URL(url).openConnection() as HttpURLConnection).apply {
            requestMethod = "GET"
            connectTimeout = config.connectTimeoutMs
            readTimeout = config.readTimeoutMs
            applyHeaders(this, headers)
            instanceFollowRedirects = true
        }
        conn.connect()
        val code = conn.responseCode
        if (code !in 200..299) {
            conn.disconnect()
            throw IllegalStateException("HTTP $code")
        }
        val text = conn.inputStream.bufferedReader(Charsets.UTF_8).use { it.readText() }
        val finalUrl = conn.url?.toString()
            ?: conn.getHeaderField("Location")
            ?: url
        conn.disconnect()
        return text to normalizeFinalUrl(url, finalUrl)
    }

    private fun normalizeFinalUrl(origin: String, finalUrl: String): String {
        if (finalUrl.startsWith("http://") || finalUrl.startsWith("https://")) return finalUrl
        return runCatching {
            val originUri = URI(origin)
            URI(
                originUri.scheme,
                originUri.authority,
                finalUrl,
                null,
                null
            ).toString()
        }.getOrElse { origin }
    }

    private fun downloadSimpleFile(url: String, target: File, headers: String) {
        val conn = (URL(url).openConnection() as HttpURLConnection).apply {
            requestMethod = "GET"
            connectTimeout = config.connectTimeoutMs
            readTimeout = config.readTimeoutMs
            applyHeaders(this, headers)
        }
        conn.connect()
        val code = conn.responseCode
        if (code !in 200..299) {
            conn.disconnect()
            throw IllegalStateException("download ts failed: HTTP $code")
        }
        target.outputStream().use { out ->
            conn.inputStream.use { input ->
                val buf = ByteArray(8 * 1024)
                while (true) {
                    val len = input.read(buf)
                    if (len <= 0) break
                    out.write(buf, 0, len)
                }
            }
        }
        conn.disconnect()
    }

    private fun createInfoFromRequest(req: HXCDownloadRequest): HXCDownloadInfo {
        return HXCDownloadInfo(
            downloadKey = req.buildDownloadKey(),
            userId = req.userId,
            videoId = req.videoId
        ).apply {
            fileId = req.fileId
            classId = req.classId
            chapterId = req.chapterId
            courseId = req.courseId
            videoName = req.videoName
            courseName = req.courseName
            imagePath = req.imagePath
            fileSize = req.fileSize
            duration = req.duration
            status = HXCDownloadStatus.IDLE
            resolvedUrl = req.plainUrl
        }
    }

    private fun applyHeaders(conn: HttpURLConnection, headers: String) {
        if (headers.isBlank()) return
        headers.split("\r\n", "\n").forEach { line ->
            val trimmed = line.trim()
            if (trimmed.isEmpty()) return@forEach
            val idx = trimmed.indexOf(':')
            if (idx <= 0 || idx >= trimmed.length - 1) return@forEach
            val key = trimmed.substring(0, idx).trim()
            val value = trimmed.substring(idx + 1).trim()
            if (key.isNotEmpty() && value.isNotEmpty()) {
                conn.setRequestProperty(key, value)
            }
        }
    }

    private fun notifyListChanged() {
        val all = getAllDownloads()
        store?.save(all)
        mainHandler.post { listener?.onDownloadListChanged(all) }
    }

    private fun postProgress(info: HXCDownloadInfo) {
        val copy = info.copy()
        mainHandler.post { listener?.onProgressUpdate(copy) }
    }

    private fun postStatusChanged(info: HXCDownloadInfo) {
        val copy = info.copy()
        mainHandler.post { listener?.onStatusChanged(copy) }
    }

    private fun ensureInit() {
        check(inited) { "HXCDownloadManager not initialized. Call init(context, config) first." }
    }

    fun formattedTime(second: Long): String {
        val h = second / 3600
        val m = (second % 3600) / 60
        val s = second % 60
        return if (h > 0) {
            String.format("%02d:%02d:%02d", h, m, s)
        } else {
            String.format("%02d:%02d", m, s)
        }
    }

    private fun d(msg: String) {
        if (BuildConfig.DEBUG) {
            Log.d(TAG, msg)
        }
    }
}
