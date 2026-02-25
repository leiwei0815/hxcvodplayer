/**
 * @file sdl_renderer.cpp
 * @brief SDL 渲染器实现
 */

#include "sdl_renderer.h"

extern "C" {
#include <libavutil/imgutils.h>
}

namespace hxcplayer {

SDLRenderer::SDLRenderer()
    : window_(nullptr)
    , renderer_(nullptr)
    , texture_(nullptr)
    , width_(0)
    , height_(0)
    , format_(PixelFormat::YUV420P) {
}

SDLRenderer::~SDLRenderer() {
    destroy();
}

bool SDLRenderer::init(int width, int height, PixelFormat format) {
    width_ = width;
    height_ = height;
    format_ = format;
    
    // 创建窗口
    window_ = SDL_CreateWindow(
        "YXVodPlayer",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        width, height,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
    );
    
    if (!window_) {
        return false;
    }
    
    // 创建渲染器
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }
    
    // 创建纹理
    Uint32 sdl_format = SDL_PIXELFORMAT_IYUV;  // YUV420P
    if (format == PixelFormat::RGB24) {
        sdl_format = SDL_PIXELFORMAT_RGB24;
    } else if (format == PixelFormat::RGBA) {
        sdl_format = SDL_PIXELFORMAT_RGBA8888;
    }
    
    texture_ = SDL_CreateTexture(
        renderer_,
        sdl_format,
        SDL_TEXTUREACCESS_STREAMING,
        width, height
    );
    
    if (!texture_) {
        destroy();
        return false;
    }
    
    return true;
}

bool SDLRenderer::render_frame(const VideoFrame* frame) {
    if (!renderer_ || !texture_ || !frame || !frame->frame) {
        return false;
    }
    
    AVFrame* av_frame = frame->frame;
    
    // 更新纹理
    if (format_ == PixelFormat::YUV420P) {
        SDL_UpdateYUVTexture(
            texture_,
            nullptr,
            av_frame->data[0], av_frame->linesize[0],
            av_frame->data[1], av_frame->linesize[1],
            av_frame->data[2], av_frame->linesize[2]
        );
    } else {
        SDL_UpdateTexture(
            texture_,
            nullptr,
            av_frame->data[0],
            av_frame->linesize[0]
        );
    }
    
    // 清空渲染器
    SDL_RenderClear(renderer_);
    
    // 复制纹理到渲染器
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    
    // 呈现
    SDL_RenderPresent(renderer_);
    
    return true;
}

void SDLRenderer::resize(int width, int height) {
    if (window_) {
        SDL_SetWindowSize(window_, width, height);
    }
}

void SDLRenderer::clear() {
    if (renderer_) {
        SDL_RenderClear(renderer_);
        SDL_RenderPresent(renderer_);
    }
}

void SDLRenderer::destroy() {
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
}

} // namespace hxcplayer
