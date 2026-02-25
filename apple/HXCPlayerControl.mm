/**
 * @file HXCPlayerControl.mm
 * @brief Apple 平台统一播放器实现（支持 iOS 和 macOS）
 */

#include "hxc_player_core_c_bridge.h"
#import "HXCPlayerControl.h"
#import "HXCPlayerView.h"
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
}

@property (nonatomic, strong) HXCPlayerView *videoView;
@property (nonatomic, strong) dispatch_queue_t renderQueue;

@end

@implementation HXCPlayerControl

- (instancetype)init {
    self = [super init];
    if (self) {
        _wrapper = new PlayerCoreWrapper();
        _state = HXCPlayerStateIdle;
        _volume = 1.0;
        _playbackRate = 1.0;
        _startPosition = 0.0;
        _aspectRatioMode = HXCAspectRatioModeFit;
        _audioQueueRunning = NO;
        
        // 创建视频视图（自动管理布局）
        _videoView = [[HXCPlayerView alloc] initWithFrame:CGRectZero];
        
#if TARGET_OS_IOS
        NSLog(@"[播放器] 初始化 HXCPlayerControl (iOS)");
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
    [self close];
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
    
    [self close];
    _playerUrl = [url copy];
    _state = HXCPlayerStateOpening;
    
    if ([_delegate respondsToSelector:@selector(playerDidChangeState:)]) {
        [_delegate playerDidChangeState:_state];
    }
    
    int ret;
    if (_startPosition > 0) {
        ret = player_core_open_with_start_position(_wrapper->handle(), url.UTF8String, _startPosition);
    } else {
        ret = player_core_open(_wrapper->handle(), url.UTF8String);
    }
    
    if (ret != 0) {
        _state = HXCPlayerStateError;
        if ([_delegate respondsToSelector:@selector(playerDidChangeState:)]) {
            [_delegate playerDidChangeState:_state];
        }
        return NO;
    }
    
    int sampleRate = player_core_get_audio_sample_rate(_wrapper->handle());
    int channels = player_core_get_audio_channels(_wrapper->handle());
    _duration = player_core_get_duration(_wrapper->handle());
    _videoWidth = player_core_get_video_width(_wrapper->handle());
    _videoHeight = player_core_get_video_height(_wrapper->handle());
    
    if (sampleRate > 0) {
        [self setupAudioQueue:sampleRate channels:channels];
    }
    
    [self startDisplayLink];
    
    _state = HXCPlayerStatePaused;
    player_core_pause(_wrapper->handle());
    if (_audioQueue && _audioQueueRunning) {
        AudioQueuePause(_audioQueue);
        _audioQueueRunning = NO;
    }
    
    if ([_delegate respondsToSelector:@selector(playerDidChangeState:)]) {
        [_delegate playerDidChangeState:_state];
    }
    
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
    
    player_core_play(_wrapper->handle());
    
    if (_audioQueue && !_audioQueueRunning) {
        AudioQueueStart(_audioQueue, NULL);
        _audioQueueRunning = YES;
    }
    
    _state = HXCPlayerStatePlaying;
    
    if ([_delegate respondsToSelector:@selector(playerDidChangeState:)]) {
        [_delegate playerDidChangeState:_state];
    }
}

- (void)pause {
    player_core_pause(_wrapper->handle());
    
    if (_audioQueue && _audioQueueRunning) {
        AudioQueuePause(_audioQueue);
        _audioQueueRunning = NO;
    }
    
    _state = HXCPlayerStatePaused;
    
    if ([_delegate respondsToSelector:@selector(playerDidChangeState:)]) {
        [_delegate playerDidChangeState:_state];
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

- (void)stop {
    // stop 是 close 的别名
    [self close];
}

- (void)seekToPosition:(double)position {
    player_core_seek(_wrapper->handle(), position);
    
#if !TARGET_OS_IOS
    // macOS 需要更新 controlTimebase
    if (_videoView.videoLayer && _videoView.videoLayer.controlTimebase) {
        CMTime newTime = CMTimeMake((int64_t)(position * 1000000), 1000000);
        CMTimebaseSetTime(_videoView.videoLayer.controlTimebase, newTime);
    }
#endif
}

- (void)close {
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
    
    if ([_delegate respondsToSelector:@selector(playerDidChangeState:)]) {
        [_delegate playerDidChangeState:_state];
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
    
    if (delay <= -threshold) {
        player_core_consume_video_frame(_wrapper->handle());
    } else if (delay <= threshold) {
        [self displayVideoFrameData:&frame_data];
        player_core_consume_video_frame(_wrapper->handle());
    }
}

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

@end
