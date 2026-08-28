/**
 * @file hxc_player_monitor.cpp
 */

#include "hxc_player_monitor.h"
#include "hxc_player_core.h"
#include "hxc_logger.h"

#include <chrono>
#include <mutex>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <algorithm>

extern "C" {
#include <libavutil/error.h>
}

#ifndef AVERROR_PATCHWELCOME
#define AVERROR_PATCHWELCOME (-MKTAG('P','A','W','E'))
#endif

namespace hxcplayer {

namespace {

std::mutex g_monitor_error_report_mutex;
MonitorErrorReportCallback g_monitor_error_report_callback;

}  // namespace

void set_monitor_error_report_callback(MonitorErrorReportCallback callback) {
    std::lock_guard<std::mutex> lock(g_monitor_error_report_mutex);
    g_monitor_error_report_callback = std::move(callback);
}

void report_monitor_error(int error_code,
                          const std::string& message,
                          const std::string& detail,
                          bool recoverable) {
    MonitorErrorReportCallback reporter;
    {
        std::lock_guard<std::mutex> lock(g_monitor_error_report_mutex);
        reporter = g_monitor_error_report_callback;
    }
    if (reporter) {
        reporter(error_code, message, detail, recoverable);
        return;
    }
    LOG_ERROR("[MonitorReport] code=", error_code, " ", message,
              detail.empty() ? "" : " | ", detail);
}

const char* monitor_event_type_name(MonitorEventType type) {
    switch (type) {
        case MonitorEventType::PlaySessionStart: return "play_session_start";
        case MonitorEventType::OpenBegin: return "open_begin";
        case MonitorEventType::OpenSuccess: return "open_success";
        case MonitorEventType::OpenFail: return "open_fail";
        case MonitorEventType::StateChange: return "state_change";
        case MonitorEventType::FirstFrame: return "first_frame";
        case MonitorEventType::PositionHeartbeat: return "position_heartbeat";
        case MonitorEventType::SeekBegin: return "seek_begin";
        case MonitorEventType::SeekComplete: return "seek_complete";
        case MonitorEventType::SeekFail: return "seek_fail";
        case MonitorEventType::LoadingBegin: return "loading_begin";
        case MonitorEventType::LoadingEnd: return "loading_end";
        case MonitorEventType::NetworkChange: return "network_change";
        case MonitorEventType::FfmpegIoEvent: return "ffmpeg_io_event";
        case MonitorEventType::DecodeError: return "decode_error";
        case MonitorEventType::RenderError: return "render_error";
        case MonitorEventType::AudioError: return "audio_error";
        case MonitorEventType::PlayComplete: return "play_complete";
        case MonitorEventType::PlaySessionEnd: return "play_session_end";
        case MonitorEventType::UserPlay: return "user_play";
        case MonitorEventType::UserPause: return "user_pause";
        case MonitorEventType::UserSeek: return "user_seek";
        case MonitorEventType::UserStop: return "user_stop";
        case MonitorEventType::UserRateChange: return "user_rate_change";
        case MonitorEventType::AppEnterBackground: return "app_enter_background";
        case MonitorEventType::AppEnterForeground: return "app_enter_foreground";
        case MonitorEventType::BgAutoPause: return "bg_auto_pause";
        case MonitorEventType::FgAutoResume: return "fg_auto_resume";
        case MonitorEventType::BgReleaseHwDecoder: return "bg_release_hw_decoder";
        case MonitorEventType::PipWillStart: return "pip_will_start";
        case MonitorEventType::PipDidStart: return "pip_did_start";
        case MonitorEventType::PipWillStop: return "pip_will_stop";
        case MonitorEventType::PipDidStop: return "pip_did_stop";
        case MonitorEventType::PipStartFail: return "pip_start_fail";
        case MonitorEventType::PipRestoreUi: return "pip_restore_ui";
        case MonitorEventType::PipUserStart: return "pip_user_start";
        case MonitorEventType::AudioQueueStart: return "audio_queue_start";
        case MonitorEventType::AudioQueuePause: return "audio_queue_pause";
        case MonitorEventType::AudioQueueStop: return "audio_queue_stop";
        case MonitorEventType::AudioQueueStartFail: return "audio_queue_start_fail";
        case MonitorEventType::AudioQueueEnqueueFail: return "audio_queue_enqueue_fail";
        case MonitorEventType::AudioSilence: return "audio_silence";
        case MonitorEventType::VideoRenderStall: return "video_render_stall";
        case MonitorEventType::VideoFrameDrop: return "video_frame_drop";
        case MonitorEventType::AudioInterruptionBegan: return "audio_interruption_began";
        case MonitorEventType::AudioInterruptionEnded: return "audio_interruption_ended";
        case MonitorEventType::AudioRouteChange: return "audio_route_change";
        case MonitorEventType::AudioOutputRecover: return "audio_output_recover";
        case MonitorEventType::AudioMediaServicesLost: return "audio_media_services_lost";
        case MonitorEventType::AudioMediaServicesReset: return "audio_media_services_reset";
        case MonitorEventType::DecodePathResolved: return "decode_path_resolved";
        case MonitorEventType::OpenUrlResolved: return "open_url_resolved";
        case MonitorEventType::Trace: return "trace";
        case MonitorEventType::NetworkSnapshot: return "network_snapshot";
        case MonitorEventType::NetworkWeakSignal: return "network_weak_signal";
    }
    return "unknown";
}

const char* monitor_error_domain_name(MonitorErrorDomain domain) {
    switch (domain) {
        case MonitorErrorDomain::Player: return "player";
        case MonitorErrorDomain::Network: return "network";
        case MonitorErrorDomain::Http: return "http";
        case MonitorErrorDomain::License: return "license";
        case MonitorErrorDomain::Secure: return "secure";
        case MonitorErrorDomain::Render: return "render";
        case MonitorErrorDomain::Audio: return "audio";
        case MonitorErrorDomain::Monitor: return "monitor";
        case MonitorErrorDomain::Ffmpeg: return "ffmpeg";
    }
    return "player";
}

const char* monitor_error_category_name(MonitorErrorCategory category) {
    switch (category) {
        case MonitorErrorCategory::None: return "none";
        case MonitorErrorCategory::Recoverable: return "recoverable";
        case MonitorErrorCategory::Fatal: return "fatal";
        case MonitorErrorCategory::UserCancel: return "user_cancel";
    }
    return "none";
}

const char* monitor_session_end_reason_name(MonitorSessionEndReason reason) {
    switch (reason) {
        case MonitorSessionEndReason::Completed: return "completed";
        case MonitorSessionEndReason::Failed: return "failed";
        case MonitorSessionEndReason::Stopped: return "stopped";
        case MonitorSessionEndReason::Replaced: return "replaced";
        case MonitorSessionEndReason::Released: return "released";
    }
    return "stopped";
}

int64_t monitor_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool monitor_error_is_recoverable(int error_code) {
    switch (error_code) {
        case ERROR_OPEN_INPUT_FAILED:
        case ERROR_READ_FRAME_FAILED:
        case ERROR_DECODE_FAILED:
        case ERROR_NET_CONNECTION_TIMEOUT:
        case ERROR_NET_CONNECTION_REFUSED:
        case ERROR_NET_UNREACHABLE:
        case ERROR_NET_DNS_FAILED:
        case ERROR_NET_TLS_FAILED:
        case ERROR_NET_READ_TIMEOUT:
        case ERROR_NET_CONNECTION_LOST:
        case ERROR_NET_RECONNECT_FAILED:
        case ERROR_HTTP_SERVER_ERROR:
            return true;
        default:
            return false;
    }
}

static MonitorErrorDomain domain_for_code(int code) {
    if (code <= -9001 && code >= -9999) return MonitorErrorDomain::Monitor;
    if (code <= -5101 && code >= -5199) return MonitorErrorDomain::Audio;
    if (code <= -5001 && code >= -5099) return MonitorErrorDomain::Render;
    if (code <= -4101 && code >= -4199) return MonitorErrorDomain::Secure;
    if (code <= -4001 && code >= -4099) return MonitorErrorDomain::License;
    if (code <= -3001 && code >= -3999) return MonitorErrorDomain::Http;
    if (code <= -2001 && code >= -2999) return MonitorErrorDomain::Network;
    if (code <= -1001 && code >= -1999) return MonitorErrorDomain::Player;
    return MonitorErrorDomain::Ffmpeg;
}

static bool hxc_contains_ci(const std::string& haystack, const char* needle) {
    if (!needle || !*needle) {
        return false;
    }
    const size_t nlen = std::strlen(needle);
    auto it = std::search(haystack.begin(), haystack.end(), needle, needle + nlen,
                          [](char a, char b) {
                              return std::tolower(static_cast<unsigned char>(a)) ==
                                     std::tolower(static_cast<unsigned char>(b));
                          });
    return it != haystack.end();
}

static bool hxc_message_suggests_dns(const std::string& text) {
    return hxc_contains_ci(text, "Name or service not known") ||
           hxc_contains_ci(text, "nodename nor servname") ||
           hxc_contains_ci(text, "Temporary failure in name resolution") ||
           hxc_contains_ci(text, "No address associated") ||
           hxc_contains_ci(text, "getaddrinfo") ||
           hxc_contains_ci(text, "Unknown host") ||
           hxc_contains_ci(text, "host not found") ||
           hxc_contains_ci(text, "DNS");
}

static bool hxc_message_suggests_tls(const std::string& text) {
    return hxc_contains_ci(text, "SSL") ||
           hxc_contains_ci(text, "TLS") ||
           hxc_contains_ci(text, "handshake") ||
           hxc_contains_ci(text, "certificate") ||
           hxc_contains_ci(text, "mbedtls");
}

static void hxc_fill_http_status_from_code(MonitorErrorInfo& info) {
    if (info.http_status != 0) {
        return;
    }
    switch (info.code) {
        case ERROR_HTTP_BAD_REQUEST: info.http_status = 400; break;
        case ERROR_HTTP_UNAUTHORIZED: info.http_status = 401; break;
        case ERROR_HTTP_FORBIDDEN: info.http_status = 403; break;
        case ERROR_HTTP_NOT_FOUND: info.http_status = 404; break;
        case ERROR_HTTP_SERVER_ERROR: info.http_status = 500; break;
        default: break;
    }
}

MonitorErrorInfo map_monitor_error(int ffmpeg_or_player_code,
                                   const std::string& message,
                                   int http_status) {
    MonitorErrorInfo info;
    info.message = message;
    info.http_status = http_status;

    char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
    // 稳定业务错误码直接使用
    if ((ffmpeg_or_player_code <= -1001 && ffmpeg_or_player_code >= -1999) ||
        (ffmpeg_or_player_code <= -2001 && ffmpeg_or_player_code >= -2999) ||
        (ffmpeg_or_player_code <= -3001 && ffmpeg_or_player_code >= -3999) ||
        (ffmpeg_or_player_code <= -4001 && ffmpeg_or_player_code >= -4199) ||
        (ffmpeg_or_player_code <= -5001 && ffmpeg_or_player_code >= -5199) ||
        (ffmpeg_or_player_code <= -9001 && ffmpeg_or_player_code >= -9999)) {
        info.code = ffmpeg_or_player_code;
        info.domain = domain_for_code(info.code);
        info.recoverable = monitor_error_is_recoverable(info.code);
        info.category = info.recoverable ? MonitorErrorCategory::Recoverable
                                         : MonitorErrorCategory::Fatal;
        hxc_fill_http_status_from_code(info);
        return info;
    }

    info.ffmpeg_code = ffmpeg_or_player_code;
    av_strerror(ffmpeg_or_player_code, errbuf, sizeof(errbuf));
    info.ffmpeg_message = errbuf;
    const std::string combined = message + " " + info.ffmpeg_message;

    if (ffmpeg_or_player_code == AVERROR_EXIT) {
        info.code = ERROR_NONE;
        info.domain = MonitorErrorDomain::Player;
        info.category = MonitorErrorCategory::UserCancel;
        info.recoverable = false;
        if (info.message.empty()) {
            info.message = "用户取消";
        }
        return info;
    }

    if (http_status == 400 || ffmpeg_or_player_code == AVERROR_HTTP_BAD_REQUEST) {
        info.code = ERROR_HTTP_BAD_REQUEST;
    } else if (http_status == 401 || ffmpeg_or_player_code == AVERROR_HTTP_UNAUTHORIZED) {
        info.code = ERROR_HTTP_UNAUTHORIZED;
    } else if (http_status == 403 || ffmpeg_or_player_code == AVERROR_HTTP_FORBIDDEN) {
        info.code = ERROR_HTTP_FORBIDDEN;
    } else if (http_status == 404 || ffmpeg_or_player_code == AVERROR_HTTP_NOT_FOUND) {
        info.code = ERROR_HTTP_NOT_FOUND;
    } else if (http_status >= 500 || ffmpeg_or_player_code == AVERROR_HTTP_SERVER_ERROR) {
        info.code = ERROR_HTTP_SERVER_ERROR;
    } else if (hxc_message_suggests_dns(combined)) {
        info.code = ERROR_NET_DNS_FAILED;
    } else if (hxc_message_suggests_tls(combined)) {
        info.code = ERROR_NET_TLS_FAILED;
    } else if (ffmpeg_or_player_code == AVERROR(ETIMEDOUT) ||
               ffmpeg_or_player_code == AVERROR(EAGAIN)) {
        info.code = (ffmpeg_or_player_code == AVERROR(EAGAIN))
                        ? ERROR_NET_READ_TIMEOUT
                        : ERROR_NET_CONNECTION_TIMEOUT;
    } else if (ffmpeg_or_player_code == AVERROR(ECONNREFUSED)) {
        info.code = ERROR_NET_CONNECTION_REFUSED;
    } else if (ffmpeg_or_player_code == AVERROR(ENETUNREACH)
#ifdef EHOSTUNREACH
               || ffmpeg_or_player_code == AVERROR(EHOSTUNREACH)
#endif
               ) {
        info.code = ERROR_NET_UNREACHABLE;
    } else if (ffmpeg_or_player_code == AVERROR(EIO)
#ifdef ECONNRESET
               || ffmpeg_or_player_code == AVERROR(ECONNRESET)
#endif
#ifdef EPIPE
               || ffmpeg_or_player_code == AVERROR(EPIPE)
#endif
#ifdef ENETDOWN
               || ffmpeg_or_player_code == AVERROR(ENETDOWN)
#endif
               || ffmpeg_or_player_code == AVERROR_EOF) {
        info.code = ERROR_NET_CONNECTION_LOST;
    } else if (ffmpeg_or_player_code == AVERROR(ENOMEM)) {
        info.code = ERROR_OUT_OF_MEMORY;
    } else if (ffmpeg_or_player_code == AVERROR_INVALIDDATA) {
        info.code = ERROR_INPUT_INVALID_DATA;
    } else if (ffmpeg_or_player_code == AVERROR_PATCHWELCOME) {
        info.code = ERROR_NOT_SUPPORT;
    } else {
        info.code = ERROR_UNKNOWN;
    }

    info.domain = domain_for_code(info.code);
    info.recoverable = monitor_error_is_recoverable(info.code);
    info.category = info.recoverable ? MonitorErrorCategory::Recoverable
                                     : MonitorErrorCategory::Fatal;
    hxc_fill_http_status_from_code(info);
    if (info.message.empty()) {
        info.message = info.ffmpeg_message;
    }
    return info;
}

}  // namespace hxcplayer
