#import "HXCVDFileStore.h"

@interface HXCVDFileStore ()
@property (nonatomic, copy, readwrite) NSURL *rootDirectoryURL;
@property (nonatomic, strong) NSLock *rootLock;
@end

@implementation HXCVDFileStore

+ (instancetype)sharedStore {
    static HXCVDFileStore *s;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        s = [[HXCVDFileStore alloc] init];
    });
    return s;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _rootLock = [[NSLock alloc] init];
        NSFileManager *fm = [NSFileManager defaultManager];
        NSURL *base = nil;

#if TARGET_OS_IPHONE
        // iOS: 默认放在应用沙盒的 Caches/HXCVDownload 目录下，便于系统在空间紧张时自动回收。
        base = [fm URLsForDirectory:NSCachesDirectory inDomains:NSUserDomainMask].firstObject;
        if (!base) {
            base = [NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES];
        }
#else
        // macOS: 仍使用 Application Support/HXCVDownload 作为默认持久化目录。
        base = [fm URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
        if (!base) {
            base = [NSURL fileURLWithPath:NSTemporaryDirectory() isDirectory:YES];
        }
#endif

        NSURL *def = [base URLByAppendingPathComponent:@"HXCVDownload" isDirectory:YES];
        _rootDirectoryURL = def;
        NSError *err = nil;
        if (![fm createDirectoryAtURL:_rootDirectoryURL withIntermediateDirectories:YES attributes:nil error:&err]) {
            NSLog(@"[HXCVD] FileStore 创建根目录失败: %@", err);
        }
    }
    return self;
}

- (NSURL *)rootDirectoryURL {
    [_rootLock lock];
    NSURL *u = [_rootDirectoryURL copy];
    [_rootLock unlock];
    return u;
}

- (BOOL)setDownloadRootDirectoryURL:(NSURL *)directoryURL error:(NSError **)outError {
    if (!directoryURL || !directoryURL.fileURL) {
        if (outError) {
            *outError = [NSError errorWithDomain:@"HXCVD" code:-10
                                        userInfo:@{ NSLocalizedDescriptionKey : @"须使用本地 file:// 目录 URL" }];
        }
        return NO;
    }
    NSURL *standard = [directoryURL URLByStandardizingPath];
    NSFileManager *fm = [NSFileManager defaultManager];
    BOOL isDir = NO;
    if ([fm fileExistsAtPath:standard.path isDirectory:&isDir]) {
        if (!isDir) {
            if (outError) {
                *outError = [NSError errorWithDomain:@"HXCVD" code:-11
                                            userInfo:@{ NSLocalizedDescriptionKey : @"路径已存在且不是目录" }];
            }
            return NO;
        }
    } else {
        NSError *mk = nil;
        if (![fm createDirectoryAtURL:standard withIntermediateDirectories:YES attributes:nil error:&mk]) {
            if (outError) {
                *outError = mk;
            }
            return NO;
        }
    }
    [_rootLock lock];
    _rootDirectoryURL = standard;
    [_rootLock unlock];
    return YES;
}

- (NSURL *)directoryURLForTaskId:(NSString *)taskId {
    NSURL *root = self.rootDirectoryURL;
    return [root URLByAppendingPathComponent:taskId isDirectory:YES];
}

- (BOOL)createTaskDirectoryForTaskId:(NSString *)taskId error:(NSError **)outError {
    NSURL *u = [self directoryURLForTaskId:taskId];
    return [[NSFileManager defaultManager] createDirectoryAtURL:u
                                    withIntermediateDirectories:YES
                                                     attributes:nil
                                                          error:outError];
}

- (BOOL)removeTaskDirectoryForTaskId:(NSString *)taskId error:(NSError **)outError {
    NSURL *u = [self directoryURLForTaskId:taskId];
    if (![[NSFileManager defaultManager] fileExistsAtPath:u.path]) {
        return YES;
    }
    return [[NSFileManager defaultManager] removeItemAtURL:u error:outError];
}

- (NSString *)relativePathFromRootForFileURL:(NSURL *)fileURL {
    NSString *root = self.rootDirectoryURL.path;
    NSString *path = fileURL.path;
    if ([path hasPrefix:root]) {
        NSString *sub = [path substringFromIndex:root.length];
        if ([sub hasPrefix:@"/"]) {
            sub = [sub substringFromIndex:1];
        }
        return sub;
    }
    return [[fileURL lastPathComponent] copy];
}

@end
