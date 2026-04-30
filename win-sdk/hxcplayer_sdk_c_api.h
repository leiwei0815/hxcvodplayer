/**
 * @file hxcplayer_sdk_c_api.h
 * @brief HXCPlayer Windows SDK C API
 * @note 提供 C 风格接口，供桌面应用和其他语言调用
 */

#ifndef HXCPLAYER_SDK_C_API_H
#define HXCPLAYER_SDK_C_API_H

#include <stddef.h>
#include <stdint.h>

// ==================== DLL 导出宏 ====================
#ifdef _WIN32
    #ifdef HXCPLAYER_SDK_EXPORTS
        // 构建 DLL 时导出符号
        #define HXCPLAYER_SDK_API __declspec(dllexport)
    #elif defined(HXCPLAYER_SDK_STATIC)
        // 静态库不需要导出
        #define HXCPLAYER_SDK_API
    #else
        // 使用 DLL 时导入符号
        #define HXCPLAYER_SDK_API __declspec(dllimport)
    #endif
#else
    // 非 Windows 平台
    #define HXCPLAYER_SDK_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 句柄类型 ====================
typedef struct HXCPlayerSDK HXCPlayerSDK;

// ==================== 枚举类型 ====================

// 播放器状态
typedef enum {
    HXC_STATE_IDLE = 0,
    HXC_STATE_LOADING,
    HXC_STATE_PLAYING,
    HXC_STATE_PAUSED,
    HXC_STATE_STOPPED,
    HXC_STATE_ERROR
} HXCPlayerState;

// 渲染器类型
typedef enum {
    HXC_RENDERER_AUTO = 0,      // 自动选择（优先 D3D11）
    HXC_RENDERER_D3D11,         // Direct3D 11
    HXC_RENDERER_OPENGL         // OpenGL 3.3+
} HXCRendererType;

// 显示模式
typedef enum {
    HXC_ASPECT_RATIO_FIT = 0,   // 适应（保持比例，完整显示）
    HXC_ASPECT_RATIO_FILL,      // 填充（保持比例，裁剪显示）
    HXC_ASPECT_RATIO_STRETCH    // 拉伸（不保持比例，填满窗口）
} HXCAspectRatioMode;

// ==================== 回调函数类型 ====================

// 状态变化回调
typedef void (*HXCStateCallback)(HXCPlayerState state, void* user_data);

// 错误回调
typedef void (*HXCErrorCallback)(int error_code, const char* message, void* user_data);

// 进度回调
typedef void (*HXCPositionCallback)(double position_sec, void* user_data);

// 缓冲进度回调
typedef void (*HXCBufferCallback)(double cached_sec, double total_sec, void* user_data);

// 播放完成回调
typedef void (*HXCCompletionCallback)(void* user_data);

// 加载中回调
typedef void (*HXCLoadingCallback)(bool is_loading, void* user_data);

// ==================== 核心 API ====================

/**
 * @brief 创建播放器实例
 * @return 播放器句柄，失败返回 NULL
 */
HXCPLAYER_SDK_API HXCPlayerSDK* hxc_player_create(void);

/**
 * @brief 销毁播放器实例
 * @param player 播放器句柄
 */
HXCPLAYER_SDK_API void hxc_player_destroy(HXCPlayerSDK* player);

/**
 * @brief 设置渲染窗口
 * @param player 播放器句柄
 * @param window_handle 窗口句柄 (HWND*)
 * @param renderer_type 渲染器类型
 * @return 0=成功，-1=失败
 */
HXCPLAYER_SDK_API int hxc_player_set_window(HXCPlayerSDK* player, void* window_handle, HXCRendererType renderer_type);

/**
 * @brief 窗口大小改变通知
 * @param player 播放器句柄
 * @param width 新宽度
 * @param height 新高度
 */
HXCPLAYER_SDK_API void hxc_player_on_resize(HXCPlayerSDK* player, int width, int height);

// ==================== 播放控制 ====================

/**
 * @brief 打开视频文件或网络地址
 * @param player 播放器句柄
 * @param url 文件路径或网络地址
 * @param start_time 起始播放时间（秒），0=从头播放
 * @return 0=成功，-1=失败
 */
HXCPLAYER_SDK_API int hxc_player_open(HXCPlayerSDK* player, const char* url, double start_time);

typedef struct {
    const char* url;
    const char* auth_token;
    const char* video_id;
    const char* device_id;
    const char* secret_id;
    const char* nonce;
    const char* play_session_id;
    const char* secure_headers;
    int64_t session_expire_at_ms;
    int key_mode; // 0=远端 key URI, 1=内联 key
    const char* key_material_b64;
    const char* key_iv_hex;
    double start_time;
} HXCSecureHLSOpenParams;

HXCPLAYER_SDK_API int hxc_player_open_secure_hls(HXCPlayerSDK* player, const HXCSecureHLSOpenParams* params);

/**
 * @brief 播放
 */
HXCPLAYER_SDK_API void hxc_player_play(HXCPlayerSDK* player);

/**
 * @brief 暂停
 */
HXCPLAYER_SDK_API void hxc_player_pause(HXCPlayerSDK* player);

/**
 * @brief 停止
 */
HXCPLAYER_SDK_API void hxc_player_stop(HXCPlayerSDK* player);

/**
 * @brief 跳转到指定位置
 * @param position_sec 目标位置（秒）
 */
HXCPLAYER_SDK_API void hxc_player_seek(HXCPlayerSDK* player, double position_sec);

// ==================== 状态查询 ====================

/**
 * @brief 获取当前状态
 */
HXCPLAYER_SDK_API HXCPlayerState hxc_player_get_state(HXCPlayerSDK* player);

/**
 * @brief 获取总时长（秒）
 */
HXCPLAYER_SDK_API double hxc_player_get_duration(HXCPlayerSDK* player);

/**
 * @brief 获取当前播放位置（秒）
 */
HXCPLAYER_SDK_API double hxc_player_get_position(HXCPlayerSDK* player);

/**
 * @brief 获取缓冲进度（秒）
 */
HXCPLAYER_SDK_API double hxc_player_get_cached_duration(HXCPlayerSDK* player);

/**
 * @brief 获取视频宽度
 */
HXCPLAYER_SDK_API int hxc_player_get_video_width(HXCPlayerSDK* player);

/**
 * @brief 获取视频高度
 */
HXCPLAYER_SDK_API int hxc_player_get_video_height(HXCPlayerSDK* player);

/**
 * @brief 获取当前渲染器名称
 * @return 渲染器名称（"D3D11", "OpenGL", "None"）
 */
HXCPLAYER_SDK_API const char* hxc_player_get_renderer_name(HXCPlayerSDK* player);

// ==================== 音量和倍速 ====================

/**
 * @brief 设置音量
 * @param volume 音量值 [0.0, 1.0]
 */
HXCPLAYER_SDK_API void hxc_player_set_volume(HXCPlayerSDK* player, float volume);

/**
 * @brief 获取音量
 */
HXCPLAYER_SDK_API float hxc_player_get_volume(HXCPlayerSDK* player);

/**
 * @brief 设置播放速度
 * @param rate 速度倍率，1.0=正常，0.5=慢速，2.0=快速
 * @return 0=成功，-1=失败（例如未启用 SoundTouch）
 */
HXCPLAYER_SDK_API int hxc_player_set_playback_rate(HXCPlayerSDK* player, float rate);

/**
 * @brief 获取播放速度
 */
HXCPLAYER_SDK_API float hxc_player_get_playback_rate(HXCPlayerSDK* player);

// ==================== 显示模式 ====================

/**
 * @brief 设置显示模式
 */
HXCPLAYER_SDK_API void hxc_player_set_aspect_ratio_mode(HXCPlayerSDK* player, HXCAspectRatioMode mode);

/**
 * @brief 获取显示模式
 */
HXCPLAYER_SDK_API HXCAspectRatioMode hxc_player_get_aspect_ratio_mode(HXCPlayerSDK* player);

// ==================== 回调设置 ====================

/**
 * @brief 设置状态变化回调
 */
HXCPLAYER_SDK_API void hxc_player_set_state_callback(HXCPlayerSDK* player, HXCStateCallback callback, void* user_data);

/**
 * @brief 设置错误回调
 */
HXCPLAYER_SDK_API void hxc_player_set_error_callback(HXCPlayerSDK* player, HXCErrorCallback callback, void* user_data);

/**
 * @brief 设置进度回调
 */
HXCPLAYER_SDK_API void hxc_player_set_position_callback(HXCPlayerSDK* player, HXCPositionCallback callback, void* user_data);

/**
 * @brief 设置缓冲进度回调
 */
HXCPLAYER_SDK_API void hxc_player_set_buffer_callback(HXCPlayerSDK* player, HXCBufferCallback callback, void* user_data);

/**
 * @brief 设置播放完成回调
 */
HXCPLAYER_SDK_API void hxc_player_set_completion_callback(HXCPlayerSDK* player, HXCCompletionCallback callback, void* user_data);

/**
 * @brief 设置加载中回调
 */
HXCPLAYER_SDK_API void hxc_player_set_loading_callback(HXCPlayerSDK* player, HXCLoadingCallback callback, void* user_data);

// ==================== 工具函数 ====================

/**
 * @brief 检查渲染器是否可用
 * @param renderer_type 渲染器类型
 * @return 1=可用，0=不可用
 */
HXCPLAYER_SDK_API int hxc_renderer_is_available(HXCRendererType renderer_type);

/**
 * @brief 获取 SDK 版本
 * @return 版本字符串，例如 "1.0.0"
 */
HXCPLAYER_SDK_API const char* hxc_player_get_version(void);

#ifdef __cplusplus
}
#endif

#endif // HXCPLAYER_SDK_C_API_H
