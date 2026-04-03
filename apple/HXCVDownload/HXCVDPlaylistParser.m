#import "HXCVDPlaylistParser.h"

@implementation HXCVDPlaylistParser

+ (BOOL)isMasterPlaylistContent:(NSString *)content {
    if (content.length == 0) {
        return NO;
    }
    NSRange r = [content rangeOfString:@"#EXT-X-STREAM-INF"];
    return r.location != NSNotFound;
}

+ (nullable NSURL *)firstVariantPlaylistURLFromMasterContent:(NSString *)content baseURL:(NSURL *)baseURL {
    NSArray<NSString *> *lines = [content componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]];
    BOOL pendingVariant = NO;
    for (NSString *line in lines) {
        NSString *trim = [line stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (trim.length == 0) {
            continue;
        }
        if ([trim hasPrefix:@"#EXT-X-STREAM-INF"]) {
            pendingVariant = YES;
            continue;
        }
        if ([trim hasPrefix:@"#"]) {
            continue;
        }
        if (pendingVariant) {
            NSURL *u = [NSURL URLWithString:trim relativeToURL:baseURL];
            pendingVariant = NO;
            return u ? u.absoluteURL : nil;
        }
    }
    return nil;
}

+ (NSArray<NSDictionary *> *)segmentEntriesFromMediaPlaylistContent:(NSString *)content
                                                            baseURL:(NSURL *)playlistURL
                                                      hasEncryption:(BOOL *)outHasEncryption {
    if (outHasEncryption) {
        *outHasEncryption = NO;
    }
    NSMutableArray<NSDictionary *> *entries = [NSMutableArray array];
    NSArray<NSString *> *lines = [content componentsSeparatedByCharactersInSet:[NSCharacterSet newlineCharacterSet]];
    NSString *pendingExtinf = nil;
    for (NSString *line in lines) {
        NSString *trim = [line stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
        if (trim.length == 0) {
            continue;
        }
        if ([trim hasPrefix:@"#EXT-X-KEY"]) {
            if (outHasEncryption) {
                *outHasEncryption = YES;
            }
            continue;
        }
        if ([trim hasPrefix:@"#EXTINF"]) {
            pendingExtinf = trim;
            continue;
        }
        if ([trim hasPrefix:@"#"]) {
            continue;
        }
        NSURL *u = [NSURL URLWithString:trim relativeToURL:playlistURL];
        if (!u) {
            pendingExtinf = nil;
            continue;
        }
        NSString *ext = pendingExtinf ?: @"#EXTINF:10.0,";
        [entries addObject:@{ @"extinf" : ext, @"url" : u.absoluteURL }];
        pendingExtinf = nil;
    }
    return entries;
}

+ (NSArray<NSURL *> *)segmentURLsFromMediaPlaylistContent:(NSString *)content
                                                  baseURL:(NSURL *)playlistURL
                                            hasEncryption:(BOOL *)outHasEncryption {
    if (outHasEncryption) {
        *outHasEncryption = NO;
    }
    NSArray<NSDictionary *> *entries = [self segmentEntriesFromMediaPlaylistContent:content baseURL:playlistURL hasEncryption:outHasEncryption];
    NSMutableArray<NSURL *> *urls = [NSMutableArray arrayWithCapacity:entries.count];
    for (NSDictionary *e in entries) {
        [urls addObject:e[@"url"]];
    }
    return urls;
}

@end
