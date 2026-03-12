package com.hxcplayer

import android.content.Context
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit

/**
 * HXC Player 控制类
 * 类似 iOS 的 HXCPlayerControl，内部管理视频渲染视图
 * 
 * 用法:
 * ```
 * val player = HXCPlayerControl(context)
 * 
 * // 获取视频视图并添加到布局
 * val videoView = player.videoView
 * containerLayout.addView(videoView)
 * 
 * // 播放视频
 * player.openURL("http://example.com/video.mp4")
 * player.play()
 * ```
 */
class HXCPlayerControl(private val context: Context) {
    
    companion object {
        init {
            System.loadLibrary("hxcplayer")
        }
        
        // ========== 日志配置（静态方法） ==========
        
        /**
         * 启用文件日志
         * @param logDir 日志文件目录
         * @param prefix 日志文件前缀（默认 "hxcplayer"）
         */
        @JvmStatic
        external fun enableFileLogging(logDir: String, prefix: String = "hxcplayer")
        
        /**
         * 禁用文件日志
         */
        @JvmStatic
        external fun disableFileLogging()
        
        /**
         * 设置日志级别
         * @param level 0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR
         */
        @JvmStatic
        external fun setLogLevel(level: Int)
        
        /**
         * 设置日志保留天数
         * @param days 保留天数（默认 7 天）
         */
        @JvmStatic
        external fun setLogRetentionDays(days: Int)
        
        /**
         * 获取当前日志文件路径
         */
        @JvmStatic
        external fun getCurrentLogFile(): String
    }
    
    // 播放器状态
    enum class PlayerState {
        IDLE,
        OPENING,
        PLAYING,
        PAUSED,
        STOPPED,
        ERROR
    }
    
    // 错误码（对应 C 层的 PlayerErrorCodeC）
    object PlayerErrorCode {
        const val NONE = 0
        const val INVALID_URL = 1
        const val OPEN_INPUT_FAILED = 2
        const val FIND_STREAM_INFO_FAILED = 3
        const val NO_VIDEO_STREAM = 4
        const val NO_AUDIO_STREAM = 5
        const val CODEC_NOT_FOUND = 6
        const val CODEC_OPEN_FAILED = 7
        const val ALLOC_CONTEXT_FAILED = 8
        const val SDL_INIT_FAILED = 9
        const val AUDIO_DEVICE_OPEN_FAILED = 10
        const val SEEK_FAILED = 11
        const val READ_FRAME_FAILED = 12
        const val DECODE_FAILED = 13
        const val OUT_OF_MEMORY = 14
        const val UNKNOWN = 999
        
        // FFmpeg 错误码范围 (负数)
        // 使用 FFmpeg 原始错误码
    }
    
    // 回调接口
    interface PlayerCallback {
        fun onPlayerStateChanged(state: PlayerState)
        fun onPlayerPositionUpdated(position: Double, duration: Double)
        fun onPlayerError(errorCode: Int, errorMessage: String)  // 添加 errorCode 参数
    }
    
    private var nativeHandle: Long = 0
    private var callback: PlayerCallback? = null
    private var updateExecutor: ScheduledExecutorService? = null
    
    // 视频视图（只读属性，由播放器管理）
    val videoView: SurfaceView = SurfaceView(context).apply {
        holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                setSurface(holder.surface)
            }
            
            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                updateSurfaceSize(width, height)
            }
            
            override fun surfaceDestroyed(holder: SurfaceHolder) {
                setSurface(null)
            }
        })
    }
    
    init {
        nativeHandle = nativeCreate()
        startPositionUpdates()
    }
    
    // 设置回调
    fun setCallback(callback: PlayerCallback) {
        this.callback = callback
    }
    
    // 内部方法：设置渲染 Surface
    private fun setSurface(surface: Surface?) {
        nativeSetSurface(nativeHandle, surface)
    }
    
    // 内部方法：更新 Surface 尺寸
    private fun updateSurfaceSize(width: Int, height: Int) {
        nativeUpdateSurfaceSize(nativeHandle, width, height)
    }
    
    // 打开 URL
    fun openURL(url: String): Boolean {
        return openURL(url, 0.0)  // 默认从头开始
    }
    
    // 打开 URL 并指定起始位置（秒）
    fun openURL(url: String, startPosition: Double): Boolean {
        val result = if (startPosition > 0.0) {
            nativeOpenURLWithStartPosition(nativeHandle, url, startPosition)
        } else {
            nativeOpenURL(nativeHandle, url)
        }
        
        if (result) {
            callback?.onPlayerStateChanged(PlayerState.OPENING)
        } else {
            callback?.onPlayerError(PlayerErrorCode.OPEN_INPUT_FAILED, "无法打开 URL: $url")
        }
        return result
    }
    
    // 播放
    fun play() {
        nativePlay(nativeHandle)
        callback?.onPlayerStateChanged(PlayerState.PLAYING)
    }
    
    // 暂停
    fun pause() {
        nativePause(nativeHandle)
        callback?.onPlayerStateChanged(PlayerState.PAUSED)
    }
    
    // 停止
    fun stop() {
        nativeStop(nativeHandle)
        callback?.onPlayerStateChanged(PlayerState.STOPPED)
    }
    
    // 跳转
    fun seekTo(position: Double) {
        nativeSeekTo(nativeHandle, position)
    }
    
    // 设置播放速度
    fun setPlaybackRate(rate: Float) {
        nativeSetPlaybackRate(nativeHandle, rate)
    }
    
    // 设置音量
    fun setVolume(volume: Float) {
        nativeSetVolume(nativeHandle, volume)
    }
    
    // 设置比例模式
    fun setAspectRatioMode(fill: Boolean) {
        nativeSetAspectRatioMode(nativeHandle, if (fill) 1 else 0)
    }
    
    // 获取时长
    fun getDuration(): Double {
        return nativeGetDuration(nativeHandle)
    }
    
    // 获取当前位置
    fun getPosition(): Double {
        return nativeGetPosition(nativeHandle)
    }
    
    // 获取状态
    fun getState(): PlayerState {
        val state = nativeGetState(nativeHandle)
        return PlayerState.values().getOrElse(state) { PlayerState.IDLE }
    }
    
    // 启动位置更新定时器
    private fun startPositionUpdates() {
        updateExecutor = Executors.newSingleThreadScheduledExecutor()
        updateExecutor?.scheduleAtFixedRate({
            try {
                val position = getPosition()
                val duration = getDuration()
                if (duration > 0) {
                    callback?.onPlayerPositionUpdated(position, duration)
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
        }, 0, 100, TimeUnit.MILLISECONDS)
    }
    
    // 释放资源
    fun release() {
        updateExecutor?.shutdown()
        nativeRelease(nativeHandle)
        nativeHandle = 0
    }
    
    // Native 方法声明
    private external fun nativeCreate(): Long
    private external fun nativeRelease(handle: Long)
    private external fun nativeSetSurface(handle: Long, surface: Surface?)
    private external fun nativeUpdateSurfaceSize(handle: Long, width: Int, height: Int)
    private external fun nativeOpenURL(handle: Long, url: String): Boolean
    private external fun nativeOpenURLWithStartPosition(handle: Long, url: String, startPosition: Double): Boolean
    private external fun nativePlay(handle: Long)
    private external fun nativePause(handle: Long)
    private external fun nativeStop(handle: Long)
    private external fun nativeSeekTo(handle: Long, position: Double)
    private external fun nativeSetPlaybackRate(handle: Long, rate: Float)
    private external fun nativeSetVolume(handle: Long, volume: Float)
    private external fun nativeSetAspectRatioMode(handle: Long, mode: Int)
    private external fun nativeGetDuration(handle: Long): Double
    private external fun nativeGetPosition(handle: Long): Double
    private external fun nativeGetState(handle: Long): Int
}
