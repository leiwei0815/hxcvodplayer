/**
 * @file YXPlayerControl.mm
 * @brief iOS 播放器核心实现
 */

// ✅ 只包含 C 接口桥接层（不直接包含 FFmpeg 头文件）
#include "yx_player_core_c_bridge.h"

// 现在可以安全地包含 iOS 框架（没有 FFmpeg 冲突）
#import "YXPlayerControl.h"
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

@interface YXPlayerControl () {
    PlayerCoreWrapper *_wrapper;
    
    // 音频渲染（AudioQueue）
    AudioQueueRef _audioQueue;
    AudioQueueBufferRef _audioBuffers[kNumberOfBuffers];
    AudioStreamBasicDescription _audioFormat;
    BOOL _audioQueueRunning;
    
    // 视频渲染定时器
    CADisplayLink *_displayLink;
    
    // 状态
    YXPlayerState _state;
    double _duration;
    double _position;
    NSString *_playerUrl;  // 当前播放的 URL
    YXAspectRatioMode _aspectRatioMode;  // 视频显示模式
}

@property (nonatomic, strong) AVSampleBufferDisplayLayer *videoLayer;
@property (nonatomic, strong) dispatch_queue_t renderQueue;

@end

@implementation YXPlayerControl

- (instancetype)init {
    self = [super init];
    if (self) {
        _wrapper = new PlayerCoreWrapper();
        _state = YXPlayerStateIdle;
        _volume = 1.0;
        _playbackRate = 1.0;
        _startPosition = 0.0;  // 默认从头开始
        _aspectRatioMode = YXAspectRatioModeFit;  // 默认 Fit 模式
        _audioQueueRunning = NO;
        
        // 创建视频显示层
        _videoLayer = [[AVSampleBufferDisplayLayer alloc] init];
        _videoLayer.videoGravity = AVLayerVideoGravityResizeAspect;  // 默认 Fit 模式
        
        // 创建渲染队列
        _renderQueue = dispatch_queue_create("com.yxplayer.ios.render", DISPATCH_QUEUE_SERIAL);
        
        // C 接口暂不支持回调，iOS 层使用轮询方式检测状态变化
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
    _state = YXPlayerStateOpening;
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
        _state = YXPlayerStateError;
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
    
    // 初始化音频队列
    if (sampleRate > 0) {
        [self setupAudioQueue:sampleRate channels:channels];
    }
    
    // 启动视频渲染定时器
    [self startDisplayLink];
    
    // 根据 autoPlay 决定状态
    if (autoPlay) {
        _state = YXPlayerStatePlaying;
    } else {
        _state = YXPlayerStatePaused;
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

- (BOOL)openURL:(NSString *)url {
    // 向后兼容：openURL 自动播放
    return [self _openURLInternal:url autoPlay:YES];
}


- (void)play {
    // 如果还没有打开文件，无法播放
    if (!_playerUrl && _state == YXPlayerStateIdle) {
        NSLog(@"警告: 请先调用 prepareToPlay: 打开文件");
        return;
    }
    
    player_core_play(_wrapper->handle());
    
    if (_audioQueue && !_audioQueueRunning) {
        AudioQueueStart(_audioQueue, NULL);
        _audioQueueRunning = YES;
    }
    
    // 更新状态
    _state = YXPlayerStatePlaying;
    
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
    _state = YXPlayerStatePaused;
    
    // 通知代理
    if ([_delegate respondsToSelector:@selector(player:didChangeState:)]) {
        [_delegate player:self didChangeState:_state];
    }
}

- (void)stop {
    [self close];
    
    // 更新状态
    _state = YXPlayerStateStopped;
    
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
    
    _state = YXPlayerStateIdle;
    _duration = 0;
    _position = 0;
    _playerUrl = nil;  // 清空 URL
    
    // 通知代理
    if ([_delegate respondsToSelector:@selector(player:didChangeState:)]) {
        [_delegate player:self didChangeState:_state];
    }
}

#pragma mark - Properties

- (YXPlayerState)state {
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

- (void)setAspectRatioMode:(YXAspectRatioMode)aspectRatioMode {
    _aspectRatioMode = aspectRatioMode;
    
    // 更新底层 C 桥接层的设置
    if (_wrapper) {
        AspectRatioModeC mode = (aspectRatioMode == YXAspectRatioModeFit) ? ASPECT_RATIO_FIT : ASPECT_RATIO_FILL;
        player_core_set_aspect_ratio_mode(_wrapper->handle(), mode);
    }
    
    // 更新 AVSampleBufferDisplayLayer 的显示模式
    dispatch_async(dispatch_get_main_queue(), ^{
        if (aspectRatioMode == YXAspectRatioModeFit) {
            self->_videoLayer.videoGravity = AVLayerVideoGravityResizeAspect;  // 保持宽高比，可能有黑边
        } else {
            self->_videoLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;  // 填充，裁剪画面
        }
    });
}

- (YXAspectRatioMode)aspectRatioMode {
    return _aspectRatioMode;
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
    
    // 创建音频队列
    OSStatus status = AudioQueueNewOutput(&_audioFormat,
                                         audioQueueCallback,
                                         (__bridge void *)self,
                                         NULL,
                                         kCFRunLoopCommonModes,
                                         0,
                                         &_audioQueue);
    
    if (status != noErr) {
        NSLog(@"AudioQueue 创建失败: %d", (int)status);
        return;
    }
    
    // 创建缓冲区
    UInt32 bufferSize = 1024 * channels * sizeof(SInt16);
    for (int i = 0; i < kNumberOfBuffers; i++) {
        AudioQueueAllocateBuffer(_audioQueue, bufferSize, &_audioBuffers[i]);
        audioQueueCallback((__bridge void *)self, _audioQueue, _audioBuffers[i]);
    }
    
    // 设置音量
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

// 音频队列回调（C 函数）
static void audioQueueCallback(void *userData, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    YXPlayerControl *player = (__bridge YXPlayerControl *)userData;
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

#pragma mark - Video Rendering

- (void)startDisplayLink {
    if (_displayLink) {
        return;
    }
    
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(renderVideoFrame)];
    _displayLink.preferredFramesPerSecond = 60;  // 60 FPS
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)stopDisplayLink {
    if (_displayLink) {
        [_displayLink invalidate];
        _displayLink = nil;
    }
}

- (void)renderVideoFrame {
    if (!_wrapper || !_wrapper->handle()) {
        return;
    }
    
    // 获取视频帧数据
    VideoFrameDataC frame_data;
    if (player_core_get_video_frame(_wrapper->handle(), &frame_data) != 0) {
        return;  // 没有可用帧
    }
    
    double currentPTS = frame_data.pts;
    double masterClock = player_core_get_position(_wrapper->handle());
    
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
        return;
    }
    
    // 创建 CMSampleBuffer
    CMSampleBufferRef sampleBuffer = NULL;
    CMVideoFormatDescriptionRef formatDesc = NULL;
    CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault, pixelBuffer, &formatDesc);
    
    CMTime presentationTime = CMTimeMake(frameData->pts * 1000000, 1000000);  // 转换为微秒
    CMSampleTimingInfo timing = {
        .duration = kCMTimeInvalid,
        .presentationTimeStamp = presentationTime,
        .decodeTimeStamp = kCMTimeInvalid
    };
    
    CMSampleBufferCreateReadyWithImageBuffer(kCFAllocatorDefault,
                                            pixelBuffer,
                                            formatDesc,
                                            &timing,
                                            &sampleBuffer);
    
    // 显示
    if (sampleBuffer) {
        [_videoLayer enqueueSampleBuffer:sampleBuffer];
        CFRelease(sampleBuffer);
    }
    
    if (formatDesc) {
        CFRelease(formatDesc);
    }
    
    CVPixelBufferRelease(pixelBuffer);
}

- (CVPixelBufferRef)createPixelBufferFromFrameData:(VideoFrameDataC *)frameData {
    // 创建 CVPixelBuffer（NV12 格式，适合 iOS 硬件加速）
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

#pragma mark - Callbacks

/*
- (void)handleStateChanged:(yxplayer::PlayerState)cppState {
    // 转换状态
    switch (cppState) {
        case yxplayer::PlayerState::Idle:
            _state = YXPlayerStateIdle;
            break;
        case yxplayer::PlayerState::Opening:
            _state = YXPlayerStateOpening;
            break;
        case yxplayer::PlayerState::Playing:
            _state = YXPlayerStatePlaying;
            break;
        case yxplayer::PlayerState::Paused:
            _state = YXPlayerStatePaused;
            break;
        case yxplayer::PlayerState::Stopped:
            _state = YXPlayerStateStopped;
            break;
        case yxplayer::PlayerState::Error:
            _state = YXPlayerStateError;
            break;
    }
    
    // 通知代理
    if ([_delegate respondsToSelector:@selector(player:didChangeState:)]) {
        [_delegate player:self didChangeState:_state];
    }
}

- (void)handleError:(NSString *)error {
    if ([_delegate respondsToSelector:@selector(player:didEncounterError:)]) {
        [_delegate player:self didEncounterError:error];
    }
}
*/

@end
