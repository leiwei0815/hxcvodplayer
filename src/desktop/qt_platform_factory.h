/**
 * @file qt_platform_factory.h
 * @brief Qt 平台工厂
 */

#ifndef QT_PLATFORM_FACTORY_H
#define QT_PLATFORM_FACTORY_H

#include "platform_interface.h"

namespace yxplayer {

class QtPlatformFactory : public IPlatformFactory {
public:
    QtPlatformFactory() = default;
    ~QtPlatformFactory() override = default;
    
    IVideoRenderer* create_video_renderer() override;
    IAudioRenderer* create_audio_renderer() override;
    IPlayerUI* create_player_ui() override;
    std::string get_platform_name() const override;
};

} // namespace yxplayer

#endif // QT_PLATFORM_FACTORY_H
