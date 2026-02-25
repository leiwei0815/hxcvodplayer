/**
 * @file sdl_renderer.h
 * @brief SDL 渲染器（可选实现）
 */

#ifndef SDL_RENDERER_H
#define SDL_RENDERER_H

#include "yx_platform_interface.h"

extern "C" {
#include <SDL2/SDL.h>
}

namespace yxplayer {

/**
 * @brief SDL2 视频渲染器
 */
class SDLRenderer : public IVideoRenderer {
public:
    SDLRenderer();
    ~SDLRenderer() override;
    
    bool init(int width, int height, PixelFormat format) override;
    bool render_frame(const VideoFrame* frame) override;
    void resize(int width, int height) override;
    void clear() override;
    void destroy() override;
    
    SDL_Window* get_window() const { return window_; }
    SDL_Renderer* get_renderer() const { return renderer_; }

private:
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    SDL_Texture* texture_;
    int width_;
    int height_;
    PixelFormat format_;
};

} // namespace yxplayer

#endif // SDL_RENDERER_H
