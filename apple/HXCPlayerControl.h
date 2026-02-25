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

// 播放器回调协议
@protocol HXCPlayerControlDelegate <NSObject>
@optional
- (void)playerDidChangeState:(HXCPlayerState)state;
- (void)playerDidUpdatePosition:(double)position duration:(double)duration;
- (void)playerDidEncounterError:(NSError *)error;
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
- (void)stop;                                          // 停止播放（等同于 close）
- (void)replay;                                        // 重新播放
- (void)seekToPosition:(double)position;               // 跳转到指定位置
- (void)close;                                         // 关闭播放器

@end

#endif // HXCPLAYER_CONTROL_H
