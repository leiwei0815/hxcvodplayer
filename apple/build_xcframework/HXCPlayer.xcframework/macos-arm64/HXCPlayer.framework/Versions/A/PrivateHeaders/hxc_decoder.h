/**
 * @file decoder.h
 * @brief 解码器（参照 ffplay 实现）
 */

#ifndef YXVODPLAYER_DECODER_H
#define YXVODPLAYER_DECODER_H

#include "hxc_player_types.h"
#include "hxc_packet_queue.h"
#include <thread>
#include <atomic>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace hxcplayer {

/**
 * @brief 解码器基类
 */
class Decoder {
public:
    Decoder();
    virtual ~Decoder();
    
    // 初始化解码器
    int init(AVCodecContext* codec_ctx, PacketQueue* packet_queue);
    
    // 解码一帧（供外部线程调用）
    int decode_frame(AVFrame* frame);
    
    // 清空解码器
    void flush();
    
    // 获取解码器上下文
    AVCodecContext* get_codec_ctx() const { return codec_ctx_; }
    
    // 暂停/恢复解码（用于外部线程控制）
    void pause() { paused_ = true; }
    void resume() { paused_ = false; }
    bool is_paused() const { return paused_; }
    
    // 检查是否暂停（外部线程可以检查这个标志）
    bool should_pause() const { return paused_; }

private:
    AVCodecContext* codec_ctx_;
    PacketQueue* packet_queue_;
    
    bool finished_;
    std::atomic<bool> paused_;      // 暂停标志
    
    AVPacket* pkt_;
    int pkt_serial_;
    
    // ⚠️ 互斥锁，保护 codec_ctx_ 的所有操作
    mutable std::mutex codec_mutex_;
    
    // 注意：decode_callback_ 和 decode_thread_ 已移除
    // 解码由外部线程（video_thread/audio_callback）调用
};

/**
 * @brief 视频解码器
 */
class VideoDecoder : public Decoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder() override = default;
};

/**
 * @brief 音频解码器
 */
class AudioDecoder : public Decoder {
public:
    AudioDecoder() = default;
    ~AudioDecoder() override = default;
};

} // namespace hxcplayer

#endif // YXVODPLAYER_DECODER_H
