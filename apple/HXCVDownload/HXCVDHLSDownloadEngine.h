#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@class HXCVDFileStore;

/// HLS：拉取 m3u8、分片落盘并生成本地 local.m3u8（不含 AES 解密）。
/// 分片文件名为 seg_00000.ts … 与 playlist 序号一致；再次启动任务时会跳过已存在且非空的分片，从首个缺失处继续（适合 VOD；直播滑动列表若与上次序号不一致则可能需整任务重下）。
@interface HXCVDHLSDownloadEngine : NSObject

- (instancetype)initWithFileStore:(HXCVDFileStore *)fileStore;

/// progress: 已写入字节；total 未知时为 -1
- (void)startTaskId:(NSString *)taskId
            entryURL:(NSURL *)url
              session:(NSURLSession *)session
             progress:(void (^)(int64_t received, int64_t total))progress
           completion:(void (^)(NSError * _Nullable error, NSString * _Nullable localRelativePath))completion
            cancelled:(BOOL (^)(void))isCancelled;

@end

NS_ASSUME_NONNULL_END
