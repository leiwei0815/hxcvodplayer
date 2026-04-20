#include <jni.h>
#include <string>
#include <android/log.h>
#include <android/native_window_jni.h>
#include "android_player.h"
#include "hxc_player_core_c_bridge.h"

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

// ========== 日志配置方法 ==========

// 启用文件日志
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_enableFileLogging(
        JNIEnv *env, jclass clazz, jstring log_dir, jstring prefix) {
    const char* logDirStr = env->GetStringUTFChars(log_dir, nullptr);
    const char* prefixStr = env->GetStringUTFChars(prefix, nullptr);
    
    LOGD("enableFileLogging: dir=%s, prefix=%s", logDirStr, prefixStr);
    
    // 调用 C 接口启用文件日志
    player_core_enable_file_logging(logDirStr, prefixStr);
    
    env->ReleaseStringUTFChars(log_dir, logDirStr);
    env->ReleaseStringUTFChars(prefix, prefixStr);
}

// 禁用文件日志
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_disableFileLogging(JNIEnv *env, jclass clazz) {
    LOGD("disableFileLogging");
    player_core_disable_file_logging();
}

// 设置日志级别
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_setLogLevel(JNIEnv *env, jclass clazz, jint level) {
    LOGD("setLogLevel: %d", level);
    player_core_set_log_level(level);
}

// 获取日志级别（0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR）
JNIEXPORT jint JNICALL
Java_com_hxcplayer_HXCPlayerControl_getLogLevel(JNIEnv *env, jclass clazz) {
    jint level = static_cast<jint>(player_core_get_log_level());
    LOGD("getLogLevel: %d", level);
    return level;
}

// 设置日志保留天数
JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_setLogRetentionDays(JNIEnv *env, jclass clazz, jint days) {
    LOGD("setLogRetentionDays: %d", days);
    player_core_set_log_retention_days(days);
}

// 获取当前日志文件路径
JNIEXPORT jstring JNICALL
Java_com_hxcplayer_HXCPlayerControl_getCurrentLogFile(JNIEnv *env, jclass clazz) {
    const char* logFile = player_core_get_current_log_file();
    LOGD("getCurrentLogFile: %s", logFile ? logFile : "");
    return env->NewStringUTF(logFile ? logFile : "");
}

// 获取当前文件日志目录
JNIEXPORT jstring JNICALL
Java_com_hxcplayer_HXCPlayerControl_getLogDirectory(JNIEnv *env, jclass clazz) {
    const char* dir = player_core_get_log_directory();
    LOGD("getLogDirectory: %s", dir ? dir : "");
    return env->NewStringUTF(dir ? dir : "");
}

// 使用自定义 HTTP 模式打开
JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeOpenWithCustomHTTP(
        JNIEnv *env, jobject thiz, jlong handle, jstring url, jint timeout_ms, jint max_retries, jboolean encrypted_file) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player) return JNI_FALSE;

    const char* urlStr = env->GetStringUTFChars(url, nullptr);
    LOGD("nativeOpenWithCustomHTTP: %s encrypted=%d", urlStr, encrypted_file ? 1 : 0);

    bool result = player->openWithCustomHTTP(urlStr, timeout_ms, max_retries, encrypted_file == JNI_TRUE);

    env->ReleaseStringUTFChars(url, urlStr);
    return result ? JNI_TRUE : JNI_FALSE;
}

// 使用自定义本地文件模式打开（与 DataSourceMode::CustomFile 一致）
JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeOpenWithCustomFile(
        JNIEnv *env, jobject thiz, jlong handle, jstring path, jint avio_buffer_size, jboolean encrypted_file) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player) return JNI_FALSE;

    const char* pathStr = env->GetStringUTFChars(path, nullptr);
    LOGD("nativeOpenWithCustomFile: %s avio_buffer_size=%d encrypted=%d",
         pathStr, avio_buffer_size, encrypted_file ? 1 : 0);

    size_t buf = avio_buffer_size > 0 ? static_cast<size_t>(avio_buffer_size) : (64 * 1024);
    bool result = player->openWithCustomFile(pathStr, buf, encrypted_file == JNI_TRUE);

    env->ReleaseStringUTFChars(path, pathStr);
    return result ? JNI_TRUE : JNI_FALSE;
}

} // extern "C"
