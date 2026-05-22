/**
 * @file player_core.h
 * @brief 播放器核心类（参照 ffplay 架构）
 */

#ifndef YXVODPLAYER_PLAYER_CORE_H
#define YXVODPLAYER_PLAYER_CORE_H

#include "hxc_player_types.h"
#include "hxc_packet_queue.h"
#include "hxc_frame_queue.h"
#include "hxc_decoder.h"
#include "hxc_custom_io.h"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <cstdint>
#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
#ifndef NO_SDL
#include <SDL2/SDL.h>
#endif
}

// ⚠️ SoundTouch 库（用于倍速播放，保持音调）
// 只有在编译时找到 SoundTouch 库时才包含
#ifdef HAS_SOUNDTOUCH
    #if defined(__APPLE__)
        // macOS/iOS 使用 Homebrew 安装的 SoundTouch
        #include <soundtouch/SoundTouch.h>
    #elif defined(_WIN32)
        // Windows 使用本地编译或 vcpkg 的 SoundTouch
        #include <soundtouch/SoundTouch.h>
    #elif defined(__ANDROID__)
        // Android 使用 NDK 编译的 SoundTouch
        #include <soundtouch/SoundTouch.h>
    #endif
#endif

namespace hxcplayer {

// ⚠️ 播放器错误码定义
enum PlayerErrorCode {
    // 自定义错误码 (1-999)
    ERROR_NONE = 0,                      // 无错误
    ERROR_INVALID_URL = -1001,               // 无效的 URL
    ERROR_OPEN_INPUT_FAILED = -1002,         // 打开输入失败
    ERROR_FIND_STREAM_INFO_FAILED = -1003,   // 查找流信息失败
    ERROR_NO_VIDEO_STREAM = -1004,           // 没有视频流
    ERROR_NO_AUDIO_STREAM = -1005,           // 没有音频流
    ERROR_CODEC_NOT_FOUND = -1006,           // 找不到解码器
    ERROR_CODEC_OPEN_FAILED = -1007,         // 打开解码器失败
    ERROR_ALLOC_CONTEXT_FAILED = -1008,      // 分配上下文失败
    
    ERROR_SDL_INIT_FAILED = -1009,           // SDL 初始化失败
    ERROR_AUDIO_DEVICE_OPEN_FAILED = -1010, // 音频设备打开失败
    ERROR_SEEK_FAILED = -1011,              // Seek 操作失败
    ERROR_READ_FRAME_FAILED = -1012,        // 读取帧失败
    ERROR_DECODE_FAILED = -1013,            // 解码失败
    ERROR_OUT_OF_MEMORY = -1014,            // 内存不足
    ERROR_NET_CONNECTION_TIMEOUT = -2001,      //网络连接超时
    ERROR_NET_CONNECTION_REFUSED = -2002,       //服务器拒绝连接
    ERROR_NET_UNREACHABLE = -2003,          //网络不可达，请检查网络设置
    
    ERROR_HTTP_BAD_REQUEST = -3001,         //HTTP 请求错误（400）
    ERROR_HTTP_NOT_FOUND = -3002,           // http 404
    ERROR_HTTP_SERVER_ERROR = -3003,        // server error
    ERROR_HTTP_UNAUTHORIZED = -3004,        //需要身份验证
    ERROR_HTTP_FORBIDDEN = -3005,           //访问被禁止（403）
    ERROR_INPUT_INVALID_DATA = -1018,        //无效数据
    ERROR_NOT_SUPPORT = -1019,
    ERROR_UNKNOWN = -1099,                 // 未知错误
    // Secure HLS 鉴权/密钥错误
    ERROR_SECURE_AUTH_FAILED = -4101,      // 鉴权失败
    ERROR_SECURE_AUTH_EXPIRED = -4102,     // 鉴权过期
    ERROR_SECURE_KEY_EXPIRED = -4103,      // 密钥过期
    ERROR_SECURE_KEY_INVALID = -4104,      // 密钥非法
    ERROR_SECURE_REPLAY_BLOCKED = -4105,   // 重放被拒绝
    ERROR_SECURE_CLOCK_SKEW = -4106,       // 设备时钟偏移过大
    
    // FFmpeg 错误码范围 (负数)
    // 使用 FFmpeg 原始错误码，例如：
    // AVERROR_EOF, AVERROR(ENOMEM), AVERROR(EINVAL) 等
    // 这些值保持为 FFmpeg 的负数错误码
};

// ⚠️ 数据源模式
enum class DataSourceMode {
    Default = 0,     // 默认模式（FFmpeg 直接打开）
    CustomHTTP = 1,  // 自定义 HTTP Range 下载器
    CustomFile = 2,  // 本地文件自定义读取（支持加密文件头解密）
    SecureHLS = 3,   // HLS AES-128 鉴权播放
    // 未来可扩展：
    // Encrypted = 2,   // 加密视频
    // P2P = 3,         // P2P 数据源
    // Cached = 4,      // 本地缓存
};

// ⚠️ 自定义数据源配置
struct CustomDataSourceConfig {
    int timeout_ms = 30000;           // 超时时间（毫秒）
    int max_retries = 3;              // 最大重试次数
    size_t cache_size = 2 * 1024 * 1024;  // 缓存大小（字节）
    size_t avio_buffer_size = 64 * 1024;  // AVIO 缓冲区大小（字节）
    bool encrypted_file = false;      // 是否为加密文件（仅解密文件头前 100 字节）
    // SecureHLS（Header 透传）参数
    const char* secure_headers = nullptr;  // "Authorization: xxx\r\nX-Playback-Session: yyy\r\n"
};

struct SecureHLSSession {
    std::string m3u8_url;
    std::string request_headers;
};

struct PlaybackResourceDelegate {
    std::function<void(const std::string& url)> on_request_manifest;
    std::function<void(const std::string& url)> on_request_segment;
    std::function<void(const std::string& url, int64_t bytes)> on_store_segment;
    std::function<void(const std::string& key_uri)> on_request_key;
};

/**
 * @brief 播放器核心类
 * 负责媒体文件的解复用、解码、同步和播放控制
 */
class PlayerCore {
public:
    PlayerCore();
    ~PlayerCore();
    
    // 设置配置
    void set_config(const PlayerConfig& config) { config_ = config; }
    const PlayerConfig& get_config() const { return config_; }
    void set_decode_mode(DecodeMode mode) { config_.decode_mode = mode; }
    DecodeMode get_decode_mode() const { return config_.decode_mode; }
    
    // 打开媒体文件
    int open(const std::string& filename);
    
    // 使用自定义数据源打开。url_for_format 建议传入与数据源一致的地址（如本地 m3u8 绝对路径）：
    // 仅用于 FFmpeg 探测格式与 HLS 解析相对分片路径；实际字节仍从 custom_io 读取。
    int open_with_custom_io(std::unique_ptr<CustomAVIOContext> custom_io,
                            const std::string& url_for_format = {});
    
    // 使用指定数据源模式打开（新接口）
    int open_with_mode(const std::string& url, DataSourceMode mode, const CustomDataSourceConfig& config = CustomDataSourceConfig());
    void set_playback_resource_delegate(PlaybackResourceDelegate delegate) { resource_delegate_ = std::move(delegate); }
    
    // 关闭
    void close();
    
    // 播放控制
    void play();
    void pause();
    void stop();
    void seek(double pos);  // 秒
    
    // 获取状态
    PlayerState get_state() const { return state_.load(std::memory_order_acquire); }
    PipelineState get_pipeline_state() const { return pipeline_state_.load(std::memory_order_acquire); }
    bool get_play_when_ready() const { return play_when_ready_.load(std::memory_order_acquire); }
    bool is_playing() const;
    const MediaInfo& get_media_info() const { return media_info_; }
    double get_position() const;    // 当前播放位置（秒）
    double get_duration() const;    // 总时长（秒）
    // seek 后由平台层在首帧显示时调用：将 master clock 重锚到指定 pts，
    // 使 delay = 0，消除 clock 因音频暂停期间自动流逝带来的累积偏差。
    void anchor_clock(double pts);  // 重锚时钟（秒）
    bool is_video_hardware_decoding() const { return video_hw_decode_active_.load(std::memory_order_acquire); }
    std::string get_video_decode_diagnostic() const;
    
    // 获取帧队列（用于渲染）
    FrameQueue<VideoFrame>* get_video_queue() { return video_queue_.get(); }
    FrameQueue<AudioFrame>* get_audio_queue() { return audio_queue_.get(); }
    int get_video_packet_serial() const { return video_packet_queue_ ? video_packet_queue_->get_serial() : 0; }
    int get_audio_packet_serial() const { return audio_packet_queue_ ? audio_packet_queue_->get_serial() : 0; }
    
    // 音量控制
    void set_volume(int volume);  // 0-100
    int get_volume() const { return volume_; }
    
    // ⚠️ 视频显示模式管理
    void set_aspect_ratio_mode(AspectRatioMode mode) { aspect_ratio_mode_ = mode; }
    AspectRatioMode get_aspect_ratio_mode() const { return aspect_ratio_mode_; }
    
    // ⚠️ 视频渲染管理
    enum class RenderMode {
        Auto = 0,    // 自动渲染
        Manual = 1   // 手动渲染
    };
    
    void set_render_window(void* window_handle);
    void set_render_mode(RenderMode mode) { render_mode_ = mode; }
    RenderMode get_render_mode() const { return render_mode_; }
    int refresh_video();  // 刷新视频帧到窗口
    
    // ⚠️ 播放速率控制（倍速播放）
    void set_playback_rate(double rate);  // 0.5 ~ 2.0
    double get_playback_rate() const { return playback_rate_; }
    
    // ⚠️ 音频时钟更新（供 iOS/macOS/Android 平台使用）
    void update_audio_pts(double pts, int serial);
    double get_seek_target_pos() const { return seek_target_pos_.load(std::memory_order_acquire); }
    int get_post_seek_warmup_frames() const { return post_seek_warmup_frames_.load(std::memory_order_acquire); }
    
    // 事件回调
    using StateChangedCallback = std::function<void(PlayerState)>;
    using ErrorCallback = std::function<void(int, const std::string&)>;  // 错误码 + 错误信息
    using PositionChangedCallback = std::function<void(double)>;  // 真实播放位置
    using BufferProgressCallback = std::function<void(double)>;   // 缓冲进度（解码位置）
    using PlaybackCompletedCallback = std::function<void()>;      // 播放完成
    using LoadingCallback = std::function<void(bool)>;            // 网络加载状态（true=加载中，false=加载完成）
    using PipelineStateChangedCallback = std::function<void(PipelineState)>;
    using PlayingChangedCallback = std::function<void(bool)>;
    
    void set_state_changed_callback(StateChangedCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        state_changed_callback_ = callback;
    }
    
    void set_error_callback(ErrorCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        error_callback_ = callback;
    }
    
    void set_position_changed_callback(PositionChangedCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        position_changed_callback_ = callback;
    }
    
    void set_buffer_progress_callback(BufferProgressCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        buffer_progress_callback_ = callback;
    }
    
    void set_playback_completed_callback(PlaybackCompletedCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        playback_completed_callback_ = callback;
    }
    
    void set_loading_callback(LoadingCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        loading_callback_ = callback;
    }
    void set_pipeline_state_changed_callback(PipelineStateChangedCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        pipeline_state_changed_callback_ = callback;
    }
    void set_playing_changed_callback(PlayingChangedCallback callback) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        playing_changed_callback_ = callback;
    }

private:
    // 读取线程（解复用）
    void read_thread();
    
    // 解码线程
    void video_thread();
    void audio_thread();  // ⚠️ 新增：音频解码线程
    void progress_timer_thread();  // ⚠️ 播放进度回调定时器线程
    
#ifndef NO_SDL
    // SDL 音频回调（只负责从队列取数据）- 仅桌面平台
    static void audio_callback(void* userdata, uint8_t* stream, int len);
    void audio_callback_impl(uint8_t* stream, int len);
#endif
    
    // 同步控制
    double get_master_clock() const;
    void update_video_pts(double pts, int serial);
    
    // 流打开
    int stream_component_open(int stream_index);
    void stream_component_close(int stream_index);

    // 打开后的公共处理（查找流、打开解码器等）
    int open_common_process(const std::string &filename);
    
    // 状态更新
    void set_state(PlayerState state);
    void set_pipeline_state(PipelineState state);
    void set_play_when_ready_internal(bool play_when_ready);
    void refresh_effective_playing_state();
    void update_pipeline_state_from_runtime();
    bool has_first_renderable_frame_ready() const;
    void emit_error(int error_code, const std::string& error_msg);
    void set_seek_loading(bool is_loading);
    void set_io_loading(bool is_loading);
    void set_starvation_loading(bool is_loading);
    void refresh_loading_state();
    /** 播放结束(Ended)后 seek：重置完成态并恢复 playWhenReady，无需 App 重开流 */
    void prepare_seek_from_ended_session();

private:
    PlayerConfig config_;
    std::atomic<PlayerState> state_;
    std::atomic<PipelineState> pipeline_state_{PipelineState::Idle};
    std::atomic<bool> play_when_ready_{false};
    std::atomic<bool> first_video_frame_ready_{false};
    std::atomic<bool> first_audio_frame_ready_{false};
    std::atomic<bool> effective_is_playing_{false};
    MediaInfo media_info_;
    
    // FFmpeg 对象
    AVFormatContext* format_ctx_;
    int video_stream_;
    int audio_stream_;
    int subtitle_stream_;
    bool video_stream_opened_;   // 视频流组件是否成功打开
    bool audio_stream_opened_;   // 音频流组件是否成功打开
    
    AVCodecContext* video_codec_ctx_;
    AVCodecContext* audio_codec_ctx_;
    
    // 数据包队列
    std::unique_ptr<PacketQueue> video_packet_queue_;
    std::unique_ptr<PacketQueue> audio_packet_queue_;
    std::unique_ptr<PacketQueue> subtitle_packet_queue_;
    
    // 帧队列
    std::unique_ptr<FrameQueue<VideoFrame>> video_queue_;
    std::unique_ptr<FrameQueue<AudioFrame>> audio_queue_;
    
    // 解码器
    std::unique_ptr<VideoDecoder> video_decoder_;
    std::unique_ptr<AudioDecoder> audio_decoder_;
    
    // 渲染相关辅助函数
    void init_sdl_renderer();    // 初始化 SDL 渲染器
    void cleanup_sdl_renderer();  // 清理 SDL 渲染器
    void render_video_frame(const VideoFrame& frame);  // 渲染视频帧
    void video_refresh_thread_func();  // 视频刷新线程函数
    
    // 时钟
    Clock video_clock_;
    Clock audio_clock_;
    Clock external_clock_;
    
    // 线程
    std::thread read_thread_;
    std::thread video_thread_;
    std::thread audio_thread_;  // ⚠️ 新增：音频解码线程
    std::thread progress_timer_thread_;  // ⚠️ 播放进度回调定时器线程
    std::thread video_refresh_thread_;  // ⚠️ 视频渲染刷新线程（自动渲染模式）
    
    // 控制标志
    std::atomic<bool> abort_request_;
    std::atomic<bool> pause_request_;
    std::atomic<bool> seek_request_;
    std::atomic<double> seek_pos_;
    std::atomic<bool> seeking_;  // ⚠️ 标识正在 seek，暂停进度回调
    std::atomic<bool> seek_loading_{false};  // seek 触发的 loading
    std::atomic<bool> io_loading_{false};    // 网络读取失败触发的 loading
    std::atomic<bool> starvation_loading_{false}; // 队列低水位/进度停滞触发的 loading
    std::atomic<bool> loading_notified_{false};  // 对外已通知的 loading 状态
    std::atomic<double> seek_target_pos_;  // ⚠️ seek 的目标位置，在 seek 期间返回此值
    std::atomic<int>   post_seek_warmup_frames_{0}; // seek 结束后放宽 A/V 同步丢帧阈值的剩余帧数
    std::atomic<bool> decode_finished_;  // ⚠️ 标识视频解码已结束（用于判断播放完成）
    std::atomic<bool> video_hw_decode_active_{false}; // 当前视频流是否在硬解
    mutable std::mutex video_decode_diag_mutex_;
    std::string video_decode_diag_;
    std::atomic<int64_t> io_last_packet_us_{0}; // 最近一次成功读取 packet 的时间（us）
    std::atomic<bool> io_watchdog_disabled_{false}; // 是否禁用阻塞读 watchdog（用于 loopback HTTP）
    int64_t io_interrupt_timeout_us_{8000000};   // 阻塞读中断阈值（us）
    std::atomic<bool> playback_completed_notified_;  // ⚠️ 是否已通知播放完成（避免重复通知）
    
#ifndef NO_SDL
    // SDL 音频（仅桌面平台）
    SDL_AudioDeviceID audio_dev_;
#endif
    int volume_;
    
    // 重采样上下文
    SwrContext* swr_ctx_;
    uint8_t* audio_buf_;
    unsigned int audio_buf_size_;
    unsigned int audio_buf_index_;
    
    // ⚠️ 音频时钟跟踪变量
    double audio_current_pts_;          // 当前音频帧的 PTS
    double audio_current_pts_drift_;    // PTS 漂移
    
    // ⚠️ 倍速播放支持（SoundTouch）
#ifdef HAS_SOUNDTOUCH
    soundtouch::SoundTouch* soundtouch_;
    std::vector<float> soundtouch_buffer_;  // SoundTouch 输出缓冲区
    size_t soundtouch_buffer_index_;        // 当前读取位置
#endif
    std::atomic<double> playback_rate_;     // 当前播放速率
    
    // ⚠️ 视频渲染相关
    void* render_window_;                   // 渲染窗口句柄 (HWND/NSView*/QWidget*/CWnd*)
    RenderMode render_mode_;                // 渲染模式
    AspectRatioMode aspect_ratio_mode_;     // 宽高比模式
    
#ifndef NO_SDL
    SDL_Renderer* sdl_renderer_;            // SDL 渲染器（用于自动渲染）
    SDL_Texture* sdl_texture_;              // SDL 纹理
    int texture_width_;                     // 纹理宽度
    int texture_height_;                    // 纹理高度
    
    // 最后一帧的缓存（用于 resize 时避免黑屏）
    std::vector<uint8_t> last_frame_y_;     // Y 平面数据
    std::vector<uint8_t> last_frame_u_;     // U 平面数据
    std::vector<uint8_t> last_frame_v_;     // V 平面数据
    int last_frame_width_;                  // 最后一帧宽度
    int last_frame_height_;                 // 最后一帧高度
    bool has_last_frame_;                   // 是否有缓存的最后一帧
#endif
    
    // 回调
    StateChangedCallback state_changed_callback_;
    ErrorCallback error_callback_;
    PositionChangedCallback position_changed_callback_;  // 真实播放位置回调
    BufferProgressCallback buffer_progress_callback_;    // 缓冲进度回调
    PlaybackCompletedCallback playback_completed_callback_;  // 播放完成回调
    LoadingCallback loading_callback_;                   // 网络加载回调
    PipelineStateChangedCallback pipeline_state_changed_callback_;
    PlayingChangedCallback playing_changed_callback_;
    mutable std::mutex callback_mutex_;
    
    // 自定义数据源
    std::unique_ptr<CustomAVIOContext> custom_io_;       // 自定义 AVIOContext
    PlaybackResourceDelegate resource_delegate_;
    SecureHLSSession secure_session_;
};

} // namespace hxcplayer

#endif // YXVODPLAYER_PLAYER_CORE_H
