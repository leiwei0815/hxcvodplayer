# HXCPlayer Android API 文档

## 简介

HXCPlayer 是一个跨平台的视频播放器库，支持 iOS、macOS、Windows 和 Android。Android 版本提供了与 iOS 相同的简洁 API 设计。

## 快速开始

### 1. 初始化播放器

```kotlin
import com.hxcplayer.test.HXCPlayerControl

class MyActivity : AppCompatActivity(), HXCPlayerControl.PlayerCallback {
    private lateinit var player: HXCPlayerControl
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // 创建播放器
        player = HXCPlayerControl(this)
        player.setCallback(this)
        
        // 获取视频视图并添加到布局
        val videoView = player.videoView
        
        // 方式1: 直接添加到容器
        findViewById<FrameLayout>(R.id.videoContainer).addView(videoView)
        
        // 方式2: 替换占位符（推荐）
        val placeholder = findViewById<View>(R.id.videoPlaceholder)
        val parent = placeholder.parent as ViewGroup
        val index = parent.indexOfChild(placeholder)
        val layoutParams = placeholder.layoutParams
        parent.removeView(placeholder)
        videoView.layoutParams = layoutParams
        parent.addView(videoView, index)
    }
}
```

### 2. 播放视频

```kotlin
// 打开 URL（不自动播放）
player.openURL("http://example.com/video.mp4")

// 打开 URL 并指定起始位置（秒）
player.openURL("http://example.com/video.mp4", 30.0)

// 开始播放
player.play()

// 暂停
player.pause()

// 停止
player.stop()

// 跳转到指定位置（秒）
player.seekTo(60.0)
```

### 3. 播放控制

```kotlin
// 设置播放速度（0.5x ~ 2.0x）
player.setPlaybackRate(1.5f)  // 1.5倍速

// 设置音量（0.0 ~ 1.0）
player.setVolume(0.8f)

// 设置显示模式
player.setAspectRatioMode(true)   // Fill 模式（裁剪填充）
player.setAspectRatioMode(false)  // Fit 模式（等比例缩放，黑边）

// 获取播放信息
val duration = player.getDuration()    // 总时长（秒）
val position = player.getPosition()    // 当前位置（秒）
val state = player.getState()          // 播放器状态
```

### 4. 回调接口

```kotlin
class MyActivity : AppCompatActivity(), HXCPlayerControl.PlayerCallback {
    
    // 状态变化回调
    override fun onPlayerStateChanged(state: HXCPlayerControl.PlayerState) {
        when (state) {
            HXCPlayerControl.PlayerState.IDLE -> { /* 空闲 */ }
            HXCPlayerControl.PlayerState.OPENING -> { /* 正在打开 */ }
            HXCPlayerControl.PlayerState.PLAYING -> { /* 播放中 */ }
            HXCPlayerControl.PlayerState.PAUSED -> { /* 已暂停 */ }
            HXCPlayerControl.PlayerState.STOPPED -> { /* 已停止 */ }
            HXCPlayerControl.PlayerState.ERROR -> { /* 错误 */ }
        }
    }
    
    // 播放进度回调（每 100ms 触发一次）
    override fun onPlayerPositionUpdated(position: Double, duration: Double) {
        val progress = (position / duration * 100).toInt()
        // 更新进度条
        seekBar.progress = progress
    }
    
    // 错误回调
    override fun onPlayerError(error: String) {
        Toast.makeText(this, "播放错误: $error", Toast.LENGTH_SHORT).show()
    }
}
```

### 5. 资源释放

```kotlin
override fun onDestroy() {
    super.onDestroy()
    player.release()
}
```

## 与 iOS API 对比

| 功能 | iOS (Objective-C) | Android (Kotlin) |
|------|-------------------|------------------|
| 创建播放器 | `[[HXCPlayerControl alloc] init]` | `HXCPlayerControl(context)` |
| 获取视图 | `player.videoView` | `player.videoView` |
| 打开 URL | `[player openURL:@"..."]` | `player.openURL("...")` |
| 播放 | `[player play]` | `player.play()` |
| 暂停 | `[player pause]` | `player.pause()` |
| 跳转 | `[player seekToPosition:60.0]` | `player.seekTo(60.0)` |
| 设置速度 | `player.playbackRate = 1.5` | `player.setPlaybackRate(1.5f)` |
| 设置音量 | `player.volume = 0.8` | `player.setVolume(0.8f)` |
| 设置模式 | `player.aspectRatioMode = ...` | `player.setAspectRatioMode(...)` |

## 布局示例

### XML 布局（占位符方式）

```xml
<?xml version="1.0" encoding="utf-8"?>
<FrameLayout xmlns:android="http://schemas.android.com/apk/res/android"
    android:layout_width="match_parent"
    android:layout_height="match_parent">

    <!-- 视频占位符（将被 player.videoView 替换） -->
    <SurfaceView
        android:id="@+id/videoPlaceholder"
        android:layout_width="match_parent"
        android:layout_height="wrap_content"
        android:layout_gravity="center" />

    <!-- 控制器 UI -->
    <LinearLayout
        android:layout_width="match_parent"
        android:layout_height="wrap_content"
        android:layout_gravity="bottom"
        android:orientation="vertical"
        android:padding="16dp"
        android:background="#80000000">
        
        <SeekBar
            android:id="@+id/seekBar"
            android:layout_width="match_parent"
            android:layout_height="wrap_content" />
            
        <Button
            android:id="@+id/playButton"
            android:layout_width="wrap_content"
            android:layout_height="wrap_content"
            android:text="Play" />
    </LinearLayout>
</FrameLayout>
```

## 特性

- ✅ **简洁的 API**：与 iOS 版本保持一致的设计
- ✅ **自动管理视图**：播放器内部创建和管理 SurfaceView
- ✅ **灵活的布局**：可以自由设置视图尺寸和位置
- ✅ **HTTP/HTTPS 支持**：支持 302 重定向
- ✅ **倍速播放**：0.5x ~ 2.0x（基于 SoundTouch）
- ✅ **多种显示模式**：Fit（等比例缩放）和 Fill（裁剪填充）
- ✅ **实时回调**：状态变化、播放进度、错误信息

## 注意事项

1. **权限**：需要在 `AndroidManifest.xml` 中声明网络权限
```xml
<uses-permission android:name="android.permission.INTERNET" />
<uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
```

2. **资源释放**：务必在 Activity/Fragment 销毁时调用 `player.release()`

3. **UI 线程**：回调方法在后台线程执行，更新 UI 需要切换到主线程

4. **生命周期**：建议在 `onPause()` 时暂停播放，`onResume()` 时恢复

## 集成方式

### 方式1: 复制源码（当前）
- 复制 `HXCPlayerControl.kt`
- 复制 `android_player.cpp/h`
- 复制 `hxcplayer_jni.cpp`
- 复制所有 `.so` 库文件

### 方式2: AAR 库（推荐，待实现）
```gradle
dependencies {
    implementation 'com.hxcplayer:hxcplayer:1.0.0'
}
```

### 方式3: 源码依赖
```gradle
dependencies {
    implementation project(':hxcplayer')
}
```

## 完整示例

参见 `examples/android-test/app` 目录中的示例应用。

## License

MIT License

---

更多信息请访问: https://github.com/yourusername/YXVodPlayer
