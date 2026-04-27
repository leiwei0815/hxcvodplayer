/**
 * @file hxcplayer_sdk_c_api.cpp
 * @brief HXCPlayer Windows SDK C API 实现
 */

#include "hxcplayer_sdk_c_api.h"
#include "windows_player_view.h"
#include "renderers/renderer_factory.h"
#include "hxc_player_core.h"
#include "hxc_player_core_c_bridge.h"
#include "hxc_logger.h"
#include <string>

// SDK 句柄结构
struct HXCPlayerSDK {
    PlayerCoreHandle* core_handle;  // Core 播放器句柄
    hxcplayer::windows::HXCWindowsPlayerView* player_view;  // 渲染器管理
    
    HXCPlayerSDK() 
        : core_handle(nullptr)
        , player_view(nullptr) {
    }
    
    ~HXCPlayerSDK() {
        if (player_view) {
            delete player_view;
            player_view = nullptr;
        }
        if (core_handle) {
            player_core_destroy(core_handle);
            core_handle = nullptr;
        }
    }
};

// ==================== 核心 API ====================

HXCPlayerSDK* hxc_player_create(void) {
    HXCPlayerSDK* player = new HXCPlayerSDK();
    
    // 创建 Core 播放器
    player->core_handle = player_core_create();
    if (!player->core_handle) {
        LOG_ERROR("hxc_player_create: 创建 Core 播放器失败");
        delete player;
        return nullptr;
    }
    
    // 获取 PlayerCore* 指针
    hxcplayer::PlayerCore* core = get_player_core_from_handle(player->core_handle);
    if (!core) {
        LOG_ERROR("hxc_player_create: 无法获取 PlayerCore 指针");
        player_core_destroy(player->core_handle);
        delete player;
        return nullptr;
    }
    
    // 创建渲染器管理（暂时不设置窗口）
    player->player_view = new hxcplayer::windows::HXCWindowsPlayerView(core);
    
    LOG_INFO("hxc_player_create: SDK 播放器创建成功");
    return player;
}

void hxc_player_destroy(HXCPlayerSDK* player) {
    if (player) {
        LOG_INFO("hxc_player_destroy: 销毁 SDK 播放器");
        delete player;
    }
}

int hxc_player_set_window(HXCPlayerSDK* player, void* window_handle, HXCRendererType renderer_type) {
    if (!player || !player->player_view || !window_handle) {
        LOG_ERROR("hxc_player_set_window: 无效参数");
        return -1;
    }
    
    // 转换渲染器类型
    hxcplayer::windows::HXCRendererType type;
    switch (renderer_type) {
        case HXC_RENDERER_D3D11:
            type = hxcplayer::windows::HXCRendererType::D3D11;
            break;
        case HXC_RENDERER_OPENGL:
            type = hxcplayer::windows::HXCRendererType::OpenGL;
            break;
        case HXC_RENDERER_AUTO:
        default:
            type = hxcplayer::windows::HXCRendererType::Auto;
            break;
    }
    
    // 设置窗口
    bool success = player->player_view->SetWindow(window_handle, type);
    if (success) {
        LOG_INFO("hxc_player_set_window: 渲染窗口设置成功");
        return 0;
    } else {
        LOG_ERROR("hxc_player_set_window: 渲染窗口设置失败");
        return -1;
    }
}

void hxc_player_on_resize(HXCPlayerSDK* player, int width, int height) {
    if (player && player->player_view) {
        player->player_view->OnWindowResize(width, height);
    }
}

// ==================== 播放控制 ====================

int hxc_player_open(HXCPlayerSDK* player, const char* url, double start_time) {
    if (!player || !player->core_handle || !url) {
        return -1;
    }
    if (start_time > 0.0) {
        return player_core_open_with_start_position(player->core_handle, url, start_time);
    } else {
        return player_core_open(player->core_handle, url);
    }
}

int hxc_player_open_secure_hls(HXCPlayerSDK* player, const HXCSecureHLSOpenParams* params) {
    if (!player || !player->core_handle || !params || !params->url) {
        return -1;
    }
    PlayerSecureHLSConfigC cfg{};
    cfg.url = params->url;
    cfg.auth_token = params->auth_token;
    cfg.video_id = params->video_id;
    cfg.device_id = params->device_id;
    cfg.app_id = params->app_id;
    cfg.nonce = params->nonce;
    cfg.play_session_id = params->play_session_id;
    cfg.secure_headers = params->secure_headers;
    cfg.session_expire_at_ms = params->session_expire_at_ms;
    cfg.key_mode = params->key_mode;
    cfg.key_material_b64 = params->key_material_b64;
    cfg.key_iv_hex = params->key_iv_hex;
    cfg.start_position = params->start_time;
    return player_core_open_secure_hls(player->core_handle, &cfg);
}

void hxc_player_play(HXCPlayerSDK* player) {
    if (player && player->core_handle) {
        player_core_play(player->core_handle);
    }
}

void hxc_player_pause(HXCPlayerSDK* player) {
    if (player && player->core_handle) {
        player_core_pause(player->core_handle);
    }
}

void hxc_player_stop(HXCPlayerSDK* player) {
    if (player && player->core_handle) {
        player_core_stop(player->core_handle);
    }
}

void hxc_player_seek(HXCPlayerSDK* player, double position_sec) {
    if (player && player->core_handle) {
        player_core_seek(player->core_handle, position_sec);
    }
}

// ==================== 状态查询 ====================

HXCPlayerState hxc_player_get_state(HXCPlayerSDK* player) {
    if (!player || !player->core_handle) {
        return HXC_STATE_IDLE;
    }
    return static_cast<HXCPlayerState>(player_core_get_state(player->core_handle));
}

double hxc_player_get_duration(HXCPlayerSDK* player) {
    if (!player || !player->core_handle) {
        return 0.0;
    }
    return player_core_get_duration(player->core_handle);
}

double hxc_player_get_position(HXCPlayerSDK* player) {
    if (!player || !player->core_handle) {
        return 0.0;
    }
    return player_core_get_position(player->core_handle);
}

double hxc_player_get_cached_duration(HXCPlayerSDK* player) {
    if (!player || !player->core_handle) {
        return 0.0;
    }
    // TODO: Core C API 尚未提供此函数
    return 0.0;
}

int hxc_player_get_video_width(HXCPlayerSDK* player) {
    if (!player || !player->core_handle) {
        return 0;
    }
    return player_core_get_video_width(player->core_handle);
}

int hxc_player_get_video_height(HXCPlayerSDK* player) {
    if (!player || !player->core_handle) {
        return 0;
    }
    return player_core_get_video_height(player->core_handle);
}

const char* hxc_player_get_renderer_name(HXCPlayerSDK* player) {
    if (!player || !player->player_view) {
        return "None";
    }
    // TODO: HXCWindowsPlayerView 暂时没有 GetRendererName 方法
    // 先返回固定值
    return "D3D11";
}

// ==================== 音量和倍速 ====================

void hxc_player_set_volume(HXCPlayerSDK* player, float volume) {
    if (player && player->core_handle) {
        player_core_set_volume(player->core_handle, volume);
    }
}

float hxc_player_get_volume(HXCPlayerSDK* player) {
    if (!player || !player->core_handle) {
        return 1.0f;
    }
    // TODO: Core C API 尚未提供此函数
    return 1.0f;
}

int hxc_player_set_playback_rate(HXCPlayerSDK* player, float rate) {
    if (!player || !player->core_handle) {
        return -1;
    }
    player_core_set_playback_rate(player->core_handle, rate);
    return 0;
}

float hxc_player_get_playback_rate(HXCPlayerSDK* player) {
    if (!player || !player->core_handle) {
        return 1.0f;
    }
    return player_core_get_playback_rate(player->core_handle);
}

// ==================== 显示模式 ====================

void hxc_player_set_aspect_ratio_mode(HXCPlayerSDK* player, HXCAspectRatioMode mode) {
    if (player && player->core_handle) {
        player_core_set_aspect_ratio_mode(player->core_handle, static_cast<AspectRatioModeC>(mode));
    }
    
    // 同时更新渲染器
    if (player && player->player_view) {
        hxcplayer::windows::AspectRatioMode renderer_mode;
        switch (mode) {
            case HXC_ASPECT_RATIO_FILL:
                renderer_mode = hxcplayer::windows::AspectRatioMode::Fill;
                break;
            case HXC_ASPECT_RATIO_STRETCH:
                renderer_mode = hxcplayer::windows::AspectRatioMode::Stretch;
                break;
            case HXC_ASPECT_RATIO_FIT:
            default:
                renderer_mode = hxcplayer::windows::AspectRatioMode::Fit;
                break;
        }
        player->player_view->SetAspectRatioMode(renderer_mode);
    }
}

HXCAspectRatioMode hxc_player_get_aspect_ratio_mode(HXCPlayerSDK* player) {
    if (!player || !player->core_handle) {
        return HXC_ASPECT_RATIO_FIT;
    }
    return static_cast<HXCAspectRatioMode>(player_core_get_aspect_ratio_mode(player->core_handle));
}

// ==================== 回调设置 ====================

void hxc_player_set_state_callback(HXCPlayerSDK* player, HXCStateCallback callback, void* user_data) {
    if (player && player->core_handle) {
        player_core_set_state_changed_callback(
            player->core_handle,
            reinterpret_cast<StateChangedCallbackC>(callback),
            user_data
        );
    }
}

void hxc_player_set_error_callback(HXCPlayerSDK* player, HXCErrorCallback callback, void* user_data) {
    if (player && player->core_handle) {
        player_core_set_error_callback(
            player->core_handle,
            reinterpret_cast<ErrorCallbackC>(callback),
            user_data
        );
    }
}

void hxc_player_set_position_callback(HXCPlayerSDK* player, HXCPositionCallback callback, void* user_data) {
    if (player && player->core_handle) {
        player_core_set_position_changed_callback(
            player->core_handle,
            reinterpret_cast<PositionChangedCallbackC>(callback),
            user_data
        );
    }
}

void hxc_player_set_buffer_callback(HXCPlayerSDK* player, HXCBufferCallback callback, void* user_data) {
    if (player && player->core_handle) {
        player_core_set_buffer_progress_callback(
            player->core_handle,
            reinterpret_cast<BufferProgressCallbackC>(callback),
            user_data
        );
    }
}

void hxc_player_set_completion_callback(HXCPlayerSDK* player, HXCCompletionCallback callback, void* user_data) {
    if (player && player->core_handle) {
        player_core_set_playback_completed_callback(
            player->core_handle,
            reinterpret_cast<PlaybackCompletedCallbackC>(callback),
            user_data
        );
    }
}

void hxc_player_set_loading_callback(HXCPlayerSDK* player, HXCLoadingCallback callback, void* user_data) {
    if (player && player->core_handle) {
        player_core_set_loading_callback(
            player->core_handle,
            reinterpret_cast<LoadingCallbackC>(callback),
            user_data
        );
    }
}

// ==================== 工具函数 ====================

int hxc_renderer_is_available(HXCRendererType renderer_type) {
    hxcplayer::windows::HXCRendererType type;
    switch (renderer_type) {
        case HXC_RENDERER_D3D11:
            type = hxcplayer::windows::HXCRendererType::D3D11;
            break;
        case HXC_RENDERER_OPENGL:
            type = hxcplayer::windows::HXCRendererType::OpenGL;
            break;
        default:
            return 0;
    }
    return hxcplayer::windows::HXCRendererFactory::IsRendererAvailable(type) ? 1 : 0;
}

const char* hxc_player_get_version(void) {
    return "1.0.0";
}
