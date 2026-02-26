/**
 * @file debug_helper.h
 * @brief 调试辅助工具
 */

#ifndef YXVODPLAYER_DEBUG_HELPER_H
#define YXVODPLAYER_DEBUG_HELPER_H

#include <string>
#include <sstream>
#include <iomanip>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libavutil/time.h>
}

namespace hxcplayer {

/**
 * @brief 调试辅助类
 */
class DebugHelper {
public:
    // 格式化时间（秒 -> HH:MM:SS.mmm）
    static std::string format_time(double seconds) {
        int hours = static_cast<int>(seconds) / 3600;
        int minutes = (static_cast<int>(seconds) % 3600) / 60;
        int secs = static_cast<int>(seconds) % 60;
        int millis = static_cast<int>((seconds - static_cast<int>(seconds)) * 1000);
        
        std::ostringstream oss;
        if (hours > 0) {
            oss << std::setfill('0') << std::setw(2) << hours << ":";
        }
        oss << std::setfill('0') << std::setw(2) << minutes << ":"
            << std::setfill('0') << std::setw(2) << secs << "."
            << std::setfill('0') << std::setw(3) << millis;
        
        return oss.str();
    }
    
    // 格式化文件大小
    static std::string format_size(int64_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int unit_index = 0;
        double size = static_cast<double>(bytes);
        
        while (size >= 1024.0 && unit_index < 4) {
            size /= 1024.0;
            unit_index++;
        }
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
        return oss.str();
    }
    
    // 格式化比特率
    static std::string format_bitrate(int64_t bps) {
        if (bps < 1000) {
            return std::to_string(bps) + " bps";
        } else if (bps < 1000000) {
            return std::to_string(bps / 1000) + " kbps";
        } else {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(2) << (bps / 1000000.0) << " Mbps";
            return oss.str();
        }
    }
    
    // 获取编码器名称
    static std::string get_codec_name(AVCodecID codec_id) {
        const AVCodec* codec = avcodec_find_decoder(codec_id);
        if (codec) {
            return std::string(codec->long_name ? codec->long_name : codec->name);
        }
        return "Unknown";
    }
    
    // 获取像素格式名称
    static std::string get_pixel_format_name(AVPixelFormat pix_fmt) {
        const char* name = av_get_pix_fmt_name(pix_fmt);
        return name ? std::string(name) : "Unknown";
    }
    
    // 获取像素格式名称（从 int）
    static std::string get_pixel_format_name_from_int(int fmt) {
        return get_pixel_format_name(static_cast<AVPixelFormat>(fmt));
    }
    
    // 获取采样格式名称
    static std::string get_sample_format_name(AVSampleFormat sample_fmt) {
        const char* name = av_get_sample_fmt_name(sample_fmt);
        return name ? std::string(name) : "Unknown";
    }
    
    // 获取采样格式名称（从 int）
    static std::string get_sample_format_name_from_int(int fmt) {
        return get_sample_format_name(static_cast<AVSampleFormat>(fmt));
    }
    
    // 打印媒体信息
    static void print_media_info(AVFormatContext* fmt_ctx) {
        if (!fmt_ctx) return;
        
        std::cout << "\n========== 媒体信息 ==========\n";
        std::cout << "文件: " << (fmt_ctx->url ? fmt_ctx->url : "Unknown") << "\n";
        std::cout << "格式: " << (fmt_ctx->iformat->long_name ? fmt_ctx->iformat->long_name : fmt_ctx->iformat->name) << "\n";
        std::cout << "时长: " << format_time(fmt_ctx->duration / static_cast<double>(AV_TIME_BASE)) << "\n";
        std::cout << "比特率: " << format_bitrate(fmt_ctx->bit_rate) << "\n";
        std::cout << "流数量: " << fmt_ctx->nb_streams << "\n";
        
        for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
            AVStream* stream = fmt_ctx->streams[i];
            AVCodecParameters* codecpar = stream->codecpar;
            
            std::cout << "\n--- 流 #" << i << " ---\n";
            
            if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                std::cout << "类型: 视频\n";
                std::cout << "编码: " << get_codec_name(codecpar->codec_id) << "\n";
                std::cout << "分辨率: " << codecpar->width << "x" << codecpar->height << "\n";
                std::cout << "像素格式: " << get_pixel_format_name_from_int(codecpar->format) << "\n";
                
                AVRational frame_rate = av_guess_frame_rate(fmt_ctx, stream, nullptr);
                if (frame_rate.num && frame_rate.den) {
                    std::cout << "帧率: " << std::fixed << std::setprecision(2) 
                             << av_q2d(frame_rate) << " fps\n";
                }
                
            } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                std::cout << "类型: 音频\n";
                std::cout << "编码: " << get_codec_name(codecpar->codec_id) << "\n";
                std::cout << "采样率: " << codecpar->sample_rate << " Hz\n";
                std::cout << "声道: " << codecpar->ch_layout.nb_channels << "\n";
                std::cout << "采样格式: " << get_sample_format_name_from_int(codecpar->format) << "\n";
                
            } else if (codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                std::cout << "类型: 字幕\n";
                std::cout << "编码: " << get_codec_name(codecpar->codec_id) << "\n";
            }
            
            if (codecpar->bit_rate > 0) {
                std::cout << "比特率: " << format_bitrate(codecpar->bit_rate) << "\n";
            }
        }
        
        std::cout << "================================\n\n";
    }
    
    // 打印帧信息
    static void print_frame_info(const char* type, AVFrame* frame, double pts) {
        std::cout << "[" << type << " Frame] "
                 << "PTS: " << format_time(pts)
                 << ", Size: " << frame->width << "x" << frame->height
                 << ", Format: " << frame->format
                 << "\n";
    }
    
    // 打印性能统计
    struct PerformanceStats {
        int frames_decoded = 0;
        int frames_dropped = 0;
        int64_t bytes_read = 0;
        double start_time = 0.0;
        
        void print() const {
            double elapsed = get_elapsed_time();
            std::cout << "\n========== 性能统计 ==========\n";
            std::cout << "运行时间: " << format_time(elapsed) << "\n";
            std::cout << "解码帧数: " << frames_decoded << "\n";
            std::cout << "丢帧数: " << frames_dropped << "\n";
            std::cout << "平均帧率: " << std::fixed << std::setprecision(2) 
                     << (frames_decoded / elapsed) << " fps\n";
            std::cout << "读取数据: " << format_size(bytes_read) << "\n";
            std::cout << "================================\n\n";
        }
        
        double get_elapsed_time() const {
            return av_gettime_relative() / 1000000.0 - start_time;
        }
    };
};

// 性能计时器
class PerformanceTimer {
public:
    PerformanceTimer(const std::string& name) 
        : name_(name)
        , start_(av_gettime_relative()) {
    }
    
    ~PerformanceTimer() {
        int64_t elapsed = av_gettime_relative() - start_;
        double ms = elapsed / 1000.0;
        
        if (ms > 10.0) {  // 只打印超过 10ms 的操作
            std::cout << "[PERF] " << name_ << ": " 
                     << std::fixed << std::setprecision(2) << ms << " ms\n";
        }
    }

private:
    std::string name_;
    int64_t start_;
};

// 调试宏
#ifdef DEBUG
    #define PERF_TIMER(name) PerformanceTimer _timer(name)
    #define DEBUG_PRINT_FRAME(type, frame, pts) DebugHelper::print_frame_info(type, frame, pts)
#else
    #define PERF_TIMER(name)
    #define DEBUG_PRINT_FRAME(type, frame, pts)
#endif

} // namespace hxcplayer

#endif // YXVODPLAYER_DEBUG_HELPER_H
