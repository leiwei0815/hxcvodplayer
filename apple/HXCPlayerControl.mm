/**
 * @file HXCPlayerControl.mm
 * @brief Apple 平台统一播放器实现（支持 iOS 和 macOS）
 */

// 只包含 C 桥接层（不包含 FFmpeg 头文件）
#include "hxc_player_core_c_bridge.h"

// 包含 Objective-C 头文件
#import "HXCPlayerControl.h"
#import "HXCPlayerView.h"

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

// ========== C 回调函数前向声明 ==========
static void state_changed_callback_c(PlayerStateC state, void* user_data);
static void error_callback_c(int error_code, const char* error_msg, void* user_data);
static void position_changed_callback_c(double position, void* user_data);
static void buffer_progress_callback_c(double position, void* user_data);
static void playback_completed_callback_c(void* user_data);
static void loading_callback_c(bool is_loading, void* user_data);

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
#else  // macOS
    CVDisplayLinkRef _displayLink;
#endif
    
    // 状态
    HXCPlayerState _state;
    double _duration;
    double _position;
    NSString *_playerUrl;
    HXCAspectRatioMode _aspectRatioMode;
    int _videoWidth;
    int _videoHeight;
    
    // 进度更新回调限流
    CFTimeInterval _lastPositionUpdateTime;
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
-(void)playerPositionChange:(double)position;

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
        if ([self.delegate respondsToSelector:@selector(player:didChangeLoadingState:)]) {
            [self.delegate player:self didChangeLoadingState:is_loading];
        }
    });
}

// ========== HXCPlayerDataSourceConfig 实现 ==========
@implementation HXCPlayerDataSourceConfig

+ (instancetype)defaultConfig {
    HXCPlayerDataSourceConfig *config = [[HXCPlayerDataSourceConfig alloc] init];
    config.timeoutMs = 30000;           // 30秒
    config.maxRetries = 3;              // 重试3次
    config.cacheSize = 2 * 1024 * 1024; // 2MB
    config.avioBufferSize = 64 * 1024;  // 64KB
    config.encryptedFile = NO;
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
        self.encryptedFile = NO;
    }
    return self;
}

@end

@implementation HXCPlayerControl

- (instancetype)init {
    self = [super init];
    if (self) {
        // ========== 配置日志系统 ==========
        [self setupLogging];
        
        _wrapper = new PlayerCoreWrapper();
        _state = HXCPlayerStateIdle;
        _volume = 1.0;
        _playbackRate = 1.0;
        _startPosition = 0.0;
        _aspectRatioMode = HXCAspectRatioModeFit;
        _audioQueueRunning = NO;
        _lastPositionUpdateTime = 0;
        
        // ========== 设置播放器回调 ==========
        player_core_set_state_changed_callback(_wrapper->handle(), state_changed_callback_c, (__bridge void*)self);
        player_core_set_error_callback(_wrapper->handle(), error_callback_c, (__bridge void*)self);
        player_core_set_position_changed_callback(_wrapper->handle(), position_changed_callback_c, (__bridge void*)self);
        player_core_set_buffer_progress_callback(_wrapper->handle(), buffer_progress_callback_c, (__bridge void*)self);
        player_core_set_playback_completed_callback(_wrapper->handle(), playback_completed_callback_c, (__bridge void*)self);
        player_core_set_loading_callback(_wrapper->handle(), loading_callback_c, (__bridge void*)self);
        
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
    
    [self stop];
    
    // 禁用文件日志（会自动刷新队列并停止后台线程）
    player_core_disable_file_logging();
    NSLog(@"📝 日志系统已关闭");
    
    if (_wrapper) {
        delete _wrapper;
        _wrapper = nullptr;
    }
}

#pragma mark - Public Methods

- (BOOL)openURL:(NSString *)url {
    if (!url || url.length == 0) {
        return NO;
    }
    [self stop];
    _playerUrl = [url copy];
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

    return YES;
}

// ✨ 使用指定数据源模式打开（推荐方式）
- (BOOL)openURL:(NSString *)url withMode:(HXCPlayerDataSourceMode)mode config:(HXCPlayerDataSourceConfig *)config {
    if (!url || url.length == 0) {
        return NO;
    }
    
    [self stop];
    _playerUrl = [url copy];
    
    // 使用默认配置（如果没有提供）
    if (!config) {
        config = [HXCPlayerDataSourceConfig defaultConfig];
    }
    
    // 转换配置参数到 C 结构
    PlayerDataSourceConfigC cConfig;
    cConfig.timeout_ms = (int)config.timeoutMs;
    cConfig.max_retries = (int)config.maxRetries;
    cConfig.cache_size = config.cacheSize;
    cConfig.avio_buffer_size = config.avioBufferSize;
    cConfig.encrypted_file = config.encryptedFile ? 1 : 0;
    
    // 转换模式枚举
    PlayerDataSourceModeC cMode;
    switch (mode) {
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
    
    // 调用底层 C 接口
    int ret = player_core_open_with_mode(_wrapper->handle(), url.UTF8String, cMode, &cConfig, _startPosition);
    
    if (ret != 0) {
        NSLog(@"❌ 打开失败: mode=%ld, ret=%d", (long)mode, ret);
        return NO;
    }
    
    // 获取媒体信息
    int sampleRate = player_core_get_audio_sample_rate(_wrapper->handle());
    int channels = player_core_get_audio_channels(_wrapper->handle());
    _duration = player_core_get_duration(_wrapper->handle());
    _videoWidth = player_core_get_video_width(_wrapper->handle());
    _videoHeight = player_core_get_video_height(_wrapper->handle());
    
    NSLog(@"✅ 使用模式 %ld 打开成功", (long)mode);
    NSLog(@"   URL: %@", url);
    NSLog(@"   时长: %.2f 秒", _duration);
    NSLog(@"   分辨率: %d x %d", _videoWidth, _videoHeight);
    NSLog(@"   音频: %d Hz, %d 通道", sampleRate, channels);
    
    [self setupAudioQueue:sampleRate channels:channels];
    
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
    
    return YES;
}

- (BOOL)prepareToPlay:(NSString *)url {
    // prepareToPlay: 是 openURL: 的别名，都是打开文件但不自动播放
    return [self openURL:url];
}

- (void)play {
    if (!_playerUrl && _state == HXCPlayerStateIdle) {
        NSLog(@"警告: 请先调用 openURL:");
        return;
    }
    HXCPlayerState previousState = _state;
    player_core_play(_wrapper->handle());
    if (_audioQueue && !_audioQueueRunning) {
        AudioQueueStart(_audioQueue, NULL);
        _audioQueueRunning = YES;
    }
    // 立即与底层一致，避免画中画依赖异步 state 回调时读到旧状态
    _state = HXCPlayerStatePlaying;
    if (previousState != HXCPlayerStatePlaying) {
        if ([_delegate respondsToSelector:@selector(player:didChangeState:)]) {
            [_delegate player:self didChangeState:_state];
        }
#if TARGET_OS_IOS
        [self invalidatePictureInPicturePlaybackStateIfNeeded];
#endif
    }
}

- (void)pause {
    HXCPlayerState previousState = _state;
    player_core_pause(_wrapper->handle());
    if (_audioQueue && _audioQueueRunning) {
        AudioQueuePause(_audioQueue);
        _audioQueueRunning = NO;
    }
    _state = HXCPlayerStatePaused;
    if (previousState != HXCPlayerStatePaused) {
        if ([_delegate respondsToSelector:@selector(player:didChangeState:)]) {
            [_delegate player:self didChangeState:_state];
        }
#if TARGET_OS_IOS
        [self invalidatePictureInPicturePlaybackStateIfNeeded];
#endif
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
    [self openURL:url];
    [self play];
}

- (void)seekToPosition:(double)position {
    player_core_seek(_wrapper->handle(), position);
    
    // iOS 和 macOS 都需要更新 controlTimebase
    if (_videoView.videoLayer && _videoView.videoLayer.controlTimebase) {
        CMTime newTime = CMTimeMake(position * 1000000, 1000000);
        CMTimebaseSetTime(_videoView.videoLayer.controlTimebase, newTime);
        
        // 确保 timebase 在运行
        if (CMTimebaseGetRate(_videoView.videoLayer.controlTimebase) == 0.0) {
            CMTimebaseSetRate(_videoView.videoLayer.controlTimebase, 1.0);
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

    [_videoView.videoLayer flushAndRemoveImage];

    _state = HXCPlayerStateIdle;
    _duration = 0;
    _position = 0;
    _playerUrl = nil;
    _videoWidth = 0;
    _videoHeight = 0;
#if TARGET_OS_IOS
    if (previousState != HXCPlayerStateIdle) {
        [self invalidatePictureInPicturePlaybackStateIfNeeded];
    }
#endif
}


#pragma mark - private method

-(void)playerPositionChange:(double)position {
    self->_position = position;
    if ([self.delegate respondsToSelector:@selector(player:didUpdatePosition:)]) {
        [self.delegate player:self didUpdatePosition:position];
    }
}

-(void)playerStateChange:(PlayerStateC)state {
    HXCPlayerState objcState;
    switch (state) {
        case PLAYER_STATE_IDLE:
            objcState = HXCPlayerStateIdle;
            break;
        case PLAYER_STATE_OPENING:
            objcState = HXCPlayerStateOpening;
            break;
        case PLAYER_STATE_PLAYING:
            objcState = HXCPlayerStatePlaying;
            break;
        case PLAYER_STATE_PAUSED:
            objcState = HXCPlayerStatePaused;
            break;
        case PLAYER_STATE_STOPPED:
            objcState = HXCPlayerStateStopped;
            break;
        case PLAYER_STATE_ERROR:
            objcState = HXCPlayerStateError;
            break;
        default:
            objcState = HXCPlayerStateIdle;
            break;
    }
    HXCPlayerState previous = _state;
    self->_state = objcState;
    // 与 play/pause/openURL 的乐观更新对齐：仅在核心状态真的变化时通知，避免重复回调
    if (previous != objcState) {
        if ([self.delegate respondsToSelector:@selector(player:didChangeState:)]) {
            [self.delegate player:self didChangeState:objcState];
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
    return _state;
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
        dispatch_async(dispatch_get_main_queue(), ^{
            if (self->_videoView.videoLayer.status == AVQueuedSampleBufferRenderingStatusFailed) {
                [self->_videoView.videoLayer flush];
            }
            [self->_videoView.videoLayer enqueueSampleBuffer:sampleBuffer];
            CFRelease(sampleBuffer);
        });
    }
    
    if (formatDesc) {
        CFRelease(formatDesc);
    }
    
    CVPixelBufferRelease(pixelBuffer);
}

- (CVPixelBufferRef)createPixelBufferFromFrameData:(VideoFrameDataC *)frameData {
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
    
    for (int i = 0; i < frameData->height; i++) {
        memcpy(yPlane + i * yStride, ySrc + i * frameData->y_linesize, frameData->width);
    }
    
    // UV 平面（NV12 交织）
    uint8_t *uvPlane = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
    size_t uvStride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
    uint8_t *uSrc = (uint8_t *)frameData->u_data;
    uint8_t *vSrc = (uint8_t *)frameData->v_data;
    
    int uvHeight = frameData->height / 2;
    int uvWidth = frameData->width / 2;
    
    for (int i = 0; i < uvHeight; i++) {
        for (int j = 0; j < uvWidth; j++) {
            uvPlane[i * uvStride + j * 2] = uSrc[i * frameData->u_linesize + j];
            uvPlane[i * uvStride + j * 2 + 1] = vSrc[i * frameData->v_linesize + j];
        }
    }
    
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    
    return pixelBuffer;
}

#pragma mark - 日志配置

+(void)setLogLevel:(HXCPlayerLogLevel)level {
    player_core_set_log_level((int)level);
}

+(void)setLogDir:(NSString *)dir {
    if (!dir) {
        NSLog(@"日志路径设置不能为空");
        return;
    }
    NSFileManager *fileManager = [NSFileManager defaultManager];
    NSError *error = nil;
    if (![fileManager fileExistsAtPath:dir]) {
        [fileManager createDirectoryAtPath:dir
               withIntermediateDirectories:YES
                                attributes:nil
                                     error:&error];
        if (error) {
            NSLog(@"❌ 创建日志目录失败: %@", error.localizedDescription);
            return;
        }
    }
    player_core_enable_file_logging([dir UTF8String], "hxcplayer");
}

- (void)setupLogging {
    // 设置日志级别为 DEBUG
    [HXCPlayerControl setLogLevel:HXCPlayerLogLevelDebug];
    // 获取 Documents 目录
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString *documentsDirectory = [paths firstObject];
    NSString *logDir = [documentsDirectory stringByAppendingPathComponent:@"HXCPlayerLogs"];
    [HXCPlayerControl setLogDir:logDir];
    // 设置日志保留天数（默认7天）
    player_core_set_log_retention_days(7);
    
    // 启用文件日志（会自动清理超过7天的旧日志）
//    player_core_enable_file_logging([logDir UTF8String], "hxcplayer");
    
    // 设置最大文件大小为 10MB
    player_core_set_max_log_file_size(10 * 1024 * 1024);
    
    // 获取当前日志文件路径
    const char* logFile = player_core_get_current_log_file();
    NSString *logFilePath = logFile ? [NSString stringWithUTF8String:logFile] : @"未知";
    
    NSLog(@"========================================");
    NSLog(@"📝 HXCPlayer 日志系统已启用");
    NSLog(@"========================================");
    NSLog(@"日志级别: DEBUG");
    NSLog(@"日志目录: %@", logDir);
    NSLog(@"日志文件: %@", logFilePath);
    NSLog(@"保留天数: 7 天");
    NSLog(@"最大大小: 10 MB");
    NSLog(@"========================================");
}

#pragma mark - Picture in Picture (iOS only)

#if TARGET_OS_IOS

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
