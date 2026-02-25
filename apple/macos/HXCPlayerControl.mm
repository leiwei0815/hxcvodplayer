/**
 * @file HXCPlayerControl.mm
 * @brief macOS 播放器核心实现
 */

// ✅ 只包含 C 接口桥接层（不直接包含 FFmpeg 头文件）
#include "hxc_player_core_c_bridge.h"

// 现在可以安全地包含 macOS 框架（没有 FFmpeg 冲突）
#import "HXCPlayerControl.h"
#include <AudioToolbox/AudioToolbox.h>

// C++ 播放器包装器（使用 C 桥接接口）
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
    
    // 视频渲染定时器（macOS 使用 CVDisplayLink）
    CVDisplayLinkRef _displayLink;
    
    // 状态
    HXCPlayerState _state;
    double _duration;
    double _position;
    NSString *_playerUrl;  // 当前播放的 URL
    HXCAspectRatioMode _aspectRatioMode;  // 视频显示模式
    int _videoWidth;
    int _videoHeight;
}

@property (nonatomic, strong) AVSampleBufferDisplayLayer *videoLayer;
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
        _startPosition = 0.0;  // 默认从头开始
        _aspectRatioMode = HXCAspectRatioModeFit;  // 默认 Fit 模式
        _audioQueueRunning = NO;
        
        NSLog(@"[播放器] 初始化 HXCPlayerControl...");
        
        // 创建视频显示层
        _videoLayer = [[AVSampleBufferDisplayLayer alloc] init];
        _videoLayer.videoGravity = AVLayerVideoGravityResizeAspect;  // 默认 Fit 模式
        _videoLayer.backgroundColor = [[NSColor blackColor] CGColor];
        
        // 设置控制时间基准（使用 CACurrentMediaTime）
        CMTimebaseRef controlTimebase;
        CMTimebaseCreateWithSourceClock(kCFAllocatorDefault, 
                                       CMClockGetHostTimeClock(), 
                                       &controlTimebase);
        _videoLayer.controlTimebase = controlTimebase;
        CFRelease(controlTimebase);
        
        // 启动时间基准
        CMTimebaseSetTime(_videoLayer.controlTimebase, kCMTimeZero);
        CMTimebaseSetRate(_videoLayer.controlTimebase, 1.0);
        
        NSLog(@"✅ VideoLayer 创建成功 - Frame: %@", NSStringFromRect(_videoLayer.frame));
        
        // 创建渲染队列
        _renderQueue = dispatch_queue_create("com.hxcplayer.macos.render", DISPATCH_QUEUE_SERIAL);
        
        // C 接口暂不支持回调，macOS 层使用轮询方式检测状态变化
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

// 内部打开方法
- (BOOL)_openURLInternal:(NSString *)url autoPlay:(BOOL)autoPlay {
    if (!url || url.length == 0) {
        return NO;
    }
    
    // 关闭之前的播放
    [self close];
    
    // 保存 URL
    _playerUrl = [url copy];
    
    // 更新状态为打开中
    _state = HXCPlayerStateOpening;
    if ([_delegate respondsToSelector:@selector(player:didChangeState:)]) {
        [_delegate player:self didChangeState:_state];
    }
    
    // 打开文件（如果设置了起始位置，一起传入）
    int ret;
    if (_startPosition > 0) {
        ret = player_core_open_with_start_position(_wrapper->handle(), url.UTF8String, _startPosition);
        if (ret == 0) {
            NSLog(@"打开视频并设置起始位置: %.2f 秒", _startPosition);
        }
    } else {
        ret = player_core_open(_wrapper->handle(), url.UTF8String);
    }
    
    if (ret != 0) {
        _state = HXCPlayerStateError;
        if ([_delegate respondsToSelector:@selector(player:didChangeState:)]) {
            [_delegate player:self didChangeState:_state];
        }
        if ([_delegate respondsToSelector:@selector(player:didEncounterError:)]) {
            [_delegate player:self didEncounterError:@"无法打开媒体文件"];
        }
        return NO;
    }
    
    // 获取媒体信息
    int sampleRate = player_core_get_audio_sample_rate(_wrapper->handle());
    int channels = player_core_get_audio_channels(_wrapper->handle());
    _duration = player_core_get_duration(_wrapper->handle());
    
    // 获取视频尺寸
    _videoWidth = player_core_get_video_width(_wrapper->handle());
    _videoHeight = player_core_get_video_height(_wrapper->handle());
    
    // 初始化音频队列
    if (sampleRate > 0) {
        [self setupAudioQueue:sampleRate channels:channels];
    }
    
    // 启动视频渲染定时器
    [self startDisplayLink];
    
    // 根据 autoPlay 决定状态
    if (autoPlay) {
        _state = HXCPlayerStatePlaying;
    } else {
        _state = HXCPlayerStatePaused;
        // 暂停底层解码器
        player_core_pause(_wrapper->handle());
        if (_audioQueue && _audioQueueRunning) {
            AudioQueuePause(_audioQueue);
            _audioQueueRunning = NO;
        }
    }
    
    if ([_delegate respondsToSelector:@selector(player:didChangeState:)]) {
        [_delegate player:self didChangeState:_state];
    }
    
    return YES;
}

- (BOOL)prepareToPlay:(NSString *)url {
    return [self _openURLInternal:url autoPlay:NO];
}

- (void)play {
    // 如果还没有打开文件，无法播放
    if (!_playerUrl && _state == HXCPlayerStateIdle) {
        NSLog(@"警告: 请先调用 prepareToPlay: 打开文件");
        return;
    }
    
    player_core_play(_wrapper->handle());
    
    if (_audioQueue && !_audioQueueRunning) {
        AudioQueueStart(_audioQueue, NULL);
        _audioQueueRunning = YES;
    }
    
    // 更新状态
    _state = HXCPlayerStatePlaying;
    
    // 通知代理
    if ([_delegate respondsToSelector:@selector(player:didChangeState:)]) {
        [_delegate player:self didChangeState:_state];
    }
}

- (void)pause {
    player_core_pause(_wrapper->handle());
    
    if (_audioQueue && _audioQueueRunning) {
        AudioQueuePause(_audioQueue);
        _audioQueueRunning = NO;
    }
    
    // 更新状态
    _state = HXCPlayerStatePaused;
    
    // 通知代理
    if ([_delegate respondsToSelector:@selector(player:didChangeState:)]) {
        [_delegate player:self didChangeState:_state];
    }
}

- (void)stop {
    [self close];
    
    // 更新状态
    _state = HXCPlayerStateStopped;
    
    // 通知代理
    if ([_delegate respondsToSelector:@selector(player:didChangeState:)]) {
        [_delegate player:self didChangeState:_state];
    }
}

- (void)replay {
    if (!_playerUrl) {
        NSLog(@"警告: 没有可重播的视频");
        return;
    }
    
    // 保存当前 URL
    NSString *url = [_playerUrl copy];
    
    // 重新打开并自动播放
    [self _openURLInternal:url autoPlay:YES];
}

- (void)seekToPosition:(double)position {
    player_core_seek(_wrapper->handle(), position);
    
    // ✅ 重要：更新 videoLayer 的 controlTimebase 到 seek 位置
    // 否则 AVSampleBufferDisplayLayer 会因为 PTS 不匹配而不显示帧
    if (_videoLayer && _videoLayer.controlTimebase) {
        CMTime newTime = CMTimeMake((int64_t)(position * 1000000), 1000000);
        CMTimebaseSetTime(_videoLayer.controlTimebase, newTime);
        NSLog(@"[Seek] 更新 controlTimebase 到: %.2f 秒", position);
    }
}

- (void)close {
    // 停止显示链接
    [self stopDisplayLink];
    
    // 停止音频队列
    [self teardownAudioQueue];
    
    // 关闭核心播放器
    if (_wrapper && _wrapper->handle()) {
        player_core_stop(_wrapper->handle());
    }
    
    // 清空视频层
    [_videoLayer flushAndRemoveImage];
    
    _state = HXCPlayerStateIdle;
    _duration = 0;
    _position = 0;
    _playerUrl = nil;  // 清空 URL
    _videoWidth = 0;
    _videoHeight = 0;
    
    // 通知代理
    if ([_delegate respondsToSelector:@selector(player:didChangeState:)]) {
        [_delegate player:self didChangeState:_state];
    }
}

#pragma mark - Properties

- (HXCPlayerState)state {
    return _state;
}

- (NSString *)playerUrl {
    return _playerUrl;
}

- (double)duration {
    return _wrapper ? player_core_get_duration(_wrapper->handle()) : 0;
}

- (double)position {
    return _wrapper ? player_core_get_position(_wrapper->handle()) : 0;
}

- (void)setVolume:(float)volume {
    _volume = MAX(0.0, MIN(1.0, volume));
    if (_audioQueue) {
        AudioQueueSetParameter(_audioQueue, kAudioQueueParam_Volume, _volume);
    }
}

- (void)setPlaybackRate:(double)playbackRate {
    playbackRate = MAX(0.5, MIN(2.0, playbackRate));
    _playbackRate = playbackRate;
    
    // 设置底层 C++ 核心的播放速度（底层会处理 SoundTouch）
    if (_wrapper) {
        player_core_set_playback_rate(_wrapper->handle(), playbackRate);
    }
}

- (void)setAspectRatioMode:(HXCAspectRatioMode)aspectRatioMode {
    _aspectRatioMode = aspectRatioMode;
    
    // 更新底层 C 桥接层的设置
    if (_wrapper) {
        AspectRatioModeC mode = (aspectRatioMode == HXCAspectRatioModeFit) ? ASPECT_RATIO_FIT : ASPECT_RATIO_FILL;
        player_core_set_aspect_ratio_mode(_wrapper->handle(), mode);
    }
    
    // 更新 AVSampleBufferDisplayLayer 的显示模式
    dispatch_async(dispatch_get_main_queue(), ^{
        if (aspectRatioMode == HXCAspectRatioModeFit) {
            self->_videoLayer.videoGravity = AVLayerVideoGravityResizeAspect;  // 保持宽高比，可能有黑边
        } else {
            self->_videoLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;  // 填充，裁剪画面
        }
    });
}

- (HXCAspectRatioMode)aspectRatioMode {
    return _aspectRatioMode;
}

- (CGSize)videoSize {
    return CGSizeMake(_videoWidth, _videoHeight);
}

#pragma mark - Audio Setup

- (void)setupAudioQueue:(int)sampleRate channels:(int)channels {
    // 配置音频格式
    _audioFormat.mSampleRate = sampleRate;
    _audioFormat.mFormatID = kAudioFormatLinearPCM;
    _audioFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    _audioFormat.mBytesPerPacket = channels * sizeof(SInt16);
    _audioFormat.mFramesPerPacket = 1;
    _audioFormat.mBytesPerFrame = channels * sizeof(SInt16);
    _audioFormat.mChannelsPerFrame = channels;
    _audioFormat.mBitsPerChannel = 16;
    
    NSLog(@"[音频] 初始化 AudioQueue - 采样率: %d, 通道: %d", sampleRate, channels);
    
    // 创建音频队列
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
    
    // 创建缓冲区（增大缓冲区以减少杂音）
    UInt32 bufferSize = 4096 * channels * sizeof(SInt16);
    for (int i = 0; i < kNumberOfBuffers; i++) {
        AudioQueueAllocateBuffer(_audioQueue, bufferSize, &_audioBuffers[i]);
        audioQueueCallback((__bridge void *)self, _audioQueue, _audioBuffers[i]);
    }
    
    // 设置音量
    AudioQueueSetParameter(_audioQueue, kAudioQueueParam_Volume, _volume);
    
    NSLog(@"✅ AudioQueue 初始化完成 - 缓冲区大小: %d bytes", (int)bufferSize);
}

- (void)teardownAudioQueue {
    if (_audioQueue) {
        AudioQueueStop(_audioQueue, YES);
        AudioQueueDispose(_audioQueue, YES);
        _audioQueue = NULL;
        _audioQueueRunning = NO;
    }
}

// 音频队列回调（C 函数）
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
    
    // 从 C 桥接层获取音频数据
    int got = player_core_get_audio_data(_wrapper->handle(), 
                                        (unsigned char*)buffer->mAudioData, 
                                        (int)buffer->mAudioDataBytesCapacity);
    
    if (got > 0) {
        buffer->mAudioDataByteSize = got;
    } else {
        // 没有数据，输出静音
        memset(buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity);
        buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
    }
}

#pragma mark - Video Rendering (CVDisplayLink for macOS)

// CVDisplayLink 回调函数
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
    
    NSLog(@"[视频] 启动 CVDisplayLink...");
    
    // 创建 CVDisplayLink
    CVReturn ret = CVDisplayLinkCreateWithActiveCGDisplays(&_displayLink);
    if (ret != kCVReturnSuccess) {
        NSLog(@"❌ CVDisplayLink 创建失败: %d", ret);
        return;
    }
    
    CVDisplayLinkSetOutputCallback(_displayLink, &displayLinkCallback, (__bridge void *)self);
    ret = CVDisplayLinkStart(_displayLink);
    
    if (ret == kCVReturnSuccess) {
        NSLog(@"✅ CVDisplayLink 启动成功");
    } else {
        NSLog(@"❌ CVDisplayLink 启动失败: %d", ret);
    }
}

- (void)stopDisplayLink {
    if (_displayLink) {
        CVDisplayLinkStop(_displayLink);
        CVDisplayLinkRelease(_displayLink);
        _displayLink = NULL;
    }
}

- (void)renderVideoFrame {
    if (!_wrapper || !_wrapper->handle()) {
        return;
    }
    
    // 获取视频帧数据
    VideoFrameDataC frame_data;
    int ret = player_core_get_video_frame(_wrapper->handle(), &frame_data);
    if (ret != 0) {
        return;  // 没有可用帧
    }
    
    double currentPTS = frame_data.pts;
    double masterClock = player_core_get_position(_wrapper->handle());
    
    // 首次渲染时打印日志
    static int frame_count = 0;
    if (frame_count++ % 60 == 0) {
        NSLog(@"[视频] 渲染帧 - PTS: %.2f, 主时钟: %.2f, 尺寸: %dx%d", 
              currentPTS, masterClock, frame_data.width, frame_data.height);
    }
    
    if (isnan(currentPTS)) {
        // 没有 PTS，直接显示
        [self displayVideoFrameData:&frame_data];
        player_core_consume_video_frame(_wrapper->handle());
        return;
    }
    
    // 计算延迟（音视频同步）
    double delay = currentPTS - masterClock;
    double playbackRate = player_core_get_playback_rate(_wrapper->handle());
    double threshold = 0.04 / playbackRate;  // 根据播放速率调整阈值
    
    if (delay <= -threshold) {
        // 视频落后，丢帧
        player_core_consume_video_frame(_wrapper->handle());
    } else if (delay <= threshold) {
        // 在同步范围内，显示
        [self displayVideoFrameData:&frame_data];
        player_core_consume_video_frame(_wrapper->handle());
    }
    // else: 视频领先，等待下一帧
}

- (void)displayVideoFrameData:(VideoFrameDataC *)frameData {
    if (!frameData || !_videoLayer) {
        return;
    }
    
    // 创建 CVPixelBuffer
    CVPixelBufferRef pixelBuffer = [self createPixelBufferFromFrameData:frameData];
    if (!pixelBuffer) {
        NSLog(@"❌ 创建 CVPixelBuffer 失败");
        return;
    }
    
    // 创建 CMSampleBuffer
    CMSampleBufferRef sampleBuffer = NULL;
    CMVideoFormatDescriptionRef formatDesc = NULL;
    OSStatus status = CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, pixelBuffer, &formatDesc);
    
    if (status != noErr) {
        NSLog(@"❌ 创建 FormatDescription 失败: %d", (int)status);
        CVPixelBufferRelease(pixelBuffer);
        return;
    }
    
    CMTime presentationTime = CMTimeMake(frameData->pts * 1000000, 1000000);  // 转换为微秒
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
        NSLog(@"❌ 创建 SampleBuffer 失败: %d", (int)status);
        CFRelease(formatDesc);
        CVPixelBufferRelease(pixelBuffer);
        return;
    }
    
    // 显示
    if (sampleBuffer) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (self->_videoLayer.status == AVQueuedSampleBufferRenderingStatusFailed) {
                NSLog(@"❌ VideoLayer 渲染失败: %@", self->_videoLayer.error);
                [self->_videoLayer flush];
            }
            [self->_videoLayer enqueueSampleBuffer:sampleBuffer];
            CFRelease(sampleBuffer);
        });
    }
    
    if (formatDesc) {
        CFRelease(formatDesc);
    }
    
    CVPixelBufferRelease(pixelBuffer);
}

- (CVPixelBufferRef)createPixelBufferFromFrameData:(VideoFrameDataC *)frameData {
    // 创建 CVPixelBuffer（NV12 格式，适合硬件加速）
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
    
    // 复制 Y 平面
    uint8_t *yPlane = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
    size_t yStride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);
    uint8_t *ySrc = (uint8_t *)frameData->y_data;
    
    for (int i = 0; i < frameData->height; i++) {
        memcpy(yPlane + i * yStride, ySrc + i * frameData->y_linesize, frameData->width);
    }
    
    // 复制 UV 平面（NV12 格式，UV 交织）
    uint8_t *uvPlane = (uint8_t *)CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
    size_t uvStride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
    uint8_t *uSrc = (uint8_t *)frameData->u_data;
    uint8_t *vSrc = (uint8_t *)frameData->v_data;
    
    int uvHeight = frameData->height / 2;
    int uvWidth = frameData->width / 2;
    
    for (int i = 0; i < uvHeight; i++) {
        for (int j = 0; j < uvWidth; j++) {
            // U 和 V 交织
            uvPlane[i * uvStride + j * 2] = uSrc[i * frameData->u_linesize + j];      // U
            uvPlane[i * uvStride + j * 2 + 1] = vSrc[i * frameData->v_linesize + j];  // V
        }
    }
    
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    
    return pixelBuffer;
}

@end
