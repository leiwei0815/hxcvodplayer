#import "HXCPlayerLicenseCrypto.h"
#include "hxc_aes256_cbc.h"
#import <CommonCrypto/CommonCryptor.h>
#import "HXCPlayerLog.h"

NSString * const HXCPlayerLicenseCryptoErrorDomain = @"HXCPlayerLicenseCrypto";

typedef NS_ENUM(NSInteger, HXCPlayerLicenseCryptoErrorCode) {
    HXCPlayerLicenseCryptoErrorEmptyInput = 1,
    HXCPlayerLicenseCryptoErrorInvalidKey = 2,
    HXCPlayerLicenseCryptoErrorInvalidBase64 = 3,
    HXCPlayerLicenseCryptoErrorInvalidCipher = 4,
    HXCPlayerLicenseCryptoErrorDecryptFailed = 5,
    HXCPlayerLicenseCryptoErrorParseFailed = 6,
};

@implementation HXCPlayerLicenseCrypto

static NSString *hxc_hex_prefix(const void *bytes, NSUInteger len, NSUInteger maxBytes) {
    if (!bytes || len == 0) return @"";
    NSUInteger n = MIN(len, maxBytes);
    const uint8_t *p = (const uint8_t *)bytes;
    NSMutableString *s = [NSMutableString stringWithCapacity:n * 2];
    for (NSUInteger i = 0; i < n; i++) {
        [s appendFormat:@"%02x", p[i]];
    }
    if (n < len) {
        [s appendString:@"..."];
    }
    return s;
}

static NSString *hxc_decimal_prefix(const void *bytes, NSUInteger len, NSUInteger maxBytes) {
    if (!bytes || len == 0) return @"[]";
    NSUInteger n = MIN(len, maxBytes);
    const uint8_t *p = (const uint8_t *)bytes;
    NSMutableString *s = [NSMutableString stringWithString:@"["];
    for (NSUInteger i = 0; i < n; i++) {
        if (i) [s appendString:@","];
        [s appendFormat:@"%u", (unsigned int)p[i]];
    }
    if (n < len) {
        [s appendString:@",..."];
    }
    [s appendString:@"]"];
    return s;
}

static BOOL hxc_read_bytes(NSData *data, NSUInteger *offset, void *out, NSUInteger len) {
    if (!data || !offset || !out) return NO;
    if (*offset + len > data.length) return NO;
    memcpy(out, (const uint8_t *)data.bytes + *offset, len);
    *offset += len;
    return YES;
}

static BOOL hxc_read_u64_be(NSData *data, NSUInteger *offset, uint64_t *out) {
    uint8_t b[8] = {0};
    if (!hxc_read_bytes(data, offset, b, sizeof(b))) return NO;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | (uint64_t)b[i];
    if (out) *out = v;
    return YES;
}

static BOOL hxc_read_u32_be(NSData *data, NSUInteger *offset, uint32_t *out) {
    uint8_t b[4] = {0};
    if (!hxc_read_bytes(data, offset, b, sizeof(b))) return NO;
    uint32_t v = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
    if (out) *out = v;
    return YES;
}

static BOOL hxc_read_u8(NSData *data, NSUInteger *offset, uint8_t *out) {
    uint8_t b = 0;
    if (!hxc_read_bytes(data, offset, &b, 1)) return NO;
    if (out) *out = b;
    return YES;
}

static NSString * _Nullable hxc_read_len_prefixed_ascii(NSData *data, NSUInteger *offset) {
    // 格式：4字节，前三个为0，最后一个是长度；随后跟 length 字节 ASCII
    uint8_t lenBuf[4] = {0};
    if (!hxc_read_bytes(data, offset, lenBuf, sizeof(lenBuf))) return nil;
    uint8_t len = 0;
    // 兼容两种长度头：
    // - [0,0,0,len]（你给的规范）
    // - [len,0,0,0]（有些实现会把长度放在首字节）
    if (lenBuf[0] == 0 && lenBuf[1] == 0 && lenBuf[2] == 0) {
        len = lenBuf[3];
    } else if (lenBuf[1] == 0 && lenBuf[2] == 0 && lenBuf[3] == 0) {
        len = lenBuf[0];
    } else {
        HXC_LOG_ERROR(@"[HXCPlayerLicenseCrypto] unexpected len header bytes=%@ at off=%lu",
                      hxc_decimal_prefix(lenBuf, sizeof(lenBuf), sizeof(lenBuf)),
                      (unsigned long)(*offset - 4));
        return nil;
    }
    if (len == 0) return @"";
    if (*offset + len > data.length) return nil;
    NSData *sData = [data subdataWithRange:NSMakeRange(*offset, len)];
    *offset += len;
    // 兼容：严格 ASCII；若出现非 ASCII 字节也用 ISO-8859-1 保留字节映射
    NSString *s = [[NSString alloc] initWithData:sData encoding:NSASCIIStringEncoding];
    if (!s) {
        s = [[NSString alloc] initWithData:sData encoding:NSISOLatin1StringEncoding];
    }
    return s;
}

static NSData * _Nullable hxc_build_json_from_plain_binary(NSData *plain, NSError **outError) {
    if (!plain.length) return nil;
    NSUInteger off = 0;
    NSMutableArray<NSDictionary *> *items = [NSMutableArray array];
    BOOL parseFailed = NO;

    while (off < plain.length) {
        NSUInteger recordStart = off;

        uint64_t userId = 0;
        if (!hxc_read_u64_be(plain, &off, &userId)) {
            break;
        }

        NSString *package_name = hxc_read_len_prefixed_ascii(plain, &off);
        if (!package_name) { parseFailed = YES; break; }

        NSString *bundle_id = hxc_read_len_prefixed_ascii(plain, &off);
        if (!bundle_id) { parseFailed = YES; break; }

        uint8_t version = 0;
        if (!hxc_read_u8(plain, &off, &version)) { parseFailed = YES; break; }

        NSString *functionalScope = hxc_read_len_prefixed_ascii(plain, &off);
        if (!functionalScope) { parseFailed = YES; break; }

        uint32_t startedAt = 0;
        if (!hxc_read_u32_be(plain, &off, &startedAt)) { parseFailed = YES; break; }

        uint32_t finishedAt = 0;
        if (!hxc_read_u32_be(plain, &off, &finishedAt)) { parseFailed = YES; break; }

        [items addObject:@{
            @"user_id": @(userId),
            @"package_name": package_name ?: @"",
            @"bundle_id": bundle_id ?: @"",
            @"version": @(version),
            @"functional_scope": functionalScope ?: @"",
            @"started_at": @(startedAt),
            @"finished_at": @(finishedAt),
        }];

        // 防御：若没有推进，避免死循环
        if (off <= recordStart) {
            parseFailed = YES;
            break;
        }
    }

    // 允许尾部全 0 填充（若存在）
    if (off < plain.length) {
        const uint8_t *p = (const uint8_t *)plain.bytes;
        BOOL allZero = YES;
        for (NSUInteger i = off; i < plain.length; i++) {
            if (p[i] != 0) { allZero = NO; break; }
        }
        if (!allZero) parseFailed = YES;
    }

    if (parseFailed) {
        if (outError) {
            *outError = [NSError errorWithDomain:HXCPlayerLicenseCryptoErrorDomain
                                            code:HXCPlayerLicenseCryptoErrorParseFailed
                                        userInfo:@{NSLocalizedDescriptionKey: @"License 解析失败"}];
        }
        HXC_LOG_ERROR(@"[HXCPlayerLicenseCrypto] parse fail at off=%lu total=%lu (items=%lu)",
                      (unsigned long)off, (unsigned long)plain.length, (unsigned long)items.count);
        return nil;
    }

    NSError *jsonErr = nil;
    NSData *json = [NSJSONSerialization dataWithJSONObject:items options:0 error:&jsonErr];
    if (!json) {
        if (outError) *outError = jsonErr;
        return nil;
    }
    return json;
}

static NSData * _Nullable hxc_cccrypt_aes256_cbc_pkcs7(NSData *cipher,
                                                       NSData *keyData,
                                                       const void *iv16,
                                                       NSError **outError) {
    if (!cipher.length || !keyData.length || !iv16) return nil;
    size_t outSize = cipher.length + kCCBlockSizeAES128;
    NSMutableData *out = [NSMutableData dataWithLength:outSize];
    size_t moved = 0;
    CCCryptorStatus st = CCCrypt(kCCDecrypt,
                                 kCCAlgorithmAES,
                                 kCCOptionPKCS7Padding,
                                 keyData.bytes,
                                 keyData.length,
                                 iv16,
                                 cipher.bytes,
                                 cipher.length,
                                 out.mutableBytes,
                                 out.length,
                                 &moved);
    if (st != kCCSuccess) {
        if (outError) {
            *outError = [NSError errorWithDomain:HXCPlayerLicenseCryptoErrorDomain
                                            code:HXCPlayerLicenseCryptoErrorDecryptFailed
                                        userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithFormat:@"CCCrypt 解密失败: %d", (int)st]}];
        }
        return nil;
    }
    out.length = moved;
    return out;
}

+ (NSData *)decryptLicensePayload:(NSData *)encryptedData
                       licenseKey:(NSString *)licenseKey
                            error:(NSError *__autoreleasing _Nullable *)outError {
    if (!encryptedData.length) {
        if (outError) {
            *outError = [NSError errorWithDomain:HXCPlayerLicenseCryptoErrorDomain
                                            code:HXCPlayerLicenseCryptoErrorEmptyInput
                                        userInfo:@{NSLocalizedDescriptionKey: @"License 响应体为空"}];
        }
        return nil;
    }

    // 1) 输入可能是：接口 JSON（包含 terminalLicenses/base64）；或直接是 base64 字符串；或直接是 base64 解出的二进制
    NSData *payloadData = nil;
    NSString *maybeString = [[NSString alloc] initWithData:encryptedData encoding:NSUTF8StringEncoding];
    if (maybeString.length) {
        HXC_LOG_DEBUG(@"[HXCPlayerLicenseCrypto] input looks like UTF-8 string, bytes=%lu", (unsigned long)encryptedData.length);
        NSData *jsonData = encryptedData;
        NSError *jsonErr = nil;
        id obj = [NSJSONSerialization JSONObjectWithData:jsonData options:0 error:&jsonErr];
        if ([obj isKindOfClass:[NSDictionary class]]) {
            HXC_LOG_DEBUG(@"[HXCPlayerLicenseCrypto] input parsed as JSON object");
            id s = ((NSDictionary *)obj)[@"terminalLicenses"];
            if ([s isKindOfClass:[NSString class]] && [(NSString *)s length] > 0) {
                HXC_LOG_DEBUG(@"[HXCPlayerLicenseCrypto] found terminalLicenses string, len=%lu", (unsigned long)[(NSString *)s length]);
                payloadData = [[NSData alloc] initWithBase64EncodedString:(NSString *)s options:0];
                if (!payloadData.length) {
                    if (outError) {
                        *outError = [NSError errorWithDomain:HXCPlayerLicenseCryptoErrorDomain
                                                        code:HXCPlayerLicenseCryptoErrorInvalidBase64
                                                    userInfo:@{NSLocalizedDescriptionKey: @"terminalLicenses Base64 解码失败"}];
                    }
                    HXC_LOG_ERROR(@"[HXCPlayerLicenseCrypto] terminalLicenses base64 decode failed");
                    return nil;
                }
            }
        }
        if (!payloadData) {
            // 非 JSON：尝试把整个响应当 base64 字符串
            NSString *trim = [maybeString stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]];
            NSData *b64 = [[NSData alloc] initWithBase64EncodedString:trim options:0];
            if (b64.length) {
                HXC_LOG_DEBUG(@"[HXCPlayerLicenseCrypto] input treated as base64 string, decoded=%lu", (unsigned long)b64.length);
                payloadData = b64;
            } else {
                HXC_LOG_WARNING(@"[HXCPlayerLicenseCrypto] input is UTF-8 but not JSON/base64; will treat as raw bytes");
            }
        }
    }
    if (!payloadData) {
        // 回退：直接当“已经是 base64 decode 后的二进制”
        payloadData = encryptedData;
        HXC_LOG_DEBUG(@"[HXCPlayerLicenseCrypto] input treated as raw binary, bytes=%lu", (unsigned long)payloadData.length);
    }

    // 打印 base64 解码后的原始二进制（十进制），用于对齐后端加密格式
    HXC_LOG_DEBUG(@"[HXCPlayerLicenseCrypto] payloadData bytes(dec) len=%lu prefix=%@",
                  (unsigned long)payloadData.length,
                  hxc_decimal_prefix(payloadData.bytes, payloadData.length, 96));

    if (payloadData.length <= AES_IV_SIZE) {
        if (outError) {
            *outError = [NSError errorWithDomain:HXCPlayerLicenseCryptoErrorDomain
                                            code:HXCPlayerLicenseCryptoErrorInvalidCipher
                                        userInfo:@{NSLocalizedDescriptionKey: @"License payload 长度不足"}];
        }
        HXC_LOG_ERROR(@"[HXCPlayerLicenseCrypto] payload too short: %lu", (unsigned long)payloadData.length);
        return nil;
    }

    // 2) IV = 前16字节；cipher = 剩余
    uint8_t iv[AES_IV_SIZE] = {0};
    [payloadData getBytes:iv length:AES_IV_SIZE];
    NSData *cipher = [payloadData subdataWithRange:NSMakeRange(AES_IV_SIZE, payloadData.length - AES_IV_SIZE)];
    HXC_LOG_DEBUG(@"[HXCPlayerLicenseCrypto] iv(prefix16)=%@ cipherLen=%lu (mod16=%lu)",
                  hxc_hex_prefix(iv, AES_IV_SIZE, 8),
                  (unsigned long)cipher.length,
                  (unsigned long)(cipher.length % AES_BLOCK_SIZE));

    // 3) AES-256-CBC + PKCS7 解密（key = licenseKey UTF-8 原始 32 字节）
    if (cipher.length % AES_BLOCK_SIZE != 0) {
        HXC_LOG_WARNING(@"[HXCPlayerLicenseCrypto] cipher length not multiple of 16: %lu", (unsigned long)cipher.length);
    }
    NSData *keyData = [licenseKey dataUsingEncoding:NSUTF8StringEncoding];
    if (keyData.length != AES_KEY_SIZE) {
        if (outError) {
            *outError = [NSError errorWithDomain:HXCPlayerLicenseCryptoErrorDomain
                                            code:HXCPlayerLicenseCryptoErrorInvalidKey
                                        userInfo:@{NSLocalizedDescriptionKey: @"licenseKey 必须为 32 字节（UTF-8）"}];
        }
        HXC_LOG_ERROR(@"[HXCPlayerLicenseCrypto] invalid licenseKey utf8Len=%lu (expected 32)", (unsigned long)keyData.length);
        return nil;
    }

    // 仅使用 CommonCryptor（AES-CBC + PKCS7）：失败直接报错，不做兜底。
    NSError *ccErr = nil;
    NSData *plainData = hxc_cccrypt_aes256_cbc_pkcs7(cipher, keyData, iv, &ccErr);
    NSString *decryptPath = @"CCCrypt";
    if (!plainData.length) {
        if (outError) {
            *outError = ccErr ?: [NSError errorWithDomain:HXCPlayerLicenseCryptoErrorDomain
                                                    code:HXCPlayerLicenseCryptoErrorDecryptFailed
                                                userInfo:@{NSLocalizedDescriptionKey: @"CCCrypt 解密失败"}];
        }
        HXC_LOG_ERROR(@"[HXCPlayerLicenseCrypto] decrypt failed (CCCrypt): cipherLen=%lu err=%@",
                      (unsigned long)cipher.length,
                      ccErr.localizedDescription ?: @"");
        return nil;
    }
    HXC_LOG_DEBUG(@"[HXCPlayerLicenseCrypto] decrypt ok (%@): keyLen=%lu keyPrefix=%@ plainLen=%lu plainPrefix=%@",
                  decryptPath ?: @"unknown",
                  (unsigned long)keyData.length,
                  hxc_hex_prefix(keyData.bytes, keyData.length, 8),
                  (unsigned long)plainData.length,
                  hxc_hex_prefix(plainData.bytes, plainData.length, 16));

    // 5) 明文是“二进制字段”，按协议解析后拼成 JSON
    NSError *parseErr = nil;
    NSData *json = hxc_build_json_from_plain_binary(plainData, &parseErr);
    if (!json) {
        if (outError) *outError = parseErr;
        HXC_LOG_ERROR(@"[HXCPlayerLicenseCrypto] parse plain binary failed: %@", parseErr.localizedDescription ?: @"");
        return nil;
    }
    HXC_LOG_DEBUG(@"[HXCPlayerLicenseCrypto] parse ok: jsonBytes=%lu", (unsigned long)json.length);
    return json;
}

@end
