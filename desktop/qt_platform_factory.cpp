/**
 * @file qt_platform_factory.cpp
 * @brief Qt 平台工厂实现
 */

#include "qt_platform_factory.h"
#include "sdl_renderer.h"

namespace hxcplayer {

IVideoRenderer* QtPlatformFactory::create_video_renderer() {
    return new SDLRenderer();
}

IAudioRenderer* QtPlatformFactory::create_audio_renderer() {
    // Qt 平台使用 SDL2 的音频，不需要单独实现
    return nullptr;
}

IPlayerUI* QtPlatformFactory::create_player_ui() {
    // UI 由 Qt 窗口实现
    return nullptr;
}

std::string QtPlatformFactory::get_platform_name() const {
    return "Qt5/Desktop";
}

} // namespace hxcplayer
