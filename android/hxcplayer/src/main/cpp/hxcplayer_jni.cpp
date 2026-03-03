#include <jni.h>
#include <string>
#include <android/log.h>
#include <android/native_window_jni.h>
#include "android_player.h"

#define LOG_TAG "HXCPlayerJNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" {

// 创建播放器实例
JNIEXPORT jlong JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeCreate(JNIEnv *env, jobject thiz) {
    LOGD("nativeCreate");
    auto* player = new AndroidPlayer();
    return reinterpret_cast<jlong>(player);
}

// 释放播放器实例
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeRelease(JNIEnv *env, jobject thiz, jlong handle) {
    LOGD("nativeRelease");
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        delete player;
    }
}

// 设置 Surface
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSetSurface(
        JNIEnv *env, jobject thiz, jlong handle, jobject surface) {
    LOGD("nativeSetSurface");
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player) return;
    
    ANativeWindow* window = nullptr;
    if (surface) {
        window = ANativeWindow_fromSurface(env, surface);
    }
    player->setSurface(window);
}

// 更新 Surface 尺寸
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeUpdateSurfaceSize(
        JNIEnv *env, jobject thiz, jlong handle, jint width, jint height) {
    LOGD("nativeUpdateSurfaceSize: %d x %d", width, height);
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->updateSurfaceSize(width, height);
    }
}

// 打开 URL
JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeOpenURL(
        JNIEnv *env, jobject thiz, jlong handle, jstring url) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player) return JNI_FALSE;
    
    const char* urlStr = env->GetStringUTFChars(url, nullptr);
    LOGD("nativeOpenURL: %s", urlStr);
    
    bool result = player->openURL(urlStr);
    
    env->ReleaseStringUTFChars(url, urlStr);
    return result ? JNI_TRUE : JNI_FALSE;
}

// 打开 URL 并指定起始位置
JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeOpenURLWithStartPosition(
        JNIEnv *env, jobject thiz, jlong handle, jstring url, jdouble start_position) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player) return JNI_FALSE;
    
    const char* urlStr = env->GetStringUTFChars(url, nullptr);
    LOGD("nativeOpenURLWithStartPosition: %s, position: %.2f", urlStr, start_position);
    
    bool result = player->openURL(urlStr, start_position);
    
    env->ReleaseStringUTFChars(url, urlStr);
    return result ? JNI_TRUE : JNI_FALSE;
}

// 播放
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativePlay(JNIEnv *env, jobject thiz, jlong handle) {
    LOGD("nativePlay");
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->play();
    }
}

// 暂停
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativePause(JNIEnv *env, jobject thiz, jlong handle) {
    LOGD("nativePause");
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->pause();
    }
}

// 停止
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeStop(JNIEnv *env, jobject thiz, jlong handle) {
    LOGD("nativeStop");
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->stop();
    }
}

// 跳转
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSeekTo(
        JNIEnv *env, jobject thiz, jlong handle, jdouble position) {
    LOGD("nativeSeekTo: %f", position);
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->seekTo(position);
    }
}

// 设置播放速度
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSetPlaybackRate(
        JNIEnv *env, jobject thiz, jlong handle, jfloat rate) {
    LOGD("nativeSetPlaybackRate: %f", rate);
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->setPlaybackRate(rate);
    }
}

// 设置音量
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSetVolume(
        JNIEnv *env, jobject thiz, jlong handle, jfloat volume) {
    LOGD("nativeSetVolume: %f", volume);
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->setVolume(volume);
    }
}

// 设置比例模式
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSetAspectRatioMode(
        JNIEnv *env, jobject thiz, jlong handle, jint mode) {
    LOGD("nativeSetAspectRatioMode: %d", mode);
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->setAspectRatioMode(mode);
    }
}

// 获取时长
JNIEXPORT jdouble JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeGetDuration(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        return player->getDuration();
    }
    return 0.0;
}

// 获取当前位置
JNIEXPORT jdouble JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeGetPosition(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        return player->getPosition();
    }
    return 0.0;
}

// 获取状态
JNIEXPORT jint JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeGetState(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        return player->getState();
    }
    return 0; // IDLE
}

} // extern "C"
