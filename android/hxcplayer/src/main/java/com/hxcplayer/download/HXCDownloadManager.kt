package com.hxcplayer.download

import android.content.Context
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.hxcplayer.download.m3u8.HXCM3u8Parser
import java.io.File
import java.io.RandomAccessFile
import java.net.URI
import java.net.HttpURLConnection
import java.net.URL
import javax.crypto.Cipher
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.SecretKeySpec
import java.security.MessageDigest
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

    private data class M3u8ResourceTask(
        val url: String,
        val rawUrl: String,
        val target: File,
        val rangeStart: Long = -1L,
        val rangeEnd: Long = -1L,
        val segmentSequence: Long = -1L,
        val decryptKeyFile: File? = null,
        val decryptIvHex: String? = null
    ) {
        fun hasRange(): Boolean = rangeStart >= 0L && rangeEnd >= rangeStart
        fun expectedLength(): Long = if (hasRange()) rangeEnd - rangeStart + 1 else -1L
        fun needsDecrypt(): Boolean = decryptKeyFile != null && target.name.endsWith(".ts")
    }

    private data class M3u8BuildArtifacts(
        val outputContent: String,
        val resources: List<M3u8ResourceTask>,
        val tsCount: Int,
        val mapCount: Int,
        val keyCount: Int,
        val byterangeCount: Int,
        val decryptedSegments: Int
    )

    private data class HlsKeyContext(
        val method: String,
        val keyFile: File?,
        val ivHex: String?
    )

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
                    info.isEncrypted = info.isEncrypted || req.encrypted || source.encrypted
                    d(
                        "download source resolved, key=$key, encrypted=${info.isEncrypted}, " +
                            "secureHeaders=${info.secureHeaders.isNotBlank()}, finalUrl=${info.resolvedUrl}"
                    )
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
            .encrypted(info.isEncrypted)
            .secureHeaders(info.secureHeaders)
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
        d("m3u8 source fetched, key=${info.downloadKey}, finalUrl=$finalUrl, chars=${content.length}")
        val artifacts = buildLocalM3u8AndDownloadResources(
            sourceContent = content,
            baseUrl = finalUrl,
            saveDir = m3u8Dir,
            headers = info.secureHeaders,
            cancelFlag = cancelFlag,
            info = info
        )
        if (info.status == HXCDownloadStatus.PAUSE) {
            d("m3u8 paused before index write, key=${info.downloadKey}")
            return
        }
        val localM3u8 = File(m3u8Dir, "index.m3u8")
        localM3u8.writeText(artifacts.outputContent)
        d(
            "m3u8 local index written, key=${info.downloadKey}, path=${localM3u8.absolutePath}, " +
                    "size=${localM3u8.length()}, lines=${artifacts.outputContent.lines().size}"
        )
        d(
            "offline_decrypt_applied, key=${info.downloadKey}, encrypted=${info.isEncrypted}, " +
                    "applied=${artifacts.decryptedSegments > 0}, decryptedTs=${artifacts.decryptedSegments}, " +
                    "tsTotal=${artifacts.tsCount}"
        )
        if (info.isEncrypted && artifacts.tsCount > 0 && artifacts.decryptedSegments == 0) {
            d("offline_decrypt_warning, key=${info.downloadKey}, reason=encrypted_stream_but_no_segment_decrypted")
        }
        auditLocalM3u8(info, localM3u8, artifacts)
        info.localPath = localM3u8.absolutePath
        info.progress = 1f
        info.status = HXCDownloadStatus.FINISH
        info.isWaiting = false
        postStatusChanged(info)
        notifyListChanged()
        d("m3u8 finish key=${info.downloadKey} path=${info.localPath}")
    }

    private fun buildLocalM3u8AndDownloadResources(
        sourceContent: String,
        baseUrl: String,
        saveDir: File,
        headers: String,
        cancelFlag: AtomicBoolean,
        info: HXCDownloadInfo
    ): M3u8BuildArtifacts {
        val resources = mutableListOf<M3u8ResourceTask>()
        val outputLines = mutableListOf<String>()
        var segmentIndex = 0
        var mapIndex = 0
        var keyIndex = 0
        var decryptedSegments = 0
        var byterangeCount = 0
        var byteRangeLength = -1L
        var byteRangeOffset = -1L
        var mediaSequenceBase = 0L
        var mediaSequence = 0L
        var activeKey: HlsKeyContext? = null

        sourceContent.lines().forEach { raw ->
            val line = raw.trim()
            if (line.isEmpty()) {
                outputLines.add("")
                return@forEach
            }
            when {
                line.startsWith("#EXT-X-MEDIA-SEQUENCE") -> {
                    mediaSequenceBase = line.substringAfter(":").trim().toLongOrNull() ?: mediaSequenceBase
                    mediaSequence = mediaSequenceBase
                    outputLines.add(line)
                }
                line.startsWith("#EXT-X-BYTERANGE") -> {
                    val (length, offset) = parseByteRange(line, byteRangeOffset + if (byteRangeLength > 0) byteRangeLength else 0L)
                    byteRangeLength = length
                    byteRangeOffset = offset
                    byterangeCount += 1
                    // 本地离线已将目标切分成独立文件，保留原 BYTERANGE 会导致再次裁剪而读坏数据。
                }
                line.startsWith("#EXT-X-MAP") -> {
                    val mapUri = extractUriAttr(line)
                    if (!mapUri.isNullOrBlank()) {
                        val abs = HXCM3u8Parser.toAbsoluteUrl(baseUrl, mapUri)
                        val localName = String.format("init_%04d.map", mapIndex++)
                        val localFile = File(saveDir, localName)
                        resources.add(M3u8ResourceTask(abs, abs, localFile))
                        outputLines.add(replaceUriAttr(line, localName))
                    } else {
                        outputLines.add(line)
                    }
                }
                line.startsWith("#EXT-X-KEY") -> {
                    val keyUri = extractUriAttr(line)
                    val method = extractAttrValue(line, "METHOD").ifBlank { "AES-128" }.uppercase()
                    val ivHex = extractAttrValue(line, "IV")
                    if (method == "NONE") {
                        activeKey = null
                        outputLines.add(line)
                    } else if (!keyUri.isNullOrBlank()) {
                        val abs = HXCM3u8Parser.toAbsoluteUrl(baseUrl, keyUri)
                        val localName = String.format("key_%04d.key", keyIndex++)
                        val localFile = File(saveDir, localName)
                        resources.add(M3u8ResourceTask(abs, abs, localFile))
                        activeKey = HlsKeyContext(method = method, keyFile = localFile, ivHex = ivHex)
                        if (method != "AES-128") {
                            // 非 AES-128 维持原始 KEY 标签，避免误处理。
                            outputLines.add(replaceUriAttr(line, localName))
                        }
                    } else {
                        outputLines.add(line)
                    }
                }
                line.startsWith("#") -> {
                    outputLines.add(line)
                }
                else -> {
                    val localName = String.format("%04d.ts", segmentIndex++)
                    val localFile = File(saveDir, localName)
                    val absRaw = HXCM3u8Parser.toAbsoluteUrl(baseUrl, line)
                    val queryRange = parseStartEndQuery(line)
                    val keyCtx = activeKey
                    val task = if (queryRange.first >= 0L && queryRange.second >= queryRange.first) {
                        val cleanLine = stripQuery(line)
                        val cleanAbs = HXCM3u8Parser.toAbsoluteUrl(baseUrl, cleanLine)
                        M3u8ResourceTask(
                            url = cleanAbs,
                            rawUrl = absRaw,
                            target = localFile,
                            rangeStart = queryRange.first,
                            rangeEnd = queryRange.second,
                            segmentSequence = mediaSequence,
                            decryptKeyFile = if (keyCtx?.method == "AES-128") keyCtx.keyFile else null,
                            decryptIvHex = if (keyCtx?.method == "AES-128") keyCtx.ivHex else null
                        )
                    } else if (byteRangeLength > 0L) {
                        val start = byteRangeOffset
                        val end = byteRangeOffset + byteRangeLength - 1
                        byteRangeLength = -1L
                        M3u8ResourceTask(
                            url = absRaw,
                            rawUrl = absRaw,
                            target = localFile,
                            rangeStart = start,
                            rangeEnd = end,
                            segmentSequence = mediaSequence,
                            decryptKeyFile = if (keyCtx?.method == "AES-128") keyCtx.keyFile else null,
                            decryptIvHex = if (keyCtx?.method == "AES-128") keyCtx.ivHex else null
                        )
                    } else {
                        M3u8ResourceTask(
                            url = absRaw,
                            rawUrl = absRaw,
                            target = localFile,
                            segmentSequence = mediaSequence,
                            decryptKeyFile = if (keyCtx?.method == "AES-128") keyCtx.keyFile else null,
                            decryptIvHex = if (keyCtx?.method == "AES-128") keyCtx.ivHex else null
                        )
                    }
                    resources.add(task)
                    outputLines.add(localName)
                    mediaSequence += 1
                }
            }
        }

        if (resources.isEmpty()) {
            throw IllegalStateException("m3u8 resources list is empty")
        }
        val mapCount = resources.count { it.target.name.endsWith(".map") }
        val keyCount = resources.count { it.target.name.endsWith(".key") }
        val tsCount = resources.size - mapCount - keyCount
        d(
            "m3u8 parse summary, key=${info.downloadKey}, resources=${resources.size}, " +
                    "ts=$tsCount, map=$mapCount, key=$keyCount, byterange=$byterangeCount"
        )

        info.status = HXCDownloadStatus.RUNNING
        info.isWaiting = false
        info.totalBytes = resources.size.toLong()
        info.downloadedBytes = 0L
        postStatusChanged(info)

        var finished = 0L
        for (task in resources) {
            if (cancelFlag.get() || Thread.currentThread().isInterrupted) {
                info.status = HXCDownloadStatus.PAUSE
                info.isWaiting = false
                postStatusChanged(info)
                notifyListChanged()
                return M3u8BuildArtifacts(
                    outputContent = "",
                    resources = resources.toList(),
                    tsCount = tsCount,
                    mapCount = mapCount,
                    keyCount = keyCount,
                    byterangeCount = byterangeCount,
                    decryptedSegments = decryptedSegments
                )
            }
            if (!task.target.exists() || !isResourceComplete(task)) {
                downloadTaskResource(task, headers)
            }
            if (task.needsDecrypt()) {
                val decrypted = decryptAes128SegmentInPlace(task)
                if (decrypted) {
                    decryptedSegments += 1
                }
            }
            finished += 1
            info.downloadedBytes = finished
            info.progress = (finished.toFloat() / info.totalBytes.toFloat()).coerceIn(0f, 1f)
            postProgress(info)
        }
        d(
            "m3u8 resources complete, key=${info.downloadKey}, finished=$finished/${resources.size}, " +
                    "outputLines=${outputLines.size}, decryptedTs=$decryptedSegments"
        )
        return M3u8BuildArtifacts(
            outputContent = outputLines.joinToString("\n", postfix = "\n"),
            resources = resources.toList(),
            tsCount = tsCount,
            mapCount = mapCount,
            keyCount = keyCount,
            byterangeCount = byterangeCount,
            decryptedSegments = decryptedSegments
        )
    }

    private fun auditLocalM3u8(info: HXCDownloadInfo, localM3u8: File, artifacts: M3u8BuildArtifacts) {
        runCatching {
            val lines = localM3u8.readLines(Charsets.UTF_8)
            val mediaUris = lines.filter { it.isNotBlank() && !it.trimStart().startsWith("#") }
            val firstUri = mediaUris.firstOrNull().orEmpty()
            val lastUri = mediaUris.lastOrNull().orEmpty()
            val keyLine = lines.firstOrNull { it.startsWith("#EXT-X-KEY") }.orEmpty()
            val mapLine = lines.firstOrNull { it.startsWith("#EXT-X-MAP") }.orEmpty()
            val firstFile = parseLocalFileFromM3u8Uri(localM3u8.parentFile, firstUri)
            val lastFile = parseLocalFileFromM3u8Uri(localM3u8.parentFile, lastUri)
            d(
                "m3u8 audit summary, key=${info.downloadKey}, encrypted=${info.isEncrypted}, " +
                    "resources=${artifacts.resources.size}, ts=${artifacts.tsCount}, map=${artifacts.mapCount}, " +
                    "key=${artifacts.keyCount}, byterange=${artifacts.byterangeCount}, " +
                    "decryptedTs=${artifacts.decryptedSegments}, mediaUris=${mediaUris.size}"
            )
            d(
                "m3u8 audit lines, key=${info.downloadKey}, total=${lines.size}, " +
                    "first='${lines.firstOrNull().orEmpty()}', last='${lines.lastOrNull().orEmpty()}'"
            )
            d(
                "m3u8 audit uri, key=${info.downloadKey}, firstUri=$firstUri, lastUri=$lastUri, " +
                    "firstExists=${firstFile?.exists() == true}, lastExists=${lastFile?.exists() == true}, " +
                    "firstSize=${firstFile?.length() ?: -1}, lastSize=${lastFile?.length() ?: -1}"
            )
            if (keyLine.isNotBlank() || mapLine.isNotBlank()) {
                d(
                    "m3u8 audit tags, key=${info.downloadKey}, keyTag=$keyLine, mapTag=$mapLine"
                )
            }
            val keyResource = artifacts.resources.firstOrNull { it.target.name.endsWith(".key") }
            if (keyResource != null && keyResource.target.exists()) {
                d(
                    "m3u8 audit keyfile, key=${info.downloadKey}, path=${keyResource.target.absolutePath}, " +
                        "size=${keyResource.target.length()}, md5=${md5Hex(keyResource.target)}"
                )
            }
        }.onFailure {
            d("m3u8 audit failed, key=${info.downloadKey}, reason=${it.message}")
        }
    }

    private fun parseLocalFileFromM3u8Uri(baseDir: File?, uri: String): File? {
        if (uri.isBlank()) return null
        val resolved = when {
            uri.startsWith("file://") -> File(uri.removePrefix("file://"))
            uri.startsWith("/") -> File(uri)
            else -> {
                val dir = baseDir ?: return null
                File(dir, uri)
            }
        }
        return resolved
    }

    private fun md5Hex(file: File): String {
        return runCatching {
            val digest = MessageDigest.getInstance("MD5")
            file.inputStream().use { input ->
                val buffer = ByteArray(8 * 1024)
                while (true) {
                    val read = input.read(buffer)
                    if (read <= 0) break
                    digest.update(buffer, 0, read)
                }
            }
            digest.digest().joinToString("") { "%02x".format(it) }
        }.getOrDefault("")
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

    private fun downloadTaskResource(task: M3u8ResourceTask, headers: String) {
        if (!task.hasRange()) {
            downloadSimpleFile(task.url, task.target, headers)
            return
        }
        val rangeHeader = "bytes=${task.rangeStart}-${task.rangeEnd}"
        val conn = (URL(task.url).openConnection() as HttpURLConnection).apply {
            requestMethod = "GET"
            connectTimeout = config.connectTimeoutMs
            readTimeout = config.readTimeoutMs
            setRequestProperty("Range", rangeHeader)
            applyHeaders(this, headers)
        }
        conn.connect()
        val code = conn.responseCode
        if (code in 200..299 || code == 206) {
            task.target.outputStream().use { out ->
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
            return
        }
        conn.disconnect()
        if (task.target.exists()) {
            task.target.delete()
        }
        if (task.rawUrl != task.url) {
            downloadSimpleFile(task.rawUrl, task.target, headers)
            return
        }
        throw IllegalStateException("download ts failed: HTTP $code, range=$rangeHeader")
    }

    private fun isResourceComplete(task: M3u8ResourceTask): Boolean {
        if (!task.target.exists() || task.target.length() <= 0L) return false
        val expected = task.expectedLength()
        return expected <= 0L || task.target.length() == expected
    }

    private fun extractUriAttr(line: String): String? {
        val start = line.indexOf("URI=\"")
        if (start < 0) return null
        val from = start + 5
        val end = line.indexOf('"', from)
        return if (end > from) line.substring(from, end) else null
    }

    private fun replaceUriAttr(line: String, newUri: String): String {
        val start = line.indexOf("URI=\"")
        if (start < 0) return line
        val from = start + 5
        val end = line.indexOf('"', from)
        if (end <= from) return line
        return line.substring(0, from) + newUri + line.substring(end)
    }

    private fun extractAttrValue(line: String, attrName: String): String {
        val regex = Regex("""$attrName=("[^"]*"|[^,]*)""")
        val match = regex.find(line) ?: return ""
        val value = match.groupValues.getOrNull(1).orEmpty().trim()
        return value.removePrefix("\"").removeSuffix("\"").trim()
    }

    private fun decryptAes128SegmentInPlace(task: M3u8ResourceTask): Boolean {
        val keyFile = task.decryptKeyFile ?: return false
        if (!keyFile.exists()) {
            throw IllegalStateException("aes key file missing: ${keyFile.absolutePath}")
        }
        val keyBytes = keyFile.readBytes()
        if (keyBytes.size != 16) {
            throw IllegalStateException("invalid aes-128 key length=${keyBytes.size}")
        }
        val encryptedBytes = task.target.readBytes()
        if (looksLikeClearTs(encryptedBytes)) {
            d("aes128 decrypt skipped(clear_ts), file=${task.target.name}, seq=${task.segmentSequence}")
            return false
        }
        if (encryptedBytes.size % 16 != 0) {
            d("aes128 decrypt skipped(non_block_aligned), file=${task.target.name}, size=${encryptedBytes.size}")
            return false
        }
        val ivBytes = resolveSegmentIv(task.decryptIvHex, task.segmentSequence)
        val clearBytes = runCatching {
            val cipher = Cipher.getInstance("AES/CBC/PKCS5Padding")
            cipher.init(Cipher.DECRYPT_MODE, SecretKeySpec(keyBytes, "AES"), IvParameterSpec(ivBytes))
            cipher.doFinal(encryptedBytes)
        }.getOrElse { firstError ->
            runCatching {
                val fallback = Cipher.getInstance("AES/CBC/NoPadding")
                fallback.init(Cipher.DECRYPT_MODE, SecretKeySpec(keyBytes, "AES"), IvParameterSpec(ivBytes))
                fallback.doFinal(encryptedBytes)
            }.getOrElse {
                d("aes128 decrypt failed, file=${task.target.name}, reason=${firstError.message}")
                return false
            }
        }
        if (!looksLikeClearTs(clearBytes)) {
            d("aes128 decrypt result invalid_ts, file=${task.target.name}, seq=${task.segmentSequence}")
            return false
        }
        task.target.writeBytes(clearBytes)
        return true
    }

    private fun resolveSegmentIv(ivHex: String?, sequence: Long): ByteArray {
        if (!ivHex.isNullOrBlank()) {
            val normalized = ivHex.removePrefix("0x").removePrefix("0X")
            val iv = ByteArray(16)
            val raw = hexToBytes(normalized)
            val copyStart = (16 - raw.size).coerceAtLeast(0)
            val from = (raw.size - 16).coerceAtLeast(0)
            val copyLen = minOf(16, raw.size)
            System.arraycopy(raw, from, iv, copyStart, copyLen)
            return iv
        }
        val iv = ByteArray(16)
        for (i in 0 until 8) {
            iv[15 - i] = ((sequence ushr (8 * i)) and 0xFF).toByte()
        }
        return iv
    }

    private fun hexToBytes(hex: String): ByteArray {
        if (hex.isBlank()) return ByteArray(0)
        val normalized = if (hex.length % 2 == 0) hex else "0$hex"
        val out = ByteArray(normalized.length / 2)
        var i = 0
        while (i < normalized.length) {
            val byteVal = normalized.substring(i, i + 2).toInt(16)
            out[i / 2] = byteVal.toByte()
            i += 2
        }
        return out
    }

    private fun looksLikeClearTs(bytes: ByteArray): Boolean {
        if (bytes.size < 188 * 2) return false
        if (bytes[0] != 0x47.toByte()) return false
        val second = 188
        val third = 376
        return second < bytes.size && bytes[second] == 0x47.toByte()
            && third < bytes.size && bytes[third] == 0x47.toByte()
    }

    private fun parseByteRange(line: String, prevEnd: Long): Pair<Long, Long> {
        return try {
            val value = line.substringAfter(':').trim()
            val at = value.indexOf('@')
            val length: Long
            val offset: Long
            if (at >= 0) {
                length = value.substring(0, at).trim().toLong()
                offset = value.substring(at + 1).trim().toLong()
            } else {
                length = value.toLong()
                offset = if (prevEnd >= 0L) prevEnd else 0L
            }
            length to offset
        } catch (_: Throwable) {
            -1L to 0L
        }
    }

    private fun parseStartEndQuery(line: String): Pair<Long, Long> {
        return try {
            val q = line.indexOf('?')
            if (q < 0) return -1L to -1L
            var start = -1L
            var end = -1L
            val query = line.substring(q + 1)
            query.split("&").forEach { p ->
                when {
                    p.startsWith("start=") -> start = p.substringAfter("start=").toLongOrNull() ?: -1L
                    p.startsWith("end=") -> end = p.substringAfter("end=").toLongOrNull() ?: -1L
                }
            }
            start to end
        } catch (_: Throwable) {
            -1L to -1L
        }
    }

    private fun stripQuery(url: String): String {
        val q = url.indexOf('?')
        return if (q > 0) url.substring(0, q) else url
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
            isEncrypted = req.encrypted || (req.secureCredential != null)
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
        Log.d(TAG, msg)
    }
}
