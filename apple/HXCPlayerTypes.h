/**
 * @file HXCPlayerTypes.h
 * @brief 对外类型定义（枚举等）
 */

#ifndef HXCPLAYER_TYPES_H
#define HXCPLAYER_TYPES_H

#import <Foundation/Foundation.h>

#if TARGET_OS_IOS
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

/// 日志等级，默认 debug 会记录所有日志，release 模式下建议开启 info
typedef NS_ENUM(NSInteger, HXCPlayerLogLevel) {
    HXCPlayerLogLevelDebug,
    HXCPlayerLogLevelInfo,
    HXCPlayerLogLevelWarning,
    HXCPlayerLogLevelError
};

/// 播放器状态
typedef NS_ENUM(NSInteger, HXCPlayerState) {
    HXCPlayerStateIdle = 0,      // 空闲
    HXCPlayerStateOpening,       // 正在打开
    HXCPlayerStateReady,         // 准备就绪
    HXCPlayerStatePlaying,       // 播放中
    HXCPlayerStatePaused,        // 暂停
    HXCPlayerStateStopped,       // 停止
    HXCPlayerStateError          // 错误
};

/// 视频显示模式
typedef NS_ENUM(NSInteger, HXCAspectRatioMode) {
    HXCAspectRatioModeFit = 0,   // 适应（保持宽高比，黑边）
    HXCAspectRatioModeFill       // 填充（裁剪）
};

/// ⚠️ 播放器错误码定义（与 C++ 层 PlayerErrorCode 保持一致，全部使用负数）
typedef NS_ENUM(NSInteger, HXCPlayerErrorCode) {
    // 自定义错误码（负数）
    HXCPlayerErrorNone = 0,                             // 无错误
    HXCPlayerErrorInvalidURL = -1001,                   // 无效的 URL
    HXCPlayerErrorOpenInputFailed = -1002,              // 打开输入失败
    HXCPlayerErrorFindStreamInfoFailed = -1003,         // 查找流信息失败
    HXCPlayerErrorNoVideoStream = -1004,                // 没有视频流
    HXCPlayerErrorNoAudioStream = -1005,                // 没有音频流
    HXCPlayerErrorCodecNotFound = -1006,                // 找不到解码器
    HXCPlayerErrorCodecOpenFailed = -1007,              // 打开解码器失败
    HXCPlayerErrorAllocContextFailed = -1008,           // 分配上下文失败
    HXCPlayerErrorSDLInitFailed = -1009,                // SDL 初始化失败
    HXCPlayerErrorAudioDeviceOpenFailed = -1010,        // 音频设备打开失败
    HXCPlayerErrorSeekFailed = -1011,                   // Seek 操作失败
    HXCPlayerErrorReadFrameFailed = -1012,              // 读取帧失败
    HXCPlayerErrorDecodeFailed = -1013,                 // 解码失败
    HXCPlayerErrorOutOfMemory = -1014,                  // 内存不足
    HXCPlayerErrorNotSupportPIPPlayer = -1015,          // 当前设备不支持画中画播放 (iOS特有)
    HXCPlayerErrorAudioSessionConfigFail = -1016,       // 音频会话配置失败 (iOS特有)
    HXCPlayerErrorInputInvalidData = -1018,             // 无效数据
    HXCPlayerErrorNotSupport = -1019,                   // 不支持的格式或协议
    HXCPlayerErrorUnknown = -1099,                      // 未知错误

    // 网络相关错误 (-2001 ~ -2999)
    HXCPlayerErrorNetConnectionTimeout = -2001,         // 网络连接超时
    HXCPlayerErrorNetConnectionRefused = -2002,         // 服务器拒绝连接
    HXCPlayerErrorNetUnreachable = -2003,               // 网络不可达

    // HTTP 相关错误 (-3001 ~ -3999)
    HXCPlayerErrorHTTPBadRequest = -3001,               // HTTP 请求错误（400）
    HXCPlayerErrorHTTPNotFound = -3002,                 // HTTP 404 文件不存在
    HXCPlayerErrorHTTPServerError = -3003,              // HTTP 服务器错误（5xx）
    HXCPlayerErrorHTTPUnauthorized = -3004,             // 需要身份验证（401）
    HXCPlayerErrorHTTPForbidden = -3005,                // 访问被禁止（403）

    // License（-4001 ~ -4099）
    HXCPlayerErrorLicenseValidationFailed = -4001,      // License 校验未通过或门禁开启但未通过校验
};

/// ⚠️ 数据源模式
typedef NS_ENUM(NSInteger, HXCPlayerDataSourceMode) {
    HXCPlayerDataSourceModeDefault = 0,      // 默认模式（FFmpeg 直接打开）
    HXCPlayerDataSourceModeCustomHTTP = 1,   // 自定义 HTTP Range 下载器
    HXCPlayerDataSourceModeCustomFile = 2,   // 本地文件自定义读取（支持加密文件头解密）
};

/// 解码模式（播放前设置，默认软解）
typedef NS_ENUM(NSInteger, HXCPlayerDecodeMode) {
    HXCPlayerDecodeModeSoftware = 0,
    HXCPlayerDecodeModeHardware = 1,
};

#if TARGET_OS_IOS
/// 画中画状态（仅 iOS）
typedef NS_ENUM(NSInteger, HXCPlayerPIPState) {
    HXCPlayerPIPStateNone = 0,
    HXCPlayerPIPStateWillStart,
    HXCPlayerPIPStateDidStart,
    HXCPlayerPIPStateWillStop,
    HXCPlayerPIPStateDidStop,
    HXCPlayerPIPStateRestore
};
#endif

#endif // HXCPLAYER_TYPES_H

