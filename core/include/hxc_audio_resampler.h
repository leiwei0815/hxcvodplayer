/**
 * @file hxc_audio_resampler.h
 * @brief 音频重采样器（按需重采样）
 */

#ifndef YXVODPLAYER_AUDIO_RESAMPLER_H
#define YXVODPLAYER_AUDIO_RESAMPLER_H

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

namespace hxcplayer {

class AudioResampler {
public:
    AudioResampler();
    ~AudioResampler();

    /**
     * @brief 配置重采样器
     * @param src_ch_layout 源通道布局
     * @param src_sample_fmt 源采样格式
     * @param src_sample_rate 源采样率
     * @param dst_ch_layout 目标通道布局
     * @param dst_sample_fmt 目标采样格式
     * @param dst_sample_rate 目标采样率
     * @return 0 成功，负数失败
     */
    int configure(const AVChannelLayout* src_ch_layout, AVSampleFormat src_sample_fmt, int src_sample_rate,
                  const AVChannelLayout* dst_ch_layout, AVSampleFormat dst_sample_fmt, int dst_sample_rate);

    /**
     * @brief 检查是否需要重采样
     * @return true 需要，false 不需要
     */
    bool is_needed() const { return need_resample_; }

    /**
     * @brief 重采样音频数据
     * @param src_data 源数据
     * @param src_nb_samples 源样本数
     * @param dst_data 输出数据指针（会自动分配）
     * @param dst_nb_samples 输出样本数
     * @return 0 成功，负数失败
     */
    int resample(uint8_t** src_data, int src_nb_samples, uint8_t** dst_data, int* dst_nb_samples);

    /**
     * @brief 获取输出缓冲区大小
     */
    int get_buffer_size() const { return buffer_size_; }

private:
    SwrContext* swr_ctx_;
    bool need_resample_;
    uint8_t* buffer_;
    int buffer_size_;
    int src_sample_rate_;
    int dst_sample_rate_;
    int dst_channels_;
    AVSampleFormat src_sample_fmt_;
    AVSampleFormat dst_sample_fmt_;
    AVChannelLayout src_ch_layout_;
    AVChannelLayout dst_ch_layout_;
    bool has_config_;
};

} // namespace hxcplayer

#endif
