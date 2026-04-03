#import "HXCAESUtility.h"
#include "hxc_aes256_cbc.h"

@implementation HXCAESUtility

+ (nullable NSString *)encryptString:(NSString *)string
                                 key:(NSString *)key
                                  iv:(NSString *)iv
{
    if (!string || !key || key.length != 32 || !iv || iv.length != 16) {
        return nil;
    }

    // 转成 UTF8 数据
    NSData *plainData = [string dataUsingEncoding:NSUTF8StringEncoding];
    if (!plainData) return nil;
    // 输出缓冲区（足够大）
    NSUInteger inLen = plainData.length;
    NSMutableData *cipherData = [NSMutableData dataWithLength:inLen + AES_BLOCK_SIZE];
    // 调用 C 语言 AES 加密
    size_t outLen = aes256_cbc_encrypt(
        (const uint8_t *)key.UTF8String,
        (const uint8_t *)iv.UTF8String,
        (const uint8_t *)plainData.bytes,
        inLen,
        (uint8_t *)cipherData.mutableBytes
    );

    if (outLen == 0) return nil;
    cipherData.length = outLen;

    // 返回 Base64
    return [cipherData base64EncodedStringWithOptions:0];
}

+ (nullable NSString *)decryptString:(NSString *)encryptedBase64
                                 key:(NSString *)key
                                  iv:(NSString *)iv
{
    if (!encryptedBase64 || !key || key.length != 32 || !iv || iv.length != 16) {
        return nil;
    }

    // Base64 → 二进制
    NSData *cipherData = [[NSData alloc] initWithBase64EncodedString:encryptedBase64 options:0];
    if (!cipherData || cipherData.length == 0) return nil;

    // 输出缓冲区
    NSMutableData *plainData = [NSMutableData dataWithLength:cipherData.length];

    // 调用 C 语言 AES 解密
    size_t outLen = aes256_cbc_decrypt(
        (const uint8_t *)key.UTF8String,
        (const uint8_t *)iv.UTF8String,
        (const uint8_t *)cipherData.bytes,
        cipherData.length,
        (uint8_t *)plainData.mutableBytes
    );

    if (outLen == 0) return nil;
    plainData.length = outLen;

    // 转回字符串
    return [[NSString alloc] initWithData:plainData encoding:NSUTF8StringEncoding];
}

@end
