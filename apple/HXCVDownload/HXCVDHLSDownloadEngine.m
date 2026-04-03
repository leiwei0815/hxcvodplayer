#import "HXCVDHLSDownloadEngine.h"
#import "HXCVDFileStore.h"
#import "HXCVDPlaylistParser.h"

@interface HXCVDHLSDownloadEngine ()
@property (nonatomic, strong) HXCVDFileStore *fileStore;
@end

@implementation HXCVDHLSDownloadEngine

- (instancetype)initWithFileStore:(HXCVDFileStore *)fileStore {
    self = [super init];
    if (self) {
        _fileStore = fileStore;
    }
    return self;
}

- (void)hxdvd_fetchTextFromURL:(NSURL *)url
                       session:(NSURLSession *)session
                    completion:(void (^)(NSString * _Nullable text, NSURL * _Nullable finalURL, NSError * _Nullable err))completion {
    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
    req.HTTPMethod = @"GET";
    NSURLSessionDataTask *task = [session dataTaskWithRequest:req completionHandler:^(NSData * _Nullable data, NSURLResponse * _Nullable response, NSError * _Nullable error) {
        if (error) {
            completion(nil, nil, error);
            return;
        }
        if (!data.length) {
            completion(nil, nil, [NSError errorWithDomain:@"HXCVD" code:-200 userInfo:@{ NSLocalizedDescriptionKey : @"空 m3u8 响应" }]);
            return;
        }
        NSString *txt = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
        if (!txt) {
            txt = [[NSString alloc] initWithData:data encoding:NSISOLatin1StringEncoding];
        }
        NSURL *fu = response.URL ?: url;
        completion(txt, fu, nil);
    }];
    [task resume];
}

- (void)hxdvd_downloadDataFromURL:(NSURL *)url
                            session:(NSURLSession *)session
                         completion:(void (^)(NSData * _Nullable data, NSError * _Nullable err))completion {
    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
    NSURLSessionDataTask *task = [session dataTaskWithRequest:req completionHandler:^(NSData * _Nullable data, NSURLResponse * _Nullable response, NSError * _Nullable error) {
        completion(data, error);
    }];
    [task resume];
}

- (void)hxdvd_writeData:(NSData *)data toURL:(NSURL *)fileURL error:(NSError **)outError {
    [[NSFileManager defaultManager] createDirectoryAtURL:[fileURL URLByDeletingLastPathComponent]
                             withIntermediateDirectories:YES
                                              attributes:nil
                                                   error:nil];
    [data writeToURL:fileURL options:NSDataWritingAtomic error:outError];
}

- (void)startTaskId:(NSString *)taskId
            entryURL:(NSURL *)url
              session:(NSURLSession *)session
             progress:(void (^)(int64_t, int64_t))progress
           completion:(void (^)(NSError * _Nullable, NSString * _Nullable))completion
            cancelled:(BOOL (^)(void))isCancelled {
    NSURL *taskDir = [self.fileStore directoryURLForTaskId:taskId];
    NSError *dirErr = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtURL:taskDir withIntermediateDirectories:YES attributes:nil error:&dirErr]) {
        completion(dirErr, nil);
        return;
    }

    __weak typeof(self) wself = self;
    [self hxdvd_fetchTextFromURL:url session:session completion:^(NSString * _Nullable text, NSURL * _Nullable finalURL, NSError * _Nullable err) {
        if (err) {
            completion(err, nil);
            return;
        }
        if (isCancelled && isCancelled()) {
            completion([NSError errorWithDomain:@"HXCVD" code:-199 userInfo:@{ NSLocalizedDescriptionKey : @"已取消" }], nil);
            return;
        }
        NSURL *baseForPlaylist = finalURL ?: url;
        if ([HXCVDPlaylistParser isMasterPlaylistContent:text]) {
            NSURL *variant = [HXCVDPlaylistParser firstVariantPlaylistURLFromMasterContent:text baseURL:baseForPlaylist];
            if (!variant) {
                completion([NSError errorWithDomain:@"HXCVD" code:-201 userInfo:@{ NSLocalizedDescriptionKey : @"无法解析 master 中的 variant" }], nil);
                return;
            }
            [wself hxdvd_fetchTextFromURL:variant session:session completion:^(NSString * _Nullable mediaText, NSURL * _Nullable mediaFinal, NSError * _Nullable err2) {
                if (err2) {
                    completion(err2, nil);
                    return;
                }
                NSURL *mediaBase = mediaFinal ?: variant;
                [wself hxdvd_processMediaPlaylistText:mediaText
                                            playlistBaseURL:mediaBase
                                                   taskDir:taskDir
                                                   taskId:taskId
                                                  session:session
                                                 progress:progress
                                               completion:completion
                                                cancelled:isCancelled];
            }];
        } else {
            [wself hxdvd_processMediaPlaylistText:text
                                  playlistBaseURL:baseForPlaylist
                                          taskDir:taskDir
                                           taskId:taskId
                                          session:session
                                         progress:progress
                                       completion:completion
                                        cancelled:isCancelled];
        }
    }];
}

- (void)hxdvd_processMediaPlaylistText:(NSString *)mediaText
                       playlistBaseURL:(NSURL *)playlistBaseURL
                               taskDir:(NSURL *)taskDir
                                taskId:(NSString *)taskId
                               session:(NSURLSession *)session
                              progress:(void (^)(int64_t, int64_t))progress
                            completion:(void (^)(NSError * _Nullable, NSString * _Nullable))completion
                             cancelled:(BOOL (^)(void))isCancelled {
    __strong HXCVDHLSDownloadEngine *strongSelf = self;
    BOOL hasEnc = NO;
    NSArray<NSDictionary *> *entries = [HXCVDPlaylistParser segmentEntriesFromMediaPlaylistContent:mediaText
                                                                                           baseURL:playlistBaseURL
                                                                                     hasEncryption:&hasEnc];
    if (hasEnc) {
        completion([NSError errorWithDomain:@"HXCVD" code:-202 userInfo:@{ NSLocalizedDescriptionKey : @"HLS AES-128 加密流需额外实现密钥下载，当前版本未支持" }], nil);
        return;
    }
    if (entries.count == 0) {
        completion([NSError errorWithDomain:@"HXCVD" code:-203 userInfo:@{ NSLocalizedDescriptionKey : @"未解析到分片" }], nil);
        return;
    }

    __block int64_t written = 0;
    __block NSError *chainErr = nil;
    dispatch_queue_t q = dispatch_queue_create("com.hxcvd.hls", DISPATCH_QUEUE_SERIAL);
    __block NSInteger idx = 0;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Warc-retain-cycles"
    __block void (^downloadNext)(void) = ^{
        if (chainErr) {
            completion(chainErr, nil);
            return;
        }
        if (isCancelled && isCancelled()) {
            completion([NSError errorWithDomain:@"HXCVD" code:-199 userInfo:@{ NSLocalizedDescriptionKey : @"已取消" }], nil);
            return;
        }
        /// 续传：按序号跳过磁盘上已存在且非空的 seg_XXXXX.ts（与写入命名一致），避免杀进程后从头重下。
        NSFileManager *fm = [NSFileManager defaultManager];
        while (idx < (NSInteger)entries.count) {
            NSString *skipName = [NSString stringWithFormat:@"seg_%05ld.ts", (long)idx];
            NSURL *skipURL = [taskDir URLByAppendingPathComponent:skipName];
            NSDictionary *attrs = [fm attributesOfItemAtPath:skipURL.path error:nil];
            int64_t existingLen = attrs ? [attrs[NSFileSize] longLongValue] : 0;
            if (existingLen <= 0) {
                break;
            }
            written += existingLen;
            if (progress) {
                progress(written, -1);
            }
            idx += 1;
        }
        if (idx >= (NSInteger)entries.count) {
            NSURL *localPlistURL = [taskDir URLByAppendingPathComponent:@"local.m3u8"];
            NSError *werr = nil;
            if (![strongSelf hxdvd_writeLocalPlaylistWithEntries:entries taskDir:taskDir toURL:localPlistURL error:&werr]) {
                completion(werr, nil);
                return;
            }
            NSString *rel = [[HXCVDFileStore sharedStore] relativePathFromRootForFileURL:localPlistURL];
            completion(nil, rel);
            return;
        }

        NSDictionary *entry = entries[(NSUInteger)idx];
        NSURL *segURL = entry[@"url"];
        NSString *fileName = [NSString stringWithFormat:@"seg_%05ld.ts", (long)idx];
        NSURL *outFile = [taskDir URLByAppendingPathComponent:fileName];

        [strongSelf hxdvd_downloadDataFromURL:segURL session:session completion:^(NSData * _Nullable data, NSError * _Nullable err) {
            dispatch_async(q, ^{
                if (err) {
                    chainErr = err;
                    downloadNext();
                    return;
                }
                NSError *we = nil;
                [strongSelf hxdvd_writeData:data toURL:outFile error:&we];
                if (we) {
                    chainErr = we;
                    downloadNext();
                    return;
                }
                written += (int64_t)data.length;
                if (progress) {
                    progress(written, -1);
                }
                idx += 1;
                downloadNext();
            });
        }];
    };
#pragma clang diagnostic pop

    dispatch_async(q, ^{
        downloadNext();
    });
}

/// 从 #EXTINF 行取时长，供 #EXT-X-TARGETDURATION（FFmpeg hls_probe 需 TARGETDURATION 或 MEDIA-SEQUENCE 等标签才会识别为 HLS）
- (double)hxdvd_maxSegmentDurationFromEntries:(NSArray<NSDictionary *> *)entries {
    double maxSec = 0;
    for (NSDictionary *e in entries) {
        NSString *extinf = e[@"extinf"];
        if (extinf.length < 9)
            continue;
        NSRange colon = [extinf rangeOfString:@":"];
        if (colon.location == NSNotFound)
            continue;
        NSString *after = [[extinf substringFromIndex:NSMaxRange(colon)]
            stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        NSScanner *sc = [NSScanner scannerWithString:after];
        double d = 0;
        if ([sc scanDouble:&d] && d > maxSec)
            maxSec = d;
    }
    if (maxSec < 1e-6)
        maxSec = 10.0;
    return maxSec;
}

- (BOOL)hxdvd_writeLocalPlaylistWithEntries:(NSArray<NSDictionary *> *)entries
                                    taskDir:(NSURL *)taskDir
                                      toURL:(NSURL *)outURL
                                      error:(NSError **)outError {
    double maxSeg = [self hxdvd_maxSegmentDurationFromEntries:entries];
    int targetDuration = (int)ceil(maxSeg);
    if (targetDuration < 1)
        targetDuration = 1;

    NSMutableString *m = [NSMutableString string];
    [m appendString:@"#EXTM3U\n"];
    [m appendString:@"#EXT-X-VERSION:3\n"];
    [m appendFormat:@"#EXT-X-TARGETDURATION:%d\n", targetDuration];
    [m appendString:@"#EXT-X-MEDIA-SEQUENCE:0\n"];
    [m appendString:@"#EXT-X-PLAYLIST-TYPE:VOD\n"];
    for (NSUInteger i = 0; i < entries.count; i++) {
        NSDictionary *e = entries[i];
        NSString *extinf = e[@"extinf"];
        if (extinf.length) {
            [m appendString:extinf];
            [m appendString:@"\n"];
        }
        NSString *fn = [NSString stringWithFormat:@"seg_%05lu.ts", (unsigned long)i];
        [m appendString:fn];
        [m appendString:@"\n"];
    }
    [m appendString:@"#EXT-X-ENDLIST\n"];
    NSData *d = [m dataUsingEncoding:NSUTF8StringEncoding];
    return [d writeToURL:outURL options:NSDataWritingAtomic error:outError];
}

@end
