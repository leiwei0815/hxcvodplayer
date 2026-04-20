/**
 * @file HXCPlayerLog.h
 * @brief 供集成方调用的日志接口：与 core 内 `hxc_logger` / `LOG_*` 写入同一套 Logger（含文件日志）。
 *
 * 使用方式（与 `NSLog` 类似，第一个参数为 format 字符串）：
 * @code
 * HXC_LOG_DEBUG(@"value=%@", @(x));
 * HXC_LOG_INFO(@"opened");
 * @endcode
 *
 * 日志级别与 `+[HXCPlayerControl setLogLevel:]` 一致；需先按需配置日志目录等。
 */

#import "HXCPlayerControl.h"

NS_ASSUME_NONNULL_BEGIN

@interface HXCPlayerControl (HXCAppLog)

+ (void)hxc_logDebugAtFile:(const char *)file
                      line:(int)line
                   function:(const char *)func
                    format:(NSString *)format, ... NS_FORMAT_FUNCTION(4, 5);

+ (void)hxc_logInfoAtFile:(const char *)file
                     line:(int)line
                 function:(const char *)func
                   format:(NSString *)format, ... NS_FORMAT_FUNCTION(4, 5);

+ (void)hxc_logWarningAtFile:(const char *)file
                        line:(int)line
                    function:(const char *)func
                      format:(NSString *)format, ... NS_FORMAT_FUNCTION(4, 5);

+ (void)hxc_logErrorAtFile:(const char *)file
                      line:(int)line
                  function:(const char *)func
                    format:(NSString *)format, ... NS_FORMAT_FUNCTION(4, 5);

@end

/// 与 core 中 LOG_DEBUG 等价（写入同一 Logger）
#define HXC_LOG_DEBUG(fmt, ...) \
    [HXCPlayerControl hxc_logDebugAtFile:__FILE__ line:__LINE__ function:__FUNCTION__ format:(fmt), ##__VA_ARGS__]

#define HXC_LOG_INFO(fmt, ...) \
    [HXCPlayerControl hxc_logInfoAtFile:__FILE__ line:__LINE__ function:__FUNCTION__ format:(fmt), ##__VA_ARGS__]

#define HXC_LOG_WARNING(fmt, ...) \
    [HXCPlayerControl hxc_logWarningAtFile:__FILE__ line:__LINE__ function:__FUNCTION__ format:(fmt), ##__VA_ARGS__]

#define HXC_LOG_ERROR(fmt, ...) \
    [HXCPlayerControl hxc_logErrorAtFile:__FILE__ line:__LINE__ function:__FUNCTION__ format:(fmt), ##__VA_ARGS__]

NS_ASSUME_NONNULL_END
