/**
 * @file player_core_c_bridge.h
 * @brief C 接口桥接层（隔离 C++ 和 Objective-C++，避免 FFmpeg/AVFoundation 冲突）
 */

#ifndef YXVODPLAYER_PLAYER_CORE_C_BRIDGE_H
#define YXVODPLAYER_PLAYER_CORE_C_BRIDGE_H

#include <stddef.h>  // for size_t
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// 不透明指针类型（隐藏 C++ 实现细节）
typedef struct PlayerCoreHandle PlayerCoreHandle;

// 播放器状态枚举
typedef enum {
    PLAYER_STATE_IDLE = 0,
    PLAYER_STATE_OPENING = 1,
    PLAYER_STATE_PLAYING = 2,
    PLAYER_STATE_PAUSED = 3,
    PLAYER_STATE_STOPPED = 4,
    PLAYER_STATE_ERROR = -1
} PlayerStateC;

// 流水线状态（参考主流播放器）
typedef enum {
    PLAYER_PIPELINE_STATE_IDLE = 0,
    PLAYER_PIPELINE_STATE_PREPARING = 1,
    PLAYER_PIPELINE_STATE_BUFFERING = 2,
    PLAYER_PIPELINE_STATE_READY = 3,
    PLAYER_PIPELINE_STATE_ENDED = 4,
    PLAYER_PIPELINE_STATE_ERROR = 5
} PlayerPipelineStateC;

// 视频显示模式（宽高比模式）
typedef enum {
    ASPECT_RATIO_FIT = 0,   // 适应模式：等比缩放，保持完整画面，可能有黑边（默认）
    ASPECT_RATIO_FILL = 1   // 填充模式：等比拉伸填充，无黑边，画面会被裁剪
} AspectRatioModeC;

// ⚠️ 播放器错误码定义（与 C++ 层 PlayerErrorCode 保持一致，全部使用负数）
typedef enum {
    // 自定义错误码（负数）
    PLAYER_ERROR_NONE = 0,                              // 无错误
    PLAYER_ERROR_INVALID_URL = -1001,                   // 无效的 URL
    PLAYER_ERROR_OPEN_INPUT_FAILED = -1002,             // 打开输入失败
    PLAYER_ERROR_FIND_STREAM_INFO_FAILED = -1003,       // 查找流信息失败
    PLAYER_ERROR_NO_VIDEO_STREAM = -1004,               // 没有视频流
    PLAYER_ERROR_NO_AUDIO_STREAM = -1005,               // 没有音频流
    PLAYER_ERROR_CODEC_NOT_FOUND = -1006,               // 找不到解码器
    PLAYER_ERROR_CODEC_OPEN_FAILED = -1007,             // 打开解码器失败
    PLAYER_ERROR_ALLOC_CONTEXT_FAILED = -1008,          // 分配上下文失败
    PLAYER_ERROR_SDL_INIT_FAILED = -1009,               // SDL 初始化失败
    PLAYER_ERROR_AUDIO_DEVICE_OPEN_FAILED = -1010,      // 音频设备打开失败
    PLAYER_ERROR_SEEK_FAILED = -1011,                   // Seek 操作失败
    PLAYER_ERROR_READ_FRAME_FAILED = -1012,             // 读取帧失败
    PLAYER_ERROR_DECODE_FAILED = -1013,                 // 解码失败
    PLAYER_ERROR_OUT_OF_MEMORY = -1014,                 // 内存不足
    PLAYER_ERROR_INPUT_INVALID_DATA = -1018,            // 无效数据
    PLAYER_ERROR_NOT_SUPPORT = -1019,                   // 不支持的格式或协议
    PLAYER_ERROR_UNKNOWN = -1099,                       // 未知错误
    
    // 网络相关错误 (-2001 ~ -2999)
    PLAYER_ERROR_NET_CONNECTION_TIMEOUT = -2001,        // 网络连接超时
    PLAYER_ERROR_NET_CONNECTION_REFUSED = -2002,        // 服务器拒绝连接
    PLAYER_ERROR_NET_UNREACHABLE = -2003,               // 网络不可达
    
    // HTTP 相关错误 (-3001 ~ -3999)
    PLAYER_ERROR_HTTP_BAD_REQUEST = -3001,              // HTTP 请求错误（400）
    PLAYER_ERROR_HTTP_NOT_FOUND = -3002,                // HTTP 404 文件不存在
    PLAYER_ERROR_HTTP_SERVER_ERROR = -3003,             // HTTP 服务器错误（5xx）
    PLAYER_ERROR_HTTP_UNAUTHORIZED = -3004,             // 需要身份验证（401）
    PLAYER_ERROR_HTTP_FORBIDDEN = -3005,                // 访问被禁止（403）
    PLAYER_ERROR_SECURE_AUTH_FAILED = -4101,            // SecureHLS 鉴权失败
    PLAYER_ERROR_SECURE_AUTH_EXPIRED = -4102,           // SecureHLS 会话过期
    PLAYER_ERROR_SECURE_KEY_EXPIRED = -4103,            // SecureHLS 密钥过期
    PLAYER_ERROR_SECURE_KEY_INVALID = -4104,            // SecureHLS 密钥非法
    PLAYER_ERROR_SECURE_REPLAY_BLOCKED = -4105,         // SecureHLS 重放被拒绝
    PLAYER_ERROR_SECURE_CLOCK_SKEW = -4106,             // SecureHLS 设备时钟偏移过大
    
    // FFmpeg 错误码范围 (负数)
    // 使用 FFmpeg 原始错误码，例如：
    // AVERROR_EOF = -541478725 (0xDFFFFFE3)
    // AVERROR(ENOMEM) = -12
    // AVERROR(EINVAL) = -22
    // 等等...
    // 可以使用 av_strerror() 将负数错误码转换为错误信息
} PlayerErrorCodeC;

// ⚠️ 数据源模式（与 C++ 层 DataSourceMode 保持一致）
typedef enum {
    PLAYER_DATA_SOURCE_MODE_DEFAULT = 0,      // 默认模式（FFmpeg 直接打开）
    PLAYER_DATA_SOURCE_MODE_CUSTOM_HTTP = 1,  // 自定义 HTTP Range 下载器
    PLAYER_DATA_SOURCE_MODE_CUSTOM_FILE = 2,  // 本地文件自定义读取（支持加密文件头解密）
    PLAYER_DATA_SOURCE_MODE_SECURE_HLS = 3,   // HLS AES-128 鉴权模式
} PlayerDataSourceModeC;

// 解码模式（播放前设置；默认软解）
typedef enum {
    PLAYER_DECODE_MODE_SOFTWARE = 0,
    PLAYER_DECODE_MODE_HARDWARE = 1,
} PlayerDecodeModeC;

// ⚠️ 自定义数据源配置（与 C++ 层 CustomDataSourceConfig 保持一致）
typedef struct {
    int timeout_ms;           // 超时时间（毫秒），默认 30000
    int max_retries;          // 最大重试次数，默认 3
    size_t cache_size;        // 缓存大小（字节），默认 2MB
    size_t avio_buffer_size;  // AVIO 缓冲区大小（字节），默认 64KB
    int encrypted_file;       // 是否为加密文件（0/1），仅解密文件头前 100 字节
    const char* auth_token;   // SecureHLS: 业务 token
    const char* video_id;     // SecureHLS: 视频 ID
    const char* device_id;    // SecureHLS: 设备 ID
    const char* app_id;       // SecureHLS: 应用 ID
    const char* nonce;        // SecureHLS: nonce
    int64_t timestamp_ms;     // SecureHLS: 时间戳（毫秒）
    const char* play_session_id;      // SecureHLS: 已鉴权会话 ID（可选）
    const char* secure_headers;       // SecureHLS: 透传 header 文本
    int64_t session_expire_at_ms;     // SecureHLS: 会话过期时间（毫秒）
    int key_mode;                     // SecureHLS: 0=远端 key URI, 1=内联 key
    const char* key_material_b64;     // SecureHLS: Base64 密钥（16字节）
    const char* key_iv_hex;           // SecureHLS: 可选 IV
} PlayerDataSourceConfigC;

typedef struct {
    const char* url;
    const char* auth_token;
    const char* video_id;
    const char* device_id;
    const char* app_id;
    const char* nonce;
    int64_t timestamp_ms;
    const char* play_session_id;
    const char* secure_headers;
    int64_t session_expire_at_ms;
    int key_mode;
    const char* key_material_b64;
    const char* key_iv_hex;
    double start_position;
} PlayerSecureHLSConfigC;

// 创建/销毁播放器
PlayerCoreHandle* player_core_create(void);
void player_core_destroy(PlayerCoreHandle* handle);

// 播放控制
int player_core_open(PlayerCoreHandle* handle, const char* url);
int player_core_open_with_start_position(PlayerCoreHandle* handle, const char* url, double start_pos);
int player_core_open_with_mode(PlayerCoreHandle* handle, const char* url, PlayerDataSourceModeC mode, const PlayerDataSourceConfigC* config, double start_position);
int player_core_open_secure_hls(PlayerCoreHandle* handle, const PlayerSecureHLSConfigC* config);
void player_core_play(PlayerCoreHandle* handle);
void player_core_pause(PlayerCoreHandle* handle);
void player_core_stop(PlayerCoreHandle* handle);

// 状态查询
PlayerStateC player_core_get_state(PlayerCoreHandle* handle);
PlayerPipelineStateC player_core_get_pipeline_state(PlayerCoreHandle* handle);
int player_core_get_play_when_ready(PlayerCoreHandle* handle); // 1=true, 0=false
int player_core_is_playing(PlayerCoreHandle* handle);          // 1=true, 0=false
double player_core_get_duration(PlayerCoreHandle* handle);
double player_core_get_position(PlayerCoreHandle* handle);
int player_core_is_video_hardware_decoding(PlayerCoreHandle* handle); // 1=硬解, 0=软解/未知

// 控制
void player_core_seek(PlayerCoreHandle* handle, double pos);
void player_core_set_volume(PlayerCoreHandle* handle, float volume);
void player_core_set_playback_rate(PlayerCoreHandle* handle, float rate);
float player_core_get_playback_rate(PlayerCoreHandle* handle);
void player_core_set_play_when_ready(PlayerCoreHandle* handle, int play_when_ready); // 0/1
void player_core_set_decode_mode(PlayerCoreHandle* handle, PlayerDecodeModeC mode);
PlayerDecodeModeC player_core_get_decode_mode(PlayerCoreHandle* handle);

// 视频显示模式
void player_core_set_aspect_ratio_mode(PlayerCoreHandle* handle, AspectRatioModeC mode);
AspectRatioModeC player_core_get_aspect_ratio_mode(PlayerCoreHandle* handle);

// ========== 视频渲染配置 ==========

// 渲染模式
typedef enum {
    RENDER_MODE_AUTO = 0,    // 自动渲染（SDK 内部渲染到窗口）
    RENDER_MODE_MANUAL = 1   // 手动渲染（用户主动获取帧并渲染）
} RenderModeC;

// 设置渲染窗口句柄
// window_handle: Windows HWND, macOS NSView*, Qt QWidget*, MFC CWnd*
void player_core_set_render_window(PlayerCoreHandle* handle, void* window_handle);

// 设置渲染模式（默认 AUTO）
void player_core_set_render_mode(PlayerCoreHandle* handle, RenderModeC mode);

// 获取当前渲染模式
RenderModeC player_core_get_render_mode(PlayerCoreHandle* handle);

// 刷新视频帧到窗口（AUTO 模式下自动调用，MANUAL 模式下用户可主动调用）
// 返回值: 0=成功，-1=失败（无新帧），-2=未设置窗口
int player_core_refresh_video(PlayerCoreHandle* handle);

// ========== Windows D3D11/OpenGL 渲染器 API ==========

#ifdef _WIN32
// 渲染器类型（仅 Windows）
typedef enum {
    HXC_RENDERER_TYPE_AUTO = 0,     // 自动选择（优先 D3D11）
    HXC_RENDERER_TYPE_D3D11,        // Direct3D 11
    HXC_RENDERER_TYPE_OPENGL        // OpenGL 3.3+
} HXCRendererTypeC;

/**
 * @brief 设置渲染窗口（扩展版，支持选择渲染器类型）
 * @param handle 播放器句柄
 * @param window_handle 窗口句柄 (HWND)
 * @param renderer_type 渲染器类型
 * @return 0=成功，-1=失败
 * 
 * @note 仅 Windows 平台可用，macOS/iOS/Android 使用原有 API
 * @note 调用此函数后会自动启动渲染线程，无需手动刷新
 */
int player_core_set_render_window_ex(
    PlayerCoreHandle* handle,
    void* window_handle,
    HXCRendererTypeC renderer_type
);

/**
 * @brief 窗口大小改变回调
 * @param handle 播放器句柄
 * @param width 新宽度
 * @param height 新高度
 * 
 * @note 在窗口 resize 事件中调用，渲染器会自动调整
 */
void player_core_on_window_resize(
    PlayerCoreHandle* handle,
    int width,
    int height
);

/**
 * @brief 获取当前使用的渲染器类型
 * @return 渲染器类型名称（"Direct3D 11", "OpenGL", "None"）
 */
const char* player_core_get_current_renderer(PlayerCoreHandle* handle);

/**
 * @brief 检查指定渲染器是否可用
 * @param type 渲染器类型
 * @return 1=可用，0=不可用
 */
int player_core_is_renderer_available(HXCRendererTypeC type);
#endif // _WIN32

// ========== 视频帧获取（跨平台）==========

// 视频帧获取（用于 MANUAL 模式或 iOS 渲染）
typedef struct {
    void* y_data;
    void* u_data;
    void* v_data;
    int y_linesize;
    int u_linesize;
    int v_linesize;
    int width;
    int height;
    double pts;
} VideoFrameDataC;

int player_core_get_video_frame(PlayerCoreHandle* handle, VideoFrameDataC* frame_data);
void player_core_consume_video_frame(PlayerCoreHandle* handle);

// ========== YUV → RGB 转换便利函数 ==========

/**
 * @brief 获取视频帧并转换为 RGB24 格式（跨平台通用）
 * 
 * 这个函数会自动处理 YUV → RGB 转换，适用于所有平台（Windows/macOS/Linux/Android/iOS）
 * 
 * @param handle 播放器句柄
 * @param rgb_buffer 输出 RGB 数据的缓冲区（用户分配，至少 width * height * 3 字节）
 * @param buffer_size 缓冲区大小（字节）
 * @param width 输出：视频宽度
 * @param height 输出：视频高度
 * @param linesize 输出：RGB 数据每行的字节数（通常是 width * 3）
 * @return 0=成功，-1=失败（无可用帧或缓冲区太小）
 * 
 * @note 调用此函数后，会自动消费当前帧（无需再调用 consume_video_frame）
 * @note RGB 格式：连续的 RGB RGB RGB...，每像素 3 字节
 * 
 * @example
 * unsigned char* rgb_buffer = malloc(1920 * 1080 * 3);
 * int width, height, linesize;
 * if (player_core_get_video_frame_rgb(player, rgb_buffer, 1920*1080*3, &width, &height, &linesize) == 0) {
 *     // 使用 rgb_buffer 绘制图像
 * }
 */
int player_core_get_video_frame_rgb(
    PlayerCoreHandle* handle,
    unsigned char* rgb_buffer,
    int buffer_size,
    int* width,
    int* height,
    int* linesize
);

// 音频数据获取（用于 iOS AudioQueue）
int player_core_get_audio_data(PlayerCoreHandle* handle, unsigned char* buffer, int buffer_size);

// 媒体信息
int player_core_get_audio_sample_rate(PlayerCoreHandle* handle);
int player_core_get_audio_channels(PlayerCoreHandle* handle);
int player_core_get_video_width(PlayerCoreHandle* handle);
int player_core_get_video_height(PlayerCoreHandle* handle);

// 回调函数类型定义
typedef void (*StateChangedCallbackC)(PlayerStateC state, void* user_data);
typedef void (*ErrorCallbackC)(int error_code, const char* error_msg, void* user_data);  // 添加错误码参数
typedef void (*PositionChangedCallbackC)(double position, void* user_data);  // 真实播放位置
typedef void (*BufferProgressCallbackC)(double position, void* user_data);   // 缓冲进度（解码位置）
typedef void (*PlaybackCompletedCallbackC)(void* user_data);                 // 播放完成
typedef void (*LoadingCallbackC)(bool is_loading, void* user_data);          // 网络加载状态
typedef void (*PipelineStateChangedCallbackC)(PlayerPipelineStateC state, void* user_data);
typedef void (*PlayingChangedCallbackC)(int is_playing, void* user_data);    // 1=true,0=false

// 设置回调函数
void player_core_set_state_changed_callback(PlayerCoreHandle* handle, StateChangedCallbackC callback, void* user_data);
void player_core_set_error_callback(PlayerCoreHandle* handle, ErrorCallbackC callback, void* user_data);
void player_core_set_position_changed_callback(PlayerCoreHandle* handle, PositionChangedCallbackC callback, void* user_data);
void player_core_set_buffer_progress_callback(PlayerCoreHandle* handle, BufferProgressCallbackC callback, void* user_data);
void player_core_set_playback_completed_callback(PlayerCoreHandle* handle, PlaybackCompletedCallbackC callback, void* user_data);
void player_core_set_loading_callback(PlayerCoreHandle* handle, LoadingCallbackC callback, void* user_data);
void player_core_set_pipeline_state_changed_callback(PlayerCoreHandle* handle, PipelineStateChangedCallbackC callback, void* user_data);
void player_core_set_playing_changed_callback(PlayerCoreHandle* handle, PlayingChangedCallbackC callback, void* user_data);

// ========== 日志配置 ==========

// 设置日志级别（0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR）
void player_core_set_log_level(int level);

// 获取当前日志级别（返回值与 set_log_level 一致）
int player_core_get_log_level(void);

// 启用文件日志
// log_dir: 日志文件目录（如 "/tmp" 或 Android 的 getExternalFilesDir().getAbsolutePath()）
// prefix: 日志文件前缀（默认 "hxcplayer"）
void player_core_enable_file_logging(const char* log_dir, const char* prefix);

// 禁用文件日志
void player_core_disable_file_logging(void);

// 设置最大日志文件大小（字节），超过后自动轮转
void player_core_set_max_log_file_size(size_t max_size);

// 设置日志保留天数（默认7天）
void player_core_set_log_retention_days(int days);

// 手动清理旧日志文件（返回删除的文件数量）
int player_core_cleanup_old_logs(void);

// 获取当前日志文件路径
const char* player_core_get_current_log_file(void);

// 获取当前文件日志目录（未启用文件日志时返回空字符串）
const char* player_core_get_log_directory(void);

/**
 * 写入与 core 内 LOG_DEBUG / LOG_INFO 等相同的 Logger（含文件异步队列）。
 * @param level 0=DEBUG, 1=INFO, 2=WARNING, 3=ERROR（与 player_core_set_log_level 一致）
 * @param file 源文件路径，可传 __FILE__
 * @param line 行号，可传 __LINE__
 * @param func 函数名，可传 __FUNCTION__ 或 __func__
 * @param utf8_message UTF-8 文本，可为 NULL
 */
void player_core_log_line(int level, const char *file, int line, const char *func, const char *utf8_message);

// ========== Windows SDK 内部辅助函数（不对外使用） ==========
#ifdef _WIN32
#ifdef __cplusplus
}  // 临时结束 extern "C"

// C++ 接口：从 PlayerCoreHandle 获取 PlayerCore* 指针
namespace hxcplayer {
    class PlayerCore;
}
hxcplayer::PlayerCore* get_player_core_from_handle(PlayerCoreHandle* handle);

extern "C" {
#endif // __cplusplus
#endif // _WIN32

#ifdef __cplusplus
}
#endif

#endif // YXVODPLAYER_PLAYER_CORE_C_BRIDGE_H
