/**
 * @file HXCVDPersistenceController.h
 * @brief Core Data 栈（代码创建模型）
 */

#import <Foundation/Foundation.h>
#import <CoreData/CoreData.h>
#import "HXCVDownloadTypes.h"

@class HXCVDownloadItem;

NS_ASSUME_NONNULL_BEGIN

@interface HXCVDPersistenceController : NSObject

@property (nonatomic, strong, readonly) NSPersistentContainer *persistentContainer;

+ (instancetype)sharedController;

- (void)save:(NSError **)outError;

/// 新建任务记录；`identifier` 为空或仅空白时持久化为 @"default"
- (NSString *)insertTaskWithURLString:(NSString *)urlString
                         downloadType:(HXCVDownloadType)type
                           identifier:(nullable NSString *)identifier
                                error:(NSError **)outError;

/// 等价于 `identifier:nil`（即 default）
- (NSString *)insertTaskWithURLString:(NSString *)urlString
                         downloadType:(HXCVDownloadType)type
                                error:(NSError **)outError;

- (BOOL)updateTaskId:(NSString *)taskId
               state:(HXCVDownloadState)state
            progress:(double)progress
        bytesWritten:(int64_t)bytesWritten
          totalBytes:(int64_t)totalBytes
   localRelativePath:(nullable NSString *)localRelativePath
        finalURLString:(nullable NSString *)finalURLString
          errorMessage:(nullable NSString *)errorMessage
               error:(NSError **)outError;

- (BOOL)setResumeData:(nullable NSData *)resumeData forTaskId:(NSString *)taskId error:(NSError **)outError;
- (nullable NSData *)resumeDataForTaskId:(NSString *)taskId;

- (BOOL)deleteTaskId:(NSString *)taskId error:(NSError **)outError;

- (NSArray<HXCVDownloadItem *> *)fetchAllTasksSortedByUpdatedDesc;

/// `identifier` 为空或仅空白时，返回 identifier 为 nil 或 @"default" 的任务（兼容旧库无该字段）
- (NSArray<HXCVDownloadItem *> *)fetchAllTasksSortedByUpdatedDescForIdentifier:(nullable NSString *)identifier;

/// 按任务状态查询，默认按 updatedAt 降序（与 fetchAllTasksSortedByUpdatedDesc 一致）
- (NSArray<HXCVDownloadItem *> *)fetchTasksWithState:(HXCVDownloadState)state;

/// 同上，并按用户标识过滤；`identifier` 为空时语义同 `fetchAllTasksSortedByUpdatedDescForIdentifier:`
- (NSArray<HXCVDownloadItem *> *)fetchTasksWithState:(HXCVDownloadState)state
                                          identifier:(nullable NSString *)identifier;

- (nullable HXCVDownloadItem *)fetchTaskWithId:(NSString *)taskId;

@end

NS_ASSUME_NONNULL_END
