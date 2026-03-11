# HXCPlayer 回调函数使用指南

本文档说明如何使用 HXCPlayer 的三个核心回调函数。

## 回调函数类型

### 1. 状态变化回调 (State Changed Callback)

当播放器状态发生变化时触发（空闲、打开中、播放中、暂停、停止、错误）。

### 2. 错误回调 (Error Callback)

当播放器遇到错误时触发，提供错误信息字符串。

### 3. 播放进度回调 (Position Changed Callback)

播放过程中定期触发，提供**当前真实播放位置**（秒）。

**重要说明**：
- 回调报告的是 **master clock 的实际播放进度**，不是解码进度
- 定时器线程每 **200ms** 触发一次回调（每秒 5 次）
- 即使在暂停状态下也会触发，方便 UI 更新显示
- 播放和暂停时都能准确反映当前位置

**适用场景**：
- ✅ 进度条显示（与音视频同步）
- ✅ 播放时间显示（"00:04:30 / 01:23:45"）
- ✅ 字幕同步
- ✅ 章节标记高亮

### 4. 缓冲进度回调 (Buffer Progress Callback)

播放过程中定期触发，提供**当前音频解码位置**（秒）。

**重要说明**：
- 回调报告的是**音频解码器已解码到的位置**，不是实际播放位置
- 每解码 10 个音频帧触发一次（约每 213ms @ 48kHz）
- 解码位置通常会**领先**播放位置几百毫秒到几秒
- 缓冲量 = 解码位置 - 播放位置

**适用场景**：
- ✅ 缓冲进度条显示
- ✅ 预加载指示器
- ✅ 网络流播放状态监控
- ✅ 判断是否有足够缓冲

## C 接口定义

```c
// 回调函数类型
typedef void (*StateChangedCallbackC)(PlayerStateC state, void* user_data);
typedef void (*ErrorCallbackC)(const char* error, void* user_data);
typedef void (*PositionChangedCallbackC)(double position, void* user_data);      // 真实播放位置
typedef void (*BufferProgressCallbackC)(double position, void* user_data);       // 缓冲进度（解码位置）

// 设置回调
void player_core_set_state_changed_callback(PlayerCoreHandle* handle, StateChangedCallbackC callback, void* user_data);
void player_core_set_error_callback(PlayerCoreHandle* handle, ErrorCallbackC callback, void* user_data);
void player_core_set_position_changed_callback(PlayerCoreHandle* handle, PositionChangedCallbackC callback, void* user_data);
void player_core_set_buffer_progress_callback(PlayerCoreHandle* handle, BufferProgressCallbackC callback, void* user_data);
```

## 使用示例

### iOS/macOS (Objective-C)

```objc
// 在 HXCPlayerControl.mm 中

// 状态变化回调
static void state_changed_callback(PlayerStateC state, void* user_data) {
    HXCPlayerControl* self = (__bridge HXCPlayerControl*)user_data;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        // 通知 delegate
        if ([self.delegate respondsToSelector:@selector(playerDidChangeState:)]) {
            HXCPlayerState playerState;
            switch (state) {
                case PLAYER_STATE_IDLE: playerState = HXCPlayerStateIdle; break;
                case PLAYER_STATE_OPENING: playerState = HXCPlayerStateOpening; break;
                case PLAYER_STATE_PLAYING: playerState = HXCPlayerStatePlaying; break;
                case PLAYER_STATE_PAUSED: playerState = HXCPlayerStatePaused; break;
                case PLAYER_STATE_STOPPED: playerState = HXCPlayerStateStopped; break;
                case PLAYER_STATE_ERROR: playerState = HXCPlayerStateError; break;
            }
            [self.delegate playerDidChangeState:playerState];
        }
    });
}

// 错误回调
static void error_callback(const char* error, void* user_data) {
    HXCPlayerControl* self = (__bridge HXCPlayerControl*)user_data;
    NSString* errorStr = [NSString stringWithUTF8String:error];
    
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([self.delegate respondsToSelector:@selector(player:didFailWithError:)]) {
            NSError* nsError = [NSError errorWithDomain:@"HXCPlayerError" 
                                                   code:-1 
                                               userInfo:@{NSLocalizedDescriptionKey: errorStr}];
            [self.delegate player:self didFailWithError:nsError];
        }
    });
}

// 播放进度回调（真实播放位置）
static void position_changed_callback(double position, void* user_data) {
    HXCPlayerControl* self = (__bridge HXCPlayerControl*)user_data;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([self.delegate respondsToSelector:@selector(player:didUpdatePosition:)]) {
            [self.delegate player:self didUpdatePosition:position];
        }
    });
}

// 缓冲进度回调（解码位置）
static void buffer_progress_callback(double position, void* user_data) {
    HXCPlayerControl* self = (__bridge HXCPlayerControl*)user_data;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([self.delegate respondsToSelector:@selector(player:didUpdateBufferProgress:)]) {
            [self.delegate player:self didUpdateBufferProgress:position];
        }
    });
}

// 在初始化时设置回调
- (instancetype)init {
    if (self = [super init]) {
        _playerCore = player_core_create();
        
        // 设置回调（传递 self 作为 user_data）
        player_core_set_state_changed_callback(_playerCore, state_changed_callback, (__bridge void*)self);
        player_core_set_error_callback(_playerCore, error_callback, (__bridge void*)self);
        player_core_set_position_changed_callback(_playerCore, position_changed_callback, (__bridge void*)self);
        player_core_set_buffer_progress_callback(_playerCore, buffer_progress_callback, (__bridge void*)self);
    }
    return self;
}
```

### Android (JNI + Kotlin)

#### 1. JNI 层 (hxcplayer_jni.cpp)

```cpp
// 全局引用 Java 对象和方法
static jclass g_callback_class = nullptr;
static jmethodID g_on_state_changed_method = nullptr;
static jmethodID g_on_error_method = nullptr;
static jmethodID g_on_position_changed_method = nullptr;
static jmethodID g_on_buffer_progress_method = nullptr;

// 状态变化回调
static void state_changed_callback(PlayerStateC state, void* user_data) {
    JNIEnv* env = nullptr;
    // 获取 JNIEnv（需要 attach 到当前线程）
    if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        return;
    }
    
    jobject callback_obj = (jobject)user_data;
    if (callback_obj && g_on_state_changed_method) {
        env->CallVoidMethod(callback_obj, g_on_state_changed_method, (jint)state);
    }
    
    g_jvm->DetachCurrentThread();
}

// 错误回调
static void error_callback(const char* error, void* user_data) {
    JNIEnv* env = nullptr;
    if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        return;
    }
    
    jobject callback_obj = (jobject)user_data;
    if (callback_obj && g_on_error_method) {
        jstring error_str = env->NewStringUTF(error);
        env->CallVoidMethod(callback_obj, g_on_error_method, error_str);
        env->DeleteLocalRef(error_str);
    }
    
    g_jvm->DetachCurrentThread();
}

// 播放进度回调（真实播放位置）
static void position_changed_callback(double position, void* user_data) {
    JNIEnv* env = nullptr;
    if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        return;
    }
    
    jobject callback_obj = (jobject)user_data;
    if (callback_obj && g_on_position_changed_method) {
        env->CallVoidMethod(callback_obj, g_on_position_changed_method, (jdouble)position);
    }
    
    g_jvm->DetachCurrentThread();
}

// 缓冲进度回调（解码位置）
static void buffer_progress_callback(double position, void* user_data) {
    JNIEnv* env = nullptr;
    if (g_jvm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
        return;
    }
    
    jobject callback_obj = (jobject)user_data;
    if (callback_obj && g_on_buffer_progress_method) {
        env->CallVoidMethod(callback_obj, g_on_buffer_progress_method, (jdouble)position);
    }
    
    g_jvm->DetachCurrentThread();
}

// JNI 函数：设置回调
extern "C" JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSetCallbacks(
    JNIEnv* env, 
    jobject /* this */, 
    jlong handle,
    jobject callback_obj
) {
    auto* player_handle = reinterpret_cast<PlayerCoreHandle*>(handle);
    if (!player_handle) return;
    
    // 创建全局引用
    jobject global_callback = env->NewGlobalRef(callback_obj);
    
    // 设置回调
    player_core_set_state_changed_callback(player_handle, state_changed_callback, global_callback);
    player_core_set_error_callback(player_handle, error_callback, global_callback);
    player_core_set_position_changed_callback(player_handle, position_changed_callback, global_callback);
    player_core_set_buffer_progress_callback(player_handle, buffer_progress_callback, global_callback);
}
```

#### 2. Kotlin 层 (HXCPlayerControl.kt)

```kotlin
class HXCPlayerControl(context: Context) {
    
    // 定义回调接口
    interface PlayerCallback {
        fun onStateChanged(state: Int)
        fun onError(error: String)
        fun onPositionChanged(position: Double)  // 真实播放位置
        fun onBufferProgress(position: Double)   // 缓冲进度（解码位置）
    }
    
    private var callback: PlayerCallback? = null
    
    // 设置回调
    fun setCallback(callback: PlayerCallback) {
        this.callback = callback
        if (nativeHandle != 0L) {
            nativeSetCallbacks(nativeHandle, this)
        }
    }
    
    // JNI 回调方法（从 native 层调用）
    @Keep
    private fun onStateChanged(state: Int) {
        // 切换到主线程
        Handler(Looper.getMainLooper()).post {
            callback?.onStateChanged(state)
        }
    }
    
    @Keep
    private fun onError(error: String) {
        Handler(Looper.getMainLooper()).post {
            callback?.onError(error)
        }
    }
    
    @Keep
    private fun onPositionChanged(position: Double) {
        Handler(Looper.getMainLooper()).post {
            callback?.onPositionChanged(position)
        }
    }
    
    // Native 方法
    private external fun nativeSetCallbacks(handle: Long, callback: Any)
}
```

#### 3. 使用示例 (MainActivity.kt)

```kotlin
class MainActivity : AppCompatActivity() {
    private lateinit var player: HXCPlayerControl
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        player = HXCPlayerControl(this)
        
        // 设置回调
        player.setCallback(object : HXCPlayerControl.PlayerCallback {
            override fun onStateChanged(state: Int) {
                Log.d(TAG, "播放器状态变化: $state")
                when (state) {
                    0 -> Log.d(TAG, "空闲")
                    1 -> Log.d(TAG, "打开中...")
                    2 -> Log.d(TAG, "播放中")
                    3 -> Log.d(TAG, "暂停")
                    4 -> Log.d(TAG, "停止")
                    -1 -> Log.e(TAG, "错误")
                }
            }
            
            override fun onError(error: String) {
                Log.e(TAG, "播放错误: $error")
                Toast.makeText(this@MainActivity, "错误: $error", Toast.LENGTH_SHORT).show()
            }
            
            override fun onPositionChanged(position: Double) {
                // 更新 UI 播放进度
                val minutes = (position / 60).toInt()
                val seconds = (position % 60).toInt()
                val positionText = String.format("%02d:%02d", minutes, seconds)
                
                runOnUiThread {
                    findViewById<TextView>(R.id.positionText)?.text = positionText
                }
            }
        })
        
        player.openURL("http://example.com/video.mp4")
    }
}
```

## 回调触发时机

### 状态变化回调
- 调用 `open()` 时 → `PLAYER_STATE_OPENING`
- 打开成功，开始播放 → `PLAYER_STATE_PLAYING`
- 调用 `pause()` → `PLAYER_STATE_PAUSED`
- 调用 `play()` 恢复 → `PLAYER_STATE_PLAYING`
- 调用 `stop()` → `PLAYER_STATE_STOPPED`
- 发生错误 → `PLAYER_STATE_ERROR`

### 错误回调
- 打开文件失败
- 解码错误
- 网络错误

### 播放进度回调
- 播放过程中，每 10 个音频帧触发一次（约每 200-300ms）
- 提供当前播放位置（秒）

## 注意事项

1. **线程安全**：回调可能在非主线程调用，需要在主线程更新 UI
2. **内存管理**：
   - iOS/macOS: 使用 `__bridge` 避免内存泄漏
   - Android: 使用 `NewGlobalRef` 创建全局引用，在 `release()` 时调用 `DeleteGlobalRef`
3. **性能**：播放进度回调频繁触发，避免在回调中执行耗时操作

## 完整实现状态

✅ **已实现**：
- C++ 核心层回调定义和触发
- C 桥接层导出函数
- 播放进度回调在音频解码线程中触发
- 状态变化和错误回调在相应位置触发

⚠️ **待各平台集成**：
- iOS/macOS: 在 `HXCPlayerControl.mm` 中设置回调
- Android: 在 JNI 层设置回调并通过 JNI 调用 Kotlin 方法

## 相关文件

- `core/include/hxc_player_core.h` - C++ 回调定义
- `core/src/hxc_player_core.cpp` - 回调触发逻辑
- `core/include/hxc_player_core_c_bridge.h` - C 接口声明
- `core/src/hxc_player_core_c_bridge.cpp` - C 接口实现
