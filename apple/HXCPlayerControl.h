/**
 * @file HXCPlayerControl.h
 * @brief Apple 平台统一播放器控制类（支持 iOS 和 macOS）
 */

#ifndef HXCPLAYER_CONTROL_H
#define HXCPLAYER_CONTROL_H

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

// 导入播放器视图
#import "HXCPlayerView.h"
@class HXCPlayerControl;
// 播放器状态
typedef NS_ENUM(NSInteger, HXCPlayerState) {
    HXCPlayerStateIdle = 0,      // 空闲
    HXCPlayerStateOpening,       // 正在打开
    HXCPlayerStateReady,         // 准备就绪
    HXCPlayerStatePlaying,       // 播放中
    HXCPlayerStatePaused,        // 暂停
    HXCPlayerStateStopped,       // 停止
    HXCPlayerStateError          // 错误
};

// 视频显示模式
typedef NS_ENUM(NSInteger, HXCAspectRatioMode) {
    HXCAspectRatioModeFit = 0,   // 适应（保持宽高比，黑边）
    HXCAspectRatioModeFill       // 填充（裁剪）
};

// ⚠️ 播放器错误码定义（对应 C 层的 PlayerErrorCodeC）
typedef NS_ENUM(NSInteger, HXCPlayerErrorCode) {
    // 自定义错误码 (1-999)
    HXCPlayerErrorNone = 0,                         // 无错误
    HXCPlayerErrorInvalidURL = 1,                   // 无效的 URL
    HXCPlayerErrorOpenInputFailed = 2,              // 打开输入失败
    HXCPlayerErrorFindStreamInfoFailed = 3,         // 查找流信息失败
    HXCPlayerErrorNoVideoStream = 4,                // 没有视频流
    HXCPlayerErrorNoAudioStream = 5,                // 没有音频流
    HXCPlayerErrorCodecNotFound = 6,                // 找不到解码器
    HXCPlayerErrorCodecOpenFailed = 7,              // 打开解码器失败
    HXCPlayerErrorAllocContextFailed = 8,           // 分配上下文失败
    HXCPlayerErrorSDLInitFailed = 9,                // SDL 初始化失败
    HXCPlayerErrorAudioDeviceOpenFailed = 10,       // 音频设备打开失败
    HXCPlayerErrorSeekFailed = 11,                  // Seek 操作失败
    HXCPlayerErrorReadFrameFailed = 12,             // 读取帧失败
    HXCPlayerErrorDecodeFailed = 13,                // 解码失败
    HXCPlayerErrorOutOfMemory = 14,                 // 内存不足
    HXCPlayerErrorUnknown = 999,                    // 未知错误
    
    // FFmpeg 错误码范围 (负数)
    // 例如：AVERROR_EOF, AVERROR(ENOMEM), AVERROR(EINVAL) 等
    // 可以通过 NSError.code 获取具体的 FFmpeg 错误码
};

// 播放器回调协议
@protocol HXCPlayerControlDelegate <NSObject>
@optional
// 状态变化通知
- (void)player:(HXCPlayerControl *)player didChangeState:(HXCPlayerState)state;

// 错误通知
- (void)player:(HXCPlayerControl *)player didFailWithError:(NSError *)error;

// 播放进度更新（真实播放位置）
- (void)player:(HXCPlayerControl *)player didUpdatePosition:(double)position;

// 缓冲进度更新（解码位置）
- (void)player:(HXCPlayerControl *)player didUpdateBufferProgress:(double)position;

// 播放完成通知
- (void)playerDidFinishPlaying:(HXCPlayerControl *)player;

// 网络加载状态通知（isLoading: YES=加载中，NO=加载完成）
- (void)player:(HXCPlayerControl *)player didChangeLoadingState:(BOOL)isLoading;
@end

/**
 * @brief Apple 平台统一播放器控制类
 * 
 * 支持 iOS 和 macOS 平台，使用系统原生的音视频渲染：
 * - 视频: AVSampleBufferDisplayLayer
 * - 音频: AudioQueue
 * - 变速: SoundTouch
 */
@interface HXCPlayerControl : NSObject

// 属性
@property (nonatomic, weak) id<HXCPlayerControlDelegate> delegate;
@property (nonatomic, assign) double volume;           // 音量 (0.0-1.0)
@property (nonatomic, assign) double playbackRate;     // 播放速度 (0.5-2.0)
@property (nonatomic, assign) double startPosition;    // 起始播放位置（秒）
@property (nonatomic, assign) HXCAspectRatioMode aspectRatioMode;  // 视频显示模式
@property (nonatomic, readonly) HXCPlayerState state;
@property (nonatomic, readonly) double duration;
@property (nonatomic, readonly) double position;
@property (nonatomic, strong, readonly) HXCPlayerView *videoView;  // 视频视图（自动管理布局）

// 播放控制
- (BOOL)openURL:(NSString *)url;                      // 打开 URL（不自动播放）
- (BOOL)prepareToPlay:(NSString *)url;                // 准备播放（等同于 openURL，不自动播放）
- (void)play;                                          // 开始播放
- (void)pause;                                         // 暂停
- (void)resume;                                        // 恢复播放
- (void)stop;                                          // 停止播放并释放资源
- (void)replay;                                        // 重新播放
- (void)seekToPosition:(double)position;               // 跳转到指定位置

@end

#endif // HXCPLAYER_CONTROL_H
