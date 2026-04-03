/**
 * @file HXCVDownloadTypes.h
 * @brief HXCVD 视频下载模块类型定义
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// 下载类型：单文件直链 / HLS(m3u8)
typedef NS_ENUM(int16_t, HXCVDownloadType) {
    HXCVDownloadTypeProgressive = 0,
    HXCVDownloadTypeHLS = 1,
};

/// 任务状态
typedef NS_ENUM(int16_t, HXCVDownloadState) {
    HXCVDownloadStateWaiting = 0,
    HXCVDownloadStateRunning,
    HXCVDownloadStatePaused,
    HXCVDownloadStateFailed,
    HXCVDownloadStateCompleted,
    HXCVDownloadStateCancelled,
};

NS_ASSUME_NONNULL_END
