/**
 * @file hxc_player_monitor.h
 * @brief 播放监控事件模型与错误映射（core 层，不负责 HTTP 上报）
 */

#ifndef HXC_PLAYER_MONITOR_H
#define HXC_PLAYER_MONITOR_H

#include <cstdint>
#include <functional>
#include <string>

namespace hxcplayer {

/// 监控事件类型（跨端统一字符串值）
enum class MonitorEventType {
    PlaySessionStart = 0,
    OpenBegin,
    OpenSuccess,
    OpenFail,
    StateChange,
    FirstFrame,
    PositionHeartbeat,
    SeekBegin,
    SeekComplete,
    SeekFail,
    LoadingBegin,
    LoadingEnd,
    NetworkChange,
    FfmpegIoEvent,
    DecodeError,
    RenderError,
    AudioError,
    PlayComplete,
    PlaySessionEnd,

    // 用户操作事件（100+，终端层 emit，core 不感知）
    UserPlay = 100,
    UserPause = 101,
    UserSeek = 102,
    UserStop = 103,
    UserRateChange = 104,

    // Apple 平台生命周期（105+，终端层 emit）
    AppEnterBackground = 105,
    AppEnterForeground = 106,
    BgAutoPause = 107,
    FgAutoResume = 108,
    BgReleaseHwDecoder = 109,

    // PiP（110+）
    PipWillStart = 110,
    PipDidStart = 111,
    PipWillStop = 112,
    PipDidStop = 113,
    PipStartFail = 114,
    PipRestoreUi = 115,
    PipUserStart = 116,

    // AudioQueue（120+）
    AudioQueueStart = 120,
    AudioQueuePause = 121,
    AudioQueueStop = 122,
    AudioQueueStartFail = 123,
    AudioQueueEnqueueFail = 124,
    AudioSilence = 125,

    // 视频渲染（130+）
    VideoRenderStall = 130,
    VideoFrameDrop = 131,

    // 音频会话 / 路由（132+）
    AudioInterruptionBegan = 132,
    AudioInterruptionEnded = 133,
    AudioRouteChange = 134,
    AudioOutputRecover = 135,
    AudioMediaServicesLost = 136,
    AudioMediaServicesReset = 137,

    /// 实际解码路径（138，core emit：open / 硬解回退 / 回前台重建）
    DecodePathResolved = 138,

    /// HTTP 302 等重定向解析结果（139，core emit：open_common_process 内、open_begin 前）
    OpenUrlResolved = 139,

    /// 流程轨迹（150，core/终端 emit：无里程碑语义的路径记录）
    Trace = 150,

    /// 网络快照（160，周期性：吞吐/缓冲/bitrate 采样）
    NetworkSnapshot = 160,

    /// 弱网信号（162，规则触发，30s debounce）
    NetworkWeakSignal = 162,
};

/// 错误域
enum class MonitorErrorDomain {
    Player = 0,
    Network,
    Http,
    License,
    Secure,
    Render,
    Audio,
    Monitor,
    Ffmpeg,
};

/// 错误分类
enum class MonitorErrorCategory {
    None = 0,
    Recoverable,
    Fatal,
    UserCancel,
};

/// 播放会话结束原因
enum class MonitorSessionEndReason {
    Completed = 0,
    Failed,
    Stopped,
    Replaced,
    Released,
};

/// 监控错误信息（稳定错误码 + FFmpeg 原始信息）
struct MonitorErrorInfo {
    int code = 0;
    MonitorErrorDomain domain = MonitorErrorDomain::Player;
    MonitorErrorCategory category = MonitorErrorCategory::None;
    std::string message;
    int ffmpeg_code = 0;
    std::string ffmpeg_message;
    bool recoverable = false;
    int http_status = 0;
};

/// core 层投递的轻量监控事件（平台层再补全用户/终端上下文）
struct MonitorEvent {
    MonitorEventType type = MonitorEventType::PlaySessionStart;
    int64_t timestamp_ms = 0;
    double position = 0.0;
    double duration = 0.0;
    double start_position = 0.0;
    double seek_target = 0.0;
    double seek_landing = 0.0;
    int64_t cost_ms = 0;
    int64_t stall_ms = 0;
    int64_t total_stall_ms = 0;
    double buffer_ahead_sec = -1.0;
    int reconnect_count = 0;
    bool loading = false;
    bool encrypted = false;
    int data_source_mode = 0;
    std::string url;
    std::string detail;          // 自由文本补充（state name / io stage 等）
    std::string message;         // trace 必填；里程碑可由平台层推导
    std::string trace_point;     // trace 稳定路径 ID，如 core.open.mode.custom_http
    std::string phase;           // trace 阶段：open/decode/seek/buffer/io 等
    MonitorErrorInfo error;
    MonitorSessionEndReason end_reason = MonitorSessionEndReason::Stopped;
};

using MonitorEventCallback = std::function<void(const MonitorEvent&)>;

/// 事件类型转稳定字符串（上报协议使用）
const char* monitor_event_type_name(MonitorEventType type);

/// 错误域转字符串
const char* monitor_error_domain_name(MonitorErrorDomain domain);

/// 错误分类转字符串
const char* monitor_error_category_name(MonitorErrorCategory category);

/// 会话结束原因转字符串
const char* monitor_session_end_reason_name(MonitorSessionEndReason reason);

/// 当前毫秒时间戳
int64_t monitor_now_ms();

/**
 * 将 FFmpeg/系统错误映射为稳定业务错误码与 MonitorErrorInfo。
 * @param ffmpeg_or_player_code 可能是 PlayerErrorCode 或 AVERROR_*
 * @param message 上层描述
 * @param http_status 已知 HTTP 状态（0 表示未知）
 */
MonitorErrorInfo map_monitor_error(int ffmpeg_or_player_code,
                                   const std::string& message,
                                   int http_status = 0);

/// 按稳定错误码判断是否可恢复
bool monitor_error_is_recoverable(int error_code);

/// 跨模块错误上报（AudioResampler 等），由 PlayerCore 注册后转发至 monitor socket。
using MonitorErrorReportCallback = std::function<void(int error_code,
                                                      const std::string& message,
                                                      const std::string& detail,
                                                      bool recoverable)>;

void set_monitor_error_report_callback(MonitorErrorReportCallback callback);

/// 上报错误：已注册 callback 时走 monitor；否则仅写本地 LOG_ERROR。
void report_monitor_error(int error_code,
                          const std::string& message,
                          const std::string& detail = "",
                          bool recoverable = false);

}  // namespace hxcplayer

#endif  // HXC_PLAYER_MONITOR_H
