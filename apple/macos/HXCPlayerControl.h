/**
 * @file HXCPlayerControl.h
 * @brief macOS 播放器核心封装（使用系统音视频渲染）
 */

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@class HXCPlayerControl;

// 播放器状态
typedef NS_ENUM(NSInteger, HXCPlayerState) {
    HXCPlayerStateIdle = 0,
    HXCPlayerStateOpening,
    HXCPlayerStatePlaying,
    HXCPlayerStatePaused,
    HXCPlayerStateStopped,
    HXCPlayerStateError
};

// 视频显示模式（宽高比模式）
typedef NS_ENUM(NSInteger, HXCAspectRatioMode) {
    HXCAspectRatioModeFit = 0,   // 适应模式：等比缩放，保持完整画面，可能有黑边（默认）
    HXCAspectRatioModeFill = 1   // 填充模式：等比拉伸填充，无黑边，画面会被裁剪
};

// 播放器代理协议
@protocol HXCPlayerControlDelegate <NSObject>
@optional
- (void)player:(HXCPlayerControl *)player didChangeState:(HXCPlayerState)state;
- (void)player:(HXCPlayerControl *)player didEncounterError:(NSString *)error;
- (void)player:(HXCPlayerControl *)player didUpdatePosition:(double)position;
@end

/**
 * @brief macOS 播放器核心类
 * 底层使用 FFmpeg 解码（src/core），上层使用 macOS 原生渲染
 */
@interface HXCPlayerControl : NSObject

@property (nonatomic, weak) id<HXCPlayerControlDelegate> delegate;
@property (nonatomic, readonly) HXCPlayerState state;
@property (nonatomic, readonly) double duration;
@property (nonatomic, readonly) double position;
@property (nonatomic, assign) float volume;  // 0.0 ~ 1.0
@property (nonatomic, assign) double playbackRate;  // 0.5 ~ 2.0
@property (nonatomic, assign) double startPosition;  // 播放起始位置（秒），默认 0（从头开始）
@property (nonatomic, readonly, nullable) NSString *playerUrl;  // 当前播放的 URL
@property (nonatomic, assign) HXCAspectRatioMode aspectRatioMode;  // 视频显示模式，默认 Fit

// 视频显示层（需要添加到视图）
@property (nonatomic, readonly) AVSampleBufferDisplayLayer *videoLayer;

/**
 * 准备播放（打开文件但不自动播放）
 * @param url 媒体文件 URL（支持本地文件和网络 URL）
 * @return YES 成功，NO 失败
 */
- (BOOL)prepareToPlay:(NSString *)url;

/**
 * 播放控制
 */
- (void)play;      // 开始/继续播放
- (void)pause;     // 暂停
- (void)stop;      // 停止
- (void)replay;    // 重新播放（从 startPosition 或开头重新开始）

/**
 * 进度控制
 */
- (void)seekToPosition:(double)position;

/**
 * 获取视频尺寸
 */
- (CGSize)videoSize;

/**
 * 关闭播放器并释放资源
 */
- (void)close;

@end

NS_ASSUME_NONNULL_END
