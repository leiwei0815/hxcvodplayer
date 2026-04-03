/**
 * @file HXCVDownloadItem+Internal.h
 * @brief 模块内部构造 HXCVDownloadItem，勿在业务层引用
 */

#import "HXCVDownloadItem.h"

NS_ASSUME_NONNULL_BEGIN

@interface HXCVDownloadItem (HXCVDInternal)

+ (instancetype)hxdvd_itemWithTaskId:(NSString *)taskId
                            identifier:(NSString *)identifier
                             urlString:(NSString *)urlString
                        finalURLString:(nullable NSString *)finalURLString
                          downloadType:(HXCVDownloadType)downloadType
                                 state:(HXCVDownloadState)state
                              progress:(double)progress
                          bytesWritten:(int64_t)bytesWritten
                            totalBytes:(int64_t)totalBytes
                     localRelativePath:(nullable NSString *)localRelativePath
                          errorMessage:(nullable NSString *)errorMessage
                             createdAt:(NSDate *)createdAt
                             updatedAt:(NSDate *)updatedAt;

@end

NS_ASSUME_NONNULL_END
