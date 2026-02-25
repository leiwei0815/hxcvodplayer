/**
 * @file HXCPlayerView.h
 * @brief 统一的播放器视图（支持 iOS 和 macOS）
 */

#ifndef HXCPLAYER_VIEW_H
#define HXCPLAYER_VIEW_H

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#define HXCView UIView
#else
#import <AppKit/AppKit.h>
#define HXCView NSView
#endif

/**
 * @brief 播放器视图
 * 
 * 内部使用 AVSampleBufferDisplayLayer 作为渲染层
 * 外部只需要像普通 View 一样添加到视图层级中即可
 */
@interface HXCPlayerView : HXCView

/**
 * 获取内部的视频渲染层（通常不需要直接访问）
 */
@property (nonatomic, strong, readonly) AVSampleBufferDisplayLayer *videoLayer;

@end

#endif // HXCPLAYER_VIEW_H
