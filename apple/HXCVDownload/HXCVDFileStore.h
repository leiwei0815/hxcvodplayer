/**
 * @file HXCVDFileStore.h
 * @brief 下载文件根目录与任务子目录
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface HXCVDFileStore : NSObject

/// 当前下载根目录（默认 Application Support/HXCVDownload）
@property (nonatomic, copy, readonly) NSURL *rootDirectoryURL;

+ (instancetype)sharedStore;

/// 设置下载根目录（须为本地 file URL 目录；不存在则尝试创建）。更改后仅影响新任务；旧任务的 localRelativePath 仍相对当时根目录。
- (BOOL)setDownloadRootDirectoryURL:(NSURL *)directoryURL error:(NSError **)outError;

- (NSURL *)directoryURLForTaskId:(NSString *)taskId;
- (BOOL)createTaskDirectoryForTaskId:(NSString *)taskId error:(NSError **)outError;
- (BOOL)removeTaskDirectoryForTaskId:(NSString *)taskId error:(NSError **)outError;

/// 相对 root 的路径（用于持久化）
- (NSString *)relativePathFromRootForFileURL:(NSURL *)fileURL;

@end

NS_ASSUME_NONNULL_END
