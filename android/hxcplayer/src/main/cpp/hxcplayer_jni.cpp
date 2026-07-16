#include <jni.h>
#include <string>
#include <android/log.h>
#include <android/native_window_jni.h>
#include <dlfcn.h>
#include "android_player.h"
#include "hxc_player_core_c_bridge.h"

#define LOG_TAG "HXCPlayerJNI"
#define LOG_TAG_DECODE "HXCSDK_DECODE"
#ifndef HXC_PLAYER_RUNTIME_LOG_LEVEL
#define HXC_PLAYER_RUNTIME_LOG_LEVEL 2
#endif
static int g_hxc_jni_runtime_log_level = HXC_PLAYER_RUNTIME_LOG_LEVEL;

#define LOGD(...) do { \
    if (g_hxc_jni_runtime_log_level <= 0) { \
        __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__); \
    } \
} while (0)

#define LOGI(...) do { \
    if (g_hxc_jni_runtime_log_level <= 1) { \
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__); \
    } \
} while (0)

#define DECODEI(...) do { \
    if (g_hxc_jni_runtime_log_level <= 1) { \
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG_DECODE, __VA_ARGS__); \
    } \
} while (0)

#define LOGW(...) do { \
    if (g_hxc_jni_runtime_log_level <= 2) { \
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__); \
    } \
} while (0)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define DECODEW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG_DECODE, __VA_ARGS__)
#define DECODEE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG_DECODE, __VA_ARGS__)

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)reserved;
    // FFmpeg Android MediaCodec 依赖 JavaVM 绑定；未绑定时常见表现是：
    // 能找到 h264_mediacodec 解码器，但 avcodec_open2 失败并回退软解。
    using AvJniSetJavaVmFn = int(*)(void*, void*);
    auto* sym = dlsym(RTLD_DEFAULT, "av_jni_set_java_vm");
    if (!sym) {
        LOGW("JNI_OnLoad: av_jni_set_java_vm symbol not found, MediaCodec may fallback to software");
        DECODEW("evt=jni_vm_bind status=symbol_not_found func=av_jni_set_java_vm");
        return JNI_VERSION_1_6;
    }
    auto fn = reinterpret_cast<AvJniSetJavaVmFn>(sym);
    int ret = fn(reinterpret_cast<void*>(vm), nullptr);
    if (ret < 0) {
        LOGE("JNI_OnLoad: av_jni_set_java_vm failed ret=%d", ret);
        DECODEE("evt=jni_vm_bind status=failed ret=%d", ret);
    } else {
        LOGI("JNI_OnLoad: av_jni_set_java_vm success ret=%d", ret);
        DECODEI("evt=jni_vm_bind status=ok ret=%d", ret);
    }
    return JNI_VERSION_1_6;
}

static jboolean hxc_native_open_with_secure_session(JNIEnv *env,
                                                    jlong handle,
                                                    jstring url,
                                                    jdouble start_position,
                                                    jstring auth_token,
                                                    jstring video_id,
                                                    jstring device_id,
                                                    jstring secret_id,
                                                    jstring nonce,
                                                    jstring play_session_id,
                                                    jstring secure_headers,
                                                    jlong session_expire_at_ms,
                                                    jint key_mode,
                                                    jstring key_material_b64,
                                                    jstring key_iv_hex) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player || !url) return JNI_FALSE;

    const char* c_url = env->GetStringUTFChars(url, nullptr);
    if (!c_url || c_url[0] == '\0') {
        if (c_url) env->ReleaseStringUTFChars(url, c_url);
        return JNI_FALSE;
    }
    const char* c_auth = auth_token ? env->GetStringUTFChars(auth_token, nullptr) : nullptr;
    const char* c_vid = video_id ? env->GetStringUTFChars(video_id, nullptr) : nullptr;
    const char* c_dev = device_id ? env->GetStringUTFChars(device_id, nullptr) : nullptr;
    const char* c_secret = secret_id ? env->GetStringUTFChars(secret_id, nullptr) : nullptr;
    const char* c_nonce = nonce ? env->GetStringUTFChars(nonce, nullptr) : nullptr;
    const char* c_sid = play_session_id ? env->GetStringUTFChars(play_session_id, nullptr) : nullptr;
    const char* c_hdr = secure_headers ? env->GetStringUTFChars(secure_headers, nullptr) : nullptr;
    const char* c_key = key_material_b64 ? env->GetStringUTFChars(key_material_b64, nullptr) : nullptr;
    const char* c_iv = key_iv_hex ? env->GetStringUTFChars(key_iv_hex, nullptr) : nullptr;

    bool result = player->openWithSecureSession(c_url, (double)start_position, c_auth, c_vid, c_dev, c_secret, c_nonce, c_sid, c_hdr,
                                                static_cast<int64_t>(session_expire_at_ms), key_mode, c_key, c_iv);

    env->ReleaseStringUTFChars(url, c_url);
    if (auth_token && c_auth) env->ReleaseStringUTFChars(auth_token, c_auth);
    if (video_id && c_vid) env->ReleaseStringUTFChars(video_id, c_vid);
    if (device_id && c_dev) env->ReleaseStringUTFChars(device_id, c_dev);
    if (secret_id && c_secret) env->ReleaseStringUTFChars(secret_id, c_secret);
    if (nonce && c_nonce) env->ReleaseStringUTFChars(nonce, c_nonce);
    if (play_session_id && c_sid) env->ReleaseStringUTFChars(play_session_id, c_sid);
    if (secure_headers && c_hdr) env->ReleaseStringUTFChars(secure_headers, c_hdr);
    if (key_material_b64 && c_key) env->ReleaseStringUTFChars(key_material_b64, c_key);
    if (key_iv_hex && c_iv) env->ReleaseStringUTFChars(key_iv_hex, c_iv);

    return result ? JNI_TRUE : JNI_FALSE;
}

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
    if (!player || !url) return JNI_FALSE;
    
    const char* urlStr = env->GetStringUTFChars(url, nullptr);
    if (!urlStr || urlStr[0] == '\0') {
        if (urlStr) env->ReleaseStringUTFChars(url, urlStr);
        return JNI_FALSE;
    }
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
    if (!player || !url) return JNI_FALSE;
    
    const char* urlStr = env->GetStringUTFChars(url, nullptr);
    if (!urlStr || urlStr[0] == '\0') {
        if (urlStr) env->ReleaseStringUTFChars(url, urlStr);
        return JNI_FALSE;
    }
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

JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSeekToWithIntent(
        JNIEnv *env, jobject thiz, jlong handle, jdouble position, jboolean resume_after_seek) {
    LOGD("nativeSeekToWithIntent: %f resume=%d", position, resume_after_seek ? 1 : 0);
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->seekToWithIntent(position, resume_after_seek == JNI_TRUE);
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

JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSetMuted(
        JNIEnv *env, jobject thiz, jlong handle, jboolean muted) {
    LOGD("nativeSetMuted: %d", muted ? 1 : 0);
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->setMuted(muted == JNI_TRUE);
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

JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSetDecodeMode(
        JNIEnv *env, jobject thiz, jlong handle, jint mode) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->setDecodeMode(mode);
    }
}

JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSetSecureSeekTuning(
        JNIEnv *env, jobject thiz, jlong handle,
        jdouble drop_only_window_backward_sec,
        jdouble drop_only_window_forward_sec,
        jdouble accept_future_backward_early_sec,
        jdouble accept_future_forward_early_sec,
        jdouble accept_future_backward_mid_sec,
        jdouble accept_future_forward_mid_sec,
        jdouble accept_future_backward_late_sec,
        jdouble accept_future_forward_late_sec,
        jint lower_bound_deadline_normal_ms,
        jint lower_bound_deadline_large_ms,
        jint recovery_deadline_normal_ms,
        jint recovery_deadline_large_ms,
        jint audio_wait_deadline_normal_ms,
        jint audio_wait_deadline_large_ms) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->setSecureSeekTuning(
                static_cast<double>(drop_only_window_backward_sec),
                static_cast<double>(drop_only_window_forward_sec),
                static_cast<double>(accept_future_backward_early_sec),
                static_cast<double>(accept_future_forward_early_sec),
                static_cast<double>(accept_future_backward_mid_sec),
                static_cast<double>(accept_future_forward_mid_sec),
                static_cast<double>(accept_future_backward_late_sec),
                static_cast<double>(accept_future_forward_late_sec),
                static_cast<int>(lower_bound_deadline_normal_ms),
                static_cast<int>(lower_bound_deadline_large_ms),
                static_cast<int>(recovery_deadline_normal_ms),
                static_cast<int>(recovery_deadline_large_ms),
                static_cast<int>(audio_wait_deadline_normal_ms),
                static_cast<int>(audio_wait_deadline_large_ms));
    }
}

JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeResetSecureSeekTuning(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->resetSecureSeekTuning();
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

JNIEXPORT jint JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeGetVideoWidth(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        return player->getVideoWidth();
    }
    return 0;
}

JNIEXPORT jint JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeGetVideoHeight(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        return player->getVideoHeight();
    }
    return 0;
}

JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeHasRenderedFirstFrame(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        return player->hasRenderedFirstFrame() ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
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

JNIEXPORT jint JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeGetPipelineState(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        return player->getPipelineState();
    }
    return 0; // IDLE
}

JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeGetPlayWhenReady(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        return player->getPlayWhenReady() ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeIsPlaying(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        return player->isPlaying() ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSetPlayWhenReady(
        JNIEnv *env, jobject thiz, jlong handle, jboolean play_when_ready) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        player->setPlayWhenReady(play_when_ready == JNI_TRUE);
    }
}

// 获取加载状态（网络波动时用于展示 loading 动画）
JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeIsLoading(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        return player->isLoading() ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

// 当前是否启用硬解（1=硬解，0=软解或未知）
JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeIsHardwareDecodingActive(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        return player->isHardwareDecodingActive() ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeIsSeekSessionActive(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        return player->isSeekSessionActive() ? JNI_TRUE : JNI_FALSE;
    }
    return JNI_FALSE;
}

// 消费一次播放中错误（有错误返回 message；无错误返回 null）
JNIEXPORT jstring JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeConsumeLastError(
        JNIEnv *env, jobject thiz, jlong handle, jintArray out_code) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player || !out_code) {
        return nullptr;
    }

    int error_code = 0;
    std::string error_message;
    if (!player->consumeLastError(error_code, error_message)) {
        return nullptr;
    }

    if (env->GetArrayLength(out_code) > 0) {
        jint code = static_cast<jint>(error_code);
        env->SetIntArrayRegion(out_code, 0, 1, &code);
    }

    return env->NewStringUTF(error_message.c_str());
}

// 消费一次播放完成事件（有事件返回 JNI_TRUE；无则返回 JNI_FALSE）
JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeConsumePlaybackCompleted(
        JNIEnv *env, jobject thiz, jlong handle) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player) {
        LOGW("[播放完成] JNI 层：nativeConsumePlaybackCompleted 调用时 player 为 null");
        return JNI_FALSE;
    }
    bool result = player->consumePlaybackCompleted();
    if (result) {
        LOGI("[播放完成] JNI 层：consumePlaybackCompleted=true，即将通知 Kotlin 层");
    }
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSettleSeekSession(
        JNIEnv *env, jobject thiz, jlong handle, jboolean by_timeout) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player) {
        return;
    }
    player->settleSeekSessionFromApp(by_timeout == JNI_TRUE);
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
    int clamped = level;
    if (clamped < 0) clamped = 0;
    if (clamped > 3) clamped = 3;
    g_hxc_jni_runtime_log_level = clamped;
    hxc_sdk_set_runtime_log_level(clamped);
    player_core_set_log_level(clamped);
    LOGI("setLogLevel: %d", clamped);
}

// 获取日志级别（0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR）
JNIEXPORT jint JNICALL
Java_com_hxcplayer_HXCPlayerControl_getLogLevel(JNIEnv *env, jclass clazz) {
    jint level = static_cast<jint>(hxc_sdk_get_runtime_log_level());
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
        JNIEnv *env, jobject thiz, jlong handle, jstring url, jdouble start_position,
        jint timeout_ms, jint max_retries, jboolean encrypted_file) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player || !url) return JNI_FALSE;

    const char* urlStr = env->GetStringUTFChars(url, nullptr);
    if (!urlStr || urlStr[0] == '\0') {
        if (urlStr) env->ReleaseStringUTFChars(url, urlStr);
        return JNI_FALSE;
    }
    LOGD("nativeOpenWithCustomHTTP: %s start=%.3f encrypted=%d",
         urlStr, static_cast<double>(start_position), encrypted_file ? 1 : 0);

    bool result = player->openWithCustomHTTP(urlStr, timeout_ms, max_retries,
                                             encrypted_file == JNI_TRUE,
                                             static_cast<double>(start_position));

    env->ReleaseStringUTFChars(url, urlStr);
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeOpenWithSecureSession(
        JNIEnv *env, jobject thiz, jlong handle, jstring url, jdouble start_position, jstring auth_token, jstring video_id,
        jstring device_id, jstring secret_id, jstring nonce, jstring play_session_id,
        jstring secure_headers, jlong session_expire_at_ms, jint key_mode, jstring key_material_b64, jstring key_iv_hex) {
    return hxc_native_open_with_secure_session(env, handle, url, start_position, auth_token, video_id, device_id, secret_id, nonce,
                                               play_session_id, secure_headers, session_expire_at_ms, key_mode,
                                               key_material_b64, key_iv_hex);
}

// 兼容 JNI 旧入口：内部转发到 nativeOpenWithSecureSession。
JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeOpenWithSecureHLS(
        JNIEnv *env, jobject thiz, jlong handle, jstring url, jdouble start_position, jstring auth_token, jstring video_id,
        jstring device_id, jstring secret_id, jstring nonce, jstring play_session_id,
        jstring secure_headers, jlong session_expire_at_ms, jint key_mode, jstring key_material_b64, jstring key_iv_hex) {
    return hxc_native_open_with_secure_session(env, handle, url, start_position, auth_token, video_id, device_id, secret_id, nonce,
                                               play_session_id, secure_headers, session_expire_at_ms, key_mode,
                                               key_material_b64, key_iv_hex);
}

// 使用自定义本地文件模式打开（与 DataSourceMode::CustomFile 一致）
JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeOpenWithCustomFile(
        JNIEnv *env, jobject thiz, jlong handle, jstring path, jdouble start_position,
        jint avio_buffer_size, jboolean encrypted_file) {
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player || !path) return JNI_FALSE;

    const char* pathStr = env->GetStringUTFChars(path, nullptr);
    if (!pathStr || pathStr[0] == '\0') {
        if (pathStr) env->ReleaseStringUTFChars(path, pathStr);
        return JNI_FALSE;
    }
    LOGD("nativeOpenWithCustomFile: %s start=%.3f avio_buffer_size=%d encrypted=%d",
         pathStr, static_cast<double>(start_position), avio_buffer_size, encrypted_file ? 1 : 0);

    size_t buf = avio_buffer_size > 0 ? static_cast<size_t>(avio_buffer_size) : (64 * 1024);
    bool result = player->openWithCustomFile(pathStr, buf, encrypted_file == JNI_TRUE,
                                             static_cast<double>(start_position));

    env->ReleaseStringUTFChars(path, pathStr);
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlongArray JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeGetAudioHealthMetrics(JNIEnv* env, jobject thiz, jlong handle) {
    (void)thiz;
    jlong values[5] = {0, 0, 0, 0, 0};
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (player) {
        int64_t silent_ms = 0;
        int underrun = 0;
        int opensl_state = 0;
        int recover_attempts = 0;
        int audio_output_state = 0;
        player->getAudioHealthMetrics(&silent_ms, &underrun, &opensl_state, &recover_attempts, &audio_output_state);
        values[0] = static_cast<jlong>(silent_ms);
        values[1] = static_cast<jlong>(underrun);
        values[2] = static_cast<jlong>(opensl_state);
        values[3] = static_cast<jlong>(recover_attempts);
        values[4] = static_cast<jlong>(audio_output_state);
    }
    jlongArray result = env->NewLongArray(5);
    if (result) {
        env->SetLongArrayRegion(result, 0, 5, values);
    }
    return result;
}

JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeRecoverAudioOutput(JNIEnv* env, jobject thiz, jlong handle) {
    (void)env;
    (void)thiz;
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player) return JNI_FALSE;
    return player->recoverAudioOutput() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeRebuildAudioOutput(JNIEnv* env, jobject thiz, jlong handle) {
    (void)env;
    (void)thiz;
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player) return JNI_FALSE;
    return player->rebuildAudioOutput() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeHandleAudioRouteChanged(JNIEnv* env, jobject thiz, jlong handle, jstring reason) {
    (void)thiz;
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player) return JNI_FALSE;
    const char* reason_str = reason ? env->GetStringUTFChars(reason, nullptr) : nullptr;
    bool result = player->handleAudioRouteChanged(reason_str ? reason_str : "audio_route_changed");
    if (reason_str) {
        env->ReleaseStringUTFChars(reason, reason_str);
    }
    return result ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeSetSystemMusicVolumeZero(JNIEnv* env, jobject thiz, jlong handle, jboolean volume_zero) {
    (void)env;
    (void)thiz;
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player) return;
    player->setSystemMusicVolumeZero(volume_zero == JNI_TRUE);
}

JNIEXPORT jdouble JNICALL
Java_com_hxcplayer_HXCPlayerControl_nativeConsumeVideoStallRecoverPosition(JNIEnv* env, jobject thiz, jlong handle) {
    (void)env;
    (void)thiz;
    auto* player = reinterpret_cast<AndroidPlayer*>(handle);
    if (!player) return -1.0;
    return player->consumeVideoStallRecoverPosition();
}

} // extern "C"
