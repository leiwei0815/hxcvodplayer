/**
 * @file HXCPlayerLicenseManager.h
 * @brief 播放器 License 校验（网络拉取 → 解密 → 校验 → 本地持久化）
 *
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

FOUNDATION_EXPORT NSString * const HXCPlayerLicenseErrorDomain;

typedef NS_ENUM(NSInteger, HXCPlayerLicenseErrorCode) {
    HXCPlayerLicenseErrorInvalidParameter = 1,
    HXCPlayerLicenseErrorHTTPStatus = 2,
    HXCPlayerLicenseErrorEmptyBody = 3,
    HXCPlayerLicenseErrorDecrypt = 4,
    HXCPlayerLicenseErrorJSONParse = 5,
    HXCPlayerLicenseErrorValidationFailed = 6,
};

@interface HXCPlayerLicenseManager : NSObject

/// 是否对 `HXCPlayerControl` 的播放相关接口做 License 门禁（默认 NO，避免影响现有集成）
//+ (void)setPlaybackLicenseGateEnabled:(BOOL)enabled;
//+ (BOOL)isPlaybackLicenseGateEnabled;

/// 是否已通过校验：内存为真，或本地已存储且重新校验仍满足 bundle + 未过期
+ (BOOL)isLicenseCheckPassed;

/// 清除本地通过状态与 `NSUserDefaults` 中已存的 License 数据（例如用户登出）
//+ (void)resetLicenseState;

/// 向 `licenseURL` 发起请求（GET 或 POST），解密后校验；`completion` 仅返回是否成功与错误信息，不返回解密数据。
+ (void)checkLicenseWithLicenseKey:(NSString *)licenseKey
                        licenseURL:(NSString *)licenseURL
                 completionHandler:(void (^)(BOOL success, NSError *_Nullable error))completion;

@end

NS_ASSUME_NONNULL_END
