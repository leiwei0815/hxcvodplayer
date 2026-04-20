/**
 * 使用方式建议：
 * 1. 启动时 `setPlaybackLicenseGateEnabled:YES`
 * 2. 调用 `checkLicenseWithLicenseKey:licenseURL:completionHandler:` 成功后，再创建/操作 `HXCPlayerControl`
 * 3. 校验规则：解密后的数组中至少一条 `bundle_id` 与当前应用一致，且 `finished_at` 大于当前 Unix 时间戳；通过后写入 `NSUserDefaults`。
 */

#import "HXCPlayerLicenseManager.h"
#import "HXCPlayerLicenseCrypto.h"
#import "HXCPlayerLog.h"

NSString * const HXCPlayerLicenseErrorDomain = @"HXCPlayerLicense";

/// 解密校验通过后的 License JSON（UTF-8 字节，内容为数组），存于 NSUserDefaults。
static NSString * const kHXCPlayerLicenseDefaultsKey = @"com.hxcplayer.license_decrypted_json_v1";

static BOOL g_playbackGateEnabled = NO;
static BOOL g_licenseCheckPassed = NO;
static NSObject *g_stateLock;

static NSString *hxc_app_bundle_identifier(void) {
    return [[NSBundle mainBundle] bundleIdentifier] ?: @"";
}

static void hxc_license_delete_stored_payload(void) {
    NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
    [ud removeObjectForKey:kHXCPlayerLicenseDefaultsKey];
}

static BOOL hxc_license_save_payload_json(NSData *jsonData, NSError **outError) {
    if (!jsonData.length) {
        if (outError) {
            *outError = [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                            code:HXCPlayerLicenseErrorInvalidParameter
                                        userInfo:@{NSLocalizedDescriptionKey: @"License 数据为空，无法存储"}];
        }
        HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] save payload failed: empty data");
        return NO;
    }
    @try {
        [[NSUserDefaults standardUserDefaults] setObject:jsonData forKey:kHXCPlayerLicenseDefaultsKey];
    } @catch (NSException *ex) {
        if (outError) {
            *outError = [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                            code:HXCPlayerLicenseErrorInvalidParameter
                                        userInfo:@{
                                            NSLocalizedDescriptionKey: @"NSUserDefaults 写入 License 失败",
                                            @"reason": ex.reason ?: @""
                                        }];
        }
        HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] save payload failed: %@", ex.reason ?: @"");
        return NO;
    }
    return YES;
}

static NSArray * _Nullable hxc_license_load_stored_items(void) {
    id raw = [[NSUserDefaults standardUserDefaults] objectForKey:kHXCPlayerLicenseDefaultsKey];
    NSData *data = nil;
    if ([raw isKindOfClass:[NSData class]]) {
        data = (NSData *)raw;
    } else if ([raw isKindOfClass:[NSString class]]) {
        data = [(NSString *)raw dataUsingEncoding:NSUTF8StringEncoding];
    }
    if (!data.length) return nil;
    NSError *err = nil;
    id obj = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingMutableContainers error:&err];
    if ([obj isKindOfClass:[NSArray class]]) {
        return (NSArray *)obj;
    }
    return nil;
}

/// 从解密后的 JSON 根对象得到待校验的 dict 数组。
static NSArray * _Nullable hxc_license_items_from_parsed_root(id obj) {
    if ([obj isKindOfClass:[NSArray class]]) {
        return (NSArray *)obj;
    }
    if ([obj isKindOfClass:[NSDictionary class]]) {
        id inner = ((NSDictionary *)obj)[@"terminalLicenses"];
        if ([inner isKindOfClass:[NSArray class]]) {
            return (NSArray *)inner;
        }
    }
    return nil;
}

/// 至少存在一条：bundle_id 与当前应用一致，且 finished_at 大于当前 Unix 时间戳。
static BOOL hxc_license_validate_items(NSArray *items, NSString *appBundleId, NSError **outError) {
    if (!items.count) {
        if (outError) {
            *outError = [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                            code:HXCPlayerLicenseErrorValidationFailed
                                        userInfo:@{NSLocalizedDescriptionKey: @"License 列表为空"}];
        }
        HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] validate failed: items empty");
        return NO;
    }
    if (!appBundleId.length) {
        if (outError) {
            *outError = [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                            code:HXCPlayerLicenseErrorValidationFailed
                                        userInfo:@{NSLocalizedDescriptionKey: @"无法获取当前应用 bundleIdentifier"}];
        }
        HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] validate failed: app bundle id empty");
        return NO;
    }
    NSTimeInterval now = [[NSDate date] timeIntervalSince1970];
    int64_t nowSec = (int64_t)floor(now);
    BOOL foundBundle = NO;
    for (id item in items) {
        if (![item isKindOfClass:[NSDictionary class]]) {
            continue;
        }
        NSDictionary *d = (NSDictionary *)item;
        id bidObj = d[@"bundle_id"];
        if (![bidObj isKindOfClass:[NSString class]] || [(NSString *)bidObj length] == 0) {
            continue;
        }
        NSString *bid = (NSString *)bidObj;
        if (![bid isEqualToString:appBundleId]) {
            continue;
        }
        foundBundle = YES;
        id finObj = d[@"finished_at"];
        int64_t finished = 0;
        if ([finObj isKindOfClass:[NSNumber class]]) {
            finished = [(NSNumber *)finObj longLongValue];
        } else if ([finObj isKindOfClass:[NSString class]]) {
            finished = [(NSString *)finObj longLongValue];
        } else {
            continue;
        }
        if (finished > nowSec) {
            HXC_LOG_INFO(@"[HXCPlayerLicenseManager] validate ok: bundle_id=%@ finished_at=%lld now=%lld",
                         bid, finished, nowSec);
            return YES;
        }
    }
    if (foundBundle) {
        HXC_LOG_WARNING(@"[HXCPlayerLicenseManager] validate failed: bundle matched but expired now=%lld", nowSec);
    } else {
        HXC_LOG_WARNING(@"[HXCPlayerLicenseManager] validate failed: no bundle_id matched (%@)", appBundleId);
    }
    if (outError) {
        *outError = [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                        code:HXCPlayerLicenseErrorValidationFailed
                                    userInfo:@{NSLocalizedDescriptionKey: @"未找到与当前应用匹配且未过期的 License 条目"}];
    }
    return NO;
}

static void hxc_license_mark_failure(void) {
    @synchronized(g_stateLock) {
        g_licenseCheckPassed = NO;
    }
    hxc_license_delete_stored_payload();
}

@implementation HXCPlayerLicenseManager

+ (void)initialize {
    if (self == [HXCPlayerLicenseManager class]) {
        g_stateLock = [[NSObject alloc] init];
    }
}

//+ (void)setPlaybackLicenseGateEnabled:(BOOL)enabled {
//    @synchronized(g_stateLock) {
//        g_playbackGateEnabled = enabled;
//        if (!enabled) {
//            g_licenseCheckPassed = NO;
//            hxc_license_delete_stored_payload();
//        }
//    }
//}

//+ (BOOL)isPlaybackLicenseGateEnabled {
//    @synchronized(g_stateLock) {
//        return g_playbackGateEnabled;
//    }
//}

+ (BOOL)isLicenseCheckPassed {
    @synchronized(g_stateLock) {
        if (g_licenseCheckPassed) {
            return YES;
        }
    }
    NSArray *stored = hxc_license_load_stored_items();
    NSString *bid = hxc_app_bundle_identifier();
    NSError *verr = nil;
    if (hxc_license_validate_items(stored, bid, &verr)) {
        @synchronized(g_stateLock) {
            g_licenseCheckPassed = YES;
        }
        HXC_LOG_INFO(@"[HXCPlayerLicenseManager] license passed by stored payload");
        return YES;
    }
    HXC_LOG_WARNING(@"[HXCPlayerLicenseManager] stored payload not valid: %@", verr.localizedDescription ?: @"");
    (void)verr;
    return NO;
}

//+ (void)resetLicenseState {
//    @synchronized(g_stateLock) {
//        g_licenseCheckPassed = NO;
//    }
//    hxc_license_delete_stored_payload();
//}

+ (void)checkLicenseWithLicenseKey:(NSString *)licenseKey
                        licenseURL:(NSString *)licenseURL
                 completionHandler:(void (^)(BOOL success, NSError *_Nullable error))completion {
    if (!completion) {
        return;
    }
    void (^completeOnMain)(BOOL, NSError *) = ^(BOOL ok, NSError *err) {
        dispatch_async(dispatch_get_main_queue(), ^{ completion(ok, err); });
    };
    if (!licenseKey.length || !licenseURL.length) {
        hxc_license_mark_failure();
        NSError *err = [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                             code:HXCPlayerLicenseErrorInvalidParameter
                                         userInfo:@{NSLocalizedDescriptionKey: @"License 参数无效"}];
        completeOnMain(NO, err);
        HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] invalid params: keyLen=%lu urlLen=%lu",
                      (unsigned long)licenseKey.length, (unsigned long)licenseURL.length);
        return;
    }

    NSURL *url = [NSURL URLWithString:licenseURL];
    if (!url || !url.scheme.length) {
        hxc_license_mark_failure();
        NSError *err = [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                             code:HXCPlayerLicenseErrorInvalidParameter
                                         userInfo:@{NSLocalizedDescriptionKey: @"License URL 无效"}];
        completeOnMain(NO, err);
        HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] invalid licenseURL: %@", licenseURL);
        return;
    }

    NSMutableURLRequest *req = [NSMutableURLRequest requestWithURL:url];
    BOOL preferGET = ([licenseURL rangeOfString:@"/license/getMobileLicense/" options:NSCaseInsensitiveSearch].location != NSNotFound);
    if (preferGET) {
        req.HTTPMethod = @"GET";
    } else {
        req.HTTPMethod = @"POST";
        [req setValue:@"application/json" forHTTPHeaderField:@"Content-Type"];
        NSDictionary *bodyDict = @{@"licenseKey": licenseKey};
        NSError *jsonErr = nil;
        NSData *bodyData = [NSJSONSerialization dataWithJSONObject:bodyDict options:0 error:&jsonErr];
        if (!bodyData) {
            hxc_license_mark_failure();
            NSError *err = jsonErr ?: [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                                          code:HXCPlayerLicenseErrorInvalidParameter
                                                      userInfo:@{NSLocalizedDescriptionKey: @"构建请求体失败"}];
            completeOnMain(NO, err);
            HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] build POST body failed: %@", err.localizedDescription ?: @"");
            return;
        }
        req.HTTPBody = bodyData;
    }
    HXC_LOG_INFO(@"[HXCPlayerLicenseManager] start check (method=%@)", req.HTTPMethod);

    NSURLSessionDataTask *task = [[NSURLSession sharedSession] dataTaskWithRequest:req
                                                                 completionHandler:^(NSData *_Nullable data,
                                                                                     NSURLResponse *_Nullable response,
                                                                                     NSError *_Nullable error) {
        void (^finish)(BOOL, NSError *) = ^(BOOL ok, NSError *err) {
            completeOnMain(ok, err);
        };
        
        BOOL (^try_fallback_to_cached)(NSError *cause) = ^BOOL(NSError *cause) {
            NSArray *stored = hxc_license_load_stored_items();
            NSString *appBid = hxc_app_bundle_identifier();
            NSError *verr = nil;
            if (hxc_license_validate_items(stored, appBid, &verr)) {
                @synchronized(g_stateLock) {
                    g_licenseCheckPassed = YES;
                }
                HXC_LOG_WARNING(@"[HXCPlayerLicenseManager] request failed, but cached license is valid. cause=%@",
                                cause.localizedDescription ?: @"");
                finish(YES, nil);
                return YES;
            }
            HXC_LOG_WARNING(@"[HXCPlayerLicenseManager] request failed and cached license not valid. cause=%@ cachedErr=%@",
                            cause.localizedDescription ?: @"",
                            verr.localizedDescription ?: @"");
            return NO;
        };

        if (error) {
            HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] request error: %@", error.localizedDescription ?: @"");
            if (try_fallback_to_cached(error)) {
                return;
            }
            hxc_license_mark_failure();
            finish(NO, error);
            return;
        }

        NSInteger status = 0;
        if ([response isKindOfClass:[NSHTTPURLResponse class]]) {
            status = ((NSHTTPURLResponse *)response).statusCode;
        }
        if (status < 200 || status >= 300) {
            NSError *err = [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                               code:HXCPlayerLicenseErrorHTTPStatus
                                           userInfo:@{
                                               NSLocalizedDescriptionKey: @"License 服务返回非成功状态码",
                                               @"HTTPStatus": @(status)
                                           }];
            HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] http status=%ld", (long)status);
            if (try_fallback_to_cached(err)) {
                return;
            }
            hxc_license_mark_failure();
            finish(NO, err);
            return;
        }

        if (!data.length) {
            NSError *err = [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                               code:HXCPlayerLicenseErrorEmptyBody
                                           userInfo:@{NSLocalizedDescriptionKey: @"License 响应体为空"}];
            HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] empty response body");
            if (try_fallback_to_cached(err)) {
                return;
            }
            hxc_license_mark_failure();
            finish(NO, err);
            return;
        }

        NSError *decErr = nil;
        NSData *plain = [HXCPlayerLicenseCrypto decryptLicensePayload:data licenseKey:licenseKey error:&decErr];
        if (!plain) {
            NSError *err = decErr ?: [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                                         code:HXCPlayerLicenseErrorDecrypt
                                                     userInfo:@{NSLocalizedDescriptionKey: @"License 解密失败"}];
            HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] decrypt failed: %@", err.localizedDescription ?: @"");
            if (try_fallback_to_cached(err)) {
                return;
            }
            hxc_license_mark_failure();
            finish(NO, err);
            return;
        }

        NSError *parseErr = nil;
        id obj = [NSJSONSerialization JSONObjectWithData:plain options:NSJSONReadingMutableContainers error:&parseErr];
        NSArray *items = hxc_license_items_from_parsed_root(obj);
        if (!items) {
            NSError *err = parseErr ?: [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                                           code:HXCPlayerLicenseErrorJSONParse
                                                       userInfo:@{NSLocalizedDescriptionKey: @"License 数据格式无效（需为数组或含 terminalLicenses 数组）"}];
            HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] json parse failed: %@", err.localizedDescription ?: @"");
            if (try_fallback_to_cached(err)) {
                return;
            }
            hxc_license_mark_failure();
            finish(NO, err);
            return;
        }

        NSString *appBid = hxc_app_bundle_identifier();
        NSError *verr = nil;
        if (!hxc_license_validate_items(items, appBid, &verr)) {
            hxc_license_mark_failure();
            finish(NO, verr);
            HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] validate failed: %@", verr.localizedDescription ?: @"");
            return;
        }

        NSError *saveErr = nil;
        if (!hxc_license_save_payload_json(plain, &saveErr)) {
            hxc_license_mark_failure();
            finish(NO, saveErr ?: [NSError errorWithDomain:HXCPlayerLicenseErrorDomain
                                                      code:HXCPlayerLicenseErrorInvalidParameter
                                                  userInfo:@{NSLocalizedDescriptionKey: @"License 写入本地失败"}]);
            HXC_LOG_ERROR(@"[HXCPlayerLicenseManager] store failed: %@", saveErr.localizedDescription ?: @"");
            return;
        }

        @synchronized(g_stateLock) {
            g_licenseCheckPassed = YES;
        }
        HXC_LOG_INFO(@"[HXCPlayerLicenseManager] check passed and stored");
        finish(YES, nil);
    }];
    [task resume];
}

@end
