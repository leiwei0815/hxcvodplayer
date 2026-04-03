/**
 * @file HXCVDownloadWindowController.h
 * @brief macOS 下载管理窗口（HXCVD）
 */

#import <Cocoa/Cocoa.h>

NS_ASSUME_NONNULL_BEGIN

@interface HXCVDownloadWindowController : NSWindowController

/// 显示下载管理窗口（居中）
- (void)showDownloadPanel;

@end

NS_ASSUME_NONNULL_END
