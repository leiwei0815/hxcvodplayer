#ifndef AES_H
#define AES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AES_BLOCK_SIZE 16
#define AES_KEY_SIZE 32    // AES-256
#define AES_IV_SIZE 16     // CBC IV

/**
 * AES-256 CBC 加密
 * @param key      密钥, 必须32字节
 * @param iv       偏移量, 必须16字节
 * @param in       明文数据
 * @param in_len   明文长度
 * @param out      输出密文数据(需要提前分配内存)
 * @return 加密后数据长度
 */
size_t aes256_cbc_encrypt(const uint8_t* key, const uint8_t* iv,
                          const uint8_t* in, size_t in_len,
                          uint8_t* out);

/**
 * AES-256 CBC 解密
 * @param key      密钥, 必须32字节
 * @param iv       偏移量, 必须16字节
 * @param in       密文数据
 * @param in_len   密文长度
 * @param out      输出明文数据(需要提前分配内存)
 * @return 解密后数据长度
 */
size_t aes256_cbc_decrypt(const uint8_t* key, const uint8_t* iv,
                          const uint8_t* in, size_t in_len,
                          uint8_t* out);

#ifdef __cplusplus
}
#endif

#endif // AES_H