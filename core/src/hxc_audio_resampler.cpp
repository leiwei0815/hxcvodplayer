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
    , dst_channels_(0)
    , dst_sample_fmt_(AV_SAMPLE_FMT_NONE) {
}

AudioResampler::~AudioResampler() {
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
    }
    if (buffer_) {
        av_free(buffer_);
    }
}

int AudioResampler::configure(const AVChannelLayout* src_ch_layout, AVSampleFormat src_sample_fmt, int src_sample_rate,
                               const AVChannelLayout* dst_ch_layout, AVSampleFormat dst_sample_fmt, int dst_sample_rate) {
    // 检查是否需要重采样
    need_resample_ = (src_sample_fmt != dst_sample_fmt) ||
                     (src_sample_rate != dst_sample_rate) ||
                     (av_channel_layout_compare(src_ch_layout, dst_ch_layout) != 0);

    if (!need_resample_) {
        LOG_INFO("音频格式匹配，无需重采样");
        return 0;
    }

    // 创建重采样器
    int ret = swr_alloc_set_opts2(&swr_ctx_,
        dst_ch_layout, dst_sample_fmt, dst_sample_rate,
        src_ch_layout, src_sample_fmt, src_sample_rate,
        0, nullptr);

    if (ret < 0 || !swr_ctx_) {
        LOG_ERROR("创建重采样器失败");
        return ret;
    }

    ret = swr_init(swr_ctx_);
    if (ret < 0) {
        LOG_ERROR("初始化重采样器失败");
        swr_free(&swr_ctx_);
        return ret;
    }

    dst_channels_ = dst_ch_layout->nb_channels;
    dst_sample_fmt_ = dst_sample_fmt;

    LOG_INFO("音频重采样器配置完成: ",
        av_get_sample_fmt_name(src_sample_fmt), "/", src_sample_rate, "Hz -> ",
        av_get_sample_fmt_name(dst_sample_fmt), "/", dst_sample_rate, "Hz");

    return 0;
}

int AudioResampler::resample(uint8_t** src_data, int src_nb_samples, uint8_t** dst_data, int* dst_nb_samples) {
    if (!need_resample_ || !swr_ctx_) {
        return -1;
    }

    // 计算输出样本数
    int out_samples = av_rescale_rnd(swr_get_delay(swr_ctx_, 48000) + src_nb_samples, 48000, 48000, AV_ROUND_UP);

    // 分配缓冲区
    int out_size = av_samples_get_buffer_size(nullptr, dst_channels_, out_samples, dst_sample_fmt_, 0);
    if (out_size > buffer_size_) {
        av_free(buffer_);
        buffer_ = (uint8_t*)av_malloc(out_size);
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
