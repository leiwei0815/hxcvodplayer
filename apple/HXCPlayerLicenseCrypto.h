/**
 * @file HXCPlayerLicenseCrypto.h
 * @brief License 二进制载荷解密（SDK 内部使用；不通过 umbrella / 公共头对外暴露）
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// 将服务端返回的加密二进制解密为「JSON 明文」对应的 NSData（通常为 UTF-8 的 JSON 字节）
/// 默认实现为**透传**（即假定服务端当前下发的就是明文 JSON 字节，便于联调）
/// 接入真实加密后：在此使用 `licenseKey` 派生密钥 / IV，解密后再返回 JSON 的 NSData
@interface HXCPlayerLicenseCrypto : NSObject

+ (nullable NSData *)decryptLicensePayload:(NSData *)encryptedData
                                licenseKey:(NSString *)licenseKey
                                      error:(NSError *__autoreleasing _Nullable *)outError;

@end

NS_ASSUME_NONNULL_END
