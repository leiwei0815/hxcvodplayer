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

### API 速查（与当前 AAR 一致）

| 类别 | 说明 |
|------|------|
| 打开 | `openURL(url)` / `openURL(url, startPosition)`、`openWithPlayModel(model)`、`playURL(url)`、`playWithModel(model)` |
| 渲染 | 构造可选 `VideoRenderViewType`：`SURFACE_VIEW`（默认）或 `TEXTURE_VIEW`；布局使用 `renderView`（与 `videoView` 同一实例） |
| 控制 | `play()`、`pause()`、`resume()`、`stop()`、`replay()`、`seekTo(position)`、`seekToPosition(position)` |
| 属性 | `autoPlayer`、`startPosition`、`setPlaybackRate`、`setVolume`、`setAspectRatioMode`、`getDuration`、`getPosition`、`getState` |
| 回调 | `PlayerCallback`：`onPlayerStateChanged`、`onPlayerPositionUpdated`、`onPlayerError(errorCode, errorMessage)` |
| License | `checkLicense(licenseKey, licenseUrl)`、`resetLicenseState()`（播放前必须通过校验） |
| 日志 | 伴生对象：`enableFileLogging`、`disableFileLogging`、`setLogLevel`、`getLogLevel`、`setLogRetentionDays`、`getLogDirectory`、`getCurrentLogFile` |
| 错误码 | `HXCPlayerControl.PlayerErrorCode` |

从 **Java** 调用时，`PlayerCallback.onPlayerError` 需实现两个参数 `(int errorCode, String errorMessage)`；伴生对象方法在 Java 中为 `HXCPlayerControl` 的静态方法。

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
        
        // 创建播放器实例（默认 SurfaceView；列表/动画内视频可改用 TextureView）
        // player = HXCPlayerControl(this, HXCPlayerControl.VideoRenderViewType.TEXTURE_VIEW)
        player = HXCPlayerControl(this)
        
        // 设置回调监听
        player.setCallback(this)
        
        // 获取视频视图并添加到布局（renderView 与 videoView 为同一 View）
        val videoView = player.renderView
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

// 错误回调（含错误码，便于区分网络/解码/打开失败等）
override fun onPlayerError(errorCode: Int, errorMessage: String) {
    Toast.makeText(this, "播放错误 [$errorCode]: $errorMessage", Toast.LENGTH_LONG).show()
}
```

错误码常量见 `HXCPlayerControl.PlayerErrorCode`，与核心层 `PlayerErrorCodeC`（`hxc_player_core_c_bridge.h`）及 iOS `HXCPlayerErrorCode` 数值一致；未列出的负数也可能为 FFmpeg `AVERROR_*` 等。

---

## 📺 播放控制

### 打开视频

```kotlin
// 默认模式：FFmpeg 直接打开（网络/本地 URL，支持 HLS、HTTP 重定向等）
val url = "https://example.com/video.mp4"
player.openURL(url)

// 从指定位置开始播放（秒）
player.openURL(url, startPosition = 30.0)  // 从第 30 秒开始

// 与 iOS 对齐：通过属性控制起播行为（不传 startPosition 参数）
player.startPosition = 30.0
player.autoPlayer = true
player.playURL(url)
```

### 使用播放模型打开（推荐）

```kotlin
// 全局配置（一次即可）
val cfg = HXCPlayerControl.PlayerDataSourceConfig.defaultConfig()
cfg.timeoutMs = 30000
cfg.maxRetries = 3
cfg.avioBufferSize = 64 * 1024
HXCPlayerControl.configureDefaultConfig(cfg)

// 自定义 HTTP（Range 下载 + CustomAVIO）
val httpModel = HXCPlayerControl.PlayerDataSourcePlayModel.modelWithURL(
    url = "https://example.com/video.mp4",
    mode = HXCPlayerControl.PlayerDataSourceMode.CUSTOM_HTTP,
    encryptedFile = false
)
player.openWithPlayModel(httpModel)

// 自定义本地文件（LocalFileDataSource + CustomAVIO）
val path = File(context.filesDir, "encrypted.mp4").absolutePath
val fileModel = HXCPlayerControl.PlayerDataSourcePlayModel.modelWithURL(
    url = path,
    mode = HXCPlayerControl.PlayerDataSourceMode.CUSTOM_FILE,
    encryptedFile = false
)
player.openWithPlayModel(fileModel)

// 与 iOS 对齐：按 autoPlayer 策略打开并自动 play/pause
player.autoPlayer = false
player.playWithModel(fileModel) // 仅打开并暂停
```

说明：**HLS（`.m3u8`）** 需要拉取清单与多个分片，与「单路自定义 IO」模型不完全一致；若核心层对 `CUSTOM_HTTP + m3u8` 做了自动降级，行为与默认模式一致。

### License 校验（必需）

Android SDK 当前为**强制 License 校验**模式：  
在调用 `openURL(...)` / `openWithPlayModel(...)` / `play()` / `seekTo(...)` 前，需要先通过 `checkLicense(...)`。

```kotlin
val player = HXCPlayerControl(this)

player.checkLicense(
    licenseKey = "你的32位licenseKey",
    licenseUrl = "https://console-api.huaxiacloud.net/license/getLicense/111453136245362688"
) { success, error ->
    if (success) {
        // 通过后再打开并播放
        val model = HXCPlayerControl.PlayerDataSourcePlayModel.modelWithURL(
            url = "https://example.com/video.mp4",
            mode = HXCPlayerControl.PlayerDataSourceMode.DEFAULT,
            encryptedFile = false
        )
        player.openWithPlayModel(model)
        player.play()
    } else {
        // 建议向用户展示 error?.message
        Toast.makeText(this, "License 校验失败: ${error?.message}", Toast.LENGTH_LONG).show()
    }
}
```

校验规则：
- 解密后的 License 数组中，至少存在一条记录满足：
  - `package_name == context.packageName`
  - `finished_at > 当前 Unix 时间戳`

离线/失败回退：
- `checkLicense(...)` 会优先走网络校验；
- 若网络失败、加载失败、解析失败，会自动尝试本地缓存；
- 本地缓存仍满足上述规则时，也会返回成功并允许播放。

清理本地 License：

```kotlin
player.resetLicenseState()
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

### 日志（静态方法，可选）

在 `Application` 或首次使用播放器前配置一次即可；方法定义在 `HXCPlayerControl` 的伴生对象中，Java 侧以静态方法调用。

```kotlin
// 启用写入文件的日志（目录需可写）
HXCPlayerControl.enableFileLogging(
    logDir = context.filesDir.absolutePath + "/hxc_logs",
    prefix = "hxcplayer"
)

// 日志级别：0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR
HXCPlayerControl.setLogLevel(1)
val level = HXCPlayerControl.getLogLevel()

// 日志文件保留天数
HXCPlayerControl.setLogRetentionDays(7)

// 当前日志目录 / 当前日志文件路径（便于上传或排查）
val dir = HXCPlayerControl.getLogDirectory()
val path = HXCPlayerControl.getCurrentLogFile()

// 关闭文件日志
HXCPlayerControl.disableFileLogging()
```

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
        
        // 自动播放测试视频（默认 FFmpeg 打开）
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
    
    override fun onPlayerError(errorCode: Int, errorMessage: String) {
        runOnUiThread {
            Toast.makeText(this, "错误 [$errorCode]: $errorMessage", Toast.LENGTH_LONG).show()
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
override fun onPlayerError(errorCode: Int, errorMessage: String) {
    when {
        errorCode == HXCPlayerControl.PlayerErrorCode.OPEN_INPUT_FAILED -> {
            Toast.makeText(this, "打开失败: $errorMessage", Toast.LENGTH_SHORT).show()
        }
        errorMessage.contains("Protocol not found", ignoreCase = true) -> {
            Toast.makeText(this, "不支持的协议", Toast.LENGTH_SHORT).show()
        }
        errorMessage.contains("Connection", ignoreCase = true) -> {
            Toast.makeText(this, "网络连接失败", Toast.LENGTH_SHORT).show()
        }
        else -> {
            Toast.makeText(this, "播放错误: $errorMessage", Toast.LENGTH_SHORT).show()
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

**症状**: 调用 `openURL()` / `openWithPlayModel()` 后无响应

**解决方案**:
1. 检查网络权限是否添加
2. 检查 URL 是否正确
3. 检查网络连接是否正常
4. 查看 Logcat 日志（过滤 `HXCPlayer` 标签）
5. 若 `openWithPlayModel(CUSTOM_FILE)`，确认 `model.url` 为可读绝对路径

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

## 附录：`PlayerErrorCode`（与 `PlayerErrorCodeC` 一致）

与 `HXCPlayerControl.PlayerErrorCode` / `PlayerErrorCodeC` 一致（便于在 `onPlayerError` 里分支处理）：

| 常量 | 值 | 含义（简述） |
|------|-----|----------------|
| `NONE` | 0 | 无错误 |
| `INVALID_URL` | -1001 | URL 无效 |
| `OPEN_INPUT_FAILED` | -1002 | 打开输入失败 |
| `FIND_STREAM_INFO_FAILED` | -1003 | 无法解析流信息 |
| `NO_VIDEO_STREAM` | -1004 | 无视频流 |
| `NO_AUDIO_STREAM` | -1005 | 无音频流 |
| `CODEC_NOT_FOUND` | -1006 | 找不到编解码器 |
| `CODEC_OPEN_FAILED` | -1007 | 打开解码器失败 |
| `ALLOC_CONTEXT_FAILED` | -1008 | 分配上下文失败 |
| `SDL_INIT_FAILED` | -1009 | SDL 初始化失败 |
| `AUDIO_DEVICE_OPEN_FAILED` | -1010 | 音频设备打开失败 |
| `SEEK_FAILED` | -1011 | Seek 失败 |
| `READ_FRAME_FAILED` | -1012 | 读取帧失败 |
| `DECODE_FAILED` | -1013 | 解码失败 |
| `OUT_OF_MEMORY` | -1014 | 内存不足 |
| `INPUT_INVALID_DATA` | -1018 | 无效数据 |
| `NOT_SUPPORT` | -1019 | 不支持的格式或协议 |
| `UNKNOWN` | -1099 | 未知错误 |
| `NET_CONNECTION_TIMEOUT` | -2001 | 网络连接超时 |
| `NET_CONNECTION_REFUSED` | -2002 | 连接被拒绝 |
| `NET_UNREACHABLE` | -2003 | 网络不可达 |
| `HTTP_BAD_REQUEST` | -3001 | HTTP 400 |
| `HTTP_NOT_FOUND` | -3002 | HTTP 404 |
| `HTTP_SERVER_ERROR` | -3003 | HTTP 5xx |
| `HTTP_UNAUTHORIZED` | -3004 | HTTP 401 |
| `HTTP_FORBIDDEN` | -3005 | HTTP 403 |

未出现在上表中的 **`errorCode` 负数** 仍可能为 FFmpeg 等返回的原始错误码，请结合 `errorMessage` 判断。

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
