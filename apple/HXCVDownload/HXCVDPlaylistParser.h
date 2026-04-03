/**
 * @file HXCVDPlaylistParser.h
 * @brief 轻量 m3u8 解析（Master / Media playlist、分片 URL）
 *
 * @note AES-128 等加密需额外处理密钥，本解析器会检测并返回 hasEncryption
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface HXCVDPlaylistParser : NSObject

/// 是否为 master（多码率）
+ (BOOL)isMasterPlaylistContent:(NSString *)content;

/// master 中取第一个 variant 的 URI（相对或绝对）
+ (nullable NSURL *)firstVariantPlaylistURLFromMasterContent:(NSString *)content baseURL:(NSURL *)baseURL;

/// media playlist 中解析分片绝对 URL；若含 #EXT-X-KEY 则 hasEncryption=YES
+ (NSArray<NSURL *> *)segmentURLsFromMediaPlaylistContent:(NSString *)content
                                                  baseURL:(NSURL *)playlistURL
                                            hasEncryption:(BOOL *)outHasEncryption;

/// 每一项为 @{ @"extinf" : NSString, @"url" : NSURL }
+ (NSArray<NSDictionary *> *)segmentEntriesFromMediaPlaylistContent:(NSString *)content
                                                            baseURL:(NSURL *)playlistURL
                                                      hasEncryption:(BOOL *)outHasEncryption;

@end

NS_ASSUME_NONNULL_END
