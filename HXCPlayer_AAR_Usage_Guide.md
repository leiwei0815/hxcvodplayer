# HXCPlayer Android AAR 使用文档

## 📦 简介

HXCPlayer 是一个基于 FFmpeg 和 SoundTouch 的高性能 Android 视频播放器库，支持多种视频格式、倍速播放、HTTP/HTTPS 协议以及 302 重定向。

### 特性

- ✅ 支持多种视频格式（MP4、FLV、HLS 等）
- ✅ 支持 HTTP/HTTPS 网络流
- ✅ 支持 302 重定向
- ✅ 支持倍速播放（0.5x ~ 2.0x）
- ✅ 支持音量调节
- ✅ 支持播放进度控制
- ✅ 支持 FIT/FILL 两种宽高比模式
- ✅ 低内存占用（优化后稳定在 100-200MB）
- ✅ 多架构支持（arm64-v8a、armeabi-v7a、x86_64）

---

## 📥 集成到项目

### 1. 添加 AAR 文件

将 `hxcplayer-release.aar` 复制到你的项目的 `app/libs/` 目录下。

### 2. 修改 build.gradle

在 `app/build.gradle` 中添加依赖：

```gradle
android {
    ...
    
    defaultConfig {
        ...
        minSdk 24  // 最低支持 Android 7.0
    }
}

dependencies {
    // HXCPlayer 库
    implementation files('libs/hxcplayer-release.aar')
    
    // 必需的依赖
    implementation 'androidx.core:core-ktx:1.12.0'
    implementation 'androidx.appcompat:appcompat:1.6.1'
}
```

### 3. 添加权限

在 `AndroidManifest.xml` 中添加网络权限：

```xml
<manifest xmlns:android="http://schemas.android.com/apk/res/android">
    
    <!-- 网络权限 -->
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
    
    <application>
        ...
    </application>
</manifest>
```

---

## 🚀 基础使用

### 1. 初始化播放器

```kotlin
import com.hxcplayer.HXCPlayerControl

class MainActivity : AppCompatActivity(), HXCPlayerControl.PlayerCallback {
    
    private lateinit var player: HXCPlayerControl
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        
        // 创建播放器实例
        player = HXCPlayerControl(this)
        
        // 设置回调监听
        player.setCallback(this)
        
        // 获取视频视图并添加到布局
        val videoView = player.videoView
        videoContainer.addView(videoView)
    }
    
    override fun onDestroy() {
        super.onDestroy()
        // 释放播放器资源
        player.release()
    }
}
```

### 2. 在 XML 布局中添加视频容器

```xml
<FrameLayout
    android:id="@+id/videoContainer"
    android:layout_width="match_parent"
    android:layout_height="300dp"
    android:background="#000000" />
```

### 3. 实现播放器回调

```kotlin
// 播放状态变化回调
override fun onPlayerStateChanged(state: HXCPlayerControl.PlayerState) {
    when (state) {
        HXCPlayerControl.PlayerState.IDLE -> {
            // 空闲状态
            statusText.text = "空闲"
        }
        HXCPlayerControl.PlayerState.OPENING -> {
            // 正在打开视频
            statusText.text = "正在加载..."
        }
        HXCPlayerControl.PlayerState.PLAYING -> {
            // 播放中
            statusText.text = "播放中"
            playButton.text = "暂停"
        }
        HXCPlayerControl.PlayerState.PAUSED -> {
            // 已暂停
            statusText.text = "已暂停"
            playButton.text = "播放"
        }
        HXCPlayerControl.PlayerState.STOPPED -> {
            // 已停止
            statusText.text = "已停止"
        }
        HXCPlayerControl.PlayerState.ERROR -> {
            // 错误状态
            statusText.text = "播放错误"
        }
    }
}

// 播放进度更新回调（每 100ms 回调一次）
override fun onPlayerPositionUpdated(position: Double, duration: Double) {
    // position: 当前播放位置（秒）
    // duration: 视频总时长（秒）
    
    val progress = ((position / duration) * 100).toInt()
    progressBar.progress = progress
    
    val posMin = (position / 60).toInt()
    val posSec = (position % 60).toInt()
    val durMin = (duration / 60).toInt()
    val durSec = (duration % 60).toInt()
    
    timeText.text = String.format("%02d:%02d / %02d:%02d", 
        posMin, posSec, durMin, durSec)
}

// 错误回调
override fun onPlayerError(error: String) {
    Toast.makeText(this, "播放错误: $error", Toast.LENGTH_LONG).show()
}
```

---

## 📺 播放控制

### 打开视频

```kotlin
// 打开网络视频
val url = "https://example.com/video.mp4"
player.openURL(url)

// 从指定位置开始播放（秒）
player.openURL(url, startPosition = 30.0)  // 从第 30 秒开始
```

### 播放/暂停

```kotlin
// 播放
player.play()

// 暂停
player.pause()

// 停止
player.stop()
```

### 进度控制

```kotlin
// 跳转到指定位置（秒）
player.seekTo(60.0)  // 跳转到第 60 秒

// 获取当前播放位置
val position = player.getPosition()  // 返回 Double（秒）

// 获取视频总时长
val duration = player.getDuration()  // 返回 Double（秒）

// 示例：拖动进度条
progressBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
    override fun onStopTrackingTouch(seekBar: SeekBar?) {
        val progress = seekBar?.progress ?: 0
        val position = (progress / 100.0) * player.getDuration()
        player.seekTo(position)
    }
    
    override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {}
    override fun onStartTrackingTouch(seekBar: SeekBar?) {}
})
```

---

## ⚙️ 高级功能

### 倍速播放

```kotlin
// 设置播放速度（0.5x ~ 2.0x）
player.setPlaybackRate(1.5f)  // 1.5 倍速

// 常用速度选项
val speedOptions = floatArrayOf(0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f)

// 切换速度示例
speedButton.setOnClickListener {
    currentSpeedIndex = (currentSpeedIndex + 1) % speedOptions.size
    val speed = speedOptions[currentSpeedIndex]
    player.setPlaybackRate(speed)
    speedButton.text = String.format("%.2fx", speed)
}
```

### 音量控制

```kotlin
// 设置音量（0.0 ~ 1.0）
player.setVolume(0.5f)  // 50% 音量

// 音量条示例
volumeBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
    override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
        if (fromUser) {
            val volume = progress / 100.0f
            player.setVolume(volume)
        }
    }
    
    override fun onStartTrackingTouch(seekBar: SeekBar?) {}
    override fun onStopTrackingTouch(seekBar: SeekBar?) {}
})
```

### 宽高比模式

```kotlin
// FIT 模式：保持视频宽高比，适配到视图内（可能有黑边）
player.setAspectRatioMode(false)

// FILL 模式：拉伸视频填满整个视图（可能变形）
player.setAspectRatioMode(true)

// 切换模式示例
var isFillMode = false
aspectRatioButton.setOnClickListener {
    isFillMode = !isFillMode
    player.setAspectRatioMode(isFillMode)
    aspectRatioButton.text = if (isFillMode) "FILL" else "FIT"
}
```

### 播放状态查询

```kotlin
// 获取当前播放状态
val state = player.getState()

when (state) {
    HXCPlayerControl.PlayerState.IDLE -> {
        // 空闲
    }
    HXCPlayerControl.PlayerState.PLAYING -> {
        // 播放中
    }
    HXCPlayerControl.PlayerState.PAUSED -> {
        // 暂停
    }
    else -> {
        // 其他状态
    }
}
```

---

## 🎨 完整示例

### 简单播放器示例

```kotlin
class SimplePlayerActivity : AppCompatActivity(), HXCPlayerControl.PlayerCallback {
    
    private lateinit var binding: ActivitySimplePlayerBinding
    private lateinit var player: HXCPlayerControl
    private var isPlaying = false
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivitySimplePlayerBinding.inflate(layoutInflater)
        setContentView(binding.root)
        
        // 初始化播放器
        initPlayer()
        
        // 设置控制按钮
        setupControls()
        
        // 自动播放测试视频
        player.openURL("https://example.com/test.mp4")
    }
    
    private fun initPlayer() {
        player = HXCPlayerControl(this)
        player.setCallback(this)
        
        // 添加视频视图
        binding.videoContainer.addView(player.videoView)
    }
    
    private fun setupControls() {
        // 播放/暂停按钮
        binding.playPauseButton.setOnClickListener {
            if (isPlaying) {
                player.pause()
            } else {
                player.play()
            }
        }
        
        // 停止按钮
        binding.stopButton.setOnClickListener {
            player.stop()
        }
        
        // 进度条
        binding.progressBar.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onStopTrackingTouch(seekBar: SeekBar?) {
                val progress = seekBar?.progress ?: 0
                val position = (progress / 100.0) * player.getDuration()
                player.seekTo(position)
            }
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {}
            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
        })
    }
    
    // 回调实现
    override fun onPlayerStateChanged(state: HXCPlayerControl.PlayerState) {
        runOnUiThread {
            isPlaying = (state == HXCPlayerControl.PlayerState.PLAYING)
            binding.playPauseButton.text = if (isPlaying) "暂停" else "播放"
        }
    }
    
    override fun onPlayerPositionUpdated(position: Double, duration: Double) {
        runOnUiThread {
            if (duration > 0) {
                val progress = ((position / duration) * 100).toInt()
                binding.progressBar.progress = progress
            }
        }
    }
    
    override fun onPlayerError(error: String) {
        runOnUiThread {
            Toast.makeText(this, "错误: $error", Toast.LENGTH_LONG).show()
        }
    }
    
    override fun onDestroy() {
        super.onDestroy()
        player.release()
    }
}
```

### 对应的 XML 布局

```xml
<?xml version="1.0" encoding="utf-8"?>
<LinearLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="match_parent"
    android:layout_height="match_parent"
    android:orientation="vertical">
    
    <!-- 视频容器 -->
    <FrameLayout
        android:id="@+id/videoContainer"
        android:layout_width="match_parent"
        android:layout_height="0dp"
        android:layout_weight="1"
        android:background="#000000" />
    
    <!-- 进度条 -->
    <SeekBar
        android:id="@+id/progressBar"
        android:layout_width="match_parent"
        android:layout_height="wrap_content"
        android:max="100" />
    
    <!-- 控制按钮 -->
    <LinearLayout
        android:layout_width="match_parent"
        android:layout_height="wrap_content"
        android:orientation="horizontal"
        android:padding="16dp">
        
        <Button
            android:id="@+id/playPauseButton"
            android:layout_width="0dp"
            android:layout_height="wrap_content"
            android:layout_weight="1"
            android:text="播放" />
        
        <Button
            android:id="@+id/stopButton"
            android:layout_width="0dp"
            android:layout_height="wrap_content"
            android:layout_weight="1"
            android:text="停止" />
    </LinearLayout>
</LinearLayout>
```

---

## 🛠️ 常见问题

### 1. 如何保持屏幕常亮？

在 Activity 的 `onCreate` 中添加：

```kotlin
window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
```

### 2. 如何隐藏导航栏（全屏播放）？

```kotlin
window.decorView.systemUiVisibility = (
    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
    or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
    or View.SYSTEM_UI_FLAG_FULLSCREEN
    or View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
)
```

### 3. 如何监控内存使用？

```kotlin
// 获取 Native Heap 使用情况
val nativeHeap = Debug.getNativeHeapAllocatedSize() / (1024 * 1024)
Log.i("Memory", "Native Heap: ${nativeHeap}MB")

// 获取进程总内存
val runtime = Runtime.getRuntime()
val totalMemory = runtime.totalMemory() / (1024 * 1024)
val freeMemory = runtime.freeMemory() / (1024 * 1024)
Log.i("Memory", "Total: ${totalMemory}MB, Free: ${freeMemory}MB")
```

### 4. 如何处理网络错误？

```kotlin
override fun onPlayerError(error: String) {
    when {
        error.contains("Protocol not found") -> {
            // 协议不支持
            Toast.makeText(this, "不支持的协议", Toast.LENGTH_SHORT).show()
        }
        error.contains("Connection") -> {
            // 网络连接错误
            Toast.makeText(this, "网络连接失败", Toast.LENGTH_SHORT).show()
        }
        else -> {
            Toast.makeText(this, "播放错误: $error", Toast.LENGTH_SHORT).show()
        }
    }
}
```

### 5. 如何优化内存使用？

- 及时调用 `player.release()` 释放资源
- 不播放时调用 `player.stop()` 释放解码器
- 避免频繁创建/销毁播放器实例
- 使用单例模式管理播放器（如果需要全局使用）

---

## 📊 性能指标

### 内存占用（优化后）

- **Java Heap**: 40-60 MB
- **Native Heap**: 100-200 MB（稳定）
- **总内存**: 150-250 MB

### 支持的视频规格

- **分辨率**: 最高支持 1920x1080 (1080p)
- **帧率**: 最高支持 60 FPS
- **码率**: 无限制（取决于网络带宽）

---

## 🔧 故障排查

### 播放器无法加载

**症状**: 调用 `openURL()` 后无响应

**解决方案**:
1. 检查网络权限是否添加
2. 检查 URL 是否正确
3. 检查网络连接是否正常
4. 查看 Logcat 日志（过滤 `HXCPlayer` 标签）

### 黑屏或无画面

**症状**: 有声音但无画面

**解决方案**:
1. 确保 `videoView` 已添加到布局
2. 检查视频容器的尺寸是否正确
3. 尝试切换宽高比模式

### 内存持续增长

**症状**: 播放一段时间后应用被系统杀死

**解决方案**:
1. 确保已更新到最新版本的 AAR（已修复内存泄漏）
2. 检查是否正确调用 `release()` 释放资源
3. 避免频繁 seek 操作

---

## 📝 版本信息

- **当前版本**: 1.0.0
- **最低 Android 版本**: Android 7.0 (API 24)
- **目标 Android 版本**: Android 10 (API 29)
- **支持架构**: arm64-v8a, armeabi-v7a, x86_64

---

## 📞 技术支持

如有问题或建议，请查看项目源码或联系开发团队。

### 相关资源

- **FFmpeg 文档**: https://ffmpeg.org/documentation.html
- **SoundTouch 文档**: https://www.surina.net/soundtouch/

---

## 📄 许可证

本库使用的开源组件：
- **FFmpeg**: LGPL v2.1+
- **SoundTouch**: LGPL v2.1
- **mbedTLS**: Apache License 2.0

使用本库时请遵守相关开源协议。
