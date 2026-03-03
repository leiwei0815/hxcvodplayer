package com.hxcplayer.test

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputType
import android.widget.EditText
import android.widget.SeekBar
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.hxcplayer.test.databinding.ActivityMainBinding
import com.hxcplayer.HXCPlayerControl
import android.app.ActivityManager
import android.os.Debug

class MainActivity : AppCompatActivity(), HXCPlayerControl.PlayerCallback {
    
    private lateinit var binding: ActivityMainBinding
    private lateinit var player: HXCPlayerControl
    private var isPlaying = false
    private var isSeeking = false
    
    // 权限请求码
    private val REQUEST_AUDIO_PERMISSION = 100
    
    // 速度选项
    private val speedOptions = floatArrayOf(0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f)
    private var currentSpeedIndex = 2 // 默认 1.0x
    
    // 比例模式
    private var currentAspectRatio = AspectRatioMode.FIT
    
    // 📊 内存监控
    private val memoryMonitorHandler = Handler(Looper.getMainLooper())
    private var memoryMonitorRunnable: Runnable? = null
    private var memoryMonitorEnabled = false
    
    enum class AspectRatioMode {
        FIT, FILL
    }
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // 🖥️ 隐藏导航栏，避免尺寸频繁变化导致渲染性能问题
        window.decorView.systemUiVisibility = (
            android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            or android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            or android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            or android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            or android.view.View.SYSTEM_UI_FLAG_FULLSCREEN
            or android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
        )
        
        // 🔋 保持屏幕常亮（播放视频时不熄屏）
        window.addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)
        
        // 🔐 请求音频权限
        checkAndRequestPermissions()
        
        // 初始化播放器
        player = HXCPlayerControl(this)
        player.setCallback(this)
        
        // ⚠️ 将播放器的 videoView 添加到容器中
        // 移除 XML 中的 surfaceView，改为动态添加播放器的 videoView
        val videoContainer = binding.surfaceView.parent as? android.view.ViewGroup
        if (videoContainer != null) {
            val surfaceViewIndex = videoContainer.indexOfChild(binding.surfaceView)
            val surfaceViewLayoutParams = binding.surfaceView.layoutParams
            
            // 移除 XML 中定义的 surfaceView
            videoContainer.removeView(binding.surfaceView)
            
            // 添加播放器的 videoView
            player.videoView.apply {
                id = android.view.View.generateViewId()  // 生成新 ID
                layoutParams = surfaceViewLayoutParams
            }
            videoContainer.addView(player.videoView, surfaceViewIndex)
        } else {
            android.util.Log.e("MainActivity", "❌ videoContainer is null!")
        }
        
        setupControls()
        
        // 📊 启动内存监控
//        startMemoryMonitor()

        // 🧪 默认测试URL（延迟播放，等待 SurfaceView 准备好）
        player.videoView.post {
            // 注释掉自动播放，避免 SurfaceView 未准备好导致崩溃
            // player.openURL("https://111453136245362688.tenwiseacademy.cn/6e05f006034f11e0772fd44df4beb686/4632d236ac2612c4729de505aa4fdab9.mp4", 67.0)
        }
    }

    
    private fun setupControls() {
        // 打开按钮
        binding.openButton.setOnClickListener {
            showOpenUrlDialog()
        }
        
        // 播放/暂停按钮
        binding.playPauseButton.setOnClickListener {
            if (isPlaying) {
                player.pause()
                binding.playPauseButton.text = getString(R.string.play)
                isPlaying = false
            } else {
                player.play()
                binding.playPauseButton.text = getString(R.string.pause)
                isPlaying = true
            }
        }
        
        // 停止按钮
        binding.stopButton.setOnClickListener {
            player.stop()
            binding.playPauseButton.text = getString(R.string.play)
            isPlaying = false
        }
        
        // 速度按钮
        binding.speedButton.setOnClickListener {
            currentSpeedIndex = (currentSpeedIndex + 1) % speedOptions.size
            val speed = speedOptions[currentSpeedIndex]
            player.setPlaybackRate(speed)
            binding.speedButton.text = String.format("%.2fx", speed)
        }
        
        // 比例按钮
        binding.aspectRatioButton.setOnClickListener {
            currentAspectRatio = when (currentAspectRatio) {
                AspectRatioMode.FIT -> AspectRatioMode.FILL
                AspectRatioMode.FILL -> AspectRatioMode.FIT
            }
            
            binding.aspectRatioButton.text = when (currentAspectRatio) {
                AspectRatioMode.FIT -> getString(R.string.aspect_fit)
                AspectRatioMode.FILL -> getString(R.string.aspect_fill)
            }
            
            player.setAspectRatioMode(currentAspectRatio == AspectRatioMode.FILL)
        }
        
        // 进度条
        binding.progressBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser && !isSeeking) {
                    isSeeking = true
                }
            }
            
            override fun onStartTrackingTouch(seekBar: SeekBar?) {
                isSeeking = true
            }
            
            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                val position = (seekBar?.progress ?: 0) / 100.0 * player.getDuration()
                player.seekTo(position)
                isSeeking = false
            }
        })
        
        // 音量条
        binding.volumeBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                if (fromUser) {
                    player.setVolume(progress / 100.0f)
                }
            }
            
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        })
    }
    
    private fun showOpenUrlDialog() {
        val editText = EditText(this).apply {
            inputType = InputType.TYPE_TEXT_VARIATION_URI
            hint = getString(R.string.video_url_hint)
            setText("https://111453136245362688.tenwiseacademy.cn/6e05f006034f11e0772fd44df4beb686/4632d236ac2612c4729de505aa4fdab9.mp4")
        }
        
        AlertDialog.Builder(this)
            .setTitle(getString(R.string.open_url))
            .setView(editText)
            .setPositiveButton(getString(R.string.ok)) { _, _ ->
                val url = editText.text.toString()
                if (url.isNotEmpty()) {
                    player.openURL(url)
                }
            }
            .setNeutralButton("诊断") { _, _ ->
                val url = editText.text.toString()
                if (url.isNotEmpty()) {
                    diagnoseURL(url)
                }
            }
            .setNegativeButton(getString(R.string.cancel), null)
            .show()
    }
    
    private fun diagnoseURL(url: String) {
        binding.statusText.text = "正在诊断 URL..."
        
        URLDiagnostics.diagnose(url) { result ->
            // 显示诊断结果
            val builder = AlertDialog.Builder(this)
                .setTitle("URL 诊断结果")
                .setMessage(result.getSummary())
                .setPositiveButton("确定", null)
                .setNeutralButton("复制日志") { _, _ ->
                    val clipboard = getSystemService(CLIPBOARD_SERVICE) as android.content.ClipboardManager
                    val clip = android.content.ClipData.newPlainText("诊断结果", result.getSummary())
                    clipboard.setPrimaryClip(clip)
                    android.widget.Toast.makeText(this, "已复制到剪贴板", android.widget.Toast.LENGTH_SHORT).show()
                }
            
            builder.show()
            
            binding.statusText.text = if (result.success) {
                if (url.startsWith("https://", ignoreCase = true)) {
                    "诊断完成：HTTPS URL 可访问（需 SSL 支持）"
                } else {
                    "诊断完成：可访问"
                }
            } else {
                "诊断完成：不可访问"
            }
        }
    }
    
    // PlayerCallback 实现
    override fun onPlayerStateChanged(state: HXCPlayerControl.PlayerState) {
        runOnUiThread {
            binding.statusText.text = when (state) {
                HXCPlayerControl.PlayerState.IDLE -> "空闲"
                HXCPlayerControl.PlayerState.OPENING -> "正在打开..."
                HXCPlayerControl.PlayerState.PLAYING -> "播放中"
                HXCPlayerControl.PlayerState.PAUSED -> "已暂停"
                HXCPlayerControl.PlayerState.STOPPED -> "已停止"
                HXCPlayerControl.PlayerState.ERROR -> "错误"
            }
            
            if (state == HXCPlayerControl.PlayerState.PLAYING) {
                isPlaying = true
                binding.playPauseButton.text = getString(R.string.pause)
            } else if (state == HXCPlayerControl.PlayerState.PAUSED || 
                       state == HXCPlayerControl.PlayerState.STOPPED) {
                isPlaying = false
                binding.playPauseButton.text = getString(R.string.play)
            }
        }
    }
    
    override fun onPlayerPositionUpdated(position: Double, duration: Double) {
        runOnUiThread {
            if (!isSeeking && duration > 0) {
                val progress = ((position / duration) * 100).toInt()
                binding.progressBar.progress = progress
                
                val posMin = (position / 60).toInt()
                val posSec = (position % 60).toInt()
                val durMin = (duration / 60).toInt()
                val durSec = (duration % 60).toInt()
                
                binding.timeText.text = String.format("%02d:%02d / %02d:%02d", 
                    posMin, posSec, durMin, durSec)
            }
        }
    }
    
    override fun onPlayerError(error: String) {
        runOnUiThread {
            binding.statusText.text = "错误: $error"
        }
    }
    
    override fun onDestroy() {
        super.onDestroy()
        stopMemoryMonitor()
        player.release()
    }
    
    // 📊 启动内存监控
    private fun startMemoryMonitor() {
        if (memoryMonitorEnabled) return
        
        memoryMonitorEnabled = true
        android.util.Log.i("MemoryMonitor", "========================================")
        android.util.Log.i("MemoryMonitor", "🔍 内存监控已启动（每5秒输出一次）")
        android.util.Log.i("MemoryMonitor", "========================================")
        
        memoryMonitorRunnable = object : Runnable {
            override fun run() {
                if (memoryMonitorEnabled) {
                    logMemoryUsage()
                    memoryMonitorHandler.postDelayed(this, 5000) // 每5秒输出一次
                }
            }
        }
        memoryMonitorHandler.post(memoryMonitorRunnable!!)
    }
    
    // 📊 停止内存监控
    private fun stopMemoryMonitor() {
        memoryMonitorEnabled = false
        memoryMonitorRunnable?.let {
            memoryMonitorHandler.removeCallbacks(it)
        }
        android.util.Log.i("MemoryMonitor", "========================================")
        android.util.Log.i("MemoryMonitor", "🛑 内存监控已停止")
        android.util.Log.i("MemoryMonitor", "========================================")
    }
    
    // 📊 输出内存使用情况
    private fun logMemoryUsage() {
        val runtime = Runtime.getRuntime()
        val activityManager = getSystemService(ACTIVITY_SERVICE) as ActivityManager
        val memoryInfo = ActivityManager.MemoryInfo()
        activityManager.getMemoryInfo(memoryInfo)
        
        // Java 堆内存
        val totalHeap = runtime.totalMemory() / (1024 * 1024)
        val freeHeap = runtime.freeMemory() / (1024 * 1024)
        val usedHeap = totalHeap - freeHeap
        val maxHeap = runtime.maxMemory() / (1024 * 1024)
        
        // Native 内存（通过 Debug 类获取）
        val nativeHeap = Debug.getNativeHeapAllocatedSize() / (1024 * 1024)
        val nativeHeapFree = Debug.getNativeHeapFreeSize() / (1024 * 1024)
        val nativeHeapSize = Debug.getNativeHeapSize() / (1024 * 1024)
        
        // 系统内存
        val totalMem = memoryInfo.totalMem / (1024 * 1024)
        val availMem = memoryInfo.availMem / (1024 * 1024)
        val usedMem = totalMem - availMem
        val threshold = memoryInfo.threshold / (1024 * 1024)
        val lowMemory = memoryInfo.lowMemory
        
        // PSS (Proportional Set Size) - 进程实际占用的物理内存
        val pid = android.os.Process.myPid()
        val pids = intArrayOf(pid)
        val memInfos = activityManager.getProcessMemoryInfo(pids)
        val pss = memInfos[0].totalPss / 1024 // KB -> MB
        val privateDirty = memInfos[0].totalPrivateDirty / 1024
        val sharedDirty = memInfos[0].totalSharedDirty / 1024
        
        android.util.Log.i("MemoryMonitor", "========================================")
        android.util.Log.i("MemoryMonitor", "📊 内存使用情况 [${System.currentTimeMillis()}]")
        android.util.Log.i("MemoryMonitor", "----------------------------------------")
        android.util.Log.i("MemoryMonitor", "📦 Java Heap:")
        android.util.Log.i("MemoryMonitor", "   使用: ${usedHeap}MB / ${maxHeap}MB (${usedHeap * 100 / maxHeap}%)")
        android.util.Log.i("MemoryMonitor", "   空闲: ${freeHeap}MB")
        android.util.Log.i("MemoryMonitor", "----------------------------------------")
        android.util.Log.i("MemoryMonitor", "🔧 Native Heap:")
        android.util.Log.i("MemoryMonitor", "   已分配: ${nativeHeap}MB")
        android.util.Log.i("MemoryMonitor", "   堆大小: ${nativeHeapSize}MB")
        android.util.Log.i("MemoryMonitor", "   空闲: ${nativeHeapFree}MB")
        android.util.Log.i("MemoryMonitor", "----------------------------------------")
        android.util.Log.i("MemoryMonitor", "💾 进程内存 (PSS):")
        android.util.Log.i("MemoryMonitor", "   总计: ${pss}MB")
        android.util.Log.i("MemoryMonitor", "   私有脏页: ${privateDirty}MB")
        android.util.Log.i("MemoryMonitor", "   共享脏页: ${sharedDirty}MB")
        android.util.Log.i("MemoryMonitor", "----------------------------------------")
        android.util.Log.i("MemoryMonitor", "🖥️  系统内存:")
        android.util.Log.i("MemoryMonitor", "   总计: ${totalMem}MB")
        android.util.Log.i("MemoryMonitor", "   已用: ${usedMem}MB (${usedMem * 100 / totalMem}%)")
        android.util.Log.i("MemoryMonitor", "   可用: ${availMem}MB")
        android.util.Log.i("MemoryMonitor", "   阈值: ${threshold}MB")
        android.util.Log.i("MemoryMonitor", "   低内存: ${if (lowMemory) "⚠️ 是" else "✅ 否"}")
        android.util.Log.i("MemoryMonitor", "========================================")
        
        // 如果接近低内存状态，额外警告
        if (lowMemory || availMem < threshold) {
            android.util.Log.w("MemoryMonitor", "⚠️⚠️⚠️ 警告：系统内存不足！")
        }
        
        // 如果 Native Heap 持续增长，警告可能存在内存泄漏
        if (nativeHeap > 300) {
            android.util.Log.w("MemoryMonitor", "⚠️⚠️⚠️ 警告：Native Heap 占用过高 (${nativeHeap}MB)，可能存在内存泄漏！")
        }
    }
    
    // 🔐 检查并请求权限
    private fun checkAndRequestPermissions() {
        val permissions = arrayOf(
            Manifest.permission.RECORD_AUDIO,
            Manifest.permission.MODIFY_AUDIO_SETTINGS
        )
        
        val permissionsToRequest = permissions.filter {
            ContextCompat.checkSelfPermission(this, it) != PackageManager.PERMISSION_GRANTED
        }
        
        if (permissionsToRequest.isNotEmpty()) {
            ActivityCompat.requestPermissions(
                this,
                permissionsToRequest.toTypedArray(),
                REQUEST_AUDIO_PERMISSION
            )
        }
    }
    
    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        
        if (requestCode == REQUEST_AUDIO_PERMISSION) {
            if (grantResults.all { it == PackageManager.PERMISSION_GRANTED }) {
                Toast.makeText(this, "音频权限已授予", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, "⚠️ 音频权限被拒绝，可能无法播放音频", Toast.LENGTH_LONG).show()
            }
        }
    }
}
