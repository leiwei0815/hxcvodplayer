/**
 * @file player_core.cpp
 * @brief 播放器核心实现（参照 ffplay）
 */

#include "hxc_player_core.h"
#include "hxc_logger.h"
#include "hxc_debug_helper.h"
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>

// SoundTouch 条件编译
#ifdef __APPLE__
    #include <soundtouch/SoundTouch.h>
    #ifndef HAS_SOUNDTOUCH
        #define HAS_SOUNDTOUCH
    #endif
#elif defined(_WIN32) && defined(HAS_SOUNDTOUCH)
    #include <soundtouch/SoundTouch.h>
#endif

extern "C" {
#include <libavutil/time.h>
#include <libavutil/opt.h>
}

// 跨平台延迟宏
#ifndef NO_SDL
    #define PLAYER_DELAY(ms) SDL_Delay(ms)
#else
    #define PLAYER_DELAY(ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms))
#endif

namespace hxcplayer {

PlayerCore::PlayerCore()
    : state_(PlayerState::Idle)
    , format_ctx_(nullptr)
    , video_stream_(-1)
    , audio_stream_(-1)
    , subtitle_stream_(-1)
    , video_codec_ctx_(nullptr)
    , audio_codec_ctx_(nullptr)
    , abort_request_(false)
    , pause_request_(false)
    , seek_request_(false)
    , seek_pos_(0.0)
#ifndef NO_SDL
    , audio_dev_(0)
#endif
    , volume_(100)
    , swr_ctx_(nullptr)
    , audio_buf_(nullptr)
    , audio_buf_size_(0)
    , audio_buf_index_(0)
    , audio_current_pts_(0.0)
    , audio_current_pts_drift_(0.0)
    , aspect_ratio_mode_(AspectRatioMode::Fit)  // ⚠️ 默认 Fit 模式
#ifdef HAS_SOUNDTOUCH
    , soundtouch_(nullptr)
    , soundtouch_buffer_index_(0)
#endif
    , playback_rate_(1.0) {  // ⚠️ 默认正常速度
    
    LOG_INFO("初始化 PlayerCore...");
    
    // 初始化 FFmpeg
    av_log_set_level(AV_LOG_WARNING);
    
#ifndef NO_SDL
    // 初始化 SDL（仅桌面平台）
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        LOG_ERROR("SDL初始化失败: ", SDL_GetError());
        std::cerr << "SDL初始化失败: " << SDL_GetError() << std::endl;
    } else {
        LOG_INFO("SDL 初始化成功");
    }
#else
    LOG_INFO("iOS 平台，跳过 SDL 初始化");
#endif
    
    LOG_INFO("PlayerCore 初始化完成");
}

PlayerCore::~PlayerCore() {
    LOG_INFO("销毁 PlayerCore...");
    close();
    
#ifdef HAS_SOUNDTOUCH
    // 释放 SoundTouch
    if (soundtouch_) {
        delete soundtouch_;
        soundtouch_ = nullptr;
    }
#endif
    
#ifndef NO_SDL
    SDL_Quit();
#endif
    
    LOG_INFO("PlayerCore 已销毁");
}

int PlayerCore::open(const std::string& filename) {
    if (state_ != PlayerState::Idle && state_ != PlayerState::Stopped) {
        LOG_WARNING("播放器状态错误，无法打开文件");
        return -1;
    }
    
    LOG_INFO("正在打开文件: ", filename);
    
    // ⚠️ 重置终止标志（重要！否则之前 close() 设置的 true 会导致新线程立即退出）
    abort_request_ = false;
    
    set_state(PlayerState::Opening);
    
    // 打开输入文件
    format_ctx_ = avformat_alloc_context();
    if (avformat_open_input(&format_ctx_, filename.c_str(), nullptr, nullptr) < 0) {
        LOG_ERROR("无法打开文件: ", filename);
        emit_error("无法打开文件: " + filename);
        set_state(PlayerState::Error);
        return -1;
    }
    
    LOG_INFO("文件打开成功");
    
    // 获取流信息
    if (avformat_find_stream_info(format_ctx_, nullptr) < 0) {
        emit_error("无法获取流信息");
        set_state(PlayerState::Error);
        return -1;
    }
    
    // 打印媒体信息
    av_dump_format(format_ctx_, 0, filename.c_str(), 0);
    
    // 打印详细的媒体信息（调试用）
    DebugHelper::print_media_info(format_ctx_);
    
    // 查找视频流和音频流
    video_stream_ = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    audio_stream_ = av_find_best_stream(format_ctx_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    
    // 填充媒体信息
    media_info_.filename = filename;
    media_info_.duration = format_ctx_->duration;
    media_info_.bitrate = format_ctx_->bit_rate;
    
    if (video_stream_ >= 0) {
        AVStream* stream = format_ctx_->streams[video_stream_];
        media_info_.video_width = stream->codecpar->width;
        media_info_.video_height = stream->codecpar->height;
        media_info_.video_codec = stream->codecpar->codec_id;
        
        AVRational frame_rate = av_guess_frame_rate(format_ctx_, stream, nullptr);
        media_info_.video_fps = av_q2d(frame_rate);
    }
    
    if (audio_stream_ >= 0) {
        AVStream* stream = format_ctx_->streams[audio_stream_];
        media_info_.audio_sample_rate = stream->codecpar->sample_rate;
        media_info_.audio_channels = stream->codecpar->ch_layout.nb_channels;
        media_info_.audio_codec = stream->codecpar->codec_id;
    }
    
    // ⚠️ 必须先创建队列，再启动解码线程！
    // 创建数据包队列
    video_packet_queue_ = std::make_unique<PacketQueue>();
    audio_packet_queue_ = std::make_unique<PacketQueue>();
    subtitle_packet_queue_ = std::make_unique<PacketQueue>();
    
    // 创建帧队列
    video_queue_ = std::make_unique<FrameQueue<VideoFrame>>(config_.video_queue_size);
    audio_queue_ = std::make_unique<FrameQueue<AudioFrame>>(config_.audio_queue_size);
    
    LOG_INFO("队列创建完成");
    
    // ⚠️ 参考 ffplay：如果配置了开始播放时间，在打开流之前先 seek
    // 这样可以避免解码不需要的数据，显著提高启动速度
    if (config_.start_time > 0.0) {
        double duration = get_duration();
        if (duration > 0 && config_.start_time < duration) {
            int64_t seek_target = config_.start_time * AV_TIME_BASE;
            LOG_INFO("配置了开始播放时间: ", config_.start_time, " 秒，在启动线程前先 seek...");
            
            // ⚠️ 使用 avformat_seek_file（比 av_seek_frame 更精确）
            int ret = avformat_seek_file(format_ctx_, -1, 
                                         INT64_MIN,      // min_ts
                                         seek_target,     // ts (目标时间)
                                         seek_target,     // max_ts
                                         0);              // flags
            if (ret < 0) {
                LOG_WARNING("初始 seek 失败，将从头开始播放");
            } else {
                LOG_INFO("初始 seek 成功，将从 ", config_.start_time, " 秒开始播放");
                // ⚠️ 清空解复用器的内部缓冲区
                avformat_flush(format_ctx_);
            }
        } else {
            LOG_WARNING("开始播放时间 ", config_.start_time, " 无效或超过视频时长，忽略");
        }
    }
    
    // 打开流组件（会创建解码器，但不启动线程）
    if (config_.enable_video && video_stream_ >= 0) {
        LOG_INFO("打开视频流...");
        if (stream_component_open(video_stream_) < 0) {
            LOG_ERROR("无法打开视频流");
        } else {
            LOG_INFO("视频流打开成功, 分辨率: ", media_info_.video_width, "x", media_info_.video_height);
        }
    }
    
    if (config_.enable_audio && audio_stream_ >= 0) {
        LOG_INFO("打开音频流...");
        if (stream_component_open(audio_stream_) < 0) {
            LOG_ERROR("无法打开音频流");
        } else {
            LOG_INFO("音频流打开成功, 采样率: ", media_info_.audio_sample_rate, " Hz");
        }
    }
    
    // 启动读取线程（此时已经 seek 到正确位置）
    abort_request_ = false;
    read_thread_ = std::thread(&PlayerCore::read_thread, this);
    
    // ⚠️ 自动开始播放（恢复解码器）
    if (video_decoder_) {
        video_decoder_->resume();
    }
    if (audio_decoder_) {
        audio_decoder_->resume();
    }
    
    LOG_INFO("解码器已恢复，开始播放");
    set_state(PlayerState::Playing);
    
    return 0;
}

void PlayerCore::close() {
    if (state_ == PlayerState::Idle) {
        return;
    }
    
    LOG_INFO("正在关闭播放器...");
    
    // ⚠️ 第一步：停止标志（让所有线程知道要停止）
    abort_request_ = true;
    
    // ⚠️ 第二步：立即停止SDL音频设备（防止音频回调继续执行）
#ifndef NO_SDL
    if (audio_dev_) {
        SDL_PauseAudioDevice(audio_dev_, 1);  // 暂停音频
        LOG_INFO("音频设备已暂停");
    }
#endif
    
    // ⚠️ 第三步：中止所有队列（唤醒阻塞的线程）
    if (video_packet_queue_) video_packet_queue_->abort();
    if (audio_packet_queue_) audio_packet_queue_->abort();
    if (subtitle_packet_queue_) subtitle_packet_queue_->abort();
    
    if (video_queue_) video_queue_->abort();
    if (audio_queue_) audio_queue_->abort();
    
    // ⚠️ 第四步：等待读取线程结束
    if (read_thread_.joinable()) {
        LOG_INFO("等待读取线程结束...");
        read_thread_.join();
        LOG_INFO("读取线程已结束");
    }
    
    if (video_thread_.joinable()) {
        LOG_INFO("等待视频线程结束...");
        video_thread_.join();
        LOG_INFO("视频线程已结束");
    }
    
    if (audio_thread_.joinable()) {
        LOG_INFO("等待音频线程结束...");
        audio_thread_.join();
        LOG_INFO("音频线程已结束");
    }
    
    // ⚠️ 第五步：现在可以安全地关闭流组件
    if (video_stream_ >= 0) stream_component_close(video_stream_);
    if (audio_stream_ >= 0) stream_component_close(audio_stream_);
    
    // 关闭格式上下文
    if (format_ctx_) {
        avformat_close_input(&format_ctx_);
        format_ctx_ = nullptr;
    }
    
    // 清理队列
    video_packet_queue_.reset();
    audio_packet_queue_.reset();
    subtitle_packet_queue_.reset();
    video_queue_.reset();
    audio_queue_.reset();
    
    // 清理音频缓冲（设备已在 stream_component_close 中关闭）
    if (audio_buf_) {
        av_free(audio_buf_);
        audio_buf_ = nullptr;
        audio_buf_size_ = 0;
        audio_buf_index_ = 0;
    }
    
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }
    
    LOG_INFO("播放器已关闭");
    set_state(PlayerState::Idle);
}

void PlayerCore::play() {
    if (state_ == PlayerState::Paused) {
        pause_request_ = false;
        
        LOG_INFO("恢复播放...");
        
        // 恢复解码器
        if (video_decoder_) {
            video_decoder_->resume();
            LOG_INFO("视频解码器已恢复");
        }
        if (audio_decoder_) {
            audio_decoder_->resume();
            LOG_INFO("音频解码器已恢复");
        }
        
        // 恢复音频设备
#ifndef NO_SDL
        if (audio_dev_) {
            SDL_PauseAudioDevice(audio_dev_, 0);
        }
#endif
        
        set_state(PlayerState::Playing);
        LOG_INFO("播放已恢复");
    }
}

void PlayerCore::pause() {
    if (state_ == PlayerState::Playing) {
        pause_request_ = true;
        
        LOG_INFO("暂停播放...");
        
        // 暂停解码器（停止解码，节省 CPU）
        if (video_decoder_) {
            video_decoder_->pause();
            LOG_INFO("视频解码器已暂停");
        }
        if (audio_decoder_) {
            audio_decoder_->pause();
            LOG_INFO("音频解码器已暂停");
        }
        
        // 暂停音频设备
#ifndef NO_SDL
        if (audio_dev_) {
            SDL_PauseAudioDevice(audio_dev_, 1);
        }
#endif
        
        set_state(PlayerState::Paused);
        LOG_INFO("播放已暂停");
    }
}

void PlayerCore::stop() {
    close();
    set_state(PlayerState::Stopped);
}

void PlayerCore::seek(double pos) {
    seek_pos_ = pos;
    seek_request_ = true;
}

double PlayerCore::get_position() const {
    return get_master_clock();
}

double PlayerCore::get_duration() const {
    if (format_ctx_) {
        return format_ctx_->duration / (double)AV_TIME_BASE;
    }
    return 0.0;
}

void PlayerCore::set_volume(int volume) {
    volume_ = std::max(0, std::min(100, volume));
}

void PlayerCore::read_thread() {
    LOG_INFO("读取线程已启动");
    
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        LOG_ERROR("无法分配 AVPacket");
        return;
    }
    
    int packet_count = 0;
    
    while (!abort_request_) {
        // 处理 seek
        if (seek_request_) {
            // ⚠️ 立即保存 seek 位置并清除请求标志，防止多次 seek 冲突
            double target_pos = seek_pos_;
            seek_request_ = false;  // ⚠️ 立即重置，允许新的 seek 请求排队
            
            int64_t seek_target = target_pos * AV_TIME_BASE;
            
            LOG_INFO("开始 Seek 到: ", target_pos, " 秒");
            
            if (av_seek_frame(format_ctx_, -1, seek_target, AVSEEK_FLAG_BACKWARD) < 0) {
                LOG_ERROR("Seek 失败");
            } else {
                LOG_INFO("Seek 成功");
                
                // ⚠️ 1. 清空数据包队列
                if (video_packet_queue_) video_packet_queue_->flush();
                if (audio_packet_queue_) audio_packet_queue_->flush();
                
                // ⚠️ 2. 清空帧队列（关键！）
                if (video_queue_) {
                    video_queue_->flush();
                    video_queue_->restart();  // 重启队列
                    LOG_INFO("视频帧队列已清空并重启");
                }
                if (audio_queue_) {
                    audio_queue_->flush();
                    audio_queue_->restart();  // 重启队列
                    LOG_INFO("音频帧队列已清空并重启");
                }
                
                // ⚠️ 3. 刷新解码器
                if (video_decoder_) {
                    video_decoder_->flush();
                    video_decoder_->resume();  // 确保解码器不在暂停状态
                    LOG_INFO("视频解码器已刷新并恢复");
                }
                if (audio_decoder_) {
                    audio_decoder_->flush();
                    audio_decoder_->resume();  // 确保解码器不在暂停状态
                    LOG_INFO("音频解码器已刷新并恢复");
                }
                
                // ⚠️ 4. 发送刷新包（告诉解码器已清空）
                if (video_stream_ >= 0) {
                    video_packet_queue_->put_nullpacket(video_stream_);
                }
                if (audio_stream_ >= 0) {
                    audio_packet_queue_->put_nullpacket(audio_stream_);
                }
                
                LOG_INFO("Seek 清理完成");
            }
        }
        
        // ⚠️ 检查队列总大小（参考 ffplay）
        // 只有当两个队列都满时才等待，避免阻塞单个队列
        int total_size = 0;
        bool video_full = false;
        bool audio_full = false;
        
        if (video_packet_queue_) {
            int vs = video_packet_queue_->get_size();
            total_size += vs;
            video_full = (vs > 15 * 1024 * 1024);
        }
        
        if (audio_packet_queue_) {
            int as = audio_packet_queue_->get_size();
            total_size += as;
            audio_full = (as > 15 * 1024 * 1024);
        }
        
        // 只有当任一队列满 AND 总大小超过阈值时才等待
        if ((video_full || audio_full) && total_size > 30 * 1024 * 1024) {
            PLAYER_DELAY(10);
            continue;
        }
        
        // 读取包
        int ret = av_read_frame(format_ctx_, pkt);
        
        if (ret < 0) {
            if (ret == AVERROR_EOF || avio_feof(format_ctx_->pb)) {
                // 文件结束
                if (video_stream_ >= 0) {
                    video_packet_queue_->put_nullpacket(video_stream_);
                }
                if (audio_stream_ >= 0) {
                    audio_packet_queue_->put_nullpacket(audio_stream_);
                }
                break;
            }
            
            if (format_ctx_->pb && format_ctx_->pb->error) {
                break;
            }
            
            PLAYER_DELAY(10);
            continue;
        }
        
        // 分发包到对应队列
        if (pkt->stream_index == video_stream_) {
            video_packet_queue_->put(pkt);
            packet_count++;
//            if (packet_count % 100 == 0) {
//                LOG_INFO("已读取 ", packet_count, " 个视频包");
//            }
        } else if (pkt->stream_index == audio_stream_) {
            audio_packet_queue_->put(pkt);
        } else {
            av_packet_unref(pkt);
        }
    }
    
    av_packet_free(&pkt);
}

int PlayerCore::stream_component_open(int stream_index) {
    if (stream_index < 0 || stream_index >= (int)format_ctx_->nb_streams) {
        return -1;
    }
    
    AVStream* stream = format_ctx_->streams[stream_index];
    AVCodecParameters* codecpar = stream->codecpar;
    
    // 查找解码器
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "找不到解码器" << std::endl;
        return -1;
    }
    
    // 创建解码器上下文
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        return -1;
    }
    
    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
        avcodec_free_context(&codec_ctx);
        return -1;
    }
    
    // 打开解码器
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        return -1;
    }
    
    if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        video_codec_ctx_ = codec_ctx;
        
        LOG_INFO("创建视频解码器...");
        video_decoder_ = std::make_unique<VideoDecoder>();
        video_decoder_->init(codec_ctx, video_packet_queue_.get());
        LOG_INFO("启动视频线程...");
        video_thread_ = std::thread(&PlayerCore::video_thread, this);
        LOG_INFO("视频线程已启动");
        
    } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        audio_codec_ctx_ = codec_ctx;
        
        // ⚠️ 先创建解码器，再启动音频设备！
        audio_decoder_ = std::make_unique<AudioDecoder>();
        audio_decoder_->init(codec_ctx, audio_packet_queue_.get());

        LOG_INFO("音频解码器已创建");
        
#ifdef HAS_SOUNDTOUCH
        // 初始化 SoundTouch（用于倍速播放）
        if (!soundtouch_) {
            soundtouch_ = new soundtouch::SoundTouch();
            
            soundtouch_->setSampleRate(codec_ctx->sample_rate);
            soundtouch_->setChannels(codec_ctx->ch_layout.nb_channels);
            
            // 优化设置
            soundtouch_->setSetting(SETTING_USE_QUICKSEEK, 0);
            soundtouch_->setSetting(SETTING_USE_AA_FILTER, 1);
            soundtouch_->setSetting(SETTING_SEQUENCE_MS, 40);
            soundtouch_->setSetting(SETTING_SEEKWINDOW_MS, 15);
            soundtouch_->setSetting(SETTING_OVERLAP_MS, 8);
            
            // 设置初始播放速率
            double current_rate = playback_rate_.load();
            soundtouch_->setTempo(current_rate);
            
            LOG_INFO("SoundTouch 初始化完成 (采样率: ", codec_ctx->sample_rate, 
                     ", 通道: ", codec_ctx->ch_layout.nb_channels, ")");
        }
#endif
        
#ifndef NO_SDL
        // 配置 SDL 音频（仅桌面平台）
        SDL_AudioSpec wanted_spec, spec;
        wanted_spec.freq = codec_ctx->sample_rate;
        wanted_spec.format = AUDIO_S16SYS;
        wanted_spec.channels = codec_ctx->ch_layout.nb_channels;
        wanted_spec.silence = 0;
        wanted_spec.samples = config_.audio_buffer_size;
        wanted_spec.callback = audio_callback;
        wanted_spec.userdata = this;
        
        audio_dev_ = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &spec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (audio_dev_ == 0) {
            LOG_ERROR("SDL_OpenAudioDevice 失败: ", SDL_GetError());
            return -1;
        }
        
        LOG_INFO("音频设备已打开，准备启动...");
        
        // 启动音频设备（此时解码器已就绪）
        SDL_PauseAudioDevice(audio_dev_, 0);
        
        LOG_INFO("音频设备已打开");
#else
        LOG_INFO("iOS 平台，音频由上层渲染");
#endif
        
        // 启动音频解码线程（提前解码到队列）
        LOG_INFO("启动音频线程...");
        audio_thread_ = std::thread(&PlayerCore::audio_thread, this);
        LOG_INFO("音频线程已启动");
        
#ifndef NO_SDL
        // 启动音频设备（此时解码器已就绪）
        SDL_PauseAudioDevice(audio_dev_, 0);
        LOG_INFO("音频设备已启动");
#endif
    }
    
    return 0;
}

void PlayerCore::stream_component_close(int stream_index) {
    if (stream_index < 0 || stream_index >= (int)format_ctx_->nb_streams) {
        return;
    }
    
    AVCodecParameters* codecpar = format_ctx_->streams[stream_index]->codecpar;
    
    if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        // 解码器不再有内部线程，直接释放即可
        if (video_decoder_) {
            video_decoder_.reset();
            LOG_INFO("视频解码器已释放");
        }
        if (video_codec_ctx_) {
            avcodec_free_context(&video_codec_ctx_);
            video_codec_ctx_ = nullptr;
        }
        video_stream_ = -1;
        
    } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
#ifndef NO_SDL
        // 先停止音频设备（防止回调继续访问解码器）
        if (audio_dev_) {
            SDL_PauseAudioDevice(audio_dev_, 1);  // 暂停
            SDL_CloseAudioDevice(audio_dev_);      // 关闭
            audio_dev_ = 0;
            LOG_INFO("音频设备已关闭");
        }
#endif
        
        // 现在可以安全地释放解码器
        if (audio_decoder_) {
            audio_decoder_.reset();
            LOG_INFO("音频解码器已释放");
        }
        
        if (audio_codec_ctx_) {
            avcodec_free_context(&audio_codec_ctx_);
            audio_codec_ctx_ = nullptr;
        }
        
        audio_stream_ = -1;
    }
}

void PlayerCore::video_thread() {
    LOG_INFO("视频线程已启动");
    
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        LOG_ERROR("无法分配视频帧");
        return;
    }
    
    double pts;
    double duration;
    int frame_count = 0;
    int error_count = 0;  // 添加错误计数
    
    LOG_INFO("[视频线程] 进入主循环, video_decoder_ exists=", (video_decoder_ != nullptr), ", abort_request_=", abort_request_);
    
    while (!abort_request_) {
        // ⚠️ 检查暂停状态
        while (video_decoder_ && video_decoder_->should_pause() && !abort_request_) {
            if (error_count == 0) {
                LOG_INFO("视频解码器暂停中，等待恢复...");
                error_count++;  // 只打印一次
            }
            PLAYER_DELAY(10);
        }
        
        // 重置错误计数
        if (video_decoder_ && !video_decoder_->should_pause()) {
            if (error_count > 0) {
                LOG_INFO("视频解码器已恢复，开始解码...");
            }
            error_count = 0;
        }
        
        if (abort_request_) {
            break;
        }
        // 解码视频帧
        int ret = video_decoder_->decode_frame(frame);
        
        if (ret < 0) {
            // ⚠️ 解码错误，可能是队列为空或其他临时问题
            if (ret != AVERROR(EAGAIN)) {
                error_count++;
                if (error_count <= 3 || error_count % 100 == 0) {
                    LOG_ERROR("视频解码错误: ", ret, " (错误次数: ", error_count, ")");
                }
            }
            PLAYER_DELAY(10);
            continue;  // 继续尝试
        } else if (ret == 0) {
            LOG_INFO("视频解码结束");
            break;
        }
        
        frame_count++;
        if (frame_count % 100 == 0) {
            LOG_INFO("已解码 ", frame_count, " 个视频帧");
        }
        
        // 计算帧时间戳
        pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : frame->pts * av_q2d(format_ctx_->streams[video_stream_]->time_base);
        
        // 计算帧持续时间
        duration = av_q2d(format_ctx_->streams[video_stream_]->time_base);
        
        // 获取可写帧
        VideoFrame* vf = video_queue_->peek_writable();
        if (!vf) {
            LOG_WARNING("视频队列满，等待消费...");
            PLAYER_DELAY(10);
            continue;  // ⚠️ 不要 break，继续尝试
        }
        
        // 复制帧数据
        vf->frame = av_frame_clone(frame);
        vf->pts = pts;
        vf->duration = duration;
        vf->width = frame->width;
        vf->height = frame->height;
        
        // 推入队列
        video_queue_->push();
        
        // 更新视频时钟
        update_video_pts(pts, video_packet_queue_->get_serial());
        
        // ⚠️ 音画同步控制（参考 ffplay）
        if (!isnan(pts)) {
            double diff = pts - get_master_clock();
            
            if (!isnan(diff)) {
                if (diff <= -0.1) {
                    // 视频太慢（落后音频超过 100ms），丢帧
                    LOG_INFO("视频落后，丢帧 diff=", diff);
                    av_frame_unref(frame);
                    video_queue_->next();  // 从队列中移除
                    continue;
                } else if (diff > 0.01) {
                    // 视频太快（领先音频），等待
                    // diff 是媒体时间差，需要转换为物理等待时间（除以 playback_rate）
                    double rate = playback_rate_.load();
                    double physical_delay = diff / rate;
                    if (physical_delay > 0.1) {
                        physical_delay = 0.1;  // 最多等待 100ms
                    }
                    
                    if (frame_count % 100 == 0) {
                        LOG_INFO("视频领先，等待 ", (int)(physical_delay * 1000), " ms");
                    }
                    
                    PLAYER_DELAY((int)(physical_delay * 1000));
                }
            }
        }
        
        av_frame_unref(frame);
        
        // 暂停控制
        while (pause_request_ && !abort_request_) {
            PLAYER_DELAY(10);
        }
    }
    
    av_frame_free(&frame);
}

// ⚠️ 新增：音频解码线程（提前解码到队列）
void PlayerCore::audio_thread() {
    LOG_INFO("音频线程已启动");
    
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        LOG_ERROR("无法分配音频帧");
        return;
    }
    
    int frame_count = 0;
    int error_count = 0;
    
    LOG_INFO("[音频线程] 进入主循环, audio_decoder_ exists=", (audio_decoder_ != nullptr), ", audio_packet_queue_ exists=", (audio_packet_queue_ != nullptr));
    
    while (!abort_request_) {
        // 检查暂停状态
        if (audio_decoder_ && audio_decoder_->should_pause()) {
            if (error_count == 0) {
                LOG_INFO("音频解码器暂停中...");
            }
            while (audio_decoder_->should_pause() && !abort_request_) {
                PLAYER_DELAY(10);
            }
            LOG_INFO("音频解码器恢复");
            error_count = 0;
        }
        
        if (abort_request_) {
            LOG_INFO("[音频线程] 收到终止请求");
            break;
        }
        
        // 检查解码器和队列状态
        if (!audio_decoder_) {
            LOG_ERROR("[音频线程] audio_decoder_ 为空");
            break;
        }
        if (!audio_packet_queue_) {
            LOG_ERROR("[音频线程] audio_packet_queue_ 为空");
            break;
        }
        if (!audio_queue_) {
            LOG_ERROR("[音频线程] audio_queue_ 为空");
            break;
        }
        
        // 解码音频帧
        int ret = audio_decoder_->decode_frame(frame);
        
        if (ret < 0) {
            // ⚠️ 解码错误，可能是队列为空或其他临时问题
            error_count++;
            if (error_count == 1 || error_count % 100 == 0) {
                LOG_ERROR("音频解码错误: ", ret, " (连续 ", error_count, " 次)");
            }
            PLAYER_DELAY(10);
            continue;  // 继续尝试
        } else if (ret == 0) {
            LOG_INFO("音频解码结束");
            break;
        }
        
        // 解码成功，重置错误计数
        error_count = 0;
        
        frame_count++;
        if (frame_count == 1 || frame_count % 100 == 0) {
            LOG_INFO("已解码 ", frame_count, " 个音频帧");
        }
        
        // 计算时间戳
        double pts = (frame->pts == AV_NOPTS_VALUE) ? NAN : 
                     frame->pts * av_q2d(format_ctx_->streams[audio_stream_]->time_base);
        
        // 更新音频时钟
        if (!isnan(pts)) {
            update_audio_pts(pts, audio_packet_queue_->get_serial());
        }
        
        // 获取可写帧
        AudioFrame* af = audio_queue_->peek_writable();
        if (!af) {
            LOG_WARNING("音频队列满，等待消费...");
            PLAYER_DELAY(10);
            continue;  // ⚠️ 不要 break，继续尝试
        }
        
        // 复制帧数据
        af->frame = av_frame_clone(frame);
        af->pts = pts;
        
        // 推入队列
        audio_queue_->push();
        
        av_frame_unref(frame);
        
        // 暂停控制
        while (pause_request_ && !abort_request_) {
            PLAYER_DELAY(10);
        }
    }
    
    av_frame_free(&frame);
    LOG_INFO("音频线程已结束");
}

#ifndef NO_SDL
void PlayerCore::audio_callback(void* userdata, uint8_t* stream, int len) {
    PlayerCore* player = static_cast<PlayerCore*>(userdata);
    player->audio_callback_impl(stream, len);
}

// ⚠️ 重写：只从队列取数据，不解码
void PlayerCore::audio_callback_impl(uint8_t* stream, int len) {
    static int callback_count = 0;
    callback_count++;
    
    SDL_memset(stream, 0, len);
    
    if (!audio_queue_ || !audio_codec_ctx_) {
        if (callback_count % 100 == 0) {
            LOG_WARNING("audio_callback: 队列或上下文为空");
        }
        return;
    }
    
    if (abort_request_) {
        return;
    }
    
    while (len > 0) {
        // 如果缓冲区为空，从队列获取新帧
        if (audio_buf_index_ >= audio_buf_size_) {
            // 检查队列中是否有帧
            int queue_size = audio_queue_->size();
            if (queue_size <= 0) {
                // 队列空，输出静音
                if (callback_count % 100 == 0) {
                    LOG_WARNING("audio_callback: 队列为空，输出静音");
                }
                return;
            }
            
            if (callback_count % 100 == 0) {
                LOG_INFO("audio_callback: 队列大小 = ", queue_size);
            }
            
            AudioFrame* af = audio_queue_->peek_readable();
            if (!af || !af->frame) {
                return;
            }
            
            AVFrame* frame = af->frame;
            
            // 重采样音频
            if (!swr_ctx_) {
                AVChannelLayout out_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
                av_channel_layout_copy(&out_ch_layout, &audio_codec_ctx_->ch_layout);
                
                swr_alloc_set_opts2(&swr_ctx_,
                    &out_ch_layout,
                    AV_SAMPLE_FMT_S16,
                    audio_codec_ctx_->sample_rate,
                    &audio_codec_ctx_->ch_layout,
                    audio_codec_ctx_->sample_fmt,
                    audio_codec_ctx_->sample_rate,
                    0, nullptr);
                
                if (!swr_ctx_ || swr_init(swr_ctx_) < 0) {
                    audio_queue_->next();
                    return;
                }
            }
            
            // 计算输出缓冲区大小
            int out_samples = static_cast<int>(av_rescale_rnd(
                swr_get_delay(swr_ctx_, audio_codec_ctx_->sample_rate) + frame->nb_samples,
                audio_codec_ctx_->sample_rate,
                audio_codec_ctx_->sample_rate,
                AV_ROUND_UP));
            
            int out_size = av_samples_get_buffer_size(
                nullptr,
                audio_codec_ctx_->ch_layout.nb_channels,
                out_samples,
                AV_SAMPLE_FMT_S16,
                1);
            
            if (out_size < 0) {
                audio_queue_->next();
                return;
            }
            
            // 分配输出缓冲区
            if (!audio_buf_ || audio_buf_size_ < (unsigned int)out_size) {
                av_free(audio_buf_);
                audio_buf_ = (uint8_t*)av_malloc(out_size);
                audio_buf_size_ = out_size;
            }
            
            // 执行重采样
            uint8_t* out[] = {audio_buf_};
            int samples = swr_convert(swr_ctx_, out, out_samples,
                                     (const uint8_t**)frame->data, frame->nb_samples);
            
            if (samples < 0) {
                audio_queue_->next();
                return;
            }
            
            // ⚠️ 保存当前帧的 PTS，用于后续时钟更新
            audio_current_pts_ = af->pts;
            audio_current_pts_drift_ = audio_current_pts_ - av_gettime_relative() / 1000000.0;
            
            // 消费队列中的帧
            audio_queue_->next();
            
#ifdef HAS_SOUNDTOUCH
            // 使用 SoundTouch 处理倍速播放
            double current_rate = playback_rate_.load();
            if (soundtouch_ && current_rate != 1.0) {
                int channels = audio_codec_ctx_->ch_layout.nb_channels;
                
                // 转换为 float
                std::vector<float> float_input(samples * channels);
                int16_t* s16_src = (int16_t*)audio_buf_;
                for (int i = 0; i < samples * channels; i++) {
                    float_input[i] = (float)s16_src[i] / 32768.0f;
                }
                
                // 送入样本到 SoundTouch
                soundtouch_->putSamples(float_input.data(), samples);
                
                // 获取可用样本数
                uint32_t available = soundtouch_->numSamples();
                
                // 计算期望输出样本数（基于输入和速率）
                uint32_t expected_output = (uint32_t)(samples / current_rate);
                
                // 只有当积累的样本 >= 期望输出的 80% 时才取出
                if (available >= expected_output * 0.8) {
                    uint32_t samples_to_get = std::max(available, expected_output);
                    std::vector<float> output_buffer(samples_to_get * channels);
                    uint32_t received = soundtouch_->receiveSamples(output_buffer.data(), samples_to_get);
                    
                    if (received > 0) {
                        // 转换回 S16
                        int out_size = received * channels * sizeof(int16_t);
                        if (audio_buf_size_ < (unsigned int)out_size) {
                            av_free(audio_buf_);
                            audio_buf_ = (uint8_t*)av_malloc(out_size * 2);
                        }
                        
                        int16_t* s16_dst = (int16_t*)audio_buf_;
                        for (size_t i = 0; i < received * channels; i++) {
                            float sample = output_buffer[i];
                            if (sample > 1.0f) sample = 1.0f;
                            if (sample < -1.0f) sample = -1.0f;
                            s16_dst[i] = (int16_t)(sample * 32767.0f);
                        }
                        
                        audio_buf_size_ = out_size;
                        audio_buf_index_ = 0;
                    } else {
                        // 降级：使用原始音频
                        audio_buf_size_ = samples * channels * sizeof(int16_t);
                        audio_buf_index_ = 0;
                    }
                } else {
                    // SoundTouch 积累中，输出静音
                    audio_buf_size_ = 0;
                    audio_buf_index_ = 0;
                }
            } else {
                // 1.0x 速度或 SoundTouch 未启用，使用原始音频
                audio_buf_size_ = samples * audio_codec_ctx_->ch_layout.nb_channels * sizeof(int16_t);
                audio_buf_index_ = 0;
            }
#else
            // 非 macOS/Windows 平台，直接使用原始音频
            audio_buf_size_ = samples * audio_codec_ctx_->ch_layout.nb_channels * sizeof(int16_t);
            audio_buf_index_ = 0;
#endif
        }
        
        // 从缓冲区复制数据到输出流
        int len1 = audio_buf_size_ - audio_buf_index_;
        if (len1 > len) {
            len1 = len;
        }
        
        // 应用音量
        if (volume_ != 100 && volume_ > 0) {
            int16_t* samples = (int16_t*)(audio_buf_ + audio_buf_index_);
            int sample_count = len1 / sizeof(int16_t);
            for (int i = 0; i < sample_count; i++) {
                samples[i] = (int16_t)(samples[i] * volume_ / 100);
            }
        }
        
        SDL_MixAudioFormat(stream, audio_buf_ + audio_buf_index_, AUDIO_S16SYS, len1, SDL_MIX_MAXVOLUME);
        
        // ⚠️ 【关键修复】在这里更新音频时钟！
        // 只有在数据真正被复制到输出流后，才更新时钟
        if (!isnan(audio_current_pts_)) {
            int channels = audio_codec_ctx_->ch_layout.nb_channels;
            if (channels > 0) {
                // 计算实际消费的样本数
                int consumed_samples = len1 / (channels * sizeof(int16_t));
                
                // 计算消费时长（媒体时间）
                double consumed_duration = (double)consumed_samples / audio_codec_ctx_->sample_rate;
                
                // ⚠️ 关键修复：SoundTouch 输出的每个样本对应 playback_rate 倍的媒体内容时间
                // 例如：2.0x 时，1024 个原始样本压缩为 512 个输出样本
                //      这 512 个输出样本对应的媒体时间 = 512 * 2.0 / sample_rate（不是 512 / sample_rate）
                // 因此时钟必须乘以 playback_rate，才能让主时钟以 2x 的速度推进
                {
                    double current_rate = playback_rate_.load();
                    if (current_rate != 1.0) {
                        consumed_duration *= current_rate;
                    }
                }
                
                // 更新 PTS（逐步累加）
                audio_current_pts_ += consumed_duration;
                audio_current_pts_drift_ = audio_current_pts_ - av_gettime_relative() / 1000000.0;
                
                // 更新音频时钟
                update_audio_pts(audio_current_pts_, 0);
            }
        }
        
        len -= len1;
        stream += len1;
        audio_buf_index_ += len1;
    }
}
#endif // NO_SDL

double PlayerCore::get_master_clock() const {
    switch (config_.sync_mode) {
        case SyncMode::AudioMaster:
            return audio_clock_.get_clock();
        case SyncMode::VideoMaster:
            return video_clock_.get_clock();
        case SyncMode::ExternalClock:
            return external_clock_.get_clock();
        default:
            return 0.0;
    }
}

void PlayerCore::update_video_pts(double pts, int serial) {
    video_clock_.set_clock(pts, serial);
}

void PlayerCore::update_audio_pts(double pts, int serial) {
    audio_clock_.set_clock(pts, serial);
}

void PlayerCore::set_state(PlayerState state) {
    if (state_ != state) {
        state_ = state;
        if (state_changed_callback_) {
            state_changed_callback_(state);
        }
    }
}

void PlayerCore::emit_error(const std::string& error) {
    if (error_callback_) {
        error_callback_(error);
    }
}

// 设置播放速率（倍速播放）
void PlayerCore::set_playback_rate(double rate) {
    // 限制范围在 0.5 ~ 2.0
    if (rate < 0.5) rate = 0.5;
    if (rate > 2.0) rate = 2.0;
    
    playback_rate_.store(rate, std::memory_order_release);
    
#ifdef HAS_SOUNDTOUCH
    // 更新 SoundTouch 的速度设置
    if (soundtouch_) {
        // 先清空缓冲区，避免旧数据影响新速度
        soundtouch_->clear();
        
        // 设置新速度
        soundtouch_->setTempo(rate);
    }
#endif
}

} // namespace hxcplayer
