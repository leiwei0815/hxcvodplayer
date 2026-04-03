#import "HXCVDownloadItem.h"
#import "HXCVDownloadItem+Internal.h"

@interface HXCVDownloadItem ()
@property (nonatomic, copy, readwrite) NSString *taskId;
@property (nonatomic, copy, readwrite) NSString *identifier;
@property (nonatomic, copy, readwrite) NSString *urlString;
@property (nonatomic, copy, readwrite, nullable) NSString *finalURLString;
@property (nonatomic, assign, readwrite) HXCVDownloadType downloadType;
@property (nonatomic, assign, readwrite) HXCVDownloadState state;
@property (nonatomic, assign, readwrite) double progress;
@property (nonatomic, assign, readwrite) int64_t bytesWritten;
@property (nonatomic, assign, readwrite) int64_t totalBytes;
@property (nonatomic, copy, readwrite, nullable) NSString *localRelativePath;
@property (nonatomic, copy, readwrite, nullable) NSString *errorMessage;
@property (nonatomic, strong, readwrite) NSDate *createdAt;
@property (nonatomic, strong, readwrite) NSDate *updatedAt;
@end

@implementation HXCVDownloadItem

+ (instancetype)hxdvd_itemWithTaskId:(NSString *)taskId
                            identifier:(NSString *)identifier
                           urlString:(NSString *)urlString
                      finalURLString:(NSString *)finalURLString
                        downloadType:(HXCVDownloadType)downloadType
                               state:(HXCVDownloadState)state
                            progress:(double)progress
                        bytesWritten:(int64_t)bytesWritten
                          totalBytes:(int64_t)totalBytes
                   localRelativePath:(NSString *)localRelativePath
                        errorMessage:(NSString *)errorMessage
                           createdAt:(NSDate *)createdAt
                           updatedAt:(NSDate *)updatedAt {
    HXCVDownloadItem *i = [[HXCVDownloadItem alloc] init];
    i.taskId = taskId;
    i.identifier = identifier.length ? identifier : @"default";
    i.urlString = urlString;
    i.finalURLString = finalURLString;
    i.downloadType = downloadType;
    i.state = state;
    i.progress = progress;
    i.bytesWritten = bytesWritten;
    i.totalBytes = totalBytes;
    i.localRelativePath = localRelativePath;
    i.errorMessage = errorMessage;
    i.createdAt = createdAt;
    i.updatedAt = updatedAt;
    return i;
}

- (id)copyWithZone:(NSZone *)zone {
    HXCVDownloadItem *c = [[HXCVDownloadItem allocWithZone:zone] init];
    if (!c) return nil;
    c.taskId = self.taskId;
    c.identifier = self.identifier;
    c.urlString = self.urlString;
    c.finalURLString = self.finalURLString;
    c.downloadType = self.downloadType;
    c.state = self.state;
    c.progress = self.progress;
    c.bytesWritten = self.bytesWritten;
    c.totalBytes = self.totalBytes;
    c.localRelativePath = self.localRelativePath;
    c.errorMessage = self.errorMessage;
    c.createdAt = self.createdAt;
    c.updatedAt = self.updatedAt;
    return c;
}

@end
