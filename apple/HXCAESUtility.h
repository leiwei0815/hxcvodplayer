#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface HXCAESUtility : NSObject

/// AES-256-CBC 加密字符串 → 返回 base64
/// @param string 明文
/// @param key 密钥（32位）
/// @param iv 偏移量（16位）
+ (nullable NSString *)encryptString:(NSString *)string
                                 key:(NSString *)key
                                  iv:(NSString *)iv;

/// AES-256-CBC 解密 base64 → 返回明文
/// @param encryptedBase64 加密后的 base64 字符串
/// @param key 密钥（32位）
/// @param iv 偏移量（16位）
+ (nullable NSString *)decryptString:(NSString *)encryptedBase64
                                 key:(NSString *)key
                                  iv:(NSString *)iv;
@end

NS_ASSUME_NONNULL_END