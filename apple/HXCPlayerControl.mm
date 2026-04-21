/**
 * @file HXCPlayerControl.mm
 * @brief Apple 平台统一播放器实现（支持 iOS 和 macOS）
 */

// 只包含 C 桥接层（不包含 FFmpeg 头文件）
#include "hxc_player_core_c_bridge.h"

// 包含 Objective-C 头文件
#import "HXCPlayerControl.h"
#import "HXCPlayerLog.h"
#import "HXCPlayerView.h"
#import "HXCPlayerLicenseManager.h"

// 包含 AVFoundation（没有冲突）
#import <AVFoundation/AVFoundation.h>
#if TARGET_OS_IOS
#import <AVKit/AVKit.h>
#endif

// 系统框架
#include <AudioToolbox/AudioToolbox.h>

// C++ 播放器包装器
class PlayerCoreWrapper {
public:
    PlayerCoreWrapper() {
        handle_ = player_core_create();
    }
    
    ~PlayerCoreWrapper() {
        if (handle_) {
            player_core_destroy(handle_);
        }
    }
    
    PlayerCoreHandle* handle() { return handle_; }
    
private:
    PlayerCoreHandle* handle_;
};

// AudioQueue 缓冲区数量
static const int kNumberOfBuffers = 3;

// 文件日志：避免每个播放器 init 都覆盖用户通过 +setLogDir: 指定的路径；默认路径仅在「用户未显式设置」时安装一次。
static BOOL gHXCUserSetLogDirExplicitly = NO;
static BOOL gHXCDefaultFileLoggingInstalled = NO;

// ========== C 回调函数前向声明 ==========
static void state_changed_callback_c(PlayerStateC state, void* user_data);
static void error_callback_c(int error_code, const char* error_msg, void* user_data);
static void position_changed_callback_c(double position, void* user_data);
static void buffer_progress_callback_c(double position, void* user_data);
static void playback_completed_callback_c(void* user_data);
static void loading_callback_c(bool is_loading, void* user_data);
static void pipeline_state_changed_callback_c(PlayerPipelineStateC state, void* user_data);
static void playing_changed_callback_c(int is_playing, void* user_data);

@interface HXCPlayerControl () {
    PlayerCoreWrapper *_wrapper;
    
    // 音频渲染（AudioQueue）
    AudioQueueRef _audioQueue;
    AudioQueueBufferRef _audioBuffers[kNumberOfBuffers];
    AudioStreamBasicDescription _audioFormat;
    BOOL _audioQueueRunning;
    
    // 视频渲染定时器（平台特定）
#if TARGET_OS_IOS
    CADisplayLink *_displayLink;
    CMSampleBufferRef _lastRenderedSampleBuffer;
    BOOL _hxcWasPausedBeforeBackground;
#else  // macOS
    CVDisplayLinkRef _displayLink;
#endif
    
    // 状态
    HXCPlayerState _state;
    HXCPlayerPipelineState _pipelineState;
    BOOL _playWhenReady;
    BOOL _isPlaying;
    double _duration;
    double _position;
    NSString *_playerUrl;
    HXCPlayerDataSourcePlayModel *_lastOpenPlayModel;
    HXCAspectRatioMode _aspectRatioMode;
    int _videoWidth;
    int _videoHeight;
    
    // 进度更新回调限流
    CFTimeInterval _lastPositionUpdateTime;

    // 弱网恢复与 QoE
    BOOL _autoReopenOnRecoverableErrorEnabled;
    NSInteger _autoReopenMaxAttempts;
    NSInteger _autoReopenAttemptCount;
    BOOL _autoReopenInFlight;
    BOOL _networkLoading;
    CFTimeInterval _networkLoadingBeginTime;
    NSInteger _networkTotalStallMs;
    NSInteger _networkReconnectCount;
    int64_t _renderGeneration;      // 渲染代数：用于丢弃 seek/stop 前的异步入队任务
    double _lastEnqueuedVideoPTS;   // 上次入队到 displayLayer 的 PTS
    BOOL _hasRenderedFirstVideoFrame;
    BOOL _deferAudioStartUntilFirstVideoFrame;
    int64_t _audioStartGeneration;
    BOOL _firstFrameBootstrapActive;
}

#if TARGET_OS_IOS
// 画中画相关（仅 iOS）
@property (nonatomic, strong) AVPictureInPictureController *pictureInPictureController;
#if __IPHONE_OS_VERSION_MAX_ALLOWED >= 150000
@property (nonatomic, strong) AVPictureInPictureControllerContentSource *pipContentSource API_AVAILABLE(ios(15.0));
#else
@property (nonatomic, strong) id pipContentSource;
#endif
#endif

@property (nonatomic, strong) HXCPlayerView *videoView;
@property (nonatomic, strong) dispatch_queue_t renderQueue;


-(void)playerStateChange:(PlayerStateC)state;
-(void)playerPipelineStateChange:(PlayerPipelineStateC)state;
-(void)playerPlayingChange:(BOOL)isPlaying;
-(void)playerPositionChange:(double)position;
- (HXCPlayerState)hxc_resolveUnifiedStateFromCoreState:(PlayerStateC)coreState
                                          pipelineState:(PlayerPipelineStateC)pipelineState
                                          playWhenReady:(BOOL)playWhenReady
                                              isPlaying:(BOOL)isPlaying;
- (void)hxc_syncUnifiedStateAndNotify;

/// 门禁开启且未通过 License 校验时回调 delegate 并返回 NO
- (BOOL)hxc_licenseAllowedOrNotifyForAction:(NSString *)action;

+ (void)hxc_applyDefaultFileLoggingIfNeeded;
+ (BOOL)hxc_installFileLoggingAtPath:(NSString *)dir error:(NSError *__autoreleasing *)outError;

- (BOOL)hxc_isRecoverableErrorCode:(NSInteger)errorCode;
- (void)hxc_tryAutoReopenForErrorCode:(NSInteger)errorCode;
- (void)hxc_onLoadingStateChanged:(BOOL)isLoading;
- (void)hxc_notifyNetworkQoEWithCurrentStallMs:(NSInteger)currentStallMs;

#if TARGET_OS_IOS
- (void)hxc_registerAppLifecycleNotifications;
- (void)hxc_unregisterAppLifecycleNotifications;
- (void)hxc_appWillResignActive:(NSNotification *)note;
- (void)hxc_appDidBecomeActive:(NSNotification *)note;
- (void)hxc_cacheLastRenderedSampleBuffer:(CMSampleBufferRef)sampleBuffer;
- (void)hxc_clearLastRenderedSampleBuffer;
- (void)hxc_restorePausedFrameIfNeeded;
#endif

@end

#if TARGET_OS_IOS
// iOS: 添加画中画代理协议
@interface HXCPlayerControl () <AVPictureInPictureControllerDelegate, AVPictureInPictureSampleBufferPlaybackDelegate>
@end
#endif

// ========== C 回调函数实现（在 @interface 扩展之后，可以访问实例变量）==========

// 状态变化回调
static void state_changed_callback_c(PlayerStateC state, void* user_data) {
    HXCPlayerControl* control = (__bridge HXCPlayerControl*)user_data;
    dispatch_async(dispatch_get_main_queue(), ^{
        [control playerStateChange:state];
    });
}

// 错误回调
static void error_callback_c(int error_code, const char* error_msg, void* user_data) {
    HXCPlayerControl* self = (__bridge HXCPlayerControl*)user_data;
    NSString* errorStr = error_msg ? [NSString stringWithUTF8String:error_msg] : @"Unknown error";

    dispatch_async(dispatch_get_main_queue(), ^{
        [self hxc_tryAutoReopenForErrorCode:error_code];
        if ([self.delegate respondsToSelector:@selector(player:didFailWithError:)]) {
            NSError* nsError = [NSError errorWithDomain:@"HXCPlayerErrorDomain"
                                                   code:error_code
                                               userInfo:@{NSLocalizedDescriptionKey: errorStr}];
            [self.delegate player:self didFailWithError:nsError];
        }
    });
}

// 播放进度回调（真实播放位置）
static void position_changed_callback_c(double position, void* user_data) {
    HXCPlayerControl* control = (__bridge HXCPlayerControl*)user_data;
    dispatch_async(dispatch_get_main_queue(), ^{
        [control playerPositionChange:position];
    });
}

// 缓冲进度回调（解码位置）
static void buffer_progress_callback_c(double position, void* user_data) {
    HXCPlayerControl* self = (__bridge HXCPlayerControl*)user_data;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([self.delegate respondsToSelector:@selector(player:didUpdateBufferProgress:)]) {
            [self.delegate player:self didUpdateBufferProgress:position];
        }
    });
}

// 播放完成回调
static void playback_completed_callback_c(void* user_data) {
    HXCPlayerControl* self = (__bridge HXCPlayerControl*)user_data;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([self.delegate respondsToSelector:@selector(playerDidFinishPlaying:)]) {
            [self.delegate playerDidFinishPlaying:self];
        }
    });
}

// 网络加载状态回调
static void loading_callback_c(bool is_loading, void* user_data) {
    HXCPlayerControl* self = (__bridge HXCPlayerControl*)user_data;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        [self hxc_onLoadingStateChanged:is_loading];
        if ([self.delegate respondsToSelector:@selector(player:didChangeLoadingState:)]) {
            [self.delegate player:self didChangeLoadingState:is_loading];
        }
    });
}

static void pipeline_state_changed_callback_c(PlayerPipelineStateC state, void* user_data) {
    HXCPlayerControl* control = (__bridge HXCPlayerControl*)user_data;
    dispatch_async(dispatch_get_main_queue(), ^{
        [control playerPipelineStateChange:state];
    });
}

static void playing_changed_callback_c(int is_playing, void* user_data) {
    HXCPlayerControl* control = (__bridge HXCPlayerControl*)user_data;
    dispatch_async(dispatch_get_main_queue(), ^{
        [control playerPlayingChange:(is_playing != 0)];
    });
}

// ========== HXCPlayerDataSourceConfig 实现 ==========
@implementation HXCPlayerDataSourceConfig

// 全局缓存的默认字段
static NSInteger g_timeoutMs = 30000;
static NSInteger g_maxRetries = 3;
static NSUInteger g_cacheSize = 2 * 1024 * 1024;
static NSUInteger g_avioBufferSize = 64 * 1024;
static BOOL g_hasConfigured = NO;

+ (void)configureDefaultConfig:(HXCPlayerDataSourceConfig *)config {
    @synchronized(self) {
        if (!config) {
            g_hasConfigured = NO;
            return;
        }
        g_timeoutMs = config.timeoutMs;
        g_maxRetries = config.maxRetries;
        g_cacheSize = config.cacheSize;
        g_avioBufferSize = config.avioBufferSize;
        g_hasConfigured = YES;
    }
}

+ (instancetype)defaultConfig {
    HXCPlayerDataSourceConfig *config = [[HXCPlayerDataSourceConfig alloc] init];
    config.timeoutMs = 30000;           // 30秒
    config.maxRetries = 3;              // 重试3次
    config.cacheSize = 2 * 1024 * 1024; // 2MB
    config.avioBufferSize = 64 * 1024;  // 64KB
    return config;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        // 使用默认值
        self.timeoutMs = 30000;
        self.maxRetries = 3;
        self.cacheSize = 2 * 1024 * 1024;
        self.avioBufferSize = 64 * 1024;
    }
    return self;
}

@end

@implementation HXCPlayerVideo

@end

@implementation HXCPlayerDataSourcePlayModel

+ (instancetype)modelWithURL:(NSString *)url
                         mode:(HXCPlayerDataSourceMode)mode
                  encryptedFile:(BOOL)encryptedFile {
    HXCPlayerDataSourcePlayModel *m = [[HXCPlayerDataSourcePlayModel alloc] init];
    m.url = url;
    m.mode = mode;
    m.encryptedFile = encryptedFile;
    return m;
}

@end

@implementation HXCPlayerControl

static HXCPlayerDataSourceConfig *hxc_build_effective_data_source_config(void) {
    HXCPlayerDataSourceConfig *config = [HXCPlayerDataSourceConfig defaultConfig];
    @synchronized([HXCPlayerDataSourceConfig class]) {
        if (g_hasConfigured) {
            config.timeoutMs = g_timeoutMs;
            config.maxRetries = g_maxRetries;
            config.cacheSize = g_cacheSize;
            config.avioBufferSize = g_avioBufferSize;
        }
    }
    return config;
}

static PlayerDecodeModeC hxc_to_c_decode_mode(HXCPlayerDecodeMode mode) {
    switch (mode) {
        case HXCPlayerDecodeModeHardware:
            return PLAYER_DECODE_MODE_HARDWARE;
        case HXCPlayerDecodeModeSoftware:
        default:
            return PLAYER_DECODE_MODE_SOFTWARE;
    }
}

static HXCPlayerState hxc_to_objc_state(PlayerStateC state) {
    switch (state) {
        case PLAYER_STATE_IDLE:
            return HXCPlayerStateIdle;
        case PLAYER_STATE_OPENING:
            return HXCPlayerStateOpening;
        case PLAYER_STATE_PLAYING:
            return HXCPlayerStatePlaying;
        case PLAYER_STATE_PAUSED:
            return HXCPlayerStatePaused;
        case PLAYER_STATE_STOPPED:
            return HXCPlayerStateStopped;
        case PLAYER_STATE_ERROR:
            return HXCPlayerStateError;
        default:
            return HXCPlayerStateIdle;
    }
}

static HXCPlayerPipelineState hxc_to_objc_pipeline_state(PlayerPipelineStateC state) {
    switch (state) {
        case PLAYER_PIPELINE_STATE_IDLE:
            return HXCPlayerPipelineStateIdle;
        case PLAYER_PIPELINE_STATE_PREPARING:
            return HXCPlayerPipelineStatePreparing;
        case PLAYER_PIPELINE_STATE_BUFFERING:
            return HXCPlayerPipelineStateBuffering;
        case PLAYER_PIPELINE_STATE_READY:
            return HXCPlayerPipelineStateReady;
        case PLAYER_PIPELINE_STATE_ENDED:
            return HXCPlayerPipelineStateEnded;
        case PLAYER_PIPELINE_STATE_ERROR:
        default:
            return HXCPlayerPipelineStateError;
    }
}

- (instancetype)init {
    self = [super init];
    if (self) {
        // 若外部未调用 +setLogDir:，则在此安装默认 Documents/HXCPlayerLogs（仅一次）
        [HXCPlayerControl hxc_applyDefaultFileLoggingIfNeeded];
        
        _wrapper = new PlayerCoreWrapper();
        _state = HXCPlayerStateIdle;
        _pipelineState = HXCPlayerPipelineStateIdle;
        _playWhenReady = NO;
        _isPlaying = NO;
        _volume = 1.0;
        _playbackRate = 1.0;
        _startPosition = 0.0;
        _aspectRatioMode = HXCAspectRatioModeFit;
        _decodeMode = HXCPlayerDecodeModeSoftware;
        _autoPlayer = YES;
        _audioQueueRunning = NO;
        _lastPositionUpdateTime = 0;
        _autoReopenOnRecoverableErrorEnabled = NO;
        _autoReopenMaxAttempts = 1;
        _autoReopenAttemptCount = 0;
        _autoReopenInFlight = NO;
        _networkLoading = NO;
        _networkLoadingBeginTime = 0;
        _networkTotalStallMs = 0;
        _networkReconnectCount = 0;
        _renderGeneration = 1;
        _lastEnqueuedVideoPTS = NAN;
        _hasRenderedFirstVideoFrame = NO;
        _deferAudioStartUntilFirstVideoFrame = NO;
        _audioStartGeneration = 1;
        _firstFrameBootstrapActive = NO;
        
        // ========== 设置播放器回调 ==========
        player_core_set_state_changed_callback(_wrapper->handle(), state_changed_callback_c, (__bridge void*)self);
        player_core_set_error_callback(_wrapper->handle(), error_callback_c, (__bridge void*)self);
        player_core_set_position_changed_callback(_wrapper->handle(), position_changed_callback_c, (__bridge void*)self);
        player_core_set_buffer_progress_callback(_wrapper->handle(), buffer_progress_callback_c, (__bridge void*)self);
        player_core_set_playback_completed_callback(_wrapper->handle(), playback_completed_callback_c, (__bridge void*)self);
        player_core_set_loading_callback(_wrapper->handle(), loading_callback_c, (__bridge void*)self);
        player_core_set_pipeline_state_changed_callback(_wrapper->handle(), pipeline_state_changed_callback_c, (__bridge void*)self);
        player_core_set_playing_changed_callback(_wrapper->handle(), playing_changed_callback_c, (__bridge void*)self);
        
        // 创建视频视图（自动管理布局）
        _videoView = [[HXCPlayerView alloc] initWithFrame:CGRectZero];
        
#if TARGET_OS_IOS
        NSLog(@"[播放器] 初始化 HXCPlayerControl (iOS)");
        // iOS 也需要设置 controlTimebase，否则 seek 后可能不渲染
        AVSampleBufferDisplayLayer *videoLayer = _videoView.videoLayer;
        CMTimebaseRef controlTimebase;
        CMTimebaseCreateWithSourceClock(kCFAllocatorDefault,
                                       CMClockGetHostTimeClock(),
                                       &controlTimebase);
        videoLayer.controlTimebase = controlTimebase;
        CFRelease(controlTimebase);
        
        CMTimebaseSetTime(videoLayer.controlTimebase, kCMTimeZero);
        CMTimebaseSetRate(videoLayer.controlTimebase, 1.0);
        
        // 初始化画中画
        [self setupPictureInPicture];
        [self hxc_registerAppLifecycleNotifications];
#else
        // macOS 需要设置 controlTimebase
        AVSampleBufferDisplayLayer *videoLayer = _videoView.videoLayer;
        CMTimebaseRef controlTimebase;
        CMTimebaseCreateWithSourceClock(kCFAllocatorDefault, 
                                       CMClockGetHostTimeClock(), 
                                       &controlTimebase);
        videoLayer.controlTimebase = controlTimebase;
        CFRelease(controlTimebase);
        
        CMTimebaseSetTime(videoLayer.controlTimebase, kCMTimeZero);
        CMTimebaseSetRate(videoLayer.controlTimebase, 1.0);
        
        NSLog(@"[播放器] 初始化 HXCPlayerControl (macOS)");
#endif
        
        // 创建渲染队列
#if TARGET_OS_IOS
        _renderQueue = dispatch_queue_create("com.hxcplayer.ios.render", DISPATCH_QUEUE_SERIAL);
#else
        _renderQueue = dispatch_queue_create("com.hxcplayer.macos.render", DISPATCH_QUEUE_SERIAL);
#endif
    }
    return self;
}

- (void)dealloc {
    NSLog(@"[播放器] HXCPlayerControl 正在销毁...");
#if TARGET_OS_IOS
    [self hxc_unregisterAppLifecycleNotifications];
    [self hxc_clearLastRenderedSampleBuffer];
#endif
    
    [self stop];
    
    if (_wrapper) {
        delete _wrapper;
        _wrapper = nullptr;
    }
}

#pragma mark - Public Methods

- (BOOL)hxc_licenseAllowedOrNotifyForAction:(NSString *)action {
//    if (![HXCPlayerLicenseManager isPlaybackLicenseGateEnabled]) {
//        return YES;
//    }
    if ([HXCPlayerLicenseManager isLicenseCheckPassed]) {
        return YES;
    }
    NSError *err = [NSError errorWithDomain:@"HXCPlayerErrorDomain"
                                       code:HXCPlayerErrorLicenseValidationFailed
                                   userInfo:@{NSLocalizedDescriptionKey: @"License校验失败"}];
    if ([self.delegate respondsToSelector:@selector(player:didFailWithError:)]) {
        [self.delegate player:self didFailWithError:err];
    }
    NSLog(@"[HXCPlayer] License校验失败 (%@)", action ?: @"");
    return NO;
}

- (BOOL)playURL:(NSString *)url {
    if (!url || url.length == 0) {
        return NO;
    }
    if (![self hxc_licenseAllowedOrNotifyForAction:@"playURL"]) {
        return NO;
    }
    [self stop];
    _playerUrl = [url copy];
    _lastOpenPlayModel = nil;
    if (!_autoReopenInFlight) {
        _autoReopenAttemptCount = 0;
        _networkTotalStallMs = 0;
        _networkReconnectCount = 0;
    }
    player_core_set_decode_mode(_wrapper->handle(), hxc_to_c_decode_mode(_decodeMode));
    int ret;
    if (_startPosition > 0) {
        ret = player_core_open_with_start_position(_wrapper->handle(), url.UTF8String, _startPosition);
    } else {
        ret = player_core_open(_wrapper->handle(), url.UTF8String);
    }

    if (ret != 0) {
        return NO;
    }

    int sampleRate = player_core_get_audio_sample_rate(_wrapper->handle());
    int channels = player_core_get_audio_channels(_wrapper->handle());
    _duration = player_core_get_duration(_wrapper->handle());
    _videoWidth = player_core_get_video_width(_wrapper->handle());
    _videoHeight = player_core_get_video_height(_wrapper->handle());
    
    [self setupAudioQueue:sampleRate channels:channels];
    _hasRenderedFirstVideoFrame = NO;
    _deferAudioStartUntilFirstVideoFrame = NO;
    _firstFrameBootstrapActive = YES;

    // 如果设置了起始播放位置，更新 controlTimebase
    if (_startPosition > 0) {
        if (_videoView.videoLayer && _videoView.videoLayer.controlTimebase) {
            CMTime newTime = CMTimeMake((int64_t)(_startPosition * 1000000), 1000000);
            CMTimebaseSetTime(_videoView.videoLayer.controlTimebase, newTime);
            CMTimebaseSetRate(_videoView.videoLayer.controlTimebase, 1.0);
        }
    }

    // 启动视频渲染定时器
    [self startDisplayLink];
    // 主动拉一次渲染，减少等待下一帧 display tick 的首帧延迟。
    [self renderVideoFrame];

    if (self.autoPlayer) {
        [self play];
    } else {
        [self pause];
    }

    return YES;
}

- (BOOL)playWithModel:(HXCPlayerDataSourcePlayModel *)model {
    if (!model || model.url.length == 0) {
        return NO;
    }
    if (![self hxc_licenseAllowedOrNotifyForAction:@"playWithModel"]) {
        return NO;
    }
    HXCPlayerDataSourceConfig *config = hxc_build_effective_data_source_config();
    // 复用旧实现来完成 stop/url/state 等逻辑，但把 encrypted_file 走 model 传入
    // 这里直接复制 openURL:withMode:config: 的关键配置段到 C 结构，避免改动旧签名行为过多。
    if (!model.url || model.url.length == 0) {
        return NO;
    }
    [self stop];
    _playerUrl = [model.url copy];
    _lastOpenPlayModel = [HXCPlayerDataSourcePlayModel modelWithURL:model.url mode:model.mode encryptedFile:model.encryptedFile];
    if (!_autoReopenInFlight) {
        _autoReopenAttemptCount = 0;
        _networkTotalStallMs = 0;
        _networkReconnectCount = 0;
    }
    player_core_set_decode_mode(_wrapper->handle(), hxc_to_c_decode_mode(_decodeMode));

    PlayerDataSourceConfigC cConfig;
    cConfig.timeout_ms = (int)config.timeoutMs;
    cConfig.max_retries = (int)config.maxRetries;
    cConfig.cache_size = config.cacheSize;
    cConfig.avio_buffer_size = config.avioBufferSize;
    cConfig.encrypted_file = model.encryptedFile ? 1 : 0;

    PlayerDataSourceModeC cMode;
    switch (model.mode) {
        case HXCPlayerDataSourceModeDefault:
            cMode = PLAYER_DATA_SOURCE_MODE_DEFAULT;
            break;
        case HXCPlayerDataSourceModeCustomHTTP:
            cMode = PLAYER_DATA_SOURCE_MODE_CUSTOM_HTTP;
            break;
        case HXCPlayerDataSourceModeCustomFile:
            cMode = PLAYER_DATA_SOURCE_MODE_CUSTOM_FILE;
            break;
        default:
            cMode = PLAYER_DATA_SOURCE_MODE_DEFAULT;
            break;
    }

    int ret = player_core_open_with_mode(_wrapper->handle(), model.url.UTF8String, cMode, &cConfig, _startPosition);
    if (ret != 0) {
        NSLog(@"❌ 打开失败: mode=%ld, ret=%d", (long)model.mode, ret);
        return NO;
    }

    int sampleRate = player_core_get_audio_sample_rate(_wrapper->handle());
    int channels = player_core_get_audio_channels(_wrapper->handle());
    _duration = player_core_get_duration(_wrapper->handle());
    _videoWidth = player_core_get_video_width(_wrapper->handle());
    _videoHeight = player_core_get_video_height(_wrapper->handle());

    NSLog(@"✅ 使用模式 %ld 打开成功", (long)model.mode);
    NSLog(@"   URL: %@", model.url);
    NSLog(@"   时长: %.2f 秒", _duration);
    NSLog(@"   分辨率: %d x %d", _videoWidth, _videoHeight);
    NSLog(@"   音频: %d Hz, %d 通道", sampleRate, channels);

    [self setupAudioQueue:sampleRate channels:channels];
    _hasRenderedFirstVideoFrame = NO;
    _deferAudioStartUntilFirstVideoFrame = NO;
    _firstFrameBootstrapActive = YES;

    if (_startPosition > 0) {
        if (_videoView.videoLayer && _videoView.videoLayer.controlTimebase) {
            CMTime newTime = CMTimeMake((int64_t)(_startPosition * 1000000), 1000000);
            CMTimebaseSetTime(_videoView.videoLayer.controlTimebase, newTime);
            CMTimebaseSetRate(_videoView.videoLayer.controlTimebase, 1.0);
        }
    }

    [self startDisplayLink];
    // 主动拉一次渲染，减少等待下一帧 display tick 的首帧延迟。
    [self renderVideoFrame];
    if (self.autoPlayer) {
        [self play];
    } else {
        [self pause];
    }
    return YES;
}

- (void)play {
    if (![self hxc_licenseAllowedOrNotifyForAction:@"play"]) {
        return;
    }
    if (!_playerUrl && _state == HXCPlayerStateIdle) {
        NSLog(@"警告: 请先调用 playURL:");
        return;
    }
    player_core_play(_wrapper->handle());
    if (_audioQueue && !_audioQueueRunning) {
        // 首帧优先：有视频轨时优先等待首帧，避免出现“先听到声音还没画面”。
        if (!_hasRenderedFirstVideoFrame && _videoWidth > 0 && _videoHeight > 0) {
            _deferAudioStartUntilFirstVideoFrame = YES;
            _firstFrameBootstrapActive = YES;
            _audioStartGeneration += 1;
            int64_t generation = _audioStartGeneration;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(400 * NSEC_PER_MSEC)),
                           dispatch_get_main_queue(), ^{
                if (generation != self->_audioStartGeneration) return;
                if (!self->_audioQueue || self->_audioQueueRunning) return;
                if (!self->_deferAudioStartUntilFirstVideoFrame) return;
                // 超时兜底：若首帧仍未到，先起音频避免长时间静音。
                AudioQueueStart(self->_audioQueue, NULL);
                self->_audioQueueRunning = YES;
                self->_deferAudioStartUntilFirstVideoFrame = NO;
                self->_firstFrameBootstrapActive = NO;
            });
        } else {
            AudioQueueStart(_audioQueue, NULL);
            _audioQueueRunning = YES;
            _firstFrameBootstrapActive = NO;
        }
    }
}

- (void)pause {
    player_core_pause(_wrapper->handle());
    if (_audioQueue && _audioQueueRunning) {
        AudioQueuePause(_audioQueue);
        _audioQueueRunning = NO;
    }
}

- (void)resume {
    [self play];
}

- (void)replay {
    if (!_playerUrl) {
        NSLog(@"警告: 没有可重播的视频");
        return;
    }

    NSString *url = [_playerUrl copy];
    if (![self playURL:url]) {
        return;
    }
    if (!self.autoPlayer) {
        [self play];
    }
}

- (void)seekToPosition:(double)position {
    if (![self hxc_licenseAllowedOrNotifyForAction:@"seekToPosition"]) {
        return;
    }
    player_core_seek(_wrapper->handle(), position);
    _renderGeneration++;
    _lastEnqueuedVideoPTS = NAN;
    
    // iOS 和 macOS 都需要更新 controlTimebase
    if (_videoView.videoLayer && _videoView.videoLayer.controlTimebase) {
        // seek 时优先保画面：先做轻量 flush，不主动移除当前已显示帧。
        [_videoView.videoLayer flush];
        CMTime newTime = CMTimeMake(position * 1000000, 1000000);
        CMTimebaseSetTime(_videoView.videoLayer.controlTimebase, newTime);
        
        // 确保 timebase 在运行
        if (CMTimebaseGetRate(_videoView.videoLayer.controlTimebase) == 0.0) {
            CMTimebaseSetRate(_videoView.videoLayer.controlTimebase, _playbackRate > 0 ? _playbackRate : 1.0);
        }
        
        NSLog(@"[Seek] 更新 controlTimebase 到: %.2f 秒", position);
    }
}

- (void)stop {
    HXCPlayerState previousState = _state;
    [self stopDisplayLink];
    [self teardownAudioQueue];
    
    if (_wrapper && _wrapper->handle()) {
        player_core_stop(_wrapper->handle());
    }

    _renderGeneration++;
    _lastEnqueuedVideoPTS = NAN;
    [_videoView.videoLayer flushAndRemoveImage];
#if TARGET_OS_IOS
    [self hxc_clearLastRenderedSampleBuffer];
    _hxcWasPausedBeforeBackground = NO;
#endif

    _state = HXCPlayerStateIdle;
    _pipelineState = HXCPlayerPipelineStateIdle;
    _playWhenReady = NO;
    _isPlaying = NO;
    _duration = 0;
    _position = 0;
    _playerUrl = nil;
    _videoWidth = 0;
    _videoHeight = 0;
    _networkLoading = NO;
    _networkLoadingBeginTime = 0;
    _autoReopenInFlight = NO;
    _hasRenderedFirstVideoFrame = NO;
    _deferAudioStartUntilFirstVideoFrame = NO;
    _audioStartGeneration += 1;
    _firstFrameBootstrapActive = NO;
#if TARGET_OS_IOS
    if (previousState != HXCPlayerStateIdle) {
        [self invalidatePictureInPicturePlaybackStateIfNeeded];
    }
#endif
}

- (BOOL)isHardwareDecodingActive {
    if (!_wrapper || !_wrapper->handle()) {
        return NO;
    }
    return player_core_is_video_hardware_decoding(_wrapper->handle()) != 0;
}


#pragma mark - private method

- (BOOL)hxc_isRecoverableErrorCode:(NSInteger)errorCode {
    switch (errorCode) {
        case HXCPlayerErrorOpenInputFailed:
        case HXCPlayerErrorReadFrameFailed:
        case HXCPlayerErrorDecodeFailed:
        case -2001: // PLAYER_ERROR_NET_CONNECTION_TIMEOUT
        case -2002: // PLAYER_ERROR_NET_CONNECTION_REFUSED
        case -2003: // PLAYER_ERROR_NET_UNREACHABLE
        case -3003: // PLAYER_ERROR_HTTP_SERVER_ERROR
            return YES;
        default:
            return NO;
    }
}

- (void)hxc_notifyNetworkQoEWithCurrentStallMs:(NSInteger)currentStallMs {
    if ([self.delegate respondsToSelector:@selector(player:didUpdateNetworkQoEWithCurrentStallMs:totalStallMs:reconnectCount:)]) {
        [self.delegate player:self
didUpdateNetworkQoEWithCurrentStallMs:currentStallMs
                  totalStallMs:_networkTotalStallMs
                reconnectCount:_networkReconnectCount];
    }
}

- (void)hxc_onLoadingStateChanged:(BOOL)isLoading {
    CFTimeInterval now = CACurrentMediaTime();
    if (isLoading && !_networkLoading) {
        _networkLoading = YES;
        _networkLoadingBeginTime = now;
        [self hxc_notifyNetworkQoEWithCurrentStallMs:0];
        return;
    }
    if (!isLoading && _networkLoading) {
        NSInteger stallMs = (NSInteger)((now - _networkLoadingBeginTime) * 1000.0);
        if (stallMs < 0) stallMs = 0;
        _networkTotalStallMs += stallMs;
        _networkLoading = NO;
        _networkLoadingBeginTime = 0;
        [self hxc_notifyNetworkQoEWithCurrentStallMs:stallMs];
    }
}

- (void)hxc_tryAutoReopenForErrorCode:(NSInteger)errorCode {
    if (!_autoReopenOnRecoverableErrorEnabled) {
        return;
    }
    if (_autoReopenInFlight) {
        return;
    }
    if (![self hxc_isRecoverableErrorCode:errorCode]) {
        return;
    }
    NSInteger maxAttempts = MAX(0, _autoReopenMaxAttempts);
    if (_autoReopenAttemptCount >= maxAttempts) {
        return;
    }
    if (!_playerUrl.length) {
        return;
    }

    _autoReopenInFlight = YES;
    _autoReopenAttemptCount += 1;
    _networkReconnectCount += 1;
    [self hxc_notifyNetworkQoEWithCurrentStallMs:0];

    HXCPlayerDataSourcePlayModel *retryModel = nil;
    if (_lastOpenPlayModel) {
        retryModel = [HXCPlayerDataSourcePlayModel modelWithURL:_lastOpenPlayModel.url
                                                           mode:_lastOpenPlayModel.mode
                                                  encryptedFile:_lastOpenPlayModel.encryptedFile];
    }
    NSString *retryURL = [_playerUrl copy];
    double retryStartPosition = _position > 0 ? _position : _startPosition;

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(300 * NSEC_PER_MSEC)),
                   dispatch_get_main_queue(), ^{
        self->_startPosition = retryStartPosition;
        BOOL ok = retryModel ? [self playWithModel:retryModel] : [self playURL:retryURL];
        if (ok) {
            [self play];
        }
        self->_autoReopenInFlight = NO;
    });
}

-(void)playerPositionChange:(double)position {
    self->_position = position;
    if ([self.delegate respondsToSelector:@selector(player:didUpdatePosition:)]) {
        [self.delegate player:self didUpdatePosition:position];
    }
}

-(void)playerStateChange:(PlayerStateC)state {
    (void)state;
    [self hxc_syncUnifiedStateAndNotify];
}

-(void)playerPipelineStateChange:(PlayerPipelineStateC)state {
    (void)state;
    [self hxc_syncUnifiedStateAndNotify];
}

-(void)playerPlayingChange:(BOOL)isPlaying {
    (void)isPlaying;
    [self hxc_syncUnifiedStateAndNotify];
}

- (HXCPlayerState)hxc_resolveUnifiedStateFromCoreState:(PlayerStateC)coreState
                                          pipelineState:(PlayerPipelineStateC)pipelineState
                                          playWhenReady:(BOOL)playWhenReady
                                              isPlaying:(BOOL)isPlaying {
    if (coreState == PLAYER_STATE_ERROR || pipelineState == PLAYER_PIPELINE_STATE_ERROR) {
        return HXCPlayerStateError;
    }
    if (coreState == PLAYER_STATE_STOPPED || pipelineState == PLAYER_PIPELINE_STATE_ENDED) {
        return HXCPlayerStateStopped;
    }
    if (coreState == PLAYER_STATE_IDLE || pipelineState == PLAYER_PIPELINE_STATE_IDLE) {
        return HXCPlayerStateIdle;
    }
    if (coreState == PLAYER_STATE_OPENING || pipelineState == PLAYER_PIPELINE_STATE_PREPARING) {
        return HXCPlayerStateOpening;
    }
    if (pipelineState == PLAYER_PIPELINE_STATE_BUFFERING) {
        return HXCPlayerStateLoading;
    }
    if (isPlaying) {
        return HXCPlayerStatePlaying;
    }
    if (coreState == PLAYER_STATE_PAUSED || !playWhenReady) {
        return HXCPlayerStatePaused;
    }
    return hxc_to_objc_state(coreState);
}

- (void)hxc_syncUnifiedStateAndNotify {
    if (!(_wrapper && _wrapper->handle())) {
        return;
    }

    PlayerStateC coreState = player_core_get_state(_wrapper->handle());
    PlayerPipelineStateC pipelineState = player_core_get_pipeline_state(_wrapper->handle());
    BOOL playWhenReady = player_core_get_play_when_ready(_wrapper->handle()) != 0;
    BOOL isPlaying = player_core_is_playing(_wrapper->handle()) != 0;

    _pipelineState = hxc_to_objc_pipeline_state(pipelineState);
    _playWhenReady = playWhenReady;
    _isPlaying = isPlaying;

    HXCPlayerState resolved = [self hxc_resolveUnifiedStateFromCoreState:coreState
                                                            pipelineState:pipelineState
                                                            playWhenReady:playWhenReady
                                                                isPlaying:isPlaying];
    HXCPlayerState previous = _state;
    _state = resolved;
    if (previous != resolved) {
        if ([self.delegate respondsToSelector:@selector(player:didChangeState:)]) {
            [self.delegate player:self didChangeState:resolved];
        }
#if TARGET_OS_IOS
        [self invalidatePictureInPicturePlaybackStateIfNeeded];
#endif
    }
}

-(void)playerErrorHandler:(HXCPlayerErrorCode)errCode errMsg:(NSString *)errMsg {
    if ([self.delegate respondsToSelector:@selector(player:didFailWithError:)]) {
        NSError *error = [NSError errorWithDomain:@"HXCPlayerErrorDomain" code:errCode userInfo:@{NSLocalizedDescriptionKey: errMsg}];
        [self.delegate player:self didFailWithError:error];
    }
}


#pragma mark - Properties

- (HXCPlayerState)state {
    if (_wrapper && _wrapper->handle()) {
        PlayerStateC coreState = player_core_get_state(_wrapper->handle());
        PlayerPipelineStateC pipelineState = player_core_get_pipeline_state(_wrapper->handle());
        BOOL playWhenReady = player_core_get_play_when_ready(_wrapper->handle()) != 0;
        BOOL isPlaying = player_core_is_playing(_wrapper->handle()) != 0;
        _pipelineState = hxc_to_objc_pipeline_state(pipelineState);
        _playWhenReady = playWhenReady;
        _isPlaying = isPlaying;
        _state = [self hxc_resolveUnifiedStateFromCoreState:coreState
                                              pipelineState:pipelineState
                                              playWhenReady:playWhenReady
                                                  isPlaying:isPlaying];
    }
    return _state;
}

- (HXCPlayerPipelineState)pipelineState {
    if (_wrapper && _wrapper->handle()) {
        PlayerPipelineStateC pipelineState = player_core_get_pipeline_state(_wrapper->handle());
        _pipelineState = hxc_to_objc_pipeline_state(pipelineState);
    }
    return _pipelineState;
}

- (BOOL)playWhenReady {
    if (_wrapper && _wrapper->handle()) {
        _playWhenReady = player_core_get_play_when_ready(_wrapper->handle()) != 0;
    }
    return _playWhenReady;
}

- (BOOL)isPlaying {
    if (_wrapper && _wrapper->handle()) {
        _isPlaying = player_core_is_playing(_wrapper->handle()) != 0;
    }
    return _isPlaying;
}

- (double)duration {
    return _wrapper ? player_core_get_duration(_wrapper->handle()) : 0;
}

- (double)position {
    return _wrapper ? player_core_get_position(_wrapper->handle()) : 0;
}

- (void)setVolume:(double)volume {
    _volume = MAX(0.0, MIN(1.0, volume));
    if (_audioQueue) {
        AudioQueueSetParameter(_audioQueue, kAudioQueueParam_Volume, _volume);
    }
}

- (void)setPlaybackRate:(double)playbackRate {
    playbackRate = MAX(0.5, MIN(2.0, playbackRate));
    _playbackRate = playbackRate;

    if (_wrapper) {
        player_core_set_playback_rate(_wrapper->handle(), playbackRate);
    }

    // 更新 controlTimebase 的速率以匹配倍速
    if (_videoView.videoLayer && _videoView.videoLayer.controlTimebase) {
        CMTimebaseSetRate(_videoView.videoLayer.controlTimebase, playbackRate);
    }
}

- (void)setAspectRatioMode:(HXCAspectRatioMode)aspectRatioMode {
    _aspectRatioMode = aspectRatioMode;
    
    if (_wrapper) {
        AspectRatioModeC mode = (aspectRatioMode == HXCAspectRatioModeFit) ? ASPECT_RATIO_FIT : ASPECT_RATIO_FILL;
        player_core_set_aspect_ratio_mode(_wrapper->handle(), mode);
    }
    
    dispatch_async(dispatch_get_main_queue(), ^{
        if (aspectRatioMode == HXCAspectRatioModeFit) {
            self->_videoView.videoLayer.videoGravity = AVLayerVideoGravityResizeAspect;
        } else {
            self->_videoView.videoLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;
        }
    });
}

- (HXCAspectRatioMode)aspectRatioMode {
    return _aspectRatioMode;
}

#pragma mark - Audio Setup

- (void)setupAudioQueue:(int)sampleRate channels:(int)channels {
    _audioFormat.mSampleRate = sampleRate;
    _audioFormat.mFormatID = kAudioFormatLinearPCM;
    _audioFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    _audioFormat.mBytesPerPacket = channels * sizeof(SInt16);
    _audioFormat.mFramesPerPacket = 1;
    _audioFormat.mBytesPerFrame = channels * sizeof(SInt16);
    _audioFormat.mChannelsPerFrame = channels;
    _audioFormat.mBitsPerChannel = 16;
    
    OSStatus status = AudioQueueNewOutput(&_audioFormat,
                                         audioQueueCallback,
                                         (__bridge void *)self,
                                         NULL,
                                         kCFRunLoopCommonModes,
                                         0,
                                         &_audioQueue);
    
    if (status != noErr) {
        NSLog(@"❌ AudioQueue 创建失败: %d", (int)status);
        return;
    }
    
    UInt32 bufferSize = 4096 * channels * sizeof(SInt16);
    for (int i = 0; i < kNumberOfBuffers; i++) {
        AudioQueueAllocateBuffer(_audioQueue, bufferSize, &_audioBuffers[i]);
        audioQueueCallback((__bridge void *)self, _audioQueue, _audioBuffers[i]);
    }
    
    AudioQueueSetParameter(_audioQueue, kAudioQueueParam_Volume, _volume);
}

- (void)teardownAudioQueue {
    if (_audioQueue) {
        AudioQueueStop(_audioQueue, YES);
        AudioQueueDispose(_audioQueue, YES);
        _audioQueue = NULL;
        _audioQueueRunning = NO;
    }
}

static void audioQueueCallback(void *userData, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    HXCPlayerControl *player = (__bridge HXCPlayerControl *)userData;
    [player fillAudioBuffer:buffer];
    AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

- (void)fillAudioBuffer:(AudioQueueBufferRef)buffer {
    if (!_wrapper || !_wrapper->handle()) {
        buffer->mAudioDataByteSize = 0;
        return;
    }
    
    int got = player_core_get_audio_data(_wrapper->handle(), 
                                        (unsigned char*)buffer->mAudioData, 
                                        (int)buffer->mAudioDataBytesCapacity);
    
    if (got > 0) {
        buffer->mAudioDataByteSize = got;
    } else {
        memset(buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity);
        buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
    }
}

#pragma mark - Video Rendering

#if TARGET_OS_IOS

// iOS: CADisplayLink
- (void)startDisplayLink {
    if (_displayLink) {
        return;
    }
    
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(renderVideoFrame)];
    _displayLink.preferredFramesPerSecond = 60;
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)stopDisplayLink {
    if (_displayLink) {
        [_displayLink invalidate];
        _displayLink = nil;
    }
}

#else

// macOS: CVDisplayLink
static CVReturn displayLinkCallback(CVDisplayLinkRef displayLink,
                                   const CVTimeStamp *now,
                                   const CVTimeStamp *outputTime,
                                   CVOptionFlags flagsIn,
                                   CVOptionFlags *flagsOut,
                                   void *displayLinkContext) {
    @autoreleasepool {
        HXCPlayerControl *player = (__bridge HXCPlayerControl *)displayLinkContext;
        [player renderVideoFrame];
    }
    return kCVReturnSuccess;
}

- (void)startDisplayLink {
    if (_displayLink) {
        return;
    }
    
    CVReturn ret = CVDisplayLinkCreateWithActiveCGDisplays(&_displayLink);
    if (ret != kCVReturnSuccess) {
        NSLog(@"❌ CVDisplayLink 创建失败: %d", ret);
        return;
    }
    
    CVDisplayLinkSetOutputCallback(_displayLink, &displayLinkCallback, (__bridge void *)self);
    CVDisplayLinkStart(_displayLink);
}

- (void)stopDisplayLink {
    if (_displayLink) {
        CVDisplayLinkStop(_displayLink);
        CVDisplayLinkRelease(_displayLink);
        _displayLink = NULL;
    }
}

#endif

- (void)renderVideoFrame {
    if (!_wrapper || !_wrapper->handle()) {
        return;
    }
    
    VideoFrameDataC frame_data;
    if (player_core_get_video_frame(_wrapper->handle(), &frame_data) != 0) {
        return;
    }
    
    double currentPTS = frame_data.pts;
    double masterClock = player_core_get_position(_wrapper->handle());

    // 首帧快速通道：首帧阶段不等待 A/V 阈值，避免“有声无画”。
    if (_firstFrameBootstrapActive && !_hasRenderedFirstVideoFrame) {
        [self displayVideoFrameData:&frame_data];
        player_core_consume_video_frame(_wrapper->handle());
        return;
    }
    
    if (isnan(currentPTS)) {
        [self displayVideoFrameData:&frame_data];
        player_core_consume_video_frame(_wrapper->handle());
        return;
    }
    
    double delay = currentPTS - masterClock;
    double playbackRate = player_core_get_playback_rate(_wrapper->handle());
    double threshold = 0.04 / playbackRate;
    
    // ⚠️ 【关键修复】如果 delay 严重异常（< -5 秒），说明时钟还未同步（比如 seek 后）
    // 此时应该强制显示帧，不丢帧，让时钟自动同步
    if (delay < -5.0) {
        NSLog(@"检测到时钟未同步: currentPTS=%.2f, masterClock=%.2f, delay=%.2f，强制显示帧",
              currentPTS, masterClock, delay);
        [self displayVideoFrameData:&frame_data];
        player_core_consume_video_frame(_wrapper->handle());
        return;
    }
    
    if (delay <= -threshold) {
        // 丢帧：视频落后太多
        player_core_consume_video_frame(_wrapper->handle());
    } else if (delay <= threshold) {
        // 正常显示
        [self displayVideoFrameData:&frame_data];
        player_core_consume_video_frame(_wrapper->handle());
    }
    // else: delay > threshold，视频超前，不消费帧，等待下次渲染
    
    // 定期更新播放位置（限制为每 0.1 秒调用一次）
//    CFTimeInterval currentTime = CACurrentMediaTime();
//    if (currentTime - _lastPositionUpdateTime >= 0.1) {
//        _lastPositionUpdateTime = currentTime;
//        [self notifyPositionUpdate];
//    }
}

//- (void)notifyPositionUpdate {
//    if (!_wrapper || !_wrapper->handle()) {
//        return;
//    }
//    
//    double position = player_core_get_position(_wrapper->handle());
//    double duration = player_core_get_duration(_wrapper->handle());
//    
//    if (duration > 0 && [self.delegate respondsToSelector:@selector(playerDidUpdatePosition:duration:)]) {
//        dispatch_async(dispatch_get_main_queue(), ^{
//            [self.delegate playerDidUpdatePosition:position duration:duration];
//        });
//    }
//}

- (void)displayVideoFrameData:(VideoFrameDataC *)frameData {
    if (!frameData || !_videoView.videoLayer) {
        return;
    }
    
    CVPixelBufferRef pixelBuffer = [self createPixelBufferFromFrameData:frameData];
    if (!pixelBuffer) {
        return;
    }
    
    CMSampleBufferRef sampleBuffer = NULL;
    CMVideoFormatDescriptionRef formatDesc = NULL;
    OSStatus status = CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, pixelBuffer, &formatDesc);
    
    if (status != noErr) {
        CVPixelBufferRelease(pixelBuffer);
        return;
    }
    
    CMTime presentationTime = CMTimeMake(frameData->pts * 1000000, 1000000);
    if (_firstFrameBootstrapActive && !_hasRenderedFirstVideoFrame &&
        _videoView.videoLayer && _videoView.videoLayer.controlTimebase) {
        // 首帧阶段直接贴当前 timebase，避免首帧因 PTS 偏前被延后显示。
        presentationTime = CMTimebaseGetTime(_videoView.videoLayer.controlTimebase);
    }
    CMSampleTimingInfo timing = {
        .duration = kCMTimeInvalid,
        .presentationTimeStamp = presentationTime,
        .decodeTimeStamp = kCMTimeInvalid
    };
    
    status = CMSampleBufferCreateReadyWithImageBuffer(kCFAllocatorDefault,
                                            pixelBuffer,
                                            formatDesc,
                                            &timing,
                                            &sampleBuffer);
    
    if (status != noErr) {
        CFRelease(formatDesc);
        CVPixelBufferRelease(pixelBuffer);
        return;
    }
    
    if (sampleBuffer) {
        const int64_t generation = _renderGeneration;
        const double framePTS = frameData->pts;
#if TARGET_OS_IOS
        [self hxc_cacheLastRenderedSampleBuffer:sampleBuffer];
#endif
        dispatch_async(dispatch_get_main_queue(), ^{
            if (generation != self->_renderGeneration) {
                CFRelease(sampleBuffer);
                return;
            }
            if (self->_videoView.videoLayer.status == AVQueuedSampleBufferRenderingStatusFailed) {
                // 异常态：强复位，避免 layer 卡住后持续不出图。
                [self->_videoView.videoLayer flushAndRemoveImage];
            }
            // PTS 发生回退时先 flush，避免 AVSampleBufferDisplayLayer 因时序倒退而卡住不出图。
            if (!isnan(framePTS) && !isnan(self->_lastEnqueuedVideoPTS) &&
                framePTS + 0.001 < self->_lastEnqueuedVideoPTS) {
                // 时序异常先轻量复位，优先保画面；若后续仍异常再走 failed 分支强复位。
                [self->_videoView.videoLayer flush];
                self->_lastEnqueuedVideoPTS = NAN;
            }
            [self->_videoView.videoLayer enqueueSampleBuffer:sampleBuffer];
            if (!self->_hasRenderedFirstVideoFrame) {
                self->_hasRenderedFirstVideoFrame = YES;
                self->_firstFrameBootstrapActive = NO;
                if (self->_deferAudioStartUntilFirstVideoFrame && self->_audioQueue && !self->_audioQueueRunning) {
                    AudioQueueStart(self->_audioQueue, NULL);
                    self->_audioQueueRunning = YES;
                    self->_deferAudioStartUntilFirstVideoFrame = NO;
                }
            }
            if (!isnan(framePTS)) {
                self->_lastEnqueuedVideoPTS = framePTS;
            }
            CFRelease(sampleBuffer);
        });
    }
    
    if (formatDesc) {
        CFRelease(formatDesc);
    }
    
    CVPixelBufferRelease(pixelBuffer);
}

- (CVPixelBufferRef)createPixelBufferFromFrameData:(VideoFrameDataC *)frameData {
    if (!frameData || !frameData->y_data || frameData->width <= 0 || frameData->height <= 0) {
        return NULL;
    }
    NSDictionary *options = @{
        (NSString *)kCVPixelBufferCGImageCompatibilityKey: @YES,
        (NSString *)kCVPixelBufferCGBitmapContextCompatibilityKey: @YES,
        (NSString *)kCVPixelBufferIOSurfacePropertiesKey: @{}
    };
    
    CVPixelBufferRef pixelBuffer = NULL;
    CVReturn ret = CVPixelBufferCreate(kCFAllocatorDefault,
                                       frameData->width,
                                       frameData->height,
                                       kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
                                       (__bridge CFDictionaryRef)options,
                                       &pixelBuffer);
    
    if (ret != kCVReturnSuccess || !pixelBuffer) {
        return NULL;
    }
    
    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    
    // Y 平面
    uint8_t *yPlane = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
    size_t yStride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);
    uint8_t *ySrc = (uint8_t *)frameData->y_data;
    int ySrcStride = frameData->y_linesize > 0 ? frameData->y_linesize : frameData->width;
    
    for (int i = 0; i < frameData->height; i++) {
        size_t copyBytes = (size_t)MIN(frameData->width, ySrcStride);
        memcpy(yPlane + i * yStride, ySrc + i * ySrcStride, copyBytes);
    }
    
    // UV 平面（NV12 交织）
    uint8_t *uvPlane = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
    size_t uvStride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
    uint8_t *uSrc = (uint8_t *)frameData->u_data;
    uint8_t *vSrc = (uint8_t *)frameData->v_data;
    int uSrcStride = frameData->u_linesize > 0 ? frameData->u_linesize : frameData->width;
    int vSrcStride = frameData->v_linesize > 0 ? frameData->v_linesize : (frameData->width / 2);
    
    int uvHeight = frameData->height / 2;
    int uvWidth = frameData->width / 2;

    if (uSrc) {
        if (!vSrc || frameData->v_linesize <= 0) {
            // NV12：u_data 即 UV 交织平面，直接按行拷贝 width 字节
            for (int i = 0; i < uvHeight; i++) {
                size_t copyBytes = (size_t)MIN(frameData->width, uSrcStride);
                memcpy(uvPlane + i * uvStride, uSrc + i * uSrcStride, copyBytes);
            }
        } else {
            // YUV420P：U/V 分离平面，手动交织成 NV12
            for (int i = 0; i < uvHeight; i++) {
                for (int j = 0; j < uvWidth; j++) {
                    uvPlane[i * uvStride + j * 2] = uSrc[i * uSrcStride + j];
                    uvPlane[i * uvStride + j * 2 + 1] = vSrc[i * vSrcStride + j];
                }
            }
        }
    }
    
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    
    return pixelBuffer;
}

#pragma mark - 日志配置

+(void)setLogLevel:(HXCPlayerLogLevel)level {
    player_core_set_log_level((int)level);
}

+ (HXCPlayerLogLevel)currentLogLevel {
    int v = player_core_get_log_level();
    if (v < 0 || v > (int)HXCPlayerLogLevelError) {
        return HXCPlayerLogLevelInfo;
    }
    return (HXCPlayerLogLevel)v;
}

+ (NSString *)currentLogDirectory {
    const char *p = player_core_get_log_directory();
    if (p && p[0]) {
        return [NSString stringWithUTF8String:p];
    }
    return @"";
}

+ (NSString *)currentLogFilePath {
    const char *p = player_core_get_current_log_file();
    if (p && p[0]) {
        return [NSString stringWithUTF8String:p];
    }
    return @"";
}

+ (BOOL)hxc_installFileLoggingAtPath:(NSString *)dir error:(NSError *__autoreleasing *)outError {
    if (dir.length == 0) {
        if (outError) {
            *outError = [NSError errorWithDomain:@"HXCPlayerControl" code:-1 userInfo:@{NSLocalizedDescriptionKey: @"日志目录为空"}];
        }
        return NO;
    }
    NSFileManager *fileManager = [NSFileManager defaultManager];
    if (![fileManager fileExistsAtPath:dir]) {
        if (![fileManager createDirectoryAtPath:dir
                     withIntermediateDirectories:YES
                                      attributes:nil
                                           error:outError]) {
            return NO;
        }
    }
    player_core_enable_file_logging([dir UTF8String], "hxcplayer");
    return YES;
}

+ (void)hxc_applyDefaultFileLoggingIfNeeded {
    @synchronized([self class]) {
        if (gHXCUserSetLogDirExplicitly || gHXCDefaultFileLoggingInstalled) {
            return;
        }
        NSArray<NSString *> *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        NSString *documentsDirectory = paths.firstObject;
        if (documentsDirectory.length == 0) {
            NSLog(@"⚠️ 无法解析 Documents 目录，跳过默认文件日志");
            return;
        }
        NSString *logDir = [documentsDirectory stringByAppendingPathComponent:@"HXCPlayerLogs"];
        NSError *error = nil;
        if (![self hxc_installFileLoggingAtPath:logDir error:&error]) {
            NSLog(@"❌ 默认日志目录启用失败: %@", error.localizedDescription);
            return;
        }
        gHXCDefaultFileLoggingInstalled = YES;
        [HXCPlayerControl setLogLevel:HXCPlayerLogLevelDebug];
        player_core_set_log_retention_days(7);
        player_core_set_max_log_file_size(10 * 1024 * 1024);
        const char *logFile = player_core_get_current_log_file();
        NSString *logFilePath = logFile ? [NSString stringWithUTF8String:logFile] : @"未知";
        NSLog(@"========================================");
        NSLog(@"📝 HXCPlayer 默认文件日志已启用（未调用 +setLogDir:）");
        NSLog(@"========================================");
        NSLog(@"日志级别: DEBUG");
        NSLog(@"日志目录: %@", logDir);
        NSLog(@"日志文件: %@", logFilePath);
        NSLog(@"保留天数: 7 天");
        NSLog(@"最大大小: 10 MB");
        NSLog(@"========================================");
    }
}

+(void)setLogDir:(NSString *)dir {
    if (!dir) {
        NSLog(@"日志路径设置不能为空");
        return;
    }
    NSError *error = nil;
    @synchronized([self class]) {
        if (![self hxc_installFileLoggingAtPath:dir error:&error]) {
            NSLog(@"❌ 设置日志目录失败: %@", error.localizedDescription);
            return;
        }
        gHXCUserSetLogDirExplicitly = YES;
        // 与默认安装策略一致，避免仅显式 setLogDir 时保留天数/单文件上限未初始化
        player_core_set_log_retention_days(7);
        player_core_set_max_log_file_size(10 * 1024 * 1024);
    }
}

+ (void)disableFileLogging {
    player_core_disable_file_logging();
    NSLog(@"📝 文件日志已关闭（全局）");
}

#pragma mark - Picture in Picture (iOS only)

#if TARGET_OS_IOS

- (void)hxc_registerAppLifecycleNotifications {
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center addObserver:self
               selector:@selector(hxc_appWillResignActive:)
                   name:UIApplicationWillResignActiveNotification
                 object:nil];
    [center addObserver:self
               selector:@selector(hxc_appDidBecomeActive:)
                   name:UIApplicationDidBecomeActiveNotification
                 object:nil];
}

- (void)hxc_unregisterAppLifecycleNotifications {
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    [center removeObserver:self name:UIApplicationWillResignActiveNotification object:nil];
    [center removeObserver:self name:UIApplicationDidBecomeActiveNotification object:nil];
}

- (void)hxc_appWillResignActive:(NSNotification *)note {
    (void)note;
    _hxcWasPausedBeforeBackground = (_state == HXCPlayerStatePaused);
}

- (void)hxc_appDidBecomeActive:(NSNotification *)note {
    (void)note;
    // 仅处理“进入后台前就是暂停”的场景，避免打扰播放中状态
    if (_hxcWasPausedBeforeBackground && _state == HXCPlayerStatePaused) {
        [self hxc_restorePausedFrameIfNeeded];
    }
    _hxcWasPausedBeforeBackground = NO;
}

- (void)hxc_cacheLastRenderedSampleBuffer:(CMSampleBufferRef)sampleBuffer {
    if (!sampleBuffer) return;
    @synchronized(self) {
        if (_lastRenderedSampleBuffer) {
            CFRelease(_lastRenderedSampleBuffer);
            _lastRenderedSampleBuffer = NULL;
        }
        _lastRenderedSampleBuffer = (CMSampleBufferRef)CFRetain(sampleBuffer);
    }
}

- (void)hxc_clearLastRenderedSampleBuffer {
    @synchronized(self) {
        if (_lastRenderedSampleBuffer) {
            CFRelease(_lastRenderedSampleBuffer);
            _lastRenderedSampleBuffer = NULL;
        }
    }
}

- (void)hxc_restorePausedFrameIfNeeded {
    AVSampleBufferDisplayLayer *layer = _videoView.videoLayer;
    if (!layer) return;
    CMSampleBufferRef cached = NULL;
    @synchronized(self) {
        if (_lastRenderedSampleBuffer) {
            cached = (CMSampleBufferRef)CFRetain(_lastRenderedSampleBuffer);
        }
    }
    if (!cached) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (layer.status == AVQueuedSampleBufferRenderingStatusFailed) {
            [layer flush];
        }
        [layer enqueueSampleBuffer:cached];
        CFRelease(cached);
    });
}

/// 通知系统重新查询 Sample Buffer PiP 的播放/暂停状态（依赖 pictureInPictureControllerIsPlaybackPaused:）
- (void)invalidatePictureInPicturePlaybackStateIfNeeded {
    if (@available(iOS 15.0, *)) {
        if (_pictureInPictureController) {
            [_pictureInPictureController invalidatePlaybackState];
        }
    }
}

- (void)setupPictureInPicture {
    // 检查设备是否支持画中画
    if (![AVPictureInPictureController isPictureInPictureSupported]) {
        NSLog(@"⚠️ 当前设备不支持画中画功能");
        [self playerErrorHandler:HXCPlayerErrorNotSupportPIPPlayer errMsg:@"当前设备不支持画中画功能"];
        return;
    }
    
    // 检查 iOS 版本（PiP 需要 iOS 15.0+ 使用 AVSampleBufferDisplayLayer）
    if (@available(iOS 15.0, *)) {
        // iOS 15.0+ 支持
    } else {
        NSLog(@"⚠️ 画中画功能需要 iOS 15.0 或更高版本");
        [self playerErrorHandler:HXCPlayerErrorNotSupportPIPPlayer errMsg:@"画中画功能需要 iOS 15.0 或更高版本"];
        return;
    }
    
    // 配置音频会话，支持后台播放
    NSError *audioSessionError = nil;
    AVAudioSession *audioSession = [AVAudioSession sharedInstance];
    [audioSession setCategory:AVAudioSessionCategoryPlayback 
                         mode:AVAudioSessionModeMoviePlayback 
                      options:0 
                        error:&audioSessionError];
    [audioSession setActive:YES error:&audioSessionError];
    
    if (audioSessionError) {
        NSString *msg = [NSString stringWithFormat:@"音频会话设置失败: %@", audioSessionError.localizedDescription];
        [self playerErrorHandler:HXCPlayerErrorAudioSessionConfigFail errMsg:msg];
    }
    
    // 获取 AVSampleBufferDisplayLayer
    AVSampleBufferDisplayLayer *sampleBufferLayer = _videoView.videoLayer;
    
    if (!sampleBufferLayer) {
        NSLog(@"❌ videoLayer 为空，无法创建画中画控制器");
        return;
    }
    
    // iOS 15.0+ 创建画中画内容源和控制器
    if (@available(iOS 15.0, *)) {
        _pipContentSource = [[AVPictureInPictureControllerContentSource alloc]
                             initWithSampleBufferDisplayLayer:sampleBufferLayer
                             playbackDelegate:self];
        
        // 创建画中画控制器
        _pictureInPictureController = [[AVPictureInPictureController alloc]
                                       initWithContentSource:_pipContentSource];
        _pictureInPictureController.delegate = self;
        
        // iOS 14.2+ 默认关闭自动画中画（用户可通过属性开启）
        if (@available(iOS 14.2, *)) {
            _pictureInPictureController.canStartPictureInPictureAutomaticallyFromInline = YES;
            NSLog(@"🎬 自动画中画默认关闭，可通过 canStartPictureInPictureAutomaticallyFromInline 属性开启");
        }
        
        NSLog(@"✅ 画中画控制器初始化成功 (iOS 15.0+)");
    }
}

- (BOOL)isPictureInPictureSupported {
    return [AVPictureInPictureController isPictureInPictureSupported];
}

- (BOOL)isPictureInPictureActive {
    return _pictureInPictureController.isPictureInPictureActive;
}

- (BOOL)isPictureInPicturePossible {
    return _pictureInPictureController.isPictureInPicturePossible;
}

- (BOOL)canStartPictureInPictureAutomaticallyFromInline {
    if (@available(iOS 14.2, *)) {
        return _pictureInPictureController.canStartPictureInPictureAutomaticallyFromInline;
    }
    return NO;
}

- (void)setCanStartPictureInPictureAutomaticallyFromInline:(BOOL)canStartPictureInPictureAutomaticallyFromInline {
    if (@available(iOS 14.2, *)) {
        if (_pictureInPictureController) {
            _pictureInPictureController.canStartPictureInPictureAutomaticallyFromInline = canStartPictureInPictureAutomaticallyFromInline;
            NSLog(@"🎬 设置自动画中画: %@", canStartPictureInPictureAutomaticallyFromInline ? @"开启" : @"关闭");
        } else {
            NSLog(@"⚠️ 画中画控制器未初始化，无法设置自动画中画");
        }
    } else {
        NSLog(@"⚠️ 自动画中画功能需要 iOS 14.2 或更高版本");
    }
}

- (void)startPictureInPicture {
    if (![self hxc_licenseAllowedOrNotifyForAction:@"startPictureInPicture"]) {
        return;
    }
    // 检查 iOS 版本
    if (@available(iOS 15.0, *)) {
        // iOS 15.0+ 支持
    } else {
        NSLog(@"❌ 画中画功能需要 iOS 15.0 或更高版本");
        return;
    }
    
    if (!_pictureInPictureController) {
        NSLog(@"❌ 画中画控制器未初始化");
        return;
    }
    
    if (![self isPictureInPicturePossible]) {
        NSLog(@"⚠️ 当前无法启动画中画（视频可能未开始播放或已在画中画模式中）");
        return;
    }
    
    NSLog(@"🎬 启动画中画...");
    [_pictureInPictureController startPictureInPicture];
}

- (void)stopPictureInPicture {
    if (!_pictureInPictureController) {
        return;
    }
    
    if ([self isPictureInPictureActive]) {
        NSLog(@"🛑 停止画中画...");
        [_pictureInPictureController stopPictureInPicture];
    }
}

#pragma mark - AVPictureInPictureControllerDelegate

- (void)pictureInPictureControllerWillStartPictureInPicture:(AVPictureInPictureController *)pictureInPictureController {
    NSLog(@"📺 画中画即将开始");
    if ([_delegate respondsToSelector:@selector(player:pictureInPictureStateDidChange:)]) {
        [_delegate player:self pictureInPictureStateDidChange:HXCPlayerPIPStateWillStart];
    }
}

- (void)pictureInPictureControllerDidStartPictureInPicture:(AVPictureInPictureController *)pictureInPictureController {
    NSLog(@"✅ 画中画已开始");
    [self invalidatePictureInPicturePlaybackStateIfNeeded];
    if ([_delegate respondsToSelector:@selector(player:pictureInPictureStateDidChange:)]) {
        [_delegate player:self pictureInPictureStateDidChange:HXCPlayerPIPStateDidStart];
    }
}

- (void)pictureInPictureControllerWillStopPictureInPicture:(AVPictureInPictureController *)pictureInPictureController {
    NSLog(@"📺 画中画即将停止");
    if ([_delegate respondsToSelector:@selector(player:pictureInPictureStateDidChange:)]) {
        [_delegate player:self pictureInPictureStateDidChange:HXCPlayerPIPStateWillStop];
    }
}

- (void)pictureInPictureControllerDidStopPictureInPicture:(AVPictureInPictureController *)pictureInPictureController {
    NSLog(@"✅ 画中画已停止");
    if ([_delegate respondsToSelector:@selector(player:pictureInPictureStateDidChange:)]) {
        [_delegate player:self pictureInPictureStateDidChange:HXCPlayerPIPStateDidStop];
    }
}

- (void)pictureInPictureController:(AVPictureInPictureController *)pictureInPictureController 
           failedToStartPictureInPictureWithError:(NSError *)error {
    NSLog(@"❌ 画中画启动失败: %@", error.localizedDescription);
    
    if ([_delegate respondsToSelector:@selector(player:didFailWithError:)]) {
        [_delegate player:self didFailWithError:error];
    }
}

- (void)pictureInPictureController:(AVPictureInPictureController *)pictureInPictureController 
    restoreUserInterfaceForPictureInPictureStopWithCompletionHandler:(void (^)(BOOL restored))completionHandler {
    NSLog(@"🔄 从画中画恢复用户界面");
    
    // 通知 delegate 恢复界面
    if ([_delegate respondsToSelector:@selector(player:restoreUserInterfaceForPictureInPictureStopWithCompletionHandler:)]) {
        [_delegate player:self restoreUserInterfaceForPictureInPictureStopWithCompletionHandler:completionHandler];
    } else {
        // 如果 delegate 没有实现，直接完成
        completionHandler(YES);
    }
    //已经恢复
    if ([_delegate respondsToSelector:@selector(player:pictureInPictureStateDidChange:)]) {
        [_delegate player:self pictureInPictureStateDidChange:HXCPlayerPIPStateRestore];
    }
}

#pragma mark - AVPictureInPictureSampleBufferPlaybackDelegate

- (void)pictureInPictureController:(AVPictureInPictureController *)pictureInPictureController 
                        setPlaying:(BOOL)playing {
    NSLog(@"📺 画中画窗口控制播放: %@", playing ? @"播放" : @"暂停");
    
    if (playing) {
        [self play];
    } else {
        [self pause];
    }
}

- (CMTimeRange)pictureInPictureControllerTimeRangeForPlayback:(AVPictureInPictureController *)pictureInPictureController {
    // 返回可播放的时间范围
    double duration = [self duration];
    if (duration > 0) {
        return CMTimeRangeMake(kCMTimeZero, CMTimeMakeWithSeconds(duration, 1000000));
    }
    return CMTimeRangeMake(kCMTimeZero, kCMTimePositiveInfinity);
}

- (BOOL)pictureInPictureControllerIsPlaybackPaused:(AVPictureInPictureController *)pictureInPictureController {
    // 返回当前是否暂停
    return _state != HXCPlayerStatePlaying;
}

- (void)pictureInPictureController:(AVPictureInPictureController *)pictureInPictureController 
                    didTransitionToRenderSize:(CMVideoDimensions)newRenderSize {
    NSLog(@"📺 画中画渲染尺寸变化: %dx%d", newRenderSize.width, newRenderSize.height);
}

- (void)pictureInPictureController:(AVPictureInPictureController *)pictureInPictureController 
                   skipByInterval:(CMTime)skipInterval 
                completionHandler:(void (^)(void))completionHandler {
    // 处理快进/快退
    double skipSeconds = CMTimeGetSeconds(skipInterval);
    double currentPos = [self position];
    double newPos = currentPos + skipSeconds;
    
    // 限制在有效范围内
    if (newPos < 0) newPos = 0;
    if (newPos > [self duration]) newPos = [self duration];
    
    NSLog(@"⏩ 画中画跳转: %.2f 秒 (%.2f → %.2f)", skipSeconds, currentPos, newPos);
    [self seekToPosition:newPos];
    
    completionHandler();
}

#endif // TARGET_OS_IOS

@end

@implementation HXCPlayerControl (HXCAppLog)

+ (void)hxc_logDebugAtFile:(const char *)file line:(int)line function:(const char *)func format:(NSString *)format, ... {
    va_list ap;
    va_start(ap, format);
    NSString *msg = [[NSString alloc] initWithFormat:format locale:nil arguments:ap];
    va_end(ap);
    player_core_log_line(0, file, line, func, msg.UTF8String ?: "");
}

+ (void)hxc_logInfoAtFile:(const char *)file line:(int)line function:(const char *)func format:(NSString *)format, ... {
    va_list ap;
    va_start(ap, format);
    NSString *msg = [[NSString alloc] initWithFormat:format locale:nil arguments:ap];
    va_end(ap);
    player_core_log_line(1, file, line, func, msg.UTF8String ?: "");
}

+ (void)hxc_logWarningAtFile:(const char *)file line:(int)line function:(const char *)func format:(NSString *)format, ... {
    va_list ap;
    va_start(ap, format);
    NSString *msg = [[NSString alloc] initWithFormat:format locale:nil arguments:ap];
    va_end(ap);
    player_core_log_line(2, file, line, func, msg.UTF8String ?: "");
}

+ (void)hxc_logErrorAtFile:(const char *)file line:(int)line function:(const char *)func format:(NSString *)format, ... {
    va_list ap;
    va_start(ap, format);
    NSString *msg = [[NSString alloc] initWithFormat:format locale:nil arguments:ap];
    va_end(ap);
    player_core_log_line(3, file, line, func, msg.UTF8String ?: "");
}

@end
