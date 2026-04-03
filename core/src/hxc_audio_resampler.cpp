/**
 * @file hxc_audio_resampler.cpp
 * @brief 音频重采样器实现
 */

#include "hxc_audio_resampler.h"
#include "hxc_logger.h"

extern "C" {
#include <libavutil/opt.h>
}

namespace hxcplayer {

AudioResampler::AudioResampler()
    : swr_ctx_(nullptr)
    , need_resample_(false)
    , buffer_(nullptr)
    , buffer_size_(0)
    , src_sample_rate_(0)
    , dst_sample_rate_(0)
    , dst_channels_(0)
    , src_sample_fmt_(AV_SAMPLE_FMT_NONE)
    , dst_sample_fmt_(AV_SAMPLE_FMT_NONE)
    , has_config_(false) {
    av_channel_layout_uninit(&src_ch_layout_);
    av_channel_layout_uninit(&dst_ch_layout_);
}

AudioResampler::~AudioResampler() {
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
    }
    if (buffer_) {
        av_free(buffer_);
    }
    av_channel_layout_uninit(&src_ch_layout_);
    av_channel_layout_uninit(&dst_ch_layout_);
}

int AudioResampler::configure(const AVChannelLayout* src_ch_layout, AVSampleFormat src_sample_fmt, int src_sample_rate,
                               const AVChannelLayout* dst_ch_layout, AVSampleFormat dst_sample_fmt, int dst_sample_rate) {
    // 如果已经有配置，并且参数完全相同，直接复用当前重采样器
    if (has_config_) {
        bool same_fmt = (src_sample_fmt_ == src_sample_fmt) &&
                        (dst_sample_fmt_ == dst_sample_fmt);
        bool same_rate = (src_sample_rate_ == src_sample_rate) &&
                         (dst_sample_rate_ == dst_sample_rate);
        bool same_layout = (av_channel_layout_compare(&src_ch_layout_, src_ch_layout) == 0) &&
                           (av_channel_layout_compare(&dst_ch_layout_, dst_ch_layout) == 0);
        if (same_fmt && same_rate && same_layout) {
            return 0;
        }
    }

    // 更新内部配置快照
    src_sample_rate_ = src_sample_rate;
    dst_sample_rate_ = dst_sample_rate;
    dst_channels_ = dst_ch_layout->nb_channels;
    src_sample_fmt_ = src_sample_fmt;
    dst_sample_fmt_ = dst_sample_fmt;

    av_channel_layout_uninit(&src_ch_layout_);
    av_channel_layout_uninit(&dst_ch_layout_);
    if (av_channel_layout_copy(&src_ch_layout_, src_ch_layout) < 0 ||
        av_channel_layout_copy(&dst_ch_layout_, dst_ch_layout) < 0) {
        LOG_ERROR("拷贝通道布局失败");
        has_config_ = false;
        need_resample_ = false;
        return AVERROR(EINVAL);
    }
    has_config_ = true;

    // 检查是否需要重采样（按当前源/目标参数重新计算）
    need_resample_ = (src_sample_fmt_ != dst_sample_fmt_) ||
                     (src_sample_rate_ != dst_sample_rate_) ||
                     (av_channel_layout_compare(&src_ch_layout_, &dst_ch_layout_) != 0);

    if (!need_resample_) {
        LOG_INFO("音频格式匹配，无需重采样");
        // 不需要重采样时释放旧的上下文
        if (swr_ctx_) {
            swr_free(&swr_ctx_);
            swr_ctx_ = nullptr;
        }
        return 0;
    }

    // 重新配置前先释放旧的重采样器上下文
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }

    // 创建重采样器
    int ret = swr_alloc_set_opts2(&swr_ctx_,
        &dst_ch_layout_, dst_sample_fmt_, dst_sample_rate_,
        &src_ch_layout_, src_sample_fmt_, src_sample_rate_,
        0, nullptr);

    if (ret < 0 || !swr_ctx_) {
        LOG_ERROR("创建重采样器失败");
        return ret;
    }

    ret = swr_init(swr_ctx_);
    if (ret < 0) {
        LOG_ERROR("初始化重采样器失败");
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
        need_resample_ = false;
        return ret;
    }

    LOG_INFO("音频重采样器配置完成: ",
        av_get_sample_fmt_name(src_sample_fmt), "/", src_sample_rate, "Hz -> ",
        av_get_sample_fmt_name(dst_sample_fmt), "/", dst_sample_rate, "Hz");

    return 0;
}

int AudioResampler::resample(uint8_t** src_data, int src_nb_samples, uint8_t** dst_data, int* dst_nb_samples) {
    if (!need_resample_ || !swr_ctx_) {
        return -1;
    }

    // 输入为空或首通道指针为空时直接返回，避免 swr_convert 访问非法地址
    if (!src_data || !src_data[0] || src_nb_samples <= 0) {
        if (dst_data) {
            *dst_data = nullptr;
        }
        if (dst_nb_samples) {
            *dst_nb_samples = 0;
        }
        return 0;
    }

    // 计算输出样本数
    int src_rate = src_sample_rate_ > 0 ? src_sample_rate_ : dst_sample_rate_;
    int dst_rate = dst_sample_rate_ > 0 ? dst_sample_rate_ : src_rate;
    int64_t delay = swr_get_delay(swr_ctx_, src_rate);
    int out_samples = (int)av_rescale_rnd(delay + src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);

    if (out_samples <= 0) {
        if (dst_data) {
            *dst_data = nullptr;
        }
        if (dst_nb_samples) {
            *dst_nb_samples = 0;
        }
        return 0;
    }

    // 分配缓冲区
    if (dst_channels_ <= 0) {
        LOG_ERROR("重采样输出通道数无效: ", dst_channels_);
        return -1;
    }

    int out_size = av_samples_get_buffer_size(nullptr, dst_channels_, out_samples, dst_sample_fmt_, 0);
    if (out_size <= 0) {
        LOG_ERROR("计算重采样输出缓冲区大小失败: out_size=", out_size,
                  ", channels=", dst_channels_, ", samples=", out_samples);
        return out_size == 0 ? -1 : out_size;
    }
    if (out_size > buffer_size_) {
        av_free(buffer_);
        buffer_ = (uint8_t*)av_malloc(out_size);
        if (!buffer_) {
            buffer_size_ = 0;
            LOG_ERROR("重采样缓冲区分配失败, out_size=", out_size);
            if (dst_data) {
                *dst_data = nullptr;
            }
            if (dst_nb_samples) {
                *dst_nb_samples = 0;
            }
            return AVERROR(ENOMEM);
        }
        buffer_size_ = out_size;
    }

    // 执行重采样
    uint8_t* out_buf = buffer_;
    int converted = swr_convert(swr_ctx_, &out_buf, out_samples, (const uint8_t**)src_data, src_nb_samples);
    if (converted < 0) {
        LOG_ERROR("重采样失败");
        return converted;
    }

    *dst_data = buffer_;
    *dst_nb_samples = converted;
    return 0;
}

} // namespace hxcplayer
