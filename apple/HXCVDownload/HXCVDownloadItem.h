/**
 * @file HXCVDownloadItem.h
 * @brief 对外暴露的下载任务快照（不可变）
 *
 * 每次从持久化读取都会构造新的实例，表示某一时刻的任务数据；逻辑上的「同一任务」以 taskId 为准，
 * 而不是对象指针。若需刷新 UI，请用 taskId 在列表中查找对应行/模型并替换为新快照，或监听
 * HXCVDownloadManagerDelegate；不要对 HXCVDownloadItem 做跨回调的 KVO（每次回调可能是不同实例）。
 */

#import <Foundation/Foundation.h>
#import "HXCVDownloadTypes.h"

NS_ASSUME_NONNULL_BEGIN

@interface HXCVDownloadItem : NSObject <NSCopying>

/// 任务唯一标识；跨回调比较、字典键、列表关联均应以 taskId 为准。
@property (nonatomic, copy, readonly) NSString *taskId;
/// 发起该下载任务的用户标识；未指定时持久化为 @"default"
@property (nonatomic, copy, readonly) NSString *identifier;
@property (nonatomic, copy, readonly) NSString *urlString;
@property (nonatomic, copy, readonly, nullable) NSString *finalURLString;
@property (nonatomic, assign, readonly) HXCVDownloadType downloadType;
@property (nonatomic, assign, readonly) HXCVDownloadState state;
@property (nonatomic, assign, readonly) double progress;
@property (nonatomic, assign, readonly) int64_t bytesWritten;
@property (nonatomic, assign, readonly) int64_t totalBytes;
/// 相对于 Application Support 根目录的本地路径（单文件为文件；HLS 为目录下 master/local.m3u8 或 index）
@property (nonatomic, copy, readonly, nullable) NSString *localRelativePath;
@property (nonatomic, copy, readonly, nullable) NSString *errorMessage;
@property (nonatomic, strong, readonly) NSDate *createdAt;
@property (nonatomic, strong, readonly) NSDate *updatedAt;

- (instancetype)init NS_UNAVAILABLE;

@end

NS_ASSUME_NONNULL_END
