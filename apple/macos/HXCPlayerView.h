/**
 * @file HXCPlayerView.h
 * @brief macOS 原生视频显示视图
 */

#import <Cocoa/Cocoa.h>

@class HXCPlayerControl;

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief macOS 原生视频显示视图
 * 使用 AVSampleBufferDisplayLayer 显示视频
 */
@interface HXCPlayerView : NSView

/**
 * 设置播放器实例
 * @param player HXCPlayerControl 实例
 */
- (void)setPlayer:(HXCPlayerControl *)player;

@end

NS_ASSUME_NONNULL_END
