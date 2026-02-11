# Android 平台实现指南

## 概述

Android 版本使用 Java/Kotlin 实现 UI 和平台特定功能，通过 JNI 调用 C++ 核心播放器。

## 架构

```
┌─────────────────────────────────────┐
│   Android UI Layer (Java/Kotlin)   │
│  - PlayerActivity                   │
│  - PlayerView (SurfaceView)         │
│  - PlayerController                 │
└─────────────────────────────────────┘
              ↓ JNI
┌─────────────────────────────────────┐
│   JNI Bridge Layer (C++)            │
│  - player_jni.cpp                   │
│  - android_renderer.cpp             │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│   Core Player (C++)                 │
│  - PlayerCore                       │
│  - Decoder / Queue / Clock          │
└─────────────────────────────────────┘
              ↓
┌─────────────────────────────────────┐
│   FFmpeg / MediaCodec               │
└─────────────────────────────────────┘
```

## 项目结构

```
android/
├── app/
│   ├── src/
│   │   ├── main/
│   │   │   ├── java/com/yx/vodplayer/
│   │   │   │   ├── PlayerActivity.java
│   │   │   │   ├── PlayerView.java
│   │   │   │   ├── PlayerController.java
│   │   │   │   ├── NativePlayer.java
│   │   │   │   └── util/
│   │   │   ├── cpp/
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── player_jni.cpp
│   │   │   │   ├── android_renderer.cpp
│   │   │   │   └── android_audio.cpp
│   │   │   ├── res/
│   │   │   │   ├── layout/
│   │   │   │   │   ├── activity_player.xml
│   │   │   │   │   └── view_player_control.xml
│   │   │   │   ├── values/
│   │   │   │   └── drawable/
│   │   │   └── AndroidManifest.xml
│   │   └── androidTest/
│   ├── libs/
│   │   └── ffmpeg/           # FFmpeg 预编译库
│   │       ├── arm64-v8a/
│   │       ├── armeabi-v7a/
│   │       └── x86_64/
│   └── build.gradle
├── gradle/
├── build.gradle
└── settings.gradle
```

## 核心组件

### 1. NativePlayer (JNI 接口)

```java
package com.yx.vodplayer;

public class NativePlayer {
    // 加载本地库
    static {
        System.loadLibrary("yxplayer");
    }
    
    // 本地方法声明
    public native long nativeCreate();
    public native void nativeDestroy(long handle);
    public native int nativeOpen(long handle, String path);
    public native void nativeClose(long handle);
    public native void nativePlay(long handle);
    public native void nativePause(long handle);
    public native void nativeStop(long handle);
    public native void nativeSeek(long handle, double position);
    public native void nativeSetVolume(long handle, int volume);
    public native double nativeGetPosition(long handle);
    public native double nativeGetDuration(long handle);
    public native int nativeGetState(long handle);
    
    // 设置 Surface
    public native void nativeSetSurface(long handle, Surface surface);
    
    // 回调
    private StateCallback stateCallback;
    private ErrorCallback errorCallback;
    
    public interface StateCallback {
        void onStateChanged(int state);
    }
    
    public interface ErrorCallback {
        void onError(String error);
    }
    
    // 从 C++ 调用的方法
    private void onNativeStateChanged(int state) {
        if (stateCallback != null) {
            stateCallback.onStateChanged(state);
        }
    }
    
    private void onNativeError(String error) {
        if (errorCallback != null) {
            errorCallback.onError(error);
        }
    }
}
```

### 2. PlayerView (视频显示)

```java
package com.yx.vodplayer;

import android.content.Context;
import android.util.AttributeSet;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

public class PlayerView extends SurfaceView implements SurfaceHolder.Callback {
    private NativePlayer player;
    
    public PlayerView(Context context) {
        super(context);
        init();
    }
    
    public PlayerView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }
    
    private void init() {
        getHolder().addCallback(this);
    }
    
    public void setPlayer(NativePlayer player) {
        this.player = player;
    }
    
    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        if (player != null) {
            player.nativeSetSurface(player.nativeHandle, holder.getSurface());
        }
    }
    
    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        // 处理尺寸变化
    }
    
    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        if (player != null) {
            player.nativeSetSurface(player.nativeHandle, null);
        }
    }
}
```

### 3. PlayerActivity

```java
package com.yx.vodplayer;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;
import android.widget.SeekBar;

public class PlayerActivity extends Activity {
    private NativePlayer player;
    private PlayerView playerView;
    private PlayerController controller;
    
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_player);
        
        playerView = findViewById(R.id.player_view);
        controller = findViewById(R.id.player_controller);
        
        // 创建播放器
        player = new NativePlayer();
        player.nativeCreate();
        playerView.setPlayer(player);
        
        // 设置回调
        player.setStateCallback(state -> {
            runOnUiThread(() -> updateUI(state));
        });
        
        player.setErrorCallback(error -> {
            runOnUiThread(() -> showError(error));
        });
        
        // 设置控制器
        controller.setOnPlayPauseListener(this::togglePlayPause);
        controller.setOnSeekListener(this::seekTo);
        
        // 从 Intent 获取视频路径
        String videoPath = getIntent().getStringExtra("VIDEO_PATH");
        if (videoPath != null) {
            openVideo(videoPath);
        }
    }
    
    private void openVideo(String path) {
        int ret = player.nativeOpen(player.nativeHandle, path);
        if (ret == 0) {
            player.nativePlay(player.nativeHandle);
        }
    }
    
    private void togglePlayPause() {
        int state = player.nativeGetState(player.nativeHandle);
        if (state == PlayerState.PLAYING) {
            player.nativePause(player.nativeHandle);
        } else {
            player.nativePlay(player.nativeHandle);
        }
    }
    
    private void seekTo(double position) {
        player.nativeSeek(player.nativeHandle, position);
    }
    
    @Override
    protected void onDestroy() {
        if (player != null) {
            player.nativeDestroy(player.nativeHandle);
        }
        super.onDestroy();
    }
}
```

## JNI 实现

### player_jni.cpp

```cpp
#include <jni.h>
#include <android/native_window_jni.h>
#include "player_core.h"
#include "android_renderer.h"

using namespace yxplayer;

// 全局 JVM 引用
static JavaVM* g_jvm = nullptr;

// JNI_OnLoad
JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

// 创建播放器
JNIEXPORT jlong JNICALL
Java_com_yx_vodplayer_NativePlayer_nativeCreate(JNIEnv* env, jobject thiz) {
    PlayerCore* player = new PlayerCore();
    return reinterpret_cast<jlong>(player);
}

// 销毁播放器
JNIEXPORT void JNICALL
Java_com_yx_vodplayer_NativePlayer_nativeDestroy(JNIEnv* env, jobject thiz, jlong handle) {
    PlayerCore* player = reinterpret_cast<PlayerCore*>(handle);
    if (player) {
        delete player;
    }
}

// 打开文件
JNIEXPORT jint JNICALL
Java_com_yx_vodplayer_NativePlayer_nativeOpen(JNIEnv* env, jobject thiz, jlong handle, jstring path) {
    PlayerCore* player = reinterpret_cast<PlayerCore*>(handle);
    if (!player) return -1;
    
    const char* path_str = env->GetStringUTFChars(path, nullptr);
    int ret = player->open(path_str);
    env->ReleaseStringUTFChars(path, path_str);
    
    return ret;
}

// 播放
JNIEXPORT void JNICALL
Java_com_yx_vodplayer_NativePlayer_nativePlay(JNIEnv* env, jobject thiz, jlong handle) {
    PlayerCore* player = reinterpret_cast<PlayerCore*>(handle);
    if (player) {
        player->play();
    }
}

// 暂停
JNIEXPORT void JNICALL
Java_com_yx_vodplayer_NativePlayer_nativePause(JNIEnv* env, jobject thiz, jlong handle) {
    PlayerCore* player = reinterpret_cast<PlayerCore*>(handle);
    if (player) {
        player->pause();
    }
}

// 设置 Surface
JNIEXPORT void JNICALL
Java_com_yx_vodplayer_NativePlayer_nativeSetSurface(JNIEnv* env, jobject thiz, jlong handle, jobject surface) {
    PlayerCore* player = reinterpret_cast<PlayerCore*>(handle);
    if (!player) return;
    
    ANativeWindow* window = nullptr;
    if (surface) {
        window = ANativeWindow_fromSurface(env, surface);
    }
    
    // 设置渲染窗口
    // TODO: 实现 Android 渲染器
}

// 更多 JNI 方法...
```

### android_renderer.cpp

```cpp
#include "android_renderer.h"
#include <android/native_window.h>

extern "C" {
#include <libswscale/swscale.h>
}

namespace yxplayer {

AndroidRenderer::AndroidRenderer()
    : window_(nullptr)
    , width_(0)
    , height_(0) {
}

AndroidRenderer::~AndroidRenderer() {
    destroy();
}

bool AndroidRenderer::init(int width, int height, PixelFormat format) {
    width_ = width;
    height_ = height;
    format_ = format;
    return true;
}

void AndroidRenderer::set_window(ANativeWindow* window) {
    if (window_) {
        ANativeWindow_release(window_);
    }
    
    window_ = window;
    
    if (window_) {
        ANativeWindow_acquire(window_);
        ANativeWindow_setBuffersGeometry(window_, width_, height_, WINDOW_FORMAT_RGBA_8888);
    }
}

bool AndroidRenderer::render_frame(const VideoFrame* frame) {
    if (!window_ || !frame || !frame->frame) {
        return false;
    }
    
    ANativeWindow_Buffer buffer;
    if (ANativeWindow_lock(window_, &buffer, nullptr) < 0) {
        return false;
    }
    
    // 转换并复制帧数据到 buffer
    // TODO: 使用 sws_scale 转换格式
    
    ANativeWindow_unlockAndPost(window_);
    
    return true;
}

void AndroidRenderer::destroy() {
    if (window_) {
        ANativeWindow_release(window_);
        window_ = nullptr;
    }
}

} // namespace yxplayer
```

## CMakeLists.txt (Android)

```cmake
cmake_minimum_required(VERSION 3.18.1)
project(yxplayer)

set(CMAKE_CXX_STANDARD 17)

# FFmpeg 路径
set(FFMPEG_DIR ${CMAKE_SOURCE_DIR}/../libs/ffmpeg/${ANDROID_ABI})

include_directories(
    ${CMAKE_SOURCE_DIR}/../../../include
    ${FFMPEG_DIR}/include
)

link_directories(
    ${FFMPEG_DIR}/lib
)

# 核心源文件
file(GLOB CORE_SOURCES
    ${CMAKE_SOURCE_DIR}/../../../src/core/*.cpp
)

# JNI 源文件
file(GLOB JNI_SOURCES
    ${CMAKE_SOURCE_DIR}/*.cpp
)

add_library(yxplayer SHARED
    ${CORE_SOURCES}
    ${JNI_SOURCES}
)

target_link_libraries(yxplayer
    # FFmpeg
    avcodec
    avformat
    avutil
    swscale
    swresample
    
    # Android 系统库
    android
    log
    
    # OpenSL ES (音频)
    OpenSLES
)
```

## build.gradle

### 项目级 build.gradle

```gradle
buildscript {
    repositories {
        google()
        mavenCentral()
    }
    dependencies {
        classpath 'com.android.tools.build:gradle:7.4.0'
    }
}

allprojects {
    repositories {
        google()
        mavenCentral()
    }
}
```

### 应用级 build.gradle

```gradle
plugins {
    id 'com.android.application'
}

android {
    namespace 'com.yx.vodplayer'
    compileSdk 33
    
    defaultConfig {
        applicationId "com.yx.vodplayer"
        minSdk 21
        targetSdk 33
        versionCode 1
        versionName "1.0"
        
        ndk {
            abiFilters 'arm64-v8a', 'armeabi-v7a'
        }
        
        externalNativeBuild {
            cmake {
                cppFlags "-std=c++17"
                arguments "-DANDROID_STL=c++_shared"
            }
        }
    }
    
    buildTypes {
        release {
            minifyEnabled false
            proguardFiles getDefaultProguardFile('proguard-android-optimize.txt'), 'proguard-rules.pro'
        }
    }
    
    externalNativeBuild {
        cmake {
            path "src/main/cpp/CMakeLists.txt"
            version "3.18.1"
        }
    }
    
    compileOptions {
        sourceCompatibility JavaVersion.VERSION_1_8
        targetCompatibility JavaVersion.VERSION_1_8
    }
}

dependencies {
    implementation 'androidx.appcompat:appcompat:1.6.1'
    implementation 'com.google.android.material:material:1.9.0'
    implementation 'androidx.constraintlayout:constraintlayout:2.1.4'
}
```

## 权限配置

### AndroidManifest.xml

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android">

    <!-- 权限 -->
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.READ_EXTERNAL_STORAGE" />
    <uses-permission android:name="android.permission.WRITE_EXTERNAL_STORAGE" />
    <uses-permission android:name="android.permission.WAKE_LOCK" />

    <application
        android:allowBackup="true"
        android:icon="@mipmap/ic_launcher"
        android:label="@string/app_name"
        android:theme="@style/Theme.YXVodPlayer">
        
        <activity
            android:name=".PlayerActivity"
            android:configChanges="orientation|screenSize|keyboardHidden"
            android:screenOrientation="landscape"
            android:exported="true">
            <intent-filter>
                <action android:name="android.intent.action.MAIN" />
                <category android:name="android.intent.category.LAUNCHER" />
            </intent-filter>
        </activity>
    </application>

</manifest>
```

## 硬件加速（MediaCodec）

可以使用 Android 的 MediaCodec 进行硬件解码以提高性能：

```cpp
// 在解码器中使用 MediaCodec
#include <media/NdkMediaCodec.h>

class AndroidHardwareDecoder {
public:
    bool init(const char* mime, int width, int height);
    bool decode(AVPacket* packet, AVFrame* frame);
    
private:
    AMediaCodec* codec_;
};
```

## 性能优化

1. **使用硬件解码**: MediaCodec
2. **OpenGL ES 渲染**: 提高渲染性能
3. **多线程**: 解码和渲染分离
4. **缓冲优化**: 调整队列大小

## 调试

### 查看日志

```bash
adb logcat | grep yxplayer
```

### 性能分析

```bash
adb shell am profile start <package> <file>
adb shell am profile stop <package>
```

## 发布

1. 生成签名密钥
2. 配置签名
3. 构建 Release APK
4. 上传到 Google Play

详见 `BUILD.md`。
