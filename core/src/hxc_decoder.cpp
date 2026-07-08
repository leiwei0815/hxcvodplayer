/**
 * @file decoder.cpp
 * @brief 解码器实现
 */

#include "hxc_decoder.h"
#include "hxc_logger.h"
#include <iostream>
#include <cstring>

extern "C" {
#include <libavutil/time.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

namespace hxcplayer {

namespace {
static bool hxc_is_hwaccel_frame_format(AVPixelFormat fmt) {
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(fmt);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
}

static bool hxc_is_mediacodec_pixel_format(AVPixelFormat fmt) {
#ifdef AV_PIX_FMT_MEDIACODEC
    if (fmt == AV_PIX_FMT_MEDIACODEC) {
        return true;
    }
#endif
    const char* name = av_get_pix_fmt_name(fmt);
    return name && std::strcmp(name, "mediacodec") == 0;
}
}

Decoder::Decoder()
    : codec_ctx_(nullptr)
    , packet_queue_(nullptr)
    , finished_(false)
    , paused_(true)      // ⚠️ 默认暂停，等待 resume() 调用
    , pkt_(nullptr)
    , pkt_serial_(0) {
    // ⚠️ atomic 类型会自动初始化
}

Decoder::~Decoder() {
    if (pkt_) {
        av_packet_free(&pkt_);
    }
}

int Decoder::init(AVCodecContext* codec_ctx, PacketQueue* packet_queue) {
    codec_ctx_ = codec_ctx;
    packet_queue_ = packet_queue;
    
    pkt_ = av_packet_alloc();
    if (!pkt_) {
        return AVERROR(ENOMEM);
    }
    
    return 0;
}

// start() 和 stop() 已移除
// 解码由外部线程控制（video_thread 或 audio_callback）

int Decoder::decode_frame(AVFrame* frame) {
    int ret;
    auto convert_hw_frame_if_needed = [&](AVFrame* target_frame) -> int {
        // 若为硬件帧，先转成软件帧，保持后续渲染链路（YUV 读取）兼容
        // 注意：部分平台下 frame->format 可能不是 HWACCEL 标记格式，但 hw_frames_ctx 仍有效。
        const AVPixelFormat frame_fmt = static_cast<AVPixelFormat>(target_frame->format);
        bool needs_hw_transfer = (target_frame->hw_frames_ctx != nullptr) ||
                                 hxc_is_hwaccel_frame_format(frame_fmt) ||
                                 hxc_is_mediacodec_pixel_format(frame_fmt) ||
                                 (codec_ctx_ && codec_ctx_->hw_device_ctx != nullptr) ||
                                 (target_frame->data[0] == nullptr);
        if (!needs_hw_transfer) {
            return 0;
        }

        AVFrame* sw_frame = av_frame_alloc();
        if (!sw_frame) {
            return AVERROR(ENOMEM);
        }
        int transfer_ret = av_hwframe_transfer_data(sw_frame, target_frame, 0);
        if (transfer_ret < 0) {
            const char* fmt_name = av_get_pix_fmt_name(frame_fmt);
            LOG_ERROR("硬件帧转 YUV 失败: fmt=", (fmt_name ? fmt_name : "unknown"),
                      " ret=", transfer_ret,
                      " hw_frames_ctx=", (target_frame->hw_frames_ctx ? 1 : 0),
                      " hw_device_ctx=", (codec_ctx_ && codec_ctx_->hw_device_ctx ? 1 : 0));
            av_frame_free(&sw_frame);
            return transfer_ret;
        }
        transfer_ret = av_frame_copy_props(sw_frame, target_frame);
        if (transfer_ret < 0) {
            av_frame_free(&sw_frame);
            return transfer_ret;
        }
        av_frame_unref(target_frame);
        av_frame_move_ref(target_frame, sw_frame);
        av_frame_free(&sw_frame);
        return 0;
    };
    
    // ⚠️ 检查有效性，防止崩溃
    if (!codec_ctx_ || !packet_queue_ || !pkt_) {
        return AVERROR(EINVAL);
    }
    
    // ⚠️ 由外部线程调用，不需要 running_ 标志
    while (true) {
        // ⚠️ 锁定 codec_ctx_，防止与 flush() 冲突
        {
            std::lock_guard<std::mutex> lock(codec_mutex_);
            if (!codec_ctx_) {
                return AVERROR(EINVAL);
            }
            // 从解码器获取解码后的帧
            ret = avcodec_receive_frame(codec_ctx_, frame);
        }
        
        if (ret == 0) {
            int convert_ret = convert_hw_frame_if_needed(frame);
            if (convert_ret < 0) {
                return convert_ret;
            }
            // 成功获取一帧
            return 1;
        } else if (ret == AVERROR(EAGAIN)) {
            // 需要更多数据
            break;
        } else if (ret == AVERROR_EOF) {
            // 解码结束
            finished_ = true;
            return 0;
        } else {
            // 错误
            return ret;
        }
    }
    
    // 向解码器发送数据包
    while (true) {
        // ⚠️ 每次循环检查有效性（防止在循环中被其他线程释放）
        if (!codec_ctx_ || !packet_queue_ || !pkt_) {
            return AVERROR(EINVAL);
        }
        
        ret = packet_queue_->get(pkt_, true);
        if (ret < 0) {
            return ret;
        }
        
        if (pkt_->data == nullptr) {
            // 刷新包（seek 后）
            // ⚠️ 锁定并检查有效性
            std::lock_guard<std::mutex> lock(codec_mutex_);
            if (codec_ctx_) {
                avcodec_flush_buffers(codec_ctx_);
            }
            finished_ = false;
            // ⚠️ 刷新后继续获取新数据
            continue;
        }
        
        // ⚠️ 锁定 codec_ctx_，发送数据包
        {
            std::lock_guard<std::mutex> lock(codec_mutex_);
            if (!codec_ctx_) {
                av_packet_unref(pkt_);
                return AVERROR(EINVAL);
            }
            
            ret = avcodec_send_packet(codec_ctx_, pkt_);
        }
        av_packet_unref(pkt_);
        
        if (ret < 0) {
            if (ret == AVERROR(EAGAIN)) {
                continue;
            } else if (ret == AVERROR_EOF) {
                finished_ = true;
                return 0;
            } else {
                return ret;
            }
        }
        
        // 再次尝试接收帧
        // ⚠️ 锁定并检查有效性
        {
            std::lock_guard<std::mutex> lock(codec_mutex_);
            if (!codec_ctx_) {
                return AVERROR(EINVAL);
            }
            
            ret = avcodec_receive_frame(codec_ctx_, frame);
        }
        
        if (ret == 0) {
            int convert_ret = convert_hw_frame_if_needed(frame);
            if (convert_ret < 0) {
                return convert_ret;
            }
            return 1;
        } else if (ret == AVERROR(EAGAIN)) {
            continue;
        } else if (ret == AVERROR_EOF) {
            finished_ = true;
            return 0;
        } else {
            return ret;
        }
    }
    
    return -1;
}

void Decoder::flush() {
    // ⚠️ 锁定 codec_ctx_，防止与 decode_frame() 冲突
    std::lock_guard<std::mutex> lock(codec_mutex_);
    
    // ⚠️ 检查有效性
    if (codec_ctx_) {
        avcodec_flush_buffers(codec_ctx_);
    }
    if (packet_queue_) {
        packet_queue_->flush();
    }
    finished_ = false;
}

// decode_thread 已移除
// 解码由外部线程控制：
// - 视频：PlayerCore::video_thread()
// - 音频：PlayerCore::audio_callback_impl()

} // namespace hxcplayer
