#import "HXCVDownloadManager.h"
#import "HXCVDPersistenceController.h"
#import "HXCVDFileStore.h"
#import "HXCVDownloadItem+Internal.h"
#import "HXCVDHLSDownloadEngine.h"
#import <TargetConditionals.h>

static NSString *gHXCVDPrepareBackgroundIdentifier = nil;
static BOOL gHXCVDPrepareBackgroundEnabled = NO;
static BOOL gHXCVDPrepareConfigurationPending = NO;
static BOOL gHXCVDSharedManagerInitialized = NO;

static NSString *hxcvd_headerValueCaseInsensitive(NSDictionary<NSString *, NSString *> *headers, NSString *name) {
    if (headers.count == 0 || name.length == 0) {
        return nil;
    }
    for (NSString *k in headers) {
        if ([k.lowercaseString isEqualToString:name.lowercaseString]) {
            return headers[k];
        }
    }
    return nil;
}

/// Content-Range: bytes 0-99/2000 或 bytes */2000 → 返回 2000
static int64_t hxcvd_contentRangeTotalBytes(NSString *contentRange) {
    if (contentRange.length == 0) {
        return -1;
    }
    NSRange slash = [contentRange rangeOfString:@"/" options:NSBackwardsSearch];
    if (slash.location == NSNotFound) {
        return -1;
    }
    NSString *tail = [[contentRange substringFromIndex:NSMaxRange(slash)] stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (tail.length == 0 || [tail isEqualToString:@"*"]) {
        return -1;
    }
    return (int64_t)[tail longLongValue];
}

static BOOL hxcvd_httpBodyMayBeCompressed(NSHTTPURLResponse *http) {
    NSString *enc = hxcvd_headerValueCaseInsensitive(http.allHeaderFields, @"Content-Encoding");
    if (enc.length == 0) {
        return NO;
    }
    NSString *l = enc.lowercaseString;
    return [l containsString:@"gzip"] || [l containsString:@"deflate"] || [l containsString:@"br"] || [l containsString:@"compress"];
}

/// 根据 URL 判断 HLS：路径以 `.m3u8` 结尾，或完整字符串中含 `.m3u8`（兼容 `index.m3u8?token=` 等）。
static HXCVDownloadType hxcvd_downloadTypeForURL(NSURL *url) {
    if (!url || url.absoluteString.length == 0) {
        return HXCVDownloadTypeProgressive;
    }
    NSString *pathLower = url.path.lowercaseString;
    if (pathLower.length > 0 && [pathLower hasSuffix:@".m3u8"]) {
        return HXCVDownloadTypeHLS;
    }
    NSString *absLower = url.absoluteString.lowercaseString;
    if ([absLower rangeOfString:@".m3u8"].location != NSNotFound) {
        return HXCVDownloadTypeHLS;
    }
    return HXCVDownloadTypeProgressive;
}

@interface HXCVDownloadManager () <NSURLSessionDelegate, NSURLSessionDownloadDelegate, NSURLSessionTaskDelegate>
@property (nonatomic, strong) HXCVDPersistenceController *persistence;
@property (nonatomic, strong) HXCVDFileStore *fileStore;
@property (nonatomic, strong) HXCVDHLSDownloadEngine *hlsEngine;
@property (nonatomic, strong) NSURLSession *downloadSession;
@property (nonatomic, strong) NSOperationQueue *downloadSessionDelegateQueue;
@property (nonatomic, copy, nullable) void (^backgroundSessionEventsCompletionHandler)(void);
@property (nonatomic, strong) dispatch_queue_t isolationQueue;

/// taskId -> 单文件保存文件名（在任务目录下）
@property (nonatomic, strong) NSMutableDictionary<NSString *, NSString *> *progressiveFileNames;
/// 单文件下载当前 NSURLSessionDownloadTask（用于暂停/取消，避免依赖异步 getTasks）
@property (nonatomic, strong) NSMutableDictionary<NSString *, NSURLSessionDownloadTask *> *progressiveTasks;
/// 正在跑的 HLS taskId 集合（用于取消）
@property (nonatomic, strong) NSMutableSet<NSString *> *activeHLSTaskIds;
/// 等待空闲槽位的 taskId（FIFO，与 state=Waiting 且未在跑的任务对应）
@property (nonatomic, strong) NSMutableArray<NSString *> *pendingTaskIds;
@end

@implementation HXCVDownloadManager

+ (void)prepareLaunchConfigurationWithBackgroundSessionIdentifier:(NSString *)identifier backgroundDownloadsEnabled:(BOOL)enabled {
    @synchronized([self class]) {
        if (gHXCVDSharedManagerInitialized) {
            NSLog(@"[HXCVD] prepareLaunchConfiguration 被忽略：+sharedManager 已初始化");
            return;
        }
        gHXCVDPrepareBackgroundIdentifier = [identifier copy];
        gHXCVDPrepareBackgroundEnabled = enabled;
        gHXCVDPrepareConfigurationPending = YES;
    }
}

+ (instancetype)sharedManager {
    static HXCVDownloadManager *s;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        s = [[HXCVDownloadManager alloc] init];
        gHXCVDSharedManagerInitialized = YES;
    });
    return s;
}

+ (void)notifyBackgroundURLSessionEventsForIdentifier:(NSString *)identifier completionHandler:(void (^)(void))completionHandler {
    HXCVDownloadManager *mgr = [self sharedManager];
    NSString *mine = mgr.backgroundSessionIdentifier;
    if (!identifier.length || !mine.length || ![mine isEqualToString:identifier]) {
        if (completionHandler) {
            completionHandler();
        }
        return;
    }
    mgr.backgroundSessionEventsCompletionHandler = [completionHandler copy];
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _persistence = [HXCVDPersistenceController sharedController];
        _fileStore = [HXCVDFileStore sharedStore];
        _hlsEngine = [[HXCVDHLSDownloadEngine alloc] initWithFileStore:_fileStore];
        _progressiveFileNames = [NSMutableDictionary dictionary];
        _progressiveTasks = [NSMutableDictionary dictionary];
        _activeHLSTaskIds = [NSMutableSet set];
        _pendingTaskIds = [NSMutableArray array];
        _maxConcurrentDownloads = 3;
        _backgroundDownloadsEnabled = NO;
        _isolationQueue = dispatch_queue_create("com.hxcvd.manager", DISPATCH_QUEUE_SERIAL);

        _downloadSessionDelegateQueue = [[NSOperationQueue alloc] init];
        _downloadSessionDelegateQueue.maxConcurrentOperationCount = 1;

        @synchronized([HXCVDownloadManager class]) {
            if (gHXCVDPrepareConfigurationPending) {
                if (gHXCVDPrepareBackgroundIdentifier.length) {
                    _backgroundSessionIdentifier = [gHXCVDPrepareBackgroundIdentifier copy];
                    _backgroundDownloadsEnabled = gHXCVDPrepareBackgroundEnabled;
                }
                gHXCVDPrepareBackgroundIdentifier = nil;
                gHXCVDPrepareBackgroundEnabled = NO;
                gHXCVDPrepareConfigurationPending = NO;
            }
        }

        [self hxdvd_createDownloadSession];

        __weak typeof(self) wself = self;
        dispatch_async(dispatch_get_main_queue(), ^{
            [wself hxdvd_rebuildPendingFromPersistedWaitingTasks];
            [wself hxdvd_reconcileStaleRunningTasksAfterLaunch];
        });
    }
    return self;
}

- (void)hxdvd_createDownloadSession {
    NSURLSessionConfiguration *cfg = nil;
    if (_backgroundDownloadsEnabled && self.backgroundSessionIdentifier.length > 0) {
        cfg = [NSURLSessionConfiguration backgroundSessionConfigurationWithIdentifier:self.backgroundSessionIdentifier];
        cfg.discretionary = NO;
        if (@available(iOS 11.0, macOS 10.13, *)) {
            cfg.waitsForConnectivity = YES;
        }
    } else {
        cfg = [NSURLSessionConfiguration defaultSessionConfiguration];
    }
    cfg.HTTPMaximumConnectionsPerHost = 4;
    cfg.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
    self.downloadSession = [NSURLSession sessionWithConfiguration:cfg delegate:self delegateQueue:self.downloadSessionDelegateQueue];
}

- (void)hxdvd_replaceDownloadSession {
    NSURLSession *old = self.downloadSession;
    self.downloadSession = nil;
    if (old) {
        [old invalidateAndCancel];
    }
    [self hxdvd_createDownloadSession];
}

- (void)setBackgroundDownloadsEnabled:(BOOL)backgroundDownloadsEnabled {
    [self hxdvd_performOnMain:^{
        [self hxdvd_setBackgroundDownloadsEnabledOnMain:backgroundDownloadsEnabled];
    }];
}

- (void)hxdvd_setBackgroundDownloadsEnabledOnMain:(BOOL)on {
    if (_backgroundDownloadsEnabled == on) {
        return;
    }
    if (on && !self.backgroundSessionIdentifier.length) {
        NSLog(@"[HXCVD] 开启后台下载前请先设置有效的 backgroundSessionIdentifier");
        return;
    }
    if ([self hxdvd_activeSlotCount] > 0) {
        NSLog(@"[HXCVD] 存在活动下载任务时无法切换后台下载模式");
        return;
    }
    _backgroundDownloadsEnabled = on;
    [self hxdvd_replaceDownloadSession];
}

- (void)setBackgroundSessionIdentifier:(NSString *)backgroundSessionIdentifier {
    NSString *copy = backgroundSessionIdentifier.length ? [backgroundSessionIdentifier copy] : nil;
    [self hxdvd_performOnMain:^{
        BOOL same = (copy.length == 0 && _backgroundSessionIdentifier.length == 0) || [copy isEqualToString:_backgroundSessionIdentifier];
        if (same) {
            return;
        }
        if (_backgroundDownloadsEnabled && [self hxdvd_activeSlotCount] > 0) {
            NSLog(@"[HXCVD] 存在活动下载任务时无法修改 backgroundSessionIdentifier");
            return;
        }
        _backgroundSessionIdentifier = copy;
        if (_backgroundDownloadsEnabled) {
            [self hxdvd_replaceDownloadSession];
        }
    }];
}

- (void)dealloc {
    [_downloadSession invalidateAndCancel];
}

- (NSURL *)downloadRootDirectoryURL {
    return self.fileStore.rootDirectoryURL;
}

- (BOOL)setDownloadRootDirectoryURL:(NSURL *)directoryURL error:(NSError **)outError {
    return [self.fileStore setDownloadRootDirectoryURL:directoryURL error:outError];
}

/// progressOnly==YES：只回调 didUpdateProgressForItem（进度节流语义），并在此处 return，不会走 didChangeState/didFail/didComplete。
/// 状态变化与完成/失败必须用 progressOnly==NO 再调本方法（例如 HLS 的 completion 块里）。
- (void)hxdvd_notifyDelegateAfterUpdateTaskId:(NSString *)taskId previousState:(HXCVDownloadState)previousState progressOnly:(BOOL)progressOnly {
    if (!taskId.length) {
        return;
    }
    HXCVDownloadItem *item = [self.persistence fetchTaskWithId:taskId];
    if (!item) {
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        id<HXCVDownloadManagerDelegate> d = self.delegate;
        if (progressOnly) {
            if ([d respondsToSelector:@selector(downloadManager:didUpdateProgressForItem:)]) {
                [d downloadManager:self didUpdateProgressForItem:item];
            }
            return;
        }
        if ([d respondsToSelector:@selector(downloadManager:didChangeStateForItem:previousState:)]) {
            [d downloadManager:self didChangeStateForItem:item previousState:previousState];
        }
        if (item.state == HXCVDownloadStateFailed) {
            if ([d respondsToSelector:@selector(downloadManager:didFailDownloadForItem:error:)]) {
                NSString *msg = item.errorMessage;
                NSError *err = nil;
                if (msg.length) {
                    err = [NSError errorWithDomain:@"HXCVDownload" code:-1 userInfo:@{ NSLocalizedDescriptionKey : [msg copy] }];
                } else {
                    err = [NSError errorWithDomain:@"HXCVDownload" code:-1 userInfo:@{ NSLocalizedDescriptionKey : @"下载失败" }];
                }
                [d downloadManager:self didFailDownloadForItem:item error:err];
            }
        } else if (item.state == HXCVDownloadStateCompleted) {
            if ([d respondsToSelector:@selector(downloadManager:didCompleteDownloadForItem:)]) {
                [d downloadManager:self didCompleteDownloadForItem:item];
            }
        }
    });
}

- (void)setMaxConcurrentDownloads:(NSInteger)maxConcurrentDownloads {
    NSInteger m = maxConcurrentDownloads;
    if (m < 1) {
        m = 1;
    }
    if (_maxConcurrentDownloads == m) {
        return;
    }
    _maxConcurrentDownloads = m;
    __weak typeof(self) wself = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        [wself hxdvd_drainPendingQueueIfNeeded];
    });
}

- (void)hxdvd_performOnMain:(void (^)(void))block {
    if (!block) {
        return;
    }
    if ([NSThread isMainThread]) {
        block();
    } else {
        dispatch_sync(dispatch_get_main_queue(), block);
    }
}

- (NSInteger)hxdvd_activeSlotCount {
    return (NSInteger)(self.progressiveTasks.count + self.activeHLSTaskIds.count);
}

- (BOOL)hxdvd_canStartAnotherDownload {
    return [self hxdvd_activeSlotCount] < self.maxConcurrentDownloads;
}

- (void)hxdvd_appendPendingTaskId:(NSString *)tid {
    if (!tid.length || [self.pendingTaskIds containsObject:tid]) {
        return;
    }
    [self.pendingTaskIds addObject:tid];
}

- (void)hxdvd_removePendingTaskId:(NSString *)tid {
    if (!tid.length) {
        return;
    }
    [self.pendingTaskIds removeObject:tid];
}

/// 强杀/崩溃后 DB 可能仍为 Running。将上次正在执行的任务一律改为 Paused；直链若在 session 中则先取 resumeData 再取消，不自动续传、不入队。
- (void)hxdvd_reconcileStaleRunningTasksAfterLaunch {
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self hxdvd_reconcileStaleRunningTasksAfterLaunch];
        });
        return;
    }
    __weak typeof(self) wself = self;
    [self.downloadSession getTasksWithCompletionHandler:^(NSArray<NSURLSessionDataTask *> *dataTasks, NSArray<NSURLSessionUploadTask *> *uploadTasks, NSArray<NSURLSessionDownloadTask *> *downloadTasks) {
        dispatch_async(dispatch_get_main_queue(), ^{
            __strong typeof(wself) sself = wself;
            if (!sself) {
                return;
            }
            NSArray<HXCVDownloadItem *> *running = [sself tasksWithState:HXCVDownloadStateRunning];
            NSMutableSet<NSString *> *runningTids = [NSMutableSet set];
            for (HXCVDownloadItem *it in running) {
                [runningTids addObject:it.taskId];
            }
            NSMutableSet<NSString *> *progressiveCancelledFromSession = [NSMutableSet set];
            for (NSURLSessionDownloadTask *dt in downloadTasks) {
                if (![dt isKindOfClass:[NSURLSessionDownloadTask class]]) {
                    continue;
                }
                NSString *tid = dt.taskDescription;
                if (!tid.length || ![runningTids containsObject:tid]) {
                    continue;
                }
                HXCVDownloadItem *match = [sself.persistence fetchTaskWithId:tid];
                if (!match || match.downloadType != HXCVDownloadTypeProgressive) {
                    continue;
                }
                [sself.progressiveTasks removeObjectForKey:tid];
                [progressiveCancelledFromSession addObject:tid];
                __weak typeof(sself) wInner = sself;
                [dt cancelByProducingResumeData:^(NSData *_Nullable resumeData) {
                    dispatch_async(dispatch_get_main_queue(), ^{
                        __strong typeof(wInner) sInner = wInner;
                        if (!sInner) {
                            return;
                        }
                        NSError *re = nil;
                        if (resumeData.length) {
                            [sInner.persistence setResumeData:resumeData forTaskId:tid error:&re];
                        }
                        HXCVDownloadItem *fresh = [sInner.persistence fetchTaskWithId:tid];
                        if (fresh && fresh.state == HXCVDownloadStateRunning) {
                            [sInner hxdvd_persistPausedFromItem:fresh];
                        }
                        [sInner hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:HXCVDownloadStateRunning progressOnly:NO];
                    });
                }];
            }
            for (HXCVDownloadItem *it in running) {
                if (it.downloadType == HXCVDownloadTypeHLS) {
                    [sself hxdvd_persistPausedFromItem:it];
                    [sself hxdvd_notifyDelegateAfterUpdateTaskId:it.taskId previousState:HXCVDownloadStateRunning progressOnly:NO];
                    continue;
                }
                if (![progressiveCancelledFromSession containsObject:it.taskId]) {
                    [sself hxdvd_persistPausedFromItem:it];
                    [sself hxdvd_notifyDelegateAfterUpdateTaskId:it.taskId previousState:HXCVDownloadStateRunning progressOnly:NO];
                }
            }
            [sself hxdvd_drainPendingQueueIfNeeded];
        });
    }];
}

- (void)hxdvd_persistPausedFromItem:(HXCVDownloadItem *)item {
    NSError *e = nil;
    [self.persistence updateTaskId:item.taskId
                             state:HXCVDownloadStatePaused
                          progress:item.progress
                      bytesWritten:item.bytesWritten
                        totalBytes:item.totalBytes
                 localRelativePath:item.localRelativePath
                    finalURLString:item.finalURLString
                      errorMessage:nil
                             error:&e];
}

/// 进程重启后内存队列为空：把仍为 Waiting 且未在跑的任务按 createdAt 补进 pending
- (void)hxdvd_rebuildPendingFromPersistedWaitingTasks {
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self hxdvd_rebuildPendingFromPersistedWaitingTasks];
        });
        return;
    }
    NSArray<HXCVDownloadItem *> *waiting = [self tasksWithState:HXCVDownloadStateWaiting];
    NSArray *sorted = [waiting sortedArrayUsingComparator:^NSComparisonResult(HXCVDownloadItem *a, HXCVDownloadItem *b) {
        return [a.createdAt compare:b.createdAt];
    }];
    for (HXCVDownloadItem *it in sorted) {
        if (self.progressiveTasks[it.taskId]) {
            continue;
        }
        if ([self.activeHLSTaskIds containsObject:it.taskId]) {
            continue;
        }
        if ([self.pendingTaskIds containsObject:it.taskId]) {
            continue;
        }
        [self.pendingTaskIds addObject:it.taskId];
    }
}

- (void)hxdvd_drainPendingQueueIfNeeded {
    if (![NSThread isMainThread]) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [self hxdvd_drainPendingQueueIfNeeded];
        });
        return;
    }
    while ([self hxdvd_canStartAnotherDownload] && self.pendingTaskIds.count > 0) {
        NSString *next = self.pendingTaskIds.firstObject;
        [self.pendingTaskIds removeObjectAtIndex:0];
        HXCVDownloadItem *item = [self.persistence fetchTaskWithId:next];
        if (!item) {
            continue;
        }
        if (item.state == HXCVDownloadStateCancelled || item.state == HXCVDownloadStateCompleted || item.state == HXCVDownloadStateFailed) {
            continue;
        }
        if (item.state == HXCVDownloadStatePaused) {
            continue;
        }
        if (item.state != HXCVDownloadStateWaiting) {
            continue;
        }
        [self hxdvd_beginDownloadForItem:item];
    }
}

/// 完成时用于与临时文件比对的「期望总字节」；未知时返回 -1（不校验）。
- (int64_t)hxdvd_expectedTotalBytesForFinishedDownloadTask:(NSURLSessionDownloadTask *)task {
    int64_t fromTask = task.countOfBytesExpectedToReceive;
    if (fromTask > 0) {
        return fromTask;
    }
    NSURLResponse *resp = task.response;
    if (![resp isKindOfClass:[NSHTTPURLResponse class]]) {
        return -1;
    }
    NSHTTPURLResponse *http = (NSHTTPURLResponse *)resp;
    if (hxcvd_httpBodyMayBeCompressed(http)) {
        return -1;
    }
    NSString *cr = hxcvd_headerValueCaseInsensitive(http.allHeaderFields, @"Content-Range");
    int64_t fromCR = hxcvd_contentRangeTotalBytes(cr);
    if (fromCR > 0) {
        return fromCR;
    }
    long long el = http.expectedContentLength;
    if (el > 0) {
        return (int64_t)el;
    }
    return -1;
}

/// 主线程；占槽并启动（state -> Running）
- (void)hxdvd_beginDownloadForItem:(HXCVDownloadItem *)item {
    NSString *tid = item.taskId;
    if (item.downloadType == HXCVDownloadTypeHLS) {
        NSURL *u = [NSURL URLWithString:item.urlString];
        if (!u) {
            return;
        }
        [self hxdvd_startHLSEngineForTaskId:tid entryURL:u];
        return;
    }
    NSURL *u = [NSURL URLWithString:item.urlString];
    if (!u) {
        return;
    }
    NSString *name = u.lastPathComponent.length ? u.lastPathComponent : @"download.bin";
    if ([name containsString:@"?"]) {
        name = [[name componentsSeparatedByString:@"?"] firstObject];
    }
    self.progressiveFileNames[tid] = name;
    NSURL *destDir = [self.fileStore directoryURLForTaskId:tid];
    NSURL *dest = [destDir URLByAppendingPathComponent:name];
    NSData *resume = [self.persistence resumeDataForTaskId:tid];
    NSURLSessionDownloadTask *task = nil;
    if (resume.length) {
        task = [self.downloadSession downloadTaskWithResumeData:resume];
    }
    if (!task) {
        [self.persistence setResumeData:nil forTaskId:tid error:nil];
        [[NSFileManager defaultManager] removeItemAtURL:dest error:nil];
        task = [self.downloadSession downloadTaskWithURL:u];
    }
    if (!task) {
        return;
    }
    task.taskDescription = tid;
    self.progressiveTasks[tid] = task;
    [task resume];
    HXCVDownloadState prevState = item.state;
    NSError *e = nil;
    [self.persistence updateTaskId:tid
                             state:HXCVDownloadStateRunning
                          progress:item.progress
                      bytesWritten:item.bytesWritten
                        totalBytes:item.totalBytes
                 localRelativePath:item.localRelativePath
                    finalURLString:item.finalURLString
                      errorMessage:nil
                             error:&e];
    [self hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:prevState progressOnly:NO];
}

- (void)hxdvd_startHLSEngineForTaskId:(NSString *)tid entryURL:(NSURL *)url {
    [self.activeHLSTaskIds addObject:tid];
    HXCVDownloadItem *hlsBefore = [self.persistence fetchTaskWithId:tid];
    HXCVDownloadState hlsPrevState = hlsBefore ? hlsBefore.state : HXCVDownloadStateWaiting;
    NSError *pe = nil;
    [self.persistence updateTaskId:tid
                             state:HXCVDownloadStateRunning
                          progress:0
                      bytesWritten:0
                        totalBytes:-1
                 localRelativePath:nil
                    finalURLString:nil
                      errorMessage:nil
                             error:&pe];
    [self hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:hlsPrevState progressOnly:NO];

    __weak typeof(self) wself = self;
    [self.hlsEngine startTaskId:tid entryURL:url session:self.downloadSession progress:^(int64_t received, int64_t total) {
        __strong typeof(wself) sself = wself;
        if (!sself) {
            return;
        }
        double p = 0;
        if (total > 0) {
            p = (double)received / (double)total;
        }
        NSError *ue = nil;
        [sself.persistence updateTaskId:tid
                                  state:HXCVDownloadStateRunning
                               progress:p
                           bytesWritten:received
                             totalBytes:total > 0 ? total : -1
                      localRelativePath:nil
                         finalURLString:nil
                           errorMessage:nil
                                  error:&ue];
        // 仅进度；完成见下方 completion 中的 notify（progressOnly:NO）
        [sself hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:HXCVDownloadStateRunning progressOnly:YES];
    } completion:^(NSError *_Nullable error, NSString *_Nullable localRelativePath) {
        __strong typeof(wself) sself = wself;
        if (!sself) {
            return;
        }
        [sself.activeHLSTaskIds removeObject:tid];
        NSError *ue = nil;
        if (error) {
            [sself.persistence updateTaskId:tid
                                      state:HXCVDownloadStateFailed
                                   progress:0
                               bytesWritten:0
                                 totalBytes:-1
                          localRelativePath:nil
                             finalURLString:nil
                               errorMessage:error.localizedDescription
                                      error:&ue];
        } else {
            [sself.persistence updateTaskId:tid
                                      state:HXCVDownloadStateCompleted
                                   progress:1.0
                               bytesWritten:0
                                 totalBytes:-1
                          localRelativePath:localRelativePath
                             finalURLString:nil
                               errorMessage:nil
                                      error:&ue];
        }
        [sself hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:HXCVDownloadStateRunning progressOnly:NO];
        dispatch_async(dispatch_get_main_queue(), ^{
            [sself hxdvd_drainPendingQueueIfNeeded];
        });
    } cancelled:^BOOL {
        __strong typeof(wself) sself = wself;
        if (!sself) {
            return YES;
        }
        return ![sself.activeHLSTaskIds containsObject:tid];
    }];
}

- (nullable NSString *)enqueueDownloadWithURL:(NSURL *)url error:(NSError **)outError {
    return [self enqueueDownloadWithURL:url identifier:nil error:outError];
}

- (nullable NSString *)enqueueDownloadWithURL:(NSURL *)url identifier:(NSString *)identifier error:(NSError **)outError {
    HXCVDownloadType t = hxcvd_downloadTypeForURL(url);
    return [self enqueueDownloadWithURL:url type:t identifier:identifier error:outError];
}

- (nullable NSString *)enqueueDownloadWithURL:(NSURL *)url type:(HXCVDownloadType)type error:(NSError **)outError {
    return [self enqueueDownloadWithURL:url type:type identifier:nil error:outError];
}

- (nullable NSString *)enqueueDownloadWithURL:(NSURL *)url type:(HXCVDownloadType)type identifier:(NSString *)identifier error:(NSError **)outError {
    if (!url.absoluteString.length) {
        if (outError) {
            *outError = [NSError errorWithDomain:@"HXCVD" code:-1 userInfo:@{ NSLocalizedDescriptionKey : @"URL 无效" }];
        }
        return nil;
    }
    NSString *tid = [self.persistence insertTaskWithURLString:url.absoluteString downloadType:type identifier:identifier error:outError];
    if (!tid) {
        return nil;
    }
    NSError *e = nil;
    if (![self.fileStore createTaskDirectoryForTaskId:tid error:&e]) {
        [self.persistence deleteTaskId:tid error:nil];
        if (outError) {
            *outError = e;
        }
        return nil;
    }

    __weak typeof(self) wself = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        __strong typeof(wself) sself = wself;
        if (!sself) {
            return;
        }
        HXCVDownloadItem *item = [sself.persistence fetchTaskWithId:tid];
        if (!item) {
            return;
        }
        if ([sself hxdvd_canStartAnotherDownload]) {
            [sself hxdvd_beginDownloadForItem:item];
        } else {
            [sself hxdvd_appendPendingTaskId:tid];
            [sself hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:item.state progressOnly:NO];
        }
    });

    return tid;
}

- (NSArray<HXCVDownloadItem *> *)allTasks {
    return [self.persistence fetchAllTasksSortedByUpdatedDesc];
}

- (NSArray<HXCVDownloadItem *> *)allTasksForIdentifier:(NSString *)identifier {
    return [self.persistence fetchAllTasksSortedByUpdatedDescForIdentifier:identifier];
}

- (NSArray<HXCVDownloadItem *> *)tasksWithState:(HXCVDownloadState)state {
    return [self.persistence fetchTasksWithState:state];
}

- (NSArray<HXCVDownloadItem *> *)tasksWithState:(HXCVDownloadState)state identifier:(NSString *)identifier {
    return [self.persistence fetchTasksWithState:state identifier:identifier];
}

- (nullable NSURL *)playableFileURLForCompletedItem:(HXCVDownloadItem *)item {
    if (!item || item.state != HXCVDownloadStateCompleted) {
        return nil;
    }
    NSString *rel = item.localRelativePath;
    if (rel.length == 0) {
        return nil;
    }
    NSString *rootPath = self.fileStore.rootDirectoryURL.path;
    NSMutableString *acc = [rootPath mutableCopy];
    for (NSString *c in [rel pathComponents]) {
        if (c.length == 0 || [c isEqualToString:@"/"]) {
            continue;
        }
        [acc appendFormat:@"/%@", c];
    }
    NSString *fullPath = [acc copy];
    if (![[NSFileManager defaultManager] fileExistsAtPath:fullPath]) {
        return nil;
    }
    return [NSURL fileURLWithPath:fullPath isDirectory:NO];
}

- (BOOL)pauseTaskId:(NSString *)taskId error:(NSError **)outError {
    __block BOOL ok = NO;
    __block NSError *innerErr = nil;
    [self hxdvd_performOnMain:^{
        ok = [self hxdvd_pauseTaskIdOnMain:taskId error:&innerErr];
    }];
    if (outError && innerErr) {
        *outError = innerErr;
    }
    return ok;
}

- (BOOL)hxdvd_pauseTaskIdOnMain:(NSString *)taskId error:(NSError **)outError {
    HXCVDownloadItem *item = [self.persistence fetchTaskWithId:taskId];
    if (!item) {
        return NO;
    }
    HXCVDownloadState prevState = item.state;
    if (item.state == HXCVDownloadStateWaiting) {
        [self hxdvd_removePendingTaskId:taskId];
        NSError *e = nil;
        [self.persistence updateTaskId:taskId
                                 state:HXCVDownloadStatePaused
                              progress:item.progress
                          bytesWritten:item.bytesWritten
                            totalBytes:item.totalBytes
                     localRelativePath:item.localRelativePath
                        finalURLString:item.finalURLString
                          errorMessage:nil
                                 error:&e];
        [self hxdvd_notifyDelegateAfterUpdateTaskId:taskId previousState:prevState progressOnly:NO];
        [self hxdvd_drainPendingQueueIfNeeded];
        if (outError) {
            *outError = e;
        }
        return e == nil;
    }
    if (item.downloadType == HXCVDownloadTypeHLS) {
        [self.activeHLSTaskIds removeObject:taskId];
        NSError *e = nil;
        [self.persistence updateTaskId:taskId
                                 state:HXCVDownloadStatePaused
                              progress:item.progress
                          bytesWritten:item.bytesWritten
                            totalBytes:item.totalBytes
                     localRelativePath:item.localRelativePath
                        finalURLString:item.finalURLString
                          errorMessage:nil
                                 error:&e];
        [self hxdvd_notifyDelegateAfterUpdateTaskId:taskId previousState:prevState progressOnly:NO];
        [self hxdvd_drainPendingQueueIfNeeded];
        if (outError) {
            *outError = e;
        }
        return e == nil;
    }

    NSURLSessionDownloadTask *dt = self.progressiveTasks[taskId];
    if (!dt) {
        return NO;
    }
    __weak typeof(self) wself = self;
    [dt cancelByProducingResumeData:^(NSData *_Nullable resumeData) {
        __strong typeof(wself) sself = wself;
        if (!sself) {
            return;
        }
        NSError *se = nil;
        [sself.persistence setResumeData:resumeData forTaskId:taskId error:&se];
        [sself.persistence updateTaskId:taskId
                                 state:HXCVDownloadStatePaused
                              progress:item.progress
                          bytesWritten:item.bytesWritten
                            totalBytes:item.totalBytes
                     localRelativePath:item.localRelativePath
                        finalURLString:item.finalURLString
                          errorMessage:nil
                                 error:&se];
        [sself.progressiveTasks removeObjectForKey:taskId];
        [sself hxdvd_notifyDelegateAfterUpdateTaskId:taskId previousState:prevState progressOnly:NO];
        dispatch_async(dispatch_get_main_queue(), ^{
            [sself hxdvd_drainPendingQueueIfNeeded];
        });
    }];
    return YES;
}

- (BOOL)resumeTaskId:(NSString *)taskId error:(NSError **)outError {
    __block BOOL ok = NO;
    __block NSError *innerErr = nil;
    [self hxdvd_performOnMain:^{
        ok = [self hxdvd_resumeTaskIdOnMain:taskId error:&innerErr];
    }];
    if (outError && innerErr) {
        *outError = innerErr;
    }
    return ok;
}

- (BOOL)hxdvd_resumeTaskIdOnMain:(NSString *)taskId error:(NSError **)outError {
    HXCVDownloadItem *item = [self.persistence fetchTaskWithId:taskId];
    if (!item) {
        return NO;
    }
    if (item.state == HXCVDownloadStateWaiting) {
        if ([self.pendingTaskIds containsObject:taskId]) {
            return YES;
        }
        [self hxdvd_appendPendingTaskId:taskId];
        [self hxdvd_drainPendingQueueIfNeeded];
        return YES;
    }
    if (item.state != HXCVDownloadStatePaused) {
        return NO;
    }
    if ([self hxdvd_canStartAnotherDownload]) {
        [self hxdvd_beginDownloadForItem:item];
        return YES;
    }
    HXCVDownloadState resumePrev = item.state;
    NSError *e = nil;
    [self.persistence updateTaskId:taskId
                             state:HXCVDownloadStateWaiting
                          progress:item.progress
                      bytesWritten:item.bytesWritten
                        totalBytes:item.totalBytes
                 localRelativePath:item.localRelativePath
                    finalURLString:item.finalURLString
                      errorMessage:nil
                             error:&e];
    [self hxdvd_appendPendingTaskId:taskId];
    [self hxdvd_notifyDelegateAfterUpdateTaskId:taskId previousState:resumePrev progressOnly:NO];
    if (outError) {
        *outError = e;
    }
    return e == nil;
}

- (BOOL)cancelTaskId:(NSString *)taskId error:(NSError **)outError {
    __block BOOL ok = NO;
    __block NSError *innerErr = nil;
    [self hxdvd_performOnMain:^{
        ok = [self hxdvd_cancelTaskIdOnMain:taskId error:&innerErr];
    }];
    if (outError && innerErr) {
        *outError = innerErr;
    }
    return ok;
}

- (BOOL)hxdvd_cancelTaskIdOnMain:(NSString *)taskId error:(NSError **)outError {
    HXCVDownloadItem *itemBeforeCancel = [self.persistence fetchTaskWithId:taskId];
    HXCVDownloadState prevCancelState = itemBeforeCancel ? itemBeforeCancel.state : HXCVDownloadStateWaiting;
    [self hxdvd_removePendingTaskId:taskId];
    [self.activeHLSTaskIds removeObject:taskId];
    NSURLSessionDownloadTask *dt = self.progressiveTasks[taskId];
    if (dt) {
        [dt cancel];
        [self.progressiveTasks removeObjectForKey:taskId];
    }
    NSError *e = nil;
    [self.persistence updateTaskId:taskId
                             state:HXCVDownloadStateCancelled
                          progress:0
                      bytesWritten:0
                        totalBytes:-1
                 localRelativePath:nil
                    finalURLString:nil
                      errorMessage:nil
                             error:&e];
    [self hxdvd_notifyDelegateAfterUpdateTaskId:taskId previousState:prevCancelState progressOnly:NO];
    [self hxdvd_drainPendingQueueIfNeeded];
    if (outError) {
        *outError = e;
    }
    return e == nil;
}

- (BOOL)deleteTaskId:(NSString *)taskId removeFiles:(BOOL)removeFiles error:(NSError **)outError {
    __block BOOL ok = NO;
    __block NSError *innerErr = nil;
    [self hxdvd_performOnMain:^{
        ok = [self hxdvd_deleteTaskIdOnMain:taskId removeFiles:removeFiles error:&innerErr];
    }];
    if (outError && innerErr) {
        *outError = innerErr;
    }
    return ok;
}

- (BOOL)hxdvd_deleteTaskIdOnMain:(NSString *)taskId removeFiles:(BOOL)removeFiles error:(NSError **)outError {
    [self hxdvd_removePendingTaskId:taskId];
    [self.activeHLSTaskIds removeObject:taskId];
    [self.progressiveFileNames removeObjectForKey:taskId];
    NSURLSessionDownloadTask *dt = self.progressiveTasks[taskId];
    if (dt) {
        [dt cancel];
        [self.progressiveTasks removeObjectForKey:taskId];
    }
    if (removeFiles) {
        NSError *fe = nil;
        if (![self.fileStore removeTaskDirectoryForTaskId:taskId error:&fe]) {
            if (outError) {
                *outError = fe;
            }
        }
    }
    BOOL delOk = [self.persistence deleteTaskId:taskId error:outError];
    [self hxdvd_drainPendingQueueIfNeeded];
    return delOk;
}

#pragma mark - NSURLSessionDelegate

- (void)URLSessionDidFinishEventsForBackgroundURLSession:(NSURLSession *)session {
    void (^h)(void) = self.backgroundSessionEventsCompletionHandler;
    self.backgroundSessionEventsCompletionHandler = nil;
    if (h) {
        dispatch_async(dispatch_get_main_queue(), ^{
            h();
        });
    }
}

#pragma mark - NSURLSessionDownloadDelegate

- (void)URLSession:(NSURLSession *)session downloadTask:(NSURLSessionDownloadTask *)downloadTask didFinishDownloadingToURL:(NSURL *)location {
    NSString *tid = downloadTask.taskDescription;
    if (!tid.length) {
        return;
    }
    NSString *name = self.progressiveFileNames[tid] ?: @"download.bin";
    NSURL *destDir = [self.fileStore directoryURLForTaskId:tid];
    NSURL *dest = [destDir URLByAppendingPathComponent:name];
    [self.progressiveTasks removeObjectForKey:tid];

    HXCVDownloadItem *preFinish = [self.persistence fetchTaskWithId:tid];
    HXCVDownloadState preFinishState = preFinish ? preFinish.state : HXCVDownloadStateRunning;

    NSFileManager *fm = [NSFileManager defaultManager];
    NSError *attrErr = nil;
    NSDictionary<NSFileAttributeKey, id> *tmpAttrs = [fm attributesOfItemAtPath:location.path error:&attrErr];
    if (!tmpAttrs) {
        NSString *msg = attrErr.localizedDescription ?: @"无法读取临时下载文件";
        [self.persistence setResumeData:nil forTaskId:tid error:nil];
        [self.persistence updateTaskId:tid
                                 state:HXCVDownloadStateFailed
                              progress:0
                          bytesWritten:0
                            totalBytes:-1
                     localRelativePath:nil
                        finalURLString:nil
                          errorMessage:msg
                                 error:nil];
        [self hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:preFinishState progressOnly:NO];
        __weak typeof(self) wself = self;
        dispatch_async(dispatch_get_main_queue(), ^{
            [wself hxdvd_drainPendingQueueIfNeeded];
        });
        return;
    }
    int64_t fileSize = [tmpAttrs[NSFileSize] longLongValue];

    BOOL skipByteCheck = NO;
    if ([downloadTask.response isKindOfClass:[NSHTTPURLResponse class]]) {
        if (hxcvd_httpBodyMayBeCompressed((NSHTTPURLResponse *)downloadTask.response)) {
            skipByteCheck = YES;
        }
    }
    int64_t expected = skipByteCheck ? -1 : [self hxdvd_expectedTotalBytesForFinishedDownloadTask:downloadTask];
    if (expected > 0 && fileSize != expected) {
        NSString *msg = [NSString stringWithFormat:@"下载大小与声明不一致（已下 %lld 字节，期望 %lld），已丢弃以免文件损坏。可重试下载。", (long long)fileSize, (long long)expected];
        [fm removeItemAtURL:location error:nil];
        [self.persistence setResumeData:nil forTaskId:tid error:nil];
        [self.persistence updateTaskId:tid
                                 state:HXCVDownloadStateFailed
                              progress:0
                          bytesWritten:0
                            totalBytes:-1
                     localRelativePath:nil
                        finalURLString:nil
                          errorMessage:msg
                                 error:nil];
        [self hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:preFinishState progressOnly:NO];
        __weak typeof(self) wself = self;
        dispatch_async(dispatch_get_main_queue(), ^{
            [wself hxdvd_drainPendingQueueIfNeeded];
        });
        return;
    }

    NSError *err = nil;
    if ([fm fileExistsAtPath:dest.path]) {
        if (![fm removeItemAtURL:dest error:&err]) {
            NSString *msg = [NSString stringWithFormat:@"无法覆盖目标文件：%@", err.localizedDescription];
            [fm removeItemAtURL:location error:nil];
            [self.persistence updateTaskId:tid
                                     state:HXCVDownloadStateFailed
                                  progress:0
                              bytesWritten:0
                                totalBytes:-1
                         localRelativePath:nil
                            finalURLString:nil
                              errorMessage:msg
                                     error:nil];
            [self hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:preFinishState progressOnly:NO];
            __weak typeof(self) wself = self;
            dispatch_async(dispatch_get_main_queue(), ^{
                [wself hxdvd_drainPendingQueueIfNeeded];
            });
            return;
        }
    }
    if (![fm moveItemAtURL:location toURL:dest error:&err]) {
        [self.persistence updateTaskId:tid
                                 state:HXCVDownloadStateFailed
                              progress:0
                          bytesWritten:0
                            totalBytes:-1
                     localRelativePath:nil
                        finalURLString:nil
                          errorMessage:err.localizedDescription
                                 error:nil];
        [self hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:preFinishState progressOnly:NO];
        __weak typeof(self) wself = self;
        dispatch_async(dispatch_get_main_queue(), ^{
            [wself hxdvd_drainPendingQueueIfNeeded];
        });
        return;
    }

    [self.persistence setResumeData:nil forTaskId:tid error:nil];

    NSString *rel = [self.fileStore relativePathFromRootForFileURL:dest];
    NSURL *finalU = downloadTask.response.URL ?: downloadTask.originalRequest.URL;
    NSError *ue = nil;
    [self.persistence updateTaskId:tid
                             state:HXCVDownloadStateCompleted
                          progress:1.0
                      bytesWritten:0
                        totalBytes:0
                 localRelativePath:rel
                    finalURLString:finalU.absoluteString
                      errorMessage:nil
                             error:&ue];
    [self hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:preFinishState progressOnly:NO];
    __weak typeof(self) wself = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        [wself hxdvd_drainPendingQueueIfNeeded];
    });
}

- (void)URLSession:(NSURLSession *)session downloadTask:(NSURLSessionDownloadTask *)downloadTask
                                       didWriteData:(int64_t)bytesWritten
                                  totalBytesWritten:(int64_t)totalBytesWritten
                          totalBytesExpectedToWrite:(int64_t)totalBytesExpectedToWrite {
    NSString *tid = downloadTask.taskDescription;
    if (!tid.length) {
        return;
    }
    double p = 0;
    if (totalBytesExpectedToWrite > 0) {
        p = (double)totalBytesWritten / (double)totalBytesExpectedToWrite;
    }
    NSError *e = nil;
    [self.persistence updateTaskId:tid
                             state:HXCVDownloadStateRunning
                          progress:p
                      bytesWritten:totalBytesWritten
                        totalBytes:totalBytesExpectedToWrite > 0 ? totalBytesExpectedToWrite : -1
                 localRelativePath:nil
                    finalURLString:nil
                      errorMessage:nil
                             error:&e];
    [self hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:HXCVDownloadStateRunning progressOnly:YES];
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task didCompleteWithError:(NSError *)error {
    NSString *tid = task.taskDescription;
    if (!tid.length) {
        return;
    }
    if ([task isKindOfClass:[NSURLSessionDownloadTask class]]) {
        [self.progressiveTasks removeObjectForKey:tid];
    }
    if (!error) {
        return;
    }
    if ([error.domain isEqualToString:NSURLErrorDomain] && error.code == NSURLErrorCancelled) {
        return;
    }
    HXCVDownloadItem *preTaskErr = [self.persistence fetchTaskWithId:tid];
    HXCVDownloadState preTaskErrState = preTaskErr ? preTaskErr.state : HXCVDownloadStateRunning;
    NSError *ue = nil;
    [self.persistence updateTaskId:tid
                             state:HXCVDownloadStateFailed
                          progress:0
                      bytesWritten:0
                        totalBytes:-1
                 localRelativePath:nil
                    finalURLString:nil
                      errorMessage:error.localizedDescription
                             error:&ue];
    [self hxdvd_notifyDelegateAfterUpdateTaskId:tid previousState:preTaskErrState progressOnly:NO];
    __weak typeof(self) wself = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        [wself hxdvd_drainPendingQueueIfNeeded];
    });
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task willPerformHTTPRedirection:(NSHTTPURLResponse *)response newRequest:(NSURLRequest *)newRequest completionHandler:(void (^)(NSURLRequest * _Nullable))completionHandler {
    completionHandler(newRequest);
}

@end
