#ifndef ANDROID_PLAYER_H
#define ANDROID_PLAYER_H

#include <android/native_window.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>

// FFmpeg swscale for hardware-accelerated scaling
extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

// 前向声明
struct PlayerCoreHandle;
namespace hxcplayer {
    class ICustomDataSource;
}

/**
 * Android 播放器实现类
 * 集成 HXC 核心播放器，实现视频渲染和音频输出
 */
class AndroidPlayer {
public:
    AndroidPlayer();
    ~AndroidPlayer();
    
    // 设置渲染 Surface
    void setSurface(ANativeWindow* window);
    
    // 更新 Surface 尺寸
    void updateSurfaceSize(int width, int height);
    
    // 打开 URL
    bool openURL(const char* url);
    bool openURL(const char* url, double start_position);

    // 使用自定义 HTTP 模式打开（encrypted_file：是否对文件头前 100 字节做解密，与核心层一致）
    bool openWithCustomHTTP(const char* url, int timeout_ms = 30000, int max_retries = 3, bool encrypted_file = false);

    // 使用自定义本地文件模式打开（经 CustomAVIOContext + LocalFileDataSource，支持可选文件头解密）
    bool openWithCustomFile(const char* path, size_t avio_buffer_size = 64 * 1024, bool encrypted_file = false);
    
    // 播放控制
    void play();
    void pause();
    void stop();
    void seekTo(double position);
    
    // 设置参数
    void setPlaybackRate(float rate);
    void setVolume(float volume);
    void setAspectRatioMode(int mode);
    void setDecodeMode(int mode);
    int getDecodeMode() const;
    
    // 获取状态
    double getDuration() const;
    double getPosition() const;
    int getState() const;
    int getPipelineState() const;
    bool getPlayWhenReady() const;
    bool isPlaying() const;
    void setPlayWhenReady(bool play_when_ready);
    bool isLoading() const;
    bool isHardwareDecodingActive() const;
    bool consumeLastError(int& error_code, std::string& error_message);
    bool consumePlaybackCompleted();

private:
    // 核心播放器句柄
    PlayerCoreHandle* player_core_;
    
    // Surface 相关
    ANativeWindow* native_window_;
    std::mutex window_mutex_;
    int surface_width_;
    int surface_height_;
    int aspect_ratio_mode_; // 0=FIT, 1=FILL
    int decode_mode_;       // 0=software, 1=hardware
    bool surface_configured_;  // Surface 是否已配置
    // Surface/尺寸变更代数：用于避免 setSurface/updateSurfaceSize 与 renderLoop 的竞态
    std::atomic<uint64_t> surface_generation_{0};
    
    // FFmpeg swscale context for YUV->RGB conversion
    SwsContext* sws_ctx_;
    uint8_t* rgb_buffer_;
    int rgb_buffer_size_;
    int last_video_width_;   // 上次视频尺寸
    int last_video_height_;
    int last_target_width_;  // 上次目标尺寸
    int last_target_height_;
    
    // 渲染线程
    std::thread render_thread_;
    std::atomic<bool> render_running_;
    void renderLoop();
    // 返回 swscale 耗时（ms），失败时返回 -1
    int renderFrame(void* y_data, void* u_data, void* v_data,
                   int y_linesize, int u_linesize, int v_linesize,
                   int width, int height);
    
    // OpenSL ES 音频输出
    SLObjectItf engineObject_;
    SLEngineItf engineEngine_;
    SLObjectItf outputMixObject_;
    SLObjectItf playerObject_;
    SLPlayItf playItf_;
    SLAndroidSimpleBufferQueueItf bufferQueueItf_;
    SLVolumeItf volumeItf_;
    bool audio_initialized_;  // 音频是否已初始化
    int audio_sample_rate_;   // 音频采样率
    int audio_channels_;      // 音频声道数
    
    bool initAudioOutput(int sample_rate, int channels);
    void destroyAudioOutput();
    static void audioCallback(SLAndroidSimpleBufferQueueItf bq, void* context);
    void onAudioData(SLAndroidSimpleBufferQueueItf bq);
    
    // 音频缓冲区（使用较小的缓冲区，减少内存占用）
    static constexpr int MAX_AUDIO_BUFFER_SIZE = 8192; // 8KB，从 16KB 减少
    uint8_t audio_buffer_[MAX_AUDIO_BUFFER_SIZE];
    int audio_buffer_size_;   // 实际使用的缓冲区大小
    std::mutex audio_mutex_;

    // 加载状态（用于业务层显示 loading 动画）
    std::atomic<bool> is_loading_;
    static void loadingStateCallback(bool is_loading, void* user_data);

    // 播放中错误透传（由 core 回调写入，JNI 轮询消费）
    std::mutex error_mutex_;
    std::atomic<bool> has_pending_error_;
    int last_error_code_;
    std::string last_error_message_;
    static void errorStateCallback(int error_code, const char* error_msg, void* user_data);

    // 播放完成透传（由 core 回调写入，JNI 轮询消费）
    std::atomic<bool> has_pending_playback_completed_;
    static void playbackCompletedCallback(void* user_data);
};

#endif // ANDROID_PLAYER_H
